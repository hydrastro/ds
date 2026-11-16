#include "str_io_posix.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

long ds_posix_write_cb(void *ud, const void *buf, size_t n) {
  int fd = (int)(long)ud;
  const char *p = (const char *)buf;
  size_t off = 0;
  if (n > (size_t)LONG_MAX) {
    n = (size_t)LONG_MAX; /* a short write: the count must fit */
  }
  while (off < n) {
    ssize_t w = write(fd, p + off, n - off);
    if (w < 0) {
      if (errno == EINTR)
        continue;
      return off > 0 ? (long)off : -1;
    }
    if (w == 0)
      return off > 0 ? (long)off : -1;
    off += (size_t)w;
  }
  return (long)off;
}

/* Writes the buffers in order, as many as 16 per writev, resuming after
 * short writes, and returns the bytes written: all of them, or as many as
 * a long counts, or what was written before an error (-1 if nothing). */
long ds_posix_writev_cb(void *ud, const void *const *bufs, const size_t *lens,
                        size_t count) {
  struct iovec vec[16];
  int fd = (int)(long)ud;
  size_t i = 0;     /* buffers before i are written */
  size_t skip = 0;  /* and the first skip bytes of buffer i */
  size_t total = 0; /* bytes written */
  while (i < count && total < (size_t)LONG_MAX) {
    size_t n = 0, asked = 0, room = (size_t)LONG_MAX - total, done;
    ssize_t w;
    while (i + n < count && n < 16 && asked < room) {
      size_t at = n == 0 ? skip : 0;
      size_t len = lens[i + n] - at;
      const void *srcp = bufs[i + n];
      void *dstp;
      if (len > room - asked) {
        len = room - asked;
      }
      memcpy(&dstp, &srcp, sizeof(void *));
      vec[n].iov_base = len == 0 ? dstp : (char *)dstp + at;
      vec[n].iov_len = len;
      asked += len;
      n++;
    }
    if (asked == 0) {
      /* only empty buffers are left */
      break;
    }
    w = writev(fd, vec, (int)n);
    if (w < 0) {
      if (errno == EINTR)
        continue;
      return total > 0 ? (long)total : -1;
    }
    if (w == 0)
      return total > 0 ? (long)total : -1;
    total += (size_t)w;
    done = (size_t)w;
    while (i < count && done >= lens[i] - skip) {
      done -= lens[i] - skip;
      skip = 0;
      i++;
      if (done == 0 && (i == count || lens[i] != 0))
        break;
    }
    skip += done;
  }
  return (long)total;
}
