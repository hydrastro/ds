#define _POSIX_C_SOURCE 200809L
#include <dsa/structures/posix_store.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PX_MAX_PAGE (1UL << 30)

typedef struct px_store {
  int fd;
  size_t ps;
  unsigned long count;
  unsigned long limit; /* addressable pages */
  bool read_only;
  char *dir; /* directory to sync once (a file this store created) */
  ds_context_t *context;
} px_store_t;

/* the largest off_t, 2^(bits - 1) - 1, built without overflow */
static off_t px_off_max(void) {
  off_t m = 1;
  size_t i;
  for (i = 0; i + 2 < sizeof(off_t) * CHAR_BIT; i++) {
    m = (off_t)(m * 2);
  }
  return (off_t)(m - 1 + m);
}

static unsigned long px_page_limit(size_t ps) {
  off_t q = (off_t)(px_off_max() / (off_t)ps);
  if (sizeof(off_t) > sizeof(unsigned long) && q > (off_t)(unsigned long)-1) {
    return (unsigned long)-1;
  }
  return (unsigned long)q;
}

static off_t px_offset(const px_store_t *p, unsigned long page) {
  return (off_t)((off_t)page * (off_t)p->ps);
}

static ds_status_t px_read(void *impl, unsigned long page, void *buf) {
  px_store_t *p = (px_store_t *)impl;
  unsigned char *b = (unsigned char *)buf;
  size_t done = 0;
  off_t off;
  if (page >= p->count) {
    memset(buf, 0, p->ps);
    return DS_OK;
  }
  off = px_offset(p, page);
  while (done < p->ps) {
    ssize_t r =
        pread(p->fd, b + done, p->ps - done, (off_t)(off + (off_t)done));
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      return DS_ERR_STATE;
    }
    if (r == 0) {
      memset(b + done, 0, p->ps - done); /* past the end of the file */
      break;
    }
    done += (size_t)r;
  }
  return DS_OK;
}

static ds_status_t px_write(void *impl, unsigned long page, const void *buf) {
  px_store_t *p = (px_store_t *)impl;
  const unsigned char *b = (const unsigned char *)buf;
  size_t done = 0;
  off_t off;
  if (p->read_only) {
    return DS_ERR_UNSUPPORTED;
  }
  if (page >= p->limit) {
    return DS_ERR_RANGE;
  }
  off = px_offset(p, page);
  while (done < p->ps) {
    ssize_t r =
        pwrite(p->fd, b + done, p->ps - done, (off_t)(off + (off_t)done));
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      return DS_ERR_STATE;
    }
    if (r == 0) {
      return DS_ERR_STATE; /* no progress */
    }
    done += (size_t)r;
  }
  if (page >= p->count) {
    p->count = page + 1;
  }
  return DS_OK;
}

static int px_fsync(int fd) {
  int r;
  do {
    r = fsync(fd);
  } while (r != 0 && errno == EINTR);
  return r;
}

static ds_status_t px_sync(void *impl) {
  px_store_t *p = (px_store_t *)impl;
  if (p->read_only) {
    return DS_OK;
  }
  if (px_fsync(p->fd) != 0) {
    return DS_ERR_STATE;
  }
  if (p->dir != NULL) {
    int d = open(p->dir, O_RDONLY);
    int r;
    if (d < 0) {
      return DS_ERR_STATE;
    }
    r = px_fsync(d);
    if (r != 0 && errno == EINVAL) {
      r = 0; /* this file system does not sync directories */
    }
    (void)close(d);
    if (r != 0) {
      return DS_ERR_STATE;
    }
    ds_context_free(p->context, p->dir);
    p->dir = NULL;
  }
  return DS_OK;
}

static unsigned long px_count(const void *impl) {
  return ((const px_store_t *)impl)->count;
}

static ds_status_t px_truncate(void *impl, unsigned long pages) {
  px_store_t *p = (px_store_t *)impl;
  int r;
  if (p->read_only) {
    return DS_ERR_UNSUPPORTED;
  }
  if (pages >= p->count) {
    return DS_OK;
  }
  do {
    r = ftruncate(p->fd, px_offset(p, pages));
  } while (r != 0 && errno == EINTR);
  if (r != 0) {
    return DS_ERR_STATE;
  }
  p->count = pages;
  return DS_OK;
}

static void px_close(void *impl) {
  px_store_t *p = (px_store_t *)impl;
  (void)close(p->fd); /* not retried: the descriptor is gone either way */
  ds_context_free(p->context, p->dir);
  ds_context_free(p->context, p);
}

static const ds_page_store_ops_t px_ops = {
    "posix file", DS_PAGE_STORE_DURABLE_SYNC | DS_PAGE_STORE_TRUNCATE_PERSISTS,
    px_read,      px_write,
    px_sync,      px_count,
    px_truncate,  px_close};

/* the directory part of path, "." when there is none */
static char *px_dirname(const char *path, ds_context_t *context) {
  const char *slash = strrchr(path, '/');
  size_t n;
  char *d;
  if (slash == NULL) {
    path = ".";
    n = 1;
  } else {
    n = slash == path ? 1 : (size_t)(slash - path);
  }
  d = (char *)ds_context_alloc(context, n + 1);
  if (d != NULL) {
    memcpy(d, path, n);
    d[n] = '\0';
  }
  return d;
}

static ds_status_t px_adopt(ds_page_store_t *store, int fd, size_t ps,
                            bool read_only, char *dir, ds_context_t *context) {
  px_store_t *p;
  struct stat sb;
  off_t pages;
  unsigned long limit = px_page_limit(ps);
  if (fstat(fd, &sb) != 0) {
    return DS_ERR_STATE;
  }
  if (sb.st_size < 0) {
    return DS_ERR_STATE;
  }
  pages = (off_t)(sb.st_size / (off_t)ps + (sb.st_size % (off_t)ps != 0));
  if (sizeof(off_t) > sizeof(unsigned long) &&
      pages > (off_t)(unsigned long)-1) {
    return DS_ERR_RANGE;
  }
  p = (px_store_t *)ds_context_calloc(context, 1, sizeof(px_store_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  p->fd = fd;
  p->ps = ps;
  p->count = (unsigned long)pages;
  p->limit = limit;
  p->read_only = read_only;
  p->dir = dir;
  p->context = context;
  store->ops = &px_ops;
  store->impl = p;
  store->page_size = ps;
  return DS_OK;
}

ds_status_t ds_page_store_posix_fd(ds_page_store_t *store, int fd,
                                   size_t page_size, ds_context_t *context) {
  int mode;
  if (store == NULL) {
    return DS_ERR_NULL;
  }
  if (fd < 0 || page_size == 0 || page_size > PX_MAX_PAGE) {
    return DS_ERR_INVALID;
  }
  mode = fcntl(fd, F_GETFL);
  if (mode < 0) {
    return DS_ERR_STATE;
  }
  return px_adopt(store, fd, page_size, (mode & O_ACCMODE) == O_RDONLY, NULL,
                  context);
}

ds_status_t ds_page_store_posix(ds_page_store_t *store, const char *path,
                                size_t page_size, unsigned flags,
                                ds_context_t *context) {
  bool read_only = (flags & DS_POSIX_STORE_READ_ONLY) != 0;
  bool created = false;
  int access = read_only ? O_RDONLY : O_RDWR;
  int fd = -1;
  char *dir = NULL;
  ds_status_t st;
  if (store == NULL || path == NULL) {
    return DS_ERR_NULL;
  }
  if (page_size == 0 || page_size > PX_MAX_PAGE ||
      (read_only && (flags & (DS_POSIX_STORE_CREATE | DS_POSIX_STORE_TRUNCATE |
                              DS_POSIX_STORE_EXCLUSIVE)) != 0) ||
      (flags & ~(DS_POSIX_STORE_CREATE | DS_POSIX_STORE_EXCLUSIVE |
                 DS_POSIX_STORE_TRUNCATE | DS_POSIX_STORE_READ_ONLY)) != 0) {
    return DS_ERR_INVALID;
  }
  if ((flags & DS_POSIX_STORE_CREATE) != 0) {
    /* create exclusively first, to know whether the directory changed */
    do {
      fd = open(path, access | O_CREAT | O_EXCL, 0666);
    } while (fd < 0 && errno == EINTR);
    if (fd >= 0) {
      created = true;
    } else if (errno != EEXIST || (flags & DS_POSIX_STORE_EXCLUSIVE) != 0) {
      return DS_ERR_STATE;
    }
  }
  if (fd < 0) {
    int extra = (flags & DS_POSIX_STORE_TRUNCATE) != 0 ? O_TRUNC : 0;
    do {
      fd = open(path, access | extra);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
      return errno == ENOENT ? DS_NOT_FOUND : DS_ERR_STATE;
    }
  }
  if (created) {
    dir = px_dirname(path, context);
    if (dir == NULL) {
      (void)close(fd);
      (void)unlink(path);
      return DS_ERR_ALLOC;
    }
  }
  st = px_adopt(store, fd, page_size, read_only, dir, context);
  if (st != DS_OK) {
    ds_context_free(context, dir);
    (void)close(fd);
    if (created) {
      (void)unlink(path); /* leave no trace of a store never opened */
    }
  }
  return st;
}
