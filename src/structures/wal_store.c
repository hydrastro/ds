#include <dsa/core/checked.h>
#include <dsa/structures/sstable.h>
#include <dsa/structures/wal_store.h>
#include <string.h>

#define WAL_SUPER_MAGIC "DSWALS1"
#define WAL_INIT_MAGIC "DSWALI1"
#define WAL_GROUP_MAGIC "DSWALG1"
#define WAL_VERSION 1UL
#define WAL_HDR 48U   /* group header before its entries */
#define WAL_ENTRY 16U /* bytes per entry */
#define WAL_FIRST 2UL /* position of the first group */
#define WAL_DATA 1U
#define WAL_TRUNC 2U
#define WAL_COMMIT 1UL
#define WAL_MIN_PAGE 128U

/* the latest log copy of a page */
typedef struct wal_ref {
  unsigned long pos;
  unsigned long crc;
} wal_ref_t;

/* an entry read back while recovering, before its commit is seen */
typedef struct wal_entry {
  unsigned long target; /* page, or the new count of a truncation */
  unsigned long pos;    /* log page; 0 for a truncation */
  unsigned long crc;
} wal_entry_t;

typedef struct wal {
  ds_page_store_t *data;
  ds_page_store_t *log;
  size_t ps;
  size_t per_group;
  unsigned long checkpoint_pages;
  ds_map_t index; /* page -> wal_ref_t */
  unsigned long epoch;
  unsigned long seq;       /* the open transaction */
  unsigned long count;     /* pages */
  unsigned long zero_from; /* past it, pages not in the index are zeros */
  unsigned long tail;      /* the open group's header position */
  size_t gentries;         /* entries of the open group */
  unsigned long gdata;     /* their data pages */
  bool txn;                /* the open transaction has entries */
  bool failed;             /* a commit failed: only sync may proceed */
  bool reset_pending;      /* the log restart must be retried */
  unsigned long *drop;     /* index keys to drop once the restart lands */
  size_t ndrop;
  unsigned char *hdr; /* the open group's header */
  unsigned char *buf; /* scratch page */
  ds_wal_store_stats_t stats;
  ds_context_t *context;
} wal_t;

/* ---- encoding ---- */

static void wal_put4(unsigned char *p, unsigned long v) {
  p[0] = (unsigned char)(v & 0xFFUL);
  p[1] = (unsigned char)((v >> 8) & 0xFFUL);
  p[2] = (unsigned char)((v >> 16) & 0xFFUL);
  p[3] = (unsigned char)((v >> 24) & 0xFFUL);
}

static unsigned long wal_get4(const unsigned char *p) {
  return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
         ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static void wal_put8(unsigned char *p, unsigned long v) {
  wal_put4(p, v & 0xFFFFFFFFUL);
  wal_put4(p + 4, ((v >> 16) >> 16) & 0xFFFFFFFFUL);
}

/* false when the value does not fit an unsigned long here */
static bool wal_get8(const unsigned char *p, unsigned long *out) {
  unsigned long hi = wal_get4(p + 4);
  if (sizeof(unsigned long) < 8 && hi != 0) {
    return false;
  }
  *out = ((hi << 16) << 16) | wal_get4(p);
  return true;
}

static unsigned long wal_crc(const void *p, size_t n) {
  return ds_sstable_crc32c(0, p, n);
}

/* ---- index ---- */

/* the logged pages at or past from, into a fresh array */
static ds_status_t wal_collect(const wal_t *w, unsigned long from,
                               unsigned long **out, size_t *n) {
  size_t cursor = 0;
  size_t cap = ds_map_size(&w->index);
  size_t k = 0;
  size_t bytes;
  const void *key;
  unsigned long *keys = NULL;
  *out = NULL;
  *n = 0;
  if (cap == 0) {
    return DS_OK;
  }
  if (!ds_size_mul(cap, sizeof(unsigned long), &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  keys = (unsigned long *)ds_context_alloc(w->context, bytes);
  if (keys == NULL) {
    return DS_ERR_ALLOC;
  }
  while (ds_map_next(&w->index, &cursor, &key, NULL)) {
    unsigned long page = *(const unsigned long *)key;
    if (page >= from && k < cap) {
      keys[k++] = page;
    }
  }
  *out = keys;
  *n = k;
  return DS_OK;
}

static void wal_drop(wal_t *w, const unsigned long *keys, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    (void)ds_map_remove(&w->index, &keys[i], NULL);
  }
}

/* ---- superblocks ---- */

static void wal_super_encode(const wal_t *w, unsigned char *p,
                             unsigned long epoch, unsigned long seq,
                             unsigned long count) {
  memset(p, 0, w->ps);
  memcpy(p, WAL_SUPER_MAGIC, 8);
  wal_put4(p + 8, (unsigned long)w->ps);
  wal_put4(p + 12, WAL_VERSION);
  wal_put8(p + 16, epoch);
  wal_put8(p + 24, seq);
  wal_put8(p + 32, count);
  wal_put4(p + 40, wal_crc(p, 40));
}

/* DS_OK for a valid superblock of this slot, DS_NOT_FOUND for none,
 * DS_ERR_UNSUPPORTED for one with counters past this host's range */
static ds_status_t wal_super_decode(const wal_t *w, const unsigned char *p,
                                    unsigned long slot, unsigned long *epoch,
                                    unsigned long *seq, unsigned long *count) {
  if (memcmp(p, WAL_SUPER_MAGIC, 8) != 0 ||
      wal_get4(p + 40) != wal_crc(p, 40) ||
      wal_get4(p + 8) != (unsigned long)w->ps ||
      wal_get4(p + 12) != WAL_VERSION) {
    return DS_NOT_FOUND;
  }
  if (!wal_get8(p + 16, epoch) || !wal_get8(p + 24, seq) ||
      !wal_get8(p + 32, count)) {
    return DS_ERR_UNSUPPORTED;
  }
  if (*epoch == 0 || *epoch % 2 != slot || *seq == 0) {
    return DS_NOT_FOUND;
  }
  return DS_OK;
}

/* ---- groups ---- */

static void wal_group_seal(wal_t *w, bool commit) {
  unsigned char *h = w->hdr;
  unsigned long crc;
  memcpy(h, WAL_GROUP_MAGIC, 8);
  wal_put8(h + 8, w->epoch);
  wal_put8(h + 16, w->seq);
  wal_put8(h + 24, w->count);
  wal_put4(h + 32, (unsigned long)w->gentries);
  wal_put4(h + 36, commit ? WAL_COMMIT : 0UL);
  wal_put4(h + 40, 0UL);
  crc = ds_sstable_crc32c(0, h, 44);
  crc = ds_sstable_crc32c(crc, h + WAL_HDR, w->gentries * WAL_ENTRY);
  wal_put4(h + 44, crc);
}

/* writes the open, full group's header and starts the next group */
static ds_status_t wal_group_flush(wal_t *w) {
  ds_status_t st;
  wal_group_seal(w, false);
  st = ds_page_store_write(w->log, w->tail, w->hdr);
  if (st != DS_OK) {
    return st;
  }
  w->tail += 1 + w->gdata;
  w->gentries = 0;
  w->gdata = 0;
  memset(w->hdr, 0, w->ps);
  return DS_OK;
}

static void wal_entry_put(wal_t *w, unsigned kind, unsigned long target,
                          unsigned long crc) {
  unsigned char *e = w->hdr + WAL_HDR + w->gentries * WAL_ENTRY;
  memset(e, 0, WAL_ENTRY);
  e[0] = (unsigned char)kind;
  wal_put4(e + 4, crc);
  wal_put8(e + 8, target);
  w->gentries++;
  w->txn = true;
}

/* ---- the log restart (end of a checkpoint) ---- */

static ds_status_t wal_reset(wal_t *w) {
  ds_status_t st;
  unsigned long next;
  if (w->epoch == (unsigned long)-1) {
    return DS_ERR_OVERFLOW;
  }
  next = w->epoch + 1;
  w->reset_pending = true;
  wal_super_encode(w, w->buf, next, w->seq, w->count);
  st = ds_page_store_write(w->log, next % 2, w->buf);
  if (st == DS_OK) {
    st = ds_page_store_sync(w->log);
  }
  if (st != DS_OK) {
    return st;
  }
  w->reset_pending = false;
  w->epoch = next;
  w->zero_from = w->count;
  w->tail = WAL_FIRST;
  wal_drop(w, w->drop, w->ndrop);
  ds_context_free(w->context, w->drop);
  w->drop = NULL;
  w->ndrop = 0;
  w->stats.checkpoints++;
  (void)ds_page_store_truncate(w->log, WAL_FIRST);
  return DS_OK;
}

static ds_status_t wal_commit(wal_t *w);

/* writes and truncations may proceed once a commit or a log restart that
 * failed has been completed */
static ds_status_t wal_ready(wal_t *w) {
  if (w->failed) {
    ds_status_t st = wal_commit(w);
    if (st != DS_OK) {
      return st;
    }
  }
  return w->reset_pending ? wal_reset(w) : DS_OK;
}

static ds_status_t wal_commit(wal_t *w) {
  ds_status_t st;
  if (w->reset_pending) {
    return wal_reset(w); /* no transaction is open meanwhile */
  }
  if (!w->txn) {
    return DS_OK;
  }
  if (w->seq == (unsigned long)-1) {
    return DS_ERR_OVERFLOW;
  }
  wal_group_seal(w, true);
  st = ds_page_store_write(w->log, w->tail, w->hdr);
  if (st == DS_OK) {
    st = ds_page_store_sync(w->log);
  }
  if (st != DS_OK) {
    w->failed = true;
    return st;
  }
  w->failed = false;
  w->txn = false;
  w->seq++;
  w->tail += 1 + w->gdata;
  w->gentries = 0;
  w->gdata = 0;
  memset(w->hdr, 0, w->ps);
  w->stats.commits++;
  return DS_OK;
}

/* precondition: committed, no restart pending */
static ds_status_t wal_checkpoint(wal_t *w) {
  unsigned long dcount = ds_page_store_count(w->data);
  unsigned long end = w->count < dcount ? w->count : dcount;
  unsigned long p;
  size_t i;
  ds_status_t st;
  st = wal_collect(w, 0, &w->drop, &w->ndrop);
  for (i = 0; st == DS_OK && i < w->ndrop; i++) {
    const wal_ref_t *ref =
        (const wal_ref_t *)ds_map_get(&w->index, &w->drop[i]);
    wal_ref_t r;
    memcpy(&r, ref, sizeof(r));
    st = ds_page_store_read(w->log, r.pos, w->buf);
    if (st == DS_OK && wal_crc(w->buf, w->ps) != r.crc) {
      st = DS_ERR_INVALID;
    }
    if (st == DS_OK) {
      st = ds_page_store_write(w->data, w->drop[i], w->buf);
    }
  }
  /* home pages that a truncation cut off and nothing rewrote */
  if (st == DS_OK && w->zero_from < end) {
    memset(w->buf, 0, w->ps);
    for (p = w->zero_from; st == DS_OK && p < end; p++) {
      if (ds_map_get(&w->index, &p) == NULL) {
        st = ds_page_store_write(w->data, p, w->buf);
      }
    }
  }
  if (st == DS_OK && w->count < ds_page_store_count(w->data)) {
    (void)ds_page_store_truncate(w->data, w->count);
  }
  if (st == DS_OK) {
    st = ds_page_store_sync(w->data);
  }
  if (st == DS_OK) {
    return wal_reset(w); /* keeps drop for a retry if it fails */
  }
  ds_context_free(w->context, w->drop);
  w->drop = NULL;
  w->ndrop = 0;
  return st;
}

/* ---- page store operations ---- */

static ds_status_t wal_read(void *impl, unsigned long page, void *buf) {
  wal_t *w = (wal_t *)impl;
  const wal_ref_t *ref;
  if (page >= w->count) {
    memset(buf, 0, w->ps);
    return DS_OK;
  }
  ref = (const wal_ref_t *)ds_map_get(&w->index, &page);
  if (ref != NULL) {
    wal_ref_t r;
    ds_status_t st;
    memcpy(&r, ref, sizeof(r));
    st = ds_page_store_read(w->log, r.pos, buf);
    if (st == DS_OK && wal_crc(buf, w->ps) != r.crc) {
      st = DS_ERR_INVALID;
    }
    return st;
  }
  if (page >= w->zero_from) {
    memset(buf, 0, w->ps);
    return DS_OK;
  }
  return ds_page_store_read(w->data, page, buf);
}

static ds_status_t wal_write(void *impl, unsigned long page, const void *buf) {
  wal_t *w = (wal_t *)impl;
  wal_ref_t ref;
  ds_status_t st = wal_ready(w);
  if (st != DS_OK) {
    return st;
  }
  if (page == (unsigned long)-1) {
    return DS_ERR_RANGE;
  }
  if (w->gentries == w->per_group) {
    st = wal_group_flush(w);
    if (st != DS_OK) {
      return st;
    }
  }
  if (w->tail > (unsigned long)-1 - 2 ||
      w->gdata > (unsigned long)-1 - 2 - w->tail) {
    return DS_ERR_OVERFLOW;
  }
  ref.pos = w->tail + 1 + w->gdata;
  ref.crc = wal_crc(buf, w->ps);
  st = ds_page_store_write(w->log, ref.pos, buf);
  if (st == DS_OK) {
    st = ds_map_put(&w->index, &page, &ref);
  }
  if (st != DS_OK && st != DS_EXISTS) {
    return st; /* the log page is not referenced: it will be reused */
  }
  wal_entry_put(w, WAL_DATA, page, ref.crc);
  w->gdata++;
  if (page >= w->count) {
    w->count = page + 1;
  }
  return DS_OK;
}

static ds_status_t wal_truncate(void *impl, unsigned long pages) {
  wal_t *w = (wal_t *)impl;
  unsigned long *keys;
  size_t n;
  ds_status_t st = wal_ready(w);
  if (st != DS_OK || pages >= w->count) {
    return st;
  }
  if (w->gentries == w->per_group) {
    st = wal_group_flush(w);
    if (st != DS_OK) {
      return st;
    }
  }
  st = wal_collect(w, pages, &keys, &n);
  if (st != DS_OK) {
    return st;
  }
  wal_drop(w, keys, n);
  ds_context_free(w->context, keys);
  wal_entry_put(w, WAL_TRUNC, pages, 0UL);
  w->count = pages;
  if (pages < w->zero_from) {
    w->zero_from = pages;
  }
  return DS_OK;
}

static ds_status_t wal_sync(void *impl) {
  wal_t *w = (wal_t *)impl;
  ds_status_t st = wal_commit(w);
  if (st == DS_OK && w->checkpoint_pages != 0 &&
      w->tail >= w->checkpoint_pages && !w->txn) {
    w->stats.last_checkpoint = wal_checkpoint(w);
  }
  return st;
}

static unsigned long wal_count(const void *impl) {
  return ((const wal_t *)impl)->count;
}

static void wal_free(wal_t *w) {
  ds_map_destroy(&w->index);
  ds_context_free(w->context, w->drop);
  ds_context_free(w->context, w->hdr);
  ds_context_free(w->context, w->buf);
  ds_context_free(w->context, w);
}

static void wal_close(void *impl) { wal_free((wal_t *)impl); }

static const ds_page_store_ops_t wal_ops = {
    "write-ahead log",
    DS_PAGE_STORE_ATOMIC_SYNC | DS_PAGE_STORE_TRUNCATE_PERSISTS,
    wal_read,
    wal_write,
    wal_sync,
    wal_count,
    wal_truncate,
    wal_close};
static const ds_page_store_ops_t wal_ops_durable = {
    "write-ahead log",
    DS_PAGE_STORE_ATOMIC_SYNC | DS_PAGE_STORE_TRUNCATE_PERSISTS |
        DS_PAGE_STORE_DURABLE_SYNC,
    wal_read,
    wal_write,
    wal_sync,
    wal_count,
    wal_truncate,
    wal_close};

static wal_t *wal_of(const ds_page_store_t *store) {
  if (store == NULL ||
      (store->ops != &wal_ops && store->ops != &wal_ops_durable)) {
    return NULL;
  }
  return (wal_t *)store->impl;
}

/* ---- opening: format or recover ---- */

static ds_status_t wal_format(wal_t *w) {
  ds_status_t st;
  w->epoch = 1;
  w->seq = 1;
  w->count = ds_page_store_count(w->data);
  /* Only an empty log or our valid marker may enter this function. Keep
   * the marker until the first superblock has been durably published. */
  wal_super_encode(w, w->buf, w->epoch, w->seq, w->count);
  memcpy(w->buf, WAL_INIT_MAGIC, 8);
  wal_put4(w->buf + 40, wal_crc(w->buf, 40));
  st = ds_page_store_write(w->log, 0, w->buf);
  if (st == DS_OK) {
    st = ds_page_store_sync(w->log);
  }
  if (st == DS_OK) {
    wal_super_encode(w, w->buf, w->epoch, w->seq, w->count);
    st = ds_page_store_write(w->log, 1, w->buf);
  }
  if (st == DS_OK) {
    st = ds_page_store_sync(w->log);
  }
  return st;
}

/* reads the group at pos into the pending entries; DS_NOT_FOUND ends the
 * log there */
static ds_status_t wal_group_read(wal_t *w, unsigned long pos,
                                  unsigned long *count, wal_entry_t **pend,
                                  size_t *npend, size_t *cap, bool *commit,
                                  unsigned long *ndata) {
  unsigned char *h = w->hdr;
  unsigned long epoch, seq, c, crc, n, flags;
  size_t i;
  size_t first = *npend;
  ds_status_t st = ds_page_store_read(w->log, pos, h);
  if (st != DS_OK) {
    return st;
  }
  if (memcmp(h, WAL_GROUP_MAGIC, 8) != 0) {
    return DS_NOT_FOUND;
  }
  n = wal_get4(h + 32);
  flags = wal_get4(h + 36);
  if ((n == 0 && flags != WAL_COMMIT) || n > (unsigned long)w->per_group ||
      flags > WAL_COMMIT || wal_get4(h + 40) != 0) {
    return DS_NOT_FOUND;
  }
  crc = ds_sstable_crc32c(0, h, 44);
  crc = ds_sstable_crc32c(crc, h + WAL_HDR, (size_t)n * WAL_ENTRY);
  if (crc != wal_get4(h + 44)) {
    return DS_NOT_FOUND;
  }
  if (!wal_get8(h + 8, &epoch) || !wal_get8(h + 16, &seq) ||
      !wal_get8(h + 24, &c)) {
    return DS_ERR_UNSUPPORTED;
  }
  if (epoch != w->epoch || seq != w->seq) {
    return DS_NOT_FOUND; /* an older epoch or an abandoned attempt */
  }
  *commit = flags == WAL_COMMIT;
  *ndata = 0;
  for (i = 0; i < (size_t)n; i++) {
    const unsigned char *e = h + WAL_HDR + i * WAL_ENTRY;
    wal_entry_t ent;
    if ((e[0] != WAL_DATA && e[0] != WAL_TRUNC) || e[1] != 0 || e[2] != 0 ||
        e[3] != 0) {
      return DS_NOT_FOUND;
    }
    if (!wal_get8(e + 8, &ent.target)) {
      return DS_ERR_UNSUPPORTED;
    }
    ent.crc = wal_get4(e + 4);
    if (e[0] == WAL_DATA) {
      if (ent.target == (unsigned long)-1 || pos > (unsigned long)-1 - 2 ||
          *ndata > (unsigned long)-1 - 2 - pos) {
        return DS_NOT_FOUND;
      }
      ent.pos = pos + 1 + (*ndata)++;
      if (*count <= ent.target) {
        *count = ent.target + 1;
      }
    } else {
      if (ent.crc != 0 || ent.target >= *count) {
        return DS_NOT_FOUND; /* only real truncations are logged */
      }
      ent.pos = 0;
      *count = ent.target;
    }
    if (*npend == *cap) {
      size_t nc = *cap < 64 ? 64 : *cap;
      size_t bytes;
      wal_entry_t *p;
      if (*cap >= 64) {
        if (nc > (size_t)-1 / 2) {
          return DS_ERR_OVERFLOW;
        }
        nc *= 2;
      }
      if (!ds_size_mul(nc, sizeof(wal_entry_t), &bytes)) {
        return DS_ERR_OVERFLOW;
      }
      p = (wal_entry_t *)ds_context_realloc(w->context, *pend, bytes);
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      *pend = p;
      *cap = nc;
    }
    (*pend)[(*npend)++] = ent;
  }
  if (c != *count) {
    return DS_NOT_FOUND;
  }
  /* the data pages must be the ones the header describes */
  for (i = first; i < *npend; i++) {
    const wal_entry_t *ent = &(*pend)[i];
    if (ent->pos != 0) {
      st = ds_page_store_read(w->log, ent->pos, w->buf);
      if (st != DS_OK) {
        return st;
      }
      if (wal_crc(w->buf, w->ps) != ent->crc) {
        return DS_NOT_FOUND;
      }
    }
  }
  return DS_OK;
}

/* applies a committed transaction's entries to the index */
static ds_status_t wal_apply(wal_t *w, const wal_entry_t *pend, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    ds_status_t st;
    if (pend[i].pos != 0) {
      wal_ref_t ref;
      ref.pos = pend[i].pos;
      ref.crc = pend[i].crc;
      st = ds_map_put(&w->index, &pend[i].target, &ref);
      if (st != DS_OK && st != DS_EXISTS) {
        return st;
      }
    } else {
      unsigned long *keys;
      size_t nk;
      st = wal_collect(w, pend[i].target, &keys, &nk);
      if (st != DS_OK) {
        return st;
      }
      wal_drop(w, keys, nk);
      ds_context_free(w->context, keys);
      if (pend[i].target < w->zero_from) {
        w->zero_from = pend[i].target;
      }
    }
  }
  return DS_OK;
}

static ds_status_t wal_recover(wal_t *w) {
  unsigned long pos = WAL_FIRST;
  unsigned long count = w->count;
  unsigned long logn = ds_page_store_count(w->log);
  wal_entry_t *pend = NULL;
  size_t npend = 0;
  size_t cap = 0;
  ds_status_t st = DS_OK;
  w->tail = WAL_FIRST;
  w->zero_from = w->count;
  while (pos < logn) {
    bool commit = false;
    unsigned long ndata = 0;
    st = wal_group_read(w, pos, &count, &pend, &npend, &cap, &commit, &ndata);
    if (st != DS_OK) {
      break;
    }
    pos += 1 + ndata;
    if (commit) {
      st = wal_apply(w, pend, npend);
      if (st != DS_OK) {
        break;
      }
      npend = 0;
      w->count = count;
      w->tail = pos;
      w->stats.recovered++;
      if (w->seq == (unsigned long)-1) {
        break; /* the sequence is exhausted: no later commit can follow */
      }
      w->seq++;
    }
  }
  ds_context_free(w->context, pend);
  memset(w->hdr, 0, w->ps);
  return st == DS_NOT_FOUND ? DS_OK : st;
}

void ds_wal_store_config_init(ds_wal_store_config_t *config) {
  if (config != NULL) {
    config->checkpoint_pages = 1024;
    config->index = NULL;
  }
}

ds_status_t ds_page_store_wal(ds_page_store_t *store, ds_page_store_t *data,
                              ds_page_store_t *log,
                              const ds_wal_store_config_t *config,
                              ds_context_t *context) {
  ds_wal_store_config_t defaults;
  wal_t *w;
  size_t ps;
  ds_status_t st;
  unsigned long best = 0;
  unsigned long slot;
  unsigned long logn;
  bool initializing = false;
  if (store == NULL || data == NULL || log == NULL || data->ops == NULL ||
      log->ops == NULL) {
    return DS_ERR_NULL;
  }
  if (config == NULL) {
    ds_wal_store_config_init(&defaults);
    config = &defaults;
  }
  ps = data->page_size;
  if (log->page_size != ps || ps < WAL_MIN_PAGE || DS_ABOVE_U32(ps) ||
      data == log) {
    return DS_ERR_INVALID;
  }
  w = (wal_t *)ds_context_calloc(context, 1, sizeof(wal_t));
  if (w == NULL) {
    return DS_ERR_ALLOC;
  }
  w->data = data;
  w->log = log;
  w->ps = ps;
  w->per_group = (ps - WAL_HDR) / WAL_ENTRY;
  w->checkpoint_pages = config->checkpoint_pages;
  w->context = context != NULL ? context : ds_default_context();
  w->hdr = (unsigned char *)ds_context_calloc(context, 1, ps);
  w->buf = (unsigned char *)ds_context_alloc(context, ps);
  if (w->hdr == NULL || w->buf == NULL) {
    ds_context_free(context, w->hdr);
    ds_context_free(context, w->buf);
    ds_context_free(context, w);
    return DS_ERR_ALLOC;
  }
  st = ds_map_init(&w->index, config->index, sizeof(unsigned long),
                   sizeof(wal_ref_t), ds_map_hash_ulong, ds_map_equal_ulong,
                   NULL, context);
  if (st != DS_OK) {
    ds_context_free(context, w->hdr);
    ds_context_free(context, w->buf);
    ds_context_free(context, w);
    return st;
  }
  logn = ds_page_store_count(log);
  for (slot = 0; slot < 2 && slot < logn; slot++) {
    unsigned long epoch = 0, seq = 0, count = 0;
    ds_status_t s = ds_page_store_read(log, slot, w->buf);
    if (s == DS_OK) {
      if (slot == 0 && logn <= WAL_FIRST &&
          memcmp(w->buf, WAL_INIT_MAGIC, 8) == 0 &&
          wal_get4(w->buf + 8) == (unsigned long)ps &&
          wal_get4(w->buf + 12) == WAL_VERSION &&
          wal_get4(w->buf + 40) == wal_crc(w->buf, 40)) {
        initializing = true;
      }
      s = wal_super_decode(w, w->buf, slot, &epoch, &seq, &count);
    }
    if (s == DS_OK && epoch > best) {
      best = epoch;
      w->epoch = epoch;
      w->seq = seq;
      w->count = count;
    } else if (s != DS_OK && s != DS_NOT_FOUND) {
      st = s;
      break;
    }
  }
  if (st == DS_OK) {
    if (best == 0) {
      st = logn == 0 || initializing ? wal_format(w) : DS_ERR_INVALID;
      w->tail = WAL_FIRST;
      w->zero_from = w->count;
    } else {
      st = wal_recover(w);
    }
  }
  if (st != DS_OK) {
    wal_free(w);
    return st;
  }
  w->stats.last_checkpoint = DS_OK;
  store->ops =
      (data->ops->traits & log->ops->traits & DS_PAGE_STORE_DURABLE_SYNC) != 0
          ? &wal_ops_durable
          : &wal_ops;
  store->impl = w;
  store->page_size = ps;
  return DS_OK;
}

ds_status_t ds_wal_store_checkpoint(ds_page_store_t *store) {
  wal_t *w = wal_of(store);
  ds_status_t st;
  if (w == NULL) {
    return store == NULL ? DS_ERR_NULL : DS_ERR_INVALID;
  }
  st = wal_commit(w);
  if (st == DS_OK && !w->reset_pending) {
    st = wal_checkpoint(w);
    w->stats.last_checkpoint = st;
  }
  return st;
}

ds_status_t ds_wal_store_stats(const ds_page_store_t *store,
                               ds_wal_store_stats_t *out) {
  const wal_t *w = wal_of(store);
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  if (w == NULL) {
    return store == NULL ? DS_ERR_NULL : DS_ERR_INVALID;
  }
  *out = w->stats;
  out->epoch = w->epoch;
  out->sequence = w->seq;
  out->log_pages = w->tail + w->gdata + (w->gentries > 0 ? 1UL : 0UL);
  out->logged_pages = (unsigned long)ds_map_size(&w->index);
  return DS_OK;
}
