#include "lsm_tree.h"
#include "checked.h"
#include <string.h>

#define LSM_NONE ((size_t) - 1)

typedef struct lsm_ent {
  size_t klen;
  size_t vlen;
  int tomb;
} lsm_ent_t;

static unsigned char *ent_key(lsm_ent_t *e) { return (unsigned char *)(e + 1); }

static unsigned char *ent_val(lsm_ent_t *e) { return ent_key(e) + e->klen; }

static lsm_ent_t *mem_at(const ds_lsm_t *l, size_t i) {
  return (lsm_ent_t *)l->mem[i];
}

static int key_cmp(const ds_lsm_t *l, const unsigned char *a, size_t al,
                   const unsigned char *b, size_t bl) {
  return l->cmp.compare(a, al, b, bl, l->cmp.user);
}

/* ---- memtable: skip list or sorted array ----
 *
 * A skip-list node is one block: the level, its forward links, then the
 * entry (header, key, value). The head has LSM_SL_MAX links and no entry.
 */

#define LSM_SL_MAX 20

typedef struct lsm_sl {
  size_t level;
} lsm_sl_t;

static lsm_sl_t **sl_fwd(lsm_sl_t *n) { return (lsm_sl_t **)(void *)(n + 1); }

static lsm_ent_t *sl_ent(lsm_sl_t *n) {
  return (lsm_ent_t *)(void *)(sl_fwd(n) + n->level);
}

static bool lsm_skip(const ds_lsm_t *l) {
  return l->config.memtable == DS_LSM_MEMTABLE_SKIPLIST;
}

/* a level in 1 .. LSM_SL_MAX with P(level > k) = 4^-k */
static size_t sl_random_level(ds_lsm_t *l) {
  size_t level = 1;
  for (;;) {
    unsigned long x = l->skip_rng;
    x ^= (x << 13) & 0xFFFFFFFFUL;
    x ^= x >> 17;
    x ^= (x << 5) & 0xFFFFFFFFUL;
    l->skip_rng = x & 0xFFFFFFFFUL;
    if (level >= LSM_SL_MAX || (x & 3UL) != 0) {
      return level;
    }
    level++;
  }
}

/* the last node before key on every level (update[0 .. LSM_SL_MAX)) */
static lsm_sl_t *sl_find(const ds_lsm_t *l, const unsigned char *key,
                         size_t klen, lsm_sl_t **update) {
  lsm_sl_t *x = (lsm_sl_t *)l->skip_head;
  size_t i = LSM_SL_MAX;
  while (i-- > 0) {
    lsm_sl_t *nx;
    while ((nx = sl_fwd(x)[i]) != NULL &&
           key_cmp(l, ent_key(sl_ent(nx)), sl_ent(nx)->klen, key, klen) < 0) {
      x = nx;
    }
    if (update != NULL) {
      update[i] = x;
    }
  }
  return sl_fwd(x)[0]; /* the first node with key >= key, or NULL */
}

/* first array entry with key >= key */
static size_t mem_lower(const ds_lsm_t *l, const unsigned char *key,
                        size_t klen) {
  size_t lo = 0, hi = l->mem_count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    lsm_ent_t *e = mem_at(l, mid);
    if (key_cmp(l, ent_key(e), e->klen, key, klen) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

/* the memtable entry with exactly this key, or NULL */
static lsm_ent_t *mem_find(const ds_lsm_t *l, const unsigned char *key,
                           size_t klen) {
  lsm_ent_t *e = NULL;
  if (lsm_skip(l)) {
    lsm_sl_t *n = sl_find(l, key, klen, NULL);
    e = n == NULL ? NULL : sl_ent(n);
  } else {
    size_t pos = mem_lower(l, key, klen);
    e = pos < l->mem_count ? mem_at(l, pos) : NULL;
  }
  if (e != NULL && key_cmp(l, ent_key(e), e->klen, key, klen) != 0) {
    e = NULL;
  }
  return e;
}

/* in-order memtable cursor */
typedef struct lsm_mcur {
  size_t pos;
  lsm_sl_t *node;
} lsm_mcur_t;

static void mcur_first(const ds_lsm_t *l, lsm_mcur_t *c) {
  c->pos = 0;
  c->node = lsm_skip(l) ? sl_fwd((lsm_sl_t *)l->skip_head)[0] : NULL;
}

static bool mcur_valid(const ds_lsm_t *l, const lsm_mcur_t *c) {
  return lsm_skip(l) ? c->node != NULL : c->pos < l->mem_count;
}

static lsm_ent_t *mcur_entry(const ds_lsm_t *l, const lsm_mcur_t *c) {
  return lsm_skip(l) ? sl_ent(c->node) : mem_at(l, c->pos);
}

static void mcur_next(const ds_lsm_t *l, lsm_mcur_t *c) {
  if (lsm_skip(l)) {
    c->node = sl_fwd(c->node)[0];
  } else {
    c->pos++;
  }
}

static void mcur_seek(const ds_lsm_t *l, lsm_mcur_t *c,
                      const unsigned char *key, size_t klen) {
  if (lsm_skip(l)) {
    c->node = sl_find(l, key, klen, NULL);
    c->pos = 0;
  } else {
    c->pos = mem_lower(l, key, klen);
    c->node = NULL;
  }
}

static void lsm_free_mem(ds_lsm_t *l) {
  size_t i;
  if (lsm_skip(l)) {
    lsm_sl_t *head = (lsm_sl_t *)l->skip_head;
    lsm_sl_t *n = head == NULL ? NULL : sl_fwd(head)[0];
    while (n != NULL) {
      lsm_sl_t *next = sl_fwd(n)[0];
      ds_context_free(l->config.context, n);
      n = next;
    }
    if (head != NULL) {
      for (i = 0; i < LSM_SL_MAX; i++) {
        sl_fwd(head)[i] = NULL;
      }
    }
    l->skip_level = 1;
  } else {
    for (i = 0; i < l->mem_count; i++) {
      ds_context_free(l->config.context, l->mem[i]);
    }
  }
  l->mem_count = 0;
  l->mem_bytes = 0;
}

static void lsm_free_table(ds_lsm_t *l, ds_lsm_table_t *t) {
  ds_sstable_close(&t->table);
  ds_context_free(l->config.context, t->data);
  memset(t, 0, sizeof(*t));
}

/* how this tree writes tables */
static void lsm_options(const ds_lsm_t *l, ds_sstable_options_t *o) {
  ds_sstable_options_init(o);
  o->block_size = l->config.block_size;
  o->restart_interval = l->config.restart_interval;
  o->bits_per_key = l->config.bits_per_key;
  o->filter = l->config.filter;
  o->filter_fp_bits = l->config.filter_fp_bits;
  o->comparator = &l->cmp;
}

static ds_status_t lsm_open(ds_lsm_t *l, ds_lsm_table_t *t) {
  return ds_sstable_open_ex(&t->table, t->data, t->len, &l->cmp,
                            l->config.filter, l->config.context);
}

/* ---- persistence through a page store ---- */

static ds_status_t lsm_set(ds_lsm_t *l, const void *key, size_t klen,
                           const void *value, size_t vlen, int tomb);
static ds_status_t lsm_compact(ds_lsm_t *l);

#define LSM_LOG_HEADER 44
#define LSM_SEALED 1u
#define LSM_M32 0xFFFFFFFFUL

static const char lsm_slot_magic[8] = {'D', 'S', 'L', 'S', 'M', 'S', 'L', '1'};
static const char lsm_man_magic[8] = {'D', 'S', 'L', 'S', 'M', 'M', 'F', '1'};
static const char lsm_log_magic[8] = {'D', 'S', 'L', 'S', 'M', 'L', 'G', '1'};

static void lsm_put4(unsigned char *p, unsigned long v) {
  p[0] = (unsigned char)(v & 0xFFUL);
  p[1] = (unsigned char)((v >> 8) & 0xFFUL);
  p[2] = (unsigned char)((v >> 16) & 0xFFUL);
  p[3] = (unsigned char)((v >> 24) & 0xFFUL);
}

static unsigned long lsm_get4(const unsigned char *p) {
  return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
         ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static void lsm_put8(unsigned char *p, unsigned long v) {
  lsm_put4(p, v & LSM_M32);
  lsm_put4(p + 4, ((v >> 16) >> 16) & LSM_M32);
}

/* 0 when the value does not fit an unsigned long */
static int lsm_get8(const unsigned char *p, unsigned long *v) {
  unsigned long lo = lsm_get4(p);
  unsigned long hi = lsm_get4(p + 4);
  unsigned long back = (hi << 16) << 16;
  if (hi != 0 && ((back >> 16) >> 16) != hi) {
    return 0;
  }
  *v = lo | back;
  return 1;
}

/* whether a length read from the store fits a size_t */
static int lsm_fits(unsigned long v) {
  size_t s = (size_t)v;
  return (unsigned long)s == v;
}

static size_t lsm_page_size(const ds_lsm_t *l) {
  return l->config.store->page_size;
}

/* the extent list: sorted by start, each tagged with the newest manifest
 * that references it (l->seq + 1 for pages not yet published) */
static ds_status_t lsm_ext_insert(ds_lsm_t *l, unsigned long start,
                                  unsigned long pages, unsigned long seq) {
  size_t i = 0;
  size_t need;
  if (start < 2 || pages == 0 || start > (unsigned long)-1 - pages) {
    return DS_ERR_INVALID;
  }
  while (i < l->extent_count && l->extents[i].start < start) {
    i++;
  }
  if ((i > 0 && l->extents[i - 1].start + l->extents[i - 1].pages > start) ||
      (i < l->extent_count && start + pages > l->extents[i].start)) {
    return DS_ERR_INVALID; /* overlapping runs or a cyclic log chain */
  }
  if (!ds_size_add(l->extent_count, 1, &need)) {
    return DS_ERR_OVERFLOW;
  }
  if (l->extent_count == l->extent_capacity) {
    size_t nc = l->extent_capacity < 16 ? 32 : l->extent_capacity;
    size_t bytes;
    void *p;
    if (nc < need) {
      nc = nc > (size_t)-1 / 2 ? need : 2 * nc;
    }
    if (!ds_size_mul(nc, sizeof(ds_lsm_extent_t), &bytes)) {
      return DS_ERR_OVERFLOW;
    }
    p = ds_context_realloc(l->config.context, l->extents, bytes);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    l->extents = (ds_lsm_extent_t *)p;
    l->extent_capacity = nc;
  }
  memmove(l->extents + i + 1, l->extents + i,
          (l->extent_count - i) * sizeof(ds_lsm_extent_t));
  l->extents[i].start = start;
  l->extents[i].pages = pages;
  l->extents[i].seq = seq;
  l->extent_count++;
  return DS_OK;
}

/* first fit after the two slot pages */
static ds_status_t lsm_ext_alloc(ds_lsm_t *l, unsigned long pages,
                                 unsigned long *start) {
  unsigned long at = 2;
  size_t i;
  if (l->seq == (unsigned long)-1) {
    return DS_ERR_OVERFLOW;
  }
  for (i = 0; i < l->extent_count; i++) {
    if (l->extents[i].start >= at && l->extents[i].start - at >= pages) {
      break;
    }
    if (l->extents[i].start + l->extents[i].pages > at) {
      at = l->extents[i].start + l->extents[i].pages;
    }
  }
  if (at > (unsigned long)-1 - pages) {
    return DS_ERR_OVERFLOW;
  }
  *start = at;
  return lsm_ext_insert(l, at, pages, l->seq + 1);
}

static void lsm_ext_mark(ds_lsm_t *l, unsigned long start, unsigned long seq) {
  size_t lo = 0;
  size_t hi = l->extent_count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (l->extents[mid].start < start) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo < l->extent_count && l->extents[lo].start == start) {
    l->extents[lo].seq = seq;
  }
}

/* forget pages no manifest that might be read references any more, and
 * shrink the store past the last page in use */
static void lsm_ext_sweep(ds_lsm_t *l) {
  size_t i;
  size_t k = 0;
  unsigned long end = 2;
  for (i = 0; i < l->extent_count; i++) {
    if (l->seq == 0 || l->extents[i].seq >= l->seq - 1) {
      l->extents[k++] = l->extents[i];
      if (l->extents[i].start + l->extents[i].pages > end) {
        end = l->extents[i].start + l->extents[i].pages;
      }
    }
  }
  l->extent_count = k;
  if (ds_page_store_count(l->config.store) > end) {
    (void)ds_page_store_truncate(l->config.store, end);
  }
}

static unsigned long lsm_pages_for(const ds_lsm_t *l, size_t len) {
  size_t ps = lsm_page_size(l);
  return (unsigned long)(len / ps + (len % ps != 0 || len == 0 ? 1 : 0));
}

/* len bytes at page start, the last page padded with zeros */
static ds_status_t lsm_write_run(ds_lsm_t *l, unsigned long start,
                                 const unsigned char *data, size_t len) {
  size_t ps = lsm_page_size(l);
  unsigned long pages = lsm_pages_for(l, len);
  unsigned long k;
  unsigned char *buf;
  ds_status_t st = DS_OK;
  buf = (unsigned char *)ds_context_alloc(l->config.context, ps);
  if (buf == NULL) {
    return DS_ERR_ALLOC;
  }
  for (k = 0; k < pages && st == DS_OK; k++) {
    size_t off = (size_t)k * ps;
    size_t take = len - off < ps ? len - off : ps;
    if (off < len) {
      memcpy(buf, data + off, take);
    } else {
      take = 0;
    }
    memset(buf + take, 0, ps - take);
    st = ds_page_store_write(l->config.store, start + k, buf);
  }
  ds_context_free(l->config.context, buf);
  return st;
}

static ds_status_t lsm_read_run(ds_lsm_t *l, unsigned long start, size_t len,
                                unsigned char **out) {
  size_t ps = lsm_page_size(l);
  unsigned long pages = lsm_pages_for(l, len);
  unsigned long k;
  unsigned char *buf;
  size_t bytes;
  ds_status_t st = DS_OK;
  unsigned long count = ds_page_store_count(l->config.store);
  if (start < 2 || start > count || pages > count - start) {
    return DS_ERR_INVALID;
  }
  if (!ds_size_mul((size_t)pages, ps, &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  buf = (unsigned char *)ds_context_alloc(l->config.context, bytes);
  if (buf == NULL) {
    return DS_ERR_ALLOC;
  }
  for (k = 0; k < pages && st == DS_OK; k++) {
    st = ds_page_store_read(l->config.store, start + k, buf + (size_t)k * ps);
  }
  if (st != DS_OK) {
    ds_context_free(l->config.context, buf);
    return st;
  }
  *out = buf;
  return DS_OK;
}

/* writes a new table's bytes to fresh pages */
static ds_status_t lsm_store_table(ds_lsm_t *l, ds_lsm_table_t *t) {
  ds_status_t st;
  unsigned long start;
  unsigned long pages;
  if (l->config.store == NULL) {
    return DS_OK;
  }
  pages = lsm_pages_for(l, t->len);
  st = lsm_ext_alloc(l, pages, &start);
  if (st == DS_OK) {
    st = lsm_write_run(l, start, t->data, t->len);
  }
  if (st == DS_OK) {
    t->page = start;
    t->pages = pages;
  }
  return st;
}

static ds_status_t lsm_pending_reserve(ds_lsm_t *l, size_t extra) {
  size_t need;
  if (!ds_size_add(l->pending_len, extra, &need)) {
    return DS_ERR_OVERFLOW;
  }
  if (need > l->pending_capacity) {
    size_t nc = l->pending_capacity < 256 ? 512 : l->pending_capacity;
    void *p;
    while (nc < need) {
      if (nc > (size_t)-1 / 2) {
        return DS_ERR_OVERFLOW;
      }
      nc *= 2;
    }
    p = ds_context_realloc(l->config.context, l->pending, nc);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    l->pending = (unsigned char *)p;
    l->pending_capacity = nc;
  }
  return DS_OK;
}

static size_t lsm_varint(unsigned char *p, size_t v) {
  size_t n = 0;
  while (v >= 0x80) {
    p[n++] = (unsigned char)((v & 0x7F) | 0x80);
    v >>= 7;
  }
  p[n++] = (unsigned char)v;
  return n;
}

/* appends an update to the records not yet written */
static ds_status_t lsm_log_record(ds_lsm_t *l, const void *key, size_t klen,
                                  const void *value, size_t vlen, int tomb) {
  unsigned char head[1 + 2 * (2 * sizeof(size_t) + 1)];
  size_t h = 0;
  size_t total;
  ds_status_t st;
  head[h++] = (unsigned char)(tomb ? 2 : 1);
  h += lsm_varint(head + h, klen);
  if (!tomb) {
    h += lsm_varint(head + h, vlen);
  }
  if (!ds_size_add(h, klen, &total) || !ds_size_add(total, vlen, &total)) {
    return DS_ERR_OVERFLOW;
  }
  st = lsm_pending_reserve(l, total);
  if (st != DS_OK) {
    return st;
  }
  memcpy(l->pending + l->pending_len, head, h);
  if (klen > 0) {
    memcpy(l->pending + l->pending_len + h, key, klen);
  }
  if (vlen > 0) {
    memcpy(l->pending + l->pending_len + h + klen, value, vlen);
  }
  l->pending_len += total;
  return DS_OK;
}

/* writes the page being filled: version log_version at the pair's
 * location for it */
static ds_status_t lsm_log_write_page(ds_lsm_t *l, int sealed) {
  unsigned char *pg = l->log_page;
  size_t ps = lsm_page_size(l);
  unsigned long crc;
  ds_status_t st;
  /* Version and index fields occupy 32 bits even on an LP64 writer.
   * Seal before a version wraps, preserving monotone recovery ordering. */
  if (l->log_version == LSM_M32) {
    sealed = 1;
  }
  if (l->log_version > LSM_M32 || l->log_index > LSM_M32 ||
      (sealed && l->log_index == LSM_M32)) {
    return DS_ERR_OVERFLOW;
  }
  if (l->log_next == 0) {
    /* the pair after this one is chosen before the page is first written,
     * so every version names the same successor */
    st = lsm_ext_alloc(l, 2, &l->log_next);
    if (st != DS_OK) {
      l->log_next = 0;
      return st;
    }
    lsm_ext_mark(l, l->log_next, l->seq);
  }
  memcpy(pg, lsm_log_magic, 8);
  lsm_put8(pg + 8, l->log_id);
  lsm_put4(pg + 16, l->log_index & LSM_M32);
  lsm_put4(pg + 20, l->log_version & LSM_M32);
  lsm_put8(pg + 24, l->log_next);
  lsm_put4(pg + 32, (unsigned long)l->log_used);
  pg[36] = (unsigned char)(sealed ? LSM_SEALED : 0u);
  pg[37] = pg[38] = pg[39] = 0;
  memset(pg + LSM_LOG_HEADER + l->log_used, 0,
         ps - LSM_LOG_HEADER - l->log_used);
  crc = ds_sstable_crc32c(0, pg, 40);
  crc = ds_sstable_crc32c(crc, pg + LSM_LOG_HEADER, l->log_used);
  lsm_put4(pg + 40, crc);
  st = ds_page_store_write(l->config.store,
                           l->log_slot + (l->log_version & 1UL), pg);
  if (st != DS_OK) {
    return st;
  }
  if (sealed) {
    l->log_slot = l->log_next;
    l->log_next = 0;
    l->log_index++;
    l->log_version = 0;
    l->log_used = 0;
    l->log_written = 0;
  } else {
    l->log_version++;
    l->log_written = 1;
  }
  l->log_dirty = 0;
  return DS_OK;
}

/* moves the records not yet written into log pages: full pages are
 * sealed, the last one is written partly filled */
static ds_status_t lsm_log_flush_pending(ds_lsm_t *l) {
  size_t payload = lsm_page_size(l) - LSM_LOG_HEADER;
  size_t off = 0;
  ds_status_t st = DS_OK;
  for (;;) {
    size_t take;
    if (l->log_used == payload) {
      st = lsm_log_write_page(l, 1);
      if (st != DS_OK) {
        break;
      }
    }
    if (off == l->pending_len) {
      break;
    }
    take = payload - l->log_used;
    if (take > l->pending_len - off) {
      take = l->pending_len - off;
    }
    memcpy(l->log_page + LSM_LOG_HEADER + l->log_used, l->pending + off, take);
    l->log_used += take;
    off += take;
    l->log_dirty = 1;
  }
  if (st == DS_OK && l->log_dirty) {
    st = lsm_log_write_page(l, 0);
  }
  /* what was copied stays in the page image even if a write failed */
  if (off > 0) {
    memmove(l->pending, l->pending + off, l->pending_len - off);
    l->pending_len -= off;
  }
  return st;
}

ds_status_t ds_lsm_sync(ds_lsm_t *lsm) {
  ds_status_t st;
  if (lsm == NULL || lsm->l0 == NULL) {
    return DS_ERR_NULL;
  }
  if (lsm->broken) {
    return DS_ERR_STATE;
  }
  if (lsm->config.store == NULL) {
    return DS_OK;
  }
  if (lsm->sync_failed) {
    /* A failed sync may leave the newest log image accepted but not yet
     * durable. Commit it before another version reuses the alternate
     * location, which can still hold the last successfully synced image. */
    st = ds_page_store_sync(lsm->config.store);
    if (st != DS_OK) {
      return st;
    }
    lsm->sync_failed = false;
  }
  st = lsm_log_flush_pending(lsm);
  if (st == DS_OK) {
    st = ds_page_store_sync(lsm->config.store);
  }
  if (st == DS_OK) {
    lsm->unsynced = 0;
  } else {
    lsm->sync_failed = true;
  }
  return st;
}

/* the manifest: every table with its pages, the log, the counters */
static ds_status_t lsm_encode_manifest(ds_lsm_t *l, unsigned long seq,
                                       unsigned long log_head,
                                       unsigned char **out, size_t *len) {
  size_t n = l->l0_count;
  size_t i;
  size_t k = 0;
  unsigned char *p;
  for (i = 0; i < DS_LSM_MAX_LEVELS; i++) {
    n += l->levels[i].data != NULL ? 1U : 0U;
  }
  *len = 8 + 8 * 8 + n * 32;
  p = (unsigned char *)ds_context_alloc(l->config.context, *len);
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  memcpy(p, lsm_man_magic, 8);
  lsm_put8(p + 8, seq);
  lsm_put8(p + 16, (unsigned long)lsm_page_size(l));
  lsm_put8(p + 24, l->cmp.id);
  lsm_put8(p + 32, seq); /* the log id is the manifest's sequence */
  lsm_put8(p + 40, log_head);
  lsm_put8(p + 48, l->flushes);
  lsm_put8(p + 56, l->compactions);
  lsm_put8(p + 64, (unsigned long)n);
  for (i = 0; i < l->l0_count + DS_LSM_MAX_LEVELS; i++) {
    const ds_lsm_table_t *t =
        i < l->l0_count ? &l->l0[i] : &l->levels[i - l->l0_count];
    unsigned char *e = p + 72 + 32 * k;
    if (t->data == NULL) {
      continue;
    }
    lsm_put8(e, i < l->l0_count ? 0UL : (unsigned long)(i - l->l0_count + 1));
    lsm_put8(e + 8, t->page);
    lsm_put8(e + 16, t->pages);
    lsm_put8(e + 24, (unsigned long)t->len);
    k++;
  }
  *out = p;
  return DS_OK;
}

/* writes the manifest of the current tables with an empty log, then
 * publishes it through the alternate slot; the memtable must be empty */
static ds_status_t lsm_publish(ds_lsm_t *l) {
  unsigned char *man = NULL;
  unsigned char *slot = NULL;
  size_t len = 0;
  size_t i;
  unsigned long head = 0;
  unsigned long mstart = 0;
  unsigned long seq;
  ds_status_t st;
  if (l->config.store == NULL) {
    return DS_OK;
  }
  if (l->broken) {
    return DS_ERR_STATE;
  }
  if (l->seq == (unsigned long)-1) {
    return DS_ERR_OVERFLOW;
  }
  seq = l->seq + 1;
  st = lsm_ext_alloc(l, 2, &head);
  if (st == DS_OK) {
    st = lsm_encode_manifest(l, seq, head, &man, &len);
  }
  if (st == DS_OK) {
    st = lsm_ext_alloc(l, lsm_pages_for(l, len), &mstart);
  }
  if (st == DS_OK) {
    st = lsm_write_run(l, mstart, man, len);
  }
  if (st == DS_OK) {
    st = ds_page_store_sync(l->config.store);
  }
  if (st == DS_OK) {
    slot = (unsigned char *)ds_context_calloc(l->config.context, 1,
                                              lsm_page_size(l));
    if (slot == NULL) {
      st = DS_ERR_ALLOC;
    }
  }
  if (st == DS_OK) {
    memcpy(slot, lsm_slot_magic, 8);
    lsm_put8(slot + 8, seq);
    lsm_put8(slot + 16, mstart);
    lsm_put8(slot + 24, (unsigned long)len);
    lsm_put4(slot + 32, ds_sstable_crc32c(0, man, len));
    lsm_put4(slot + 36, ds_sstable_crc32c(0, slot, 36));
    st = ds_page_store_write(l->config.store, seq & 1UL, slot);
    if (st == DS_OK) {
      st = ds_page_store_sync(l->config.store);
    }
    if (st != DS_OK) {
      /* the slot may or may not have reached the store: further updates
       * could land in a log the store no longer reads */
      l->broken = 1;
    }
  }
  ds_context_free(l->config.context, man);
  ds_context_free(l->config.context, slot);
  if (st != DS_OK) {
    return st;
  }
  l->seq = seq;
  lsm_ext_mark(l, mstart, seq);
  lsm_ext_mark(l, head, seq);
  for (i = 0; i < l->l0_count; i++) {
    lsm_ext_mark(l, l->l0[i].page, seq);
  }
  for (i = 0; i < DS_LSM_MAX_LEVELS; i++) {
    if (l->levels[i].data != NULL) {
      lsm_ext_mark(l, l->levels[i].page, seq);
    }
  }
  /* the new log starts empty: everything logged so far is in tables */
  l->log_id = seq;
  l->log_head = l->log_slot = head;
  l->log_index = 0;
  l->log_next = 0;
  l->log_version = 0;
  l->log_written = 0;
  l->log_dirty = 0;
  l->log_used = 0;
  l->pending_len = 0;
  l->unsynced = 0;
  l->sync_failed = false;
  lsm_ext_sweep(l);
  return DS_OK;
}

/* ---- recovery ---- */

typedef struct lsm_slot {
  unsigned long seq;
  unsigned long start;
  unsigned long len;
  unsigned long crc;
} lsm_slot_t;

static ds_status_t lsm_read_slot(ds_lsm_t *l, unsigned long page,
                                 unsigned char *buf, lsm_slot_t *s, int *ok) {
  ds_status_t st = ds_page_store_read(l->config.store, page, buf);
  *ok = 0;
  if (st != DS_OK) {
    return st;
  }
  if (memcmp(buf, lsm_slot_magic, 8) != 0 ||
      lsm_get4(buf + 36) != ds_sstable_crc32c(0, buf, 36) ||
      !lsm_get8(buf + 8, &s->seq) || !lsm_get8(buf + 16, &s->start) ||
      !lsm_get8(buf + 24, &s->len) || (s->seq & 1UL) != page || s->start < 2 ||
      s->seq == 0 || s->len < 72) {
    return DS_OK;
  }
  s->crc = lsm_get4(buf + 32);
  *ok = 1;
  return DS_OK;
}

/* loads the tables of a manifest; *log_head gets the log */
static ds_status_t lsm_load(ds_lsm_t *l, const lsm_slot_t *slot,
                            unsigned long *log_head) {
  unsigned char *man = NULL;
  unsigned long v;
  unsigned long n;
  unsigned long i;
  size_t l0 = 0;
  ds_status_t st;
  if (!lsm_fits(slot->len)) {
    return DS_ERR_INVALID;
  }
  st = lsm_read_run(l, slot->start, (size_t)slot->len, &man);
  if (st != DS_OK) {
    return st;
  }
  st = DS_ERR_INVALID;
  if (ds_sstable_crc32c(0, man, (size_t)slot->len) != slot->crc ||
      memcmp(man, lsm_man_magic, 8) != 0 || !lsm_get8(man + 8, &v) ||
      v != slot->seq || !lsm_get8(man + 16, &v) ||
      v != (unsigned long)lsm_page_size(l) || !lsm_get8(man + 24, &v) ||
      v != l->cmp.id || !lsm_get8(man + 32, &v) || v != slot->seq ||
      !lsm_get8(man + 40, log_head) || !lsm_get8(man + 48, &l->flushes) ||
      !lsm_get8(man + 56, &l->compactions) || !lsm_get8(man + 64, &n) ||
      n > 64 + DS_LSM_MAX_LEVELS || 72 + 32 * n != slot->len) {
    goto done;
  }
  for (i = 0; i < n; i++) {
    if (lsm_get8(man + 72 + 32 * i, &v) && v == 0) {
      l0++;
    }
  }
  if (l0 > 64) {
    goto done; /* the merge fan-in allows at most 64 level-zero tables */
  }
  if (l0 > l->config.l0_tables) {
    void *p = ds_context_realloc(l->config.context, l->l0,
                                 (l0 + 1) * sizeof(ds_lsm_table_t));
    if (p == NULL) {
      st = DS_ERR_ALLOC;
      goto done;
    }
    l->l0 = (ds_lsm_table_t *)p;
    memset(l->l0, 0, (l0 + 1) * sizeof(ds_lsm_table_t));
  }
  st = lsm_ext_insert(l, slot->start, lsm_pages_for(l, (size_t)slot->len),
                      slot->seq);
  for (i = 0; i < n && st == DS_OK; i++) {
    const unsigned char *e = man + 72 + 32 * i;
    unsigned long level;
    unsigned long start;
    unsigned long pages;
    unsigned long len;
    ds_lsm_table_t *t;
    if (!lsm_get8(e, &level) || !lsm_get8(e + 8, &start) ||
        !lsm_get8(e + 16, &pages) || !lsm_get8(e + 24, &len) ||
        level > DS_LSM_MAX_LEVELS || start < 2 || pages == 0 ||
        !lsm_fits(len) || pages != lsm_pages_for(l, (size_t)len)) {
      st = DS_ERR_INVALID;
      break;
    }
    t = level == 0 ? &l->l0[l->l0_count] : &l->levels[level - 1];
    if (t->data != NULL) {
      st = DS_ERR_INVALID;
      break;
    }
    st = lsm_ext_insert(l, start, pages, slot->seq);
    if (st == DS_OK) {
      st = lsm_read_run(l, start, (size_t)len, &t->data);
    }
    if (st == DS_OK) {
      t->len = (size_t)len;
      t->page = start;
      t->pages = pages;
      st = lsm_open(l, t);
      if (st != DS_OK) {
        ds_context_free(l->config.context, t->data);
        t->data = NULL;
        st = st == DS_ERR_ALLOC ? st : DS_ERR_INVALID;
      } else if (level == 0) {
        l->l0_count++;
      }
    }
  }
  /* extents must not overlap */
  for (i = 1; st == DS_OK && i < (unsigned long)l->extent_count; i++) {
    if (l->extents[i - 1].start + l->extents[i - 1].pages >
        l->extents[i].start) {
      st = DS_ERR_INVALID;
    }
  }
done:
  ds_context_free(l->config.context, man);
  return st;
}

static size_t lsm_get_varint(const unsigned char *p, size_t n, size_t *pos,
                             size_t *v) {
  size_t shift = 0;
  *v = 0;
  while (*pos < n) {
    unsigned char b = p[(*pos)++];
    if (shift >= 8 * sizeof(size_t) ||
        ((size_t)(b & 0x7F) << shift) >> shift != (size_t)(b & 0x7F)) {
      return 0;
    }
    *v |= (size_t)(b & 0x7F) << shift;
    if ((b & 0x80) == 0) {
      return 1;
    }
    shift += 7;
  }
  return 0;
}

/* replays the log into the memtable: pages in chain order, each the newer
 * valid version of its pair, until one is missing or a page that is not
 * sealed ends it; a record cut short at the end is dropped */
static ds_status_t lsm_replay(ds_lsm_t *l, unsigned long head) {
  size_t ps = lsm_page_size(l);
  unsigned char *a = NULL;
  unsigned char *b = NULL;
  unsigned char *data = NULL;
  size_t len = 0;
  size_t cap = 0;
  size_t pos = 0;
  unsigned long pair = head;
  unsigned long index = 0;
  ds_status_t st = DS_OK;
  a = (unsigned char *)ds_context_alloc(l->config.context, ps);
  b = (unsigned char *)ds_context_alloc(l->config.context, ps);
  if (a == NULL || b == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  st = lsm_ext_insert(l, head, 2, l->seq);
  while (st == DS_OK) {
    unsigned char *pg[2];
    unsigned char *best = NULL;
    unsigned long best_version = 0;
    unsigned long used;
    unsigned long next = 0;
    int k;
    pg[0] = a;
    pg[1] = b;
    for (k = 0; k < 2; k++) {
      unsigned long id;
      unsigned long ver;
      unsigned long u;
      unsigned long nx;
      st = ds_page_store_read(l->config.store, pair + (unsigned long)k, pg[k]);
      if (st != DS_OK) {
        break;
      }
      if (memcmp(pg[k], lsm_log_magic, 8) != 0 || !lsm_get8(pg[k] + 8, &id) ||
          id != l->seq || lsm_get4(pg[k] + 16) != (index & LSM_M32)) {
        continue;
      }
      ver = lsm_get4(pg[k] + 20);
      u = lsm_get4(pg[k] + 32);
      if ((ver & 1UL) != (unsigned long)k || u > ps - LSM_LOG_HEADER ||
          !lsm_get8(pg[k] + 24, &nx) ||
          lsm_get4(pg[k] + 40) !=
              ds_sstable_crc32c(ds_sstable_crc32c(0, pg[k], 40),
                                pg[k] + LSM_LOG_HEADER, (size_t)u)) {
        continue;
      }
      if (best == NULL || ver > best_version) {
        best = pg[k];
        best_version = ver;
      }
    }
    if (st != DS_OK || best == NULL) {
      break;
    }
    used = lsm_get4(best + 32);
    {
      size_t need;
      if (!ds_size_add(len, (size_t)used, &need)) {
        st = DS_ERR_OVERFLOW;
        break;
      }
      if (need > cap) {
        size_t nc = cap < 4096 ? 8192 : cap;
        void *p;
        while (nc < need) {
          if (nc > (size_t)-1 / 2) {
            nc = need;
            break;
          }
          nc *= 2;
        }
        p = ds_context_realloc(l->config.context, data, nc);
        if (p == NULL) {
          st = DS_ERR_ALLOC;
          break;
        }
        data = (unsigned char *)p;
        cap = nc;
      }
    }
    memcpy(data + len, best + LSM_LOG_HEADER, (size_t)used);
    len += (size_t)used;
    if ((best[36] & LSM_SEALED) == 0 || !lsm_get8(best + 24, &next) ||
        next < 2) {
      break;
    }
    pair = next;
    if (index == LSM_M32) {
      st = DS_ERR_INVALID;
      break;
    }
    index++;
    st = lsm_ext_insert(l, pair, 2, l->seq);
  }
  /* the records */
  while (st == DS_OK && pos < len) {
    size_t klen;
    size_t vlen = 0;
    unsigned char type = data[pos++];
    if ((type != 1 && type != 2) || !lsm_get_varint(data, len, &pos, &klen) ||
        (type == 1 && !lsm_get_varint(data, len, &pos, &vlen)) ||
        klen > len - pos || vlen > len - pos - klen) {
      break; /* cut short */
    }
    st = lsm_set(l, data + pos, klen, data + pos + klen, vlen, type == 2);
    pos += klen + vlen;
  }
done:
  ds_context_free(l->config.context, a);
  ds_context_free(l->config.context, b);
  ds_context_free(l->config.context, data);
  return st;
}

/* opens the tree a store holds, or starts one in an empty store */
static ds_status_t lsm_recover(ds_lsm_t *l) {
  lsm_slot_t slots[2];
  int ok[2];
  int order[2];
  int k;
  size_t extent_i;
  unsigned long head = 0;
  unsigned char *buf;
  ds_status_t st = DS_ERR_INVALID;
  if (ds_page_store_count(l->config.store) == 0) {
    return lsm_publish(l);
  }
  buf = (unsigned char *)ds_context_alloc(l->config.context, lsm_page_size(l));
  if (buf == NULL) {
    return DS_ERR_ALLOC;
  }
  st = lsm_read_slot(l, 0, buf, &slots[0], &ok[0]);
  if (st == DS_OK) {
    st = lsm_read_slot(l, 1, buf, &slots[1], &ok[1]);
  }
  ds_context_free(l->config.context, buf);
  if (st != DS_OK) {
    return st;
  }
  st = DS_ERR_INVALID;
  /* newest first */
  order[0] = ok[1] && (!ok[0] || slots[1].seq > slots[0].seq) ? 1 : 0;
  order[1] = 1 - order[0];
  for (k = 0; k < 2; k++) {
    int s = order[k];
    size_t i;
    if (!ok[s]) {
      continue;
    }
    l->seq = slots[s].seq;
    st = lsm_load(l, &slots[s], &head);
    if (st == DS_OK) {
      break;
    }
    /* undo a partial load before trying the older manifest */
    for (i = 0; i < l->l0_count; i++) {
      lsm_free_table(l, &l->l0[i]);
    }
    for (i = 0; i < DS_LSM_MAX_LEVELS; i++) {
      lsm_free_table(l, &l->levels[i]);
    }
    l->l0_count = 0;
    l->extent_count = 0;
    if (st != DS_ERR_INVALID) {
      return st;
    }
  }
  if (st != DS_OK) {
    return st;
  }
  l->log_id = l->seq;
  l->log_head = l->log_slot = head;
  l->replaying = 1;
  st = lsm_replay(l, head);
  l->replaying = 0;
  if (st != DS_OK) {
    return st;
  }
  for (extent_i = 1; extent_i < l->extent_count; extent_i++) {
    if (l->extents[extent_i - 1].start + l->extents[extent_i - 1].pages >
        l->extents[extent_i].start) {
      return DS_ERR_INVALID; /* the log runs into a table */
    }
  }
  if (l->mem_count > 0) {
    /* a checkpoint: the replayed updates go into a table, the log restarts */
    st = ds_lsm_flush(l);
  } else if (l->l0_count > l->config.l0_tables) {
    st = lsm_compact(l);
    if (st == DS_OK) {
      st = lsm_publish(l);
    }
  }
  return st;
}

void ds_lsm_config_init(ds_lsm_config_t *config) {
  if (config != NULL) {
    memset(config, 0, sizeof(*config));
  }
}

ds_status_t ds_lsm_init(ds_lsm_t *lsm, const ds_lsm_config_t *config) {
  ds_lsm_config_t def;
  if (lsm == NULL) {
    return DS_ERR_NULL;
  }
  memset(lsm, 0, sizeof(*lsm));
  if (config == NULL) {
    ds_lsm_config_init(&def);
    config = &def;
  }
  lsm->config = *config;
  if (lsm->config.memtable_bytes == 0) {
    lsm->config.memtable_bytes = 1UL << 20;
  }
  if (lsm->config.l0_tables == 0) {
    lsm->config.l0_tables = 4;
  }
  if (lsm->config.l0_tables > 63) {
    lsm->config.l0_tables = 63; /* bounds the merge fan-in */
  }
  if (lsm->config.fanout < 2) {
    lsm->config.fanout = 10;
  }
  if (lsm->config.filter != NULL) {
    if (lsm->config.bits_per_key != 0) {
      return DS_ERR_INVALID;
    }
    if (lsm->config.filter->save == NULL || lsm->config.filter->load == NULL ||
        lsm->config.filter->id < 1 || lsm->config.filter->id > 255) {
      return DS_ERR_UNSUPPORTED;
    }
  } else if (lsm->config.bits_per_key == 0) {
    lsm->config.bits_per_key = 10;
  } else if (lsm->config.bits_per_key == DS_LSM_NO_FILTER) {
    lsm->config.bits_per_key = 0; /* the sstable setting for none */
  } else if (lsm->config.bits_per_key > 32) {
    return DS_ERR_INVALID;
  }
  if (lsm->config.memtable != DS_LSM_MEMTABLE_SKIPLIST &&
      lsm->config.memtable != DS_LSM_MEMTABLE_ARRAY) {
    return DS_ERR_INVALID;
  }
  if (lsm->config.comparator == NULL) {
    lsm->cmp = *ds_sstable_bytewise();
  } else if (lsm->config.comparator->compare == NULL ||
             (lsm->config.comparator->id == 0 &&
              lsm->config.comparator->compare !=
                  ds_sstable_bytewise()->compare)) {
    return DS_ERR_INVALID;
  } else {
    lsm->cmp = *lsm->config.comparator;
  }
  /* lsm->cmp is the copy in use; the caller's comparator is not kept */
  lsm->config.comparator = NULL;
  if (lsm->config.memtable == DS_LSM_MEMTABLE_SKIPLIST) {
    size_t i;
    lsm->skip_head = ds_context_alloc(
        config->context, sizeof(lsm_sl_t) + LSM_SL_MAX * sizeof(lsm_sl_t *));
    if (lsm->skip_head == NULL) {
      return DS_ERR_ALLOC;
    }
    ((lsm_sl_t *)lsm->skip_head)->level = LSM_SL_MAX;
    for (i = 0; i < LSM_SL_MAX; i++) {
      sl_fwd((lsm_sl_t *)lsm->skip_head)[i] = NULL;
    }
    lsm->skip_level = 1;
    lsm->skip_rng = 0x9E3779B9UL;
  }
  lsm->l0 = (ds_lsm_table_t *)ds_context_calloc(
      config->context, lsm->config.l0_tables + 1, sizeof(ds_lsm_table_t));
  if (lsm->l0 == NULL) {
    ds_context_free(config->context, lsm->skip_head);
    lsm->skip_head = NULL;
    return DS_ERR_ALLOC;
  }
  if (lsm->config.store != NULL) {
    ds_status_t st = DS_ERR_INVALID;
    if (lsm->config.store->ops != NULL && lsm->config.store->page_size >= 128 &&
        !DS_ABOVE_U32(lsm->config.store->page_size)) {
      lsm->log_page = (unsigned char *)ds_context_calloc(
          config->context, 1, lsm->config.store->page_size);
      st = lsm->log_page == NULL ? DS_ERR_ALLOC : lsm_recover(lsm);
    }
    if (st != DS_OK) {
      ds_lsm_destroy(lsm);
      return st;
    }
  }
  return DS_OK;
}

void ds_lsm_destroy(ds_lsm_t *lsm) {
  size_t i;
  if (lsm == NULL || lsm->l0 == NULL) {
    return;
  }
  lsm_free_mem(lsm);
  ds_context_free(lsm->config.context, lsm->mem);
  ds_context_free(lsm->config.context, lsm->skip_head);
  for (i = 0; i < lsm->l0_count; i++) {
    lsm_free_table(lsm, &lsm->l0[i]);
  }
  for (i = 0; i < DS_LSM_MAX_LEVELS; i++) {
    lsm_free_table(lsm, &lsm->levels[i]);
  }
  ds_context_free(lsm->config.context, lsm->l0);
  ds_context_free(lsm->config.context, lsm->extents);
  ds_context_free(lsm->config.context, lsm->log_page);
  ds_context_free(lsm->config.context, lsm->pending);
  memset(lsm, 0, sizeof(*lsm));
}

/* ---- compaction ---- */

static size_t lsm_level_limit(const ds_lsm_t *l, size_t i) {
  size_t lim, k;
  if (!ds_size_mul(l->config.memtable_bytes, l->config.l0_tables, &lim)) {
    return (size_t)-1;
  }
  for (k = 0; k < i; k++) {
    if (!ds_size_mul(lim, l->config.fanout, &lim)) {
      return (size_t)-1;
    }
  }
  return lim;
}

static bool lsm_deeper_empty(const ds_lsm_t *l, size_t level) {
  size_t i;
  for (i = level + 1; i < DS_LSM_MAX_LEVELS; i++) {
    if (l->levels[i].data != NULL) {
      return false;
    }
  }
  return true;
}

/* merge srcs (newest first) into levels[target]; the sources are freed
 * (and emptied) only on success */
static ds_status_t lsm_merge_into(ds_lsm_t *l, ds_lsm_table_t **srcs, size_t n,
                                  size_t target) {
  ds_sstable_t tables[DS_LSM_MAX_LEVELS + 64];
  ds_sstable_options_t o;
  ds_lsm_table_t out;
  size_t i;
  ds_status_t st;
  if (n > sizeof(tables) / sizeof(tables[0])) {
    return DS_ERR_RANGE;
  }
  for (i = 0; i < n; i++) {
    tables[i] = srcs[i]->table; /* shallow copies, only read */
  }
  memset(&out, 0, sizeof(out));
  lsm_options(l, &o);
  st = ds_sstable_merge_opts(tables, n, lsm_deeper_empty(l, target), &o,
                             &out.data, &out.len, l->config.context);
  if (st != DS_OK) {
    return st;
  }
  st = lsm_open(l, &out);
  if (st != DS_OK) {
    ds_context_free(l->config.context, out.data);
    return st;
  }
  if (ds_sstable_entries(&out.table) != 0) {
    st = lsm_store_table(l, &out);
    if (st != DS_OK) {
      lsm_free_table(l, &out);
      return st;
    }
  }
  for (i = 0; i < n; i++) {
    lsm_free_table(l, srcs[i]);
  }
  if (ds_sstable_entries(&out.table) == 0) {
    lsm_free_table(l, &out);
  } else {
    l->levels[target] = out;
  }
  l->compactions++;
  return DS_OK;
}

static ds_status_t lsm_compact(ds_lsm_t *l) {
  ds_lsm_table_t *srcs[DS_LSM_MAX_LEVELS + 64];
  size_t i, n = 0;
  ds_status_t st;
  if (l->l0_count > l->config.l0_tables) {
    for (i = 0; i < l->l0_count; i++) {
      srcs[n++] = &l->l0[i];
    }
    if (l->levels[0].data != NULL) {
      srcs[n++] = &l->levels[0];
    }
    st = lsm_merge_into(l, srcs, n, 0);
    if (st != DS_OK) {
      return st;
    }
    l->l0_count = 0;
  }
  for (i = 0; i + 1 < DS_LSM_MAX_LEVELS; i++) {
    if (l->levels[i].data != NULL && l->levels[i].len > lsm_level_limit(l, i)) {
      n = 0;
      srcs[n++] = &l->levels[i];
      if (l->levels[i + 1].data != NULL) {
        srcs[n++] = &l->levels[i + 1];
      }
      st = lsm_merge_into(l, srcs, n, i + 1);
      if (st != DS_OK) {
        return st;
      }
    }
  }
  return DS_OK;
}

ds_status_t ds_lsm_flush(ds_lsm_t *lsm) {
  ds_sstable_builder_t b;
  ds_sstable_options_t o;
  ds_lsm_table_t t;
  ds_status_t st;
  lsm_mcur_t c;
  if (lsm == NULL || lsm->l0 == NULL) {
    return DS_ERR_NULL;
  }
  if (lsm->broken) {
    return DS_ERR_STATE;
  }
  if (lsm->mem_count == 0) {
    /* A previous flush may have built tables but failed to publish them. */
    return lsm->config.store != NULL && lsm->unsynced != 0 ? lsm_publish(lsm)
                                                           : DS_OK;
  }
  if (lsm->l0_count > lsm->config.l0_tables) {
    /* an earlier compaction failed: retry it before adding a table */
    st = lsm_compact(lsm);
    if (st != DS_OK) {
      return st;
    }
  }
  lsm_options(lsm, &o);
  st = ds_sstable_builder_init_opts(&b, &o, lsm->config.context);
  if (st != DS_OK) {
    return st;
  }
  for (mcur_first(lsm, &c); mcur_valid(lsm, &c) && st == DS_OK;
       mcur_next(lsm, &c)) {
    lsm_ent_t *e = mcur_entry(lsm, &c);
    st = e->tomb ? ds_sstable_builder_add_tombstone(&b, ent_key(e), e->klen)
                 : ds_sstable_builder_add(&b, ent_key(e), e->klen, ent_val(e),
                                          e->vlen);
  }
  memset(&t, 0, sizeof(t));
  if (st == DS_OK) {
    st = ds_sstable_builder_finish(&b, &t.data, &t.len);
  }
  ds_sstable_builder_destroy(&b);
  if (st != DS_OK) {
    return st;
  }
  st = lsm_open(lsm, &t);
  if (st != DS_OK) {
    ds_context_free(lsm->config.context, t.data);
    return st;
  }
  st = lsm_store_table(lsm, &t);
  if (st != DS_OK) {
    lsm_free_table(lsm, &t);
    return st;
  }
  memmove(lsm->l0 + 1, lsm->l0, lsm->l0_count * sizeof(ds_lsm_table_t));
  lsm->l0[0] = t;
  lsm->l0_count++;
  lsm_free_mem(lsm);
  lsm->flushes++;
  /* the data is safe in level 0 even if a compaction fails now */
  (void)lsm_compact(lsm);
  /* with a store: the tables become durable and the log restarts (if this
   * fails, the log still holds the updates) */
  return lsm_publish(lsm);
}

static void ent_fill(lsm_ent_t *e, const void *key, size_t klen,
                     const void *value, size_t vlen, int tomb) {
  e->klen = klen;
  e->vlen = vlen;
  e->tomb = tomb;
  if (klen > 0) {
    memcpy(ent_key(e), key, klen);
  }
  if (vlen > 0) {
    memcpy(ent_val(e), value, vlen);
  }
}

/* skip-list insert or replace; bytes is the entry's size */
static ds_status_t sl_set(ds_lsm_t *l, const void *key, size_t klen,
                          const void *value, size_t vlen, int tomb,
                          size_t bytes) {
  lsm_sl_t *update[LSM_SL_MAX];
  lsm_sl_t *old = sl_find(l, (const unsigned char *)key, klen, update);
  lsm_sl_t *n;
  size_t level;
  size_t total;
  size_t i;
  bool replace =
      old != NULL && key_cmp(l, ent_key(sl_ent(old)), sl_ent(old)->klen,
                             (const unsigned char *)key, klen) == 0;
  level = replace ? old->level : sl_random_level(l);
  if (!ds_size_mul_add(level, sizeof(lsm_sl_t *), sizeof(lsm_sl_t), &total) ||
      !ds_size_add(total, bytes, &total)) {
    return DS_ERR_OVERFLOW;
  }
  n = (lsm_sl_t *)ds_context_alloc(l->config.context, total);
  if (n == NULL) {
    return DS_ERR_ALLOC;
  }
  n->level = level;
  ent_fill(sl_ent(n), key, klen, value, vlen, tomb);
  if (replace) {
    /* take the old node's place on each of its levels */
    for (i = 0; i < level; i++) {
      sl_fwd(n)[i] = sl_fwd(old)[i];
      sl_fwd(update[i])[i] = n;
    }
    l->mem_bytes -= sizeof(lsm_ent_t) + sl_ent(old)->klen + sl_ent(old)->vlen;
    ds_context_free(l->config.context, old);
  } else {
    for (i = 0; i < level; i++) {
      sl_fwd(n)[i] = sl_fwd(update[i])[i];
      sl_fwd(update[i])[i] = n;
    }
    if (level > l->skip_level) {
      l->skip_level = level;
    }
    l->mem_count++;
  }
  l->mem_bytes += bytes;
  return DS_OK;
}

/* sorted-array insert or replace */
static ds_status_t arr_set(ds_lsm_t *l, const void *key, size_t klen,
                           const void *value, size_t vlen, int tomb,
                           size_t bytes) {
  size_t pos = mem_lower(l, (const unsigned char *)key, klen);
  lsm_ent_t *e;
  if (pos == l->mem_count ||
      key_cmp(l, ent_key(mem_at(l, pos)), mem_at(l, pos)->klen,
              (const unsigned char *)key, klen) != 0) {
    if (l->mem_count == l->mem_capacity) {
      size_t cap = l->mem_capacity < 16 ? 16 : l->mem_capacity * 2, b2;
      void **m;
      if (l->mem_capacity > ((size_t)-1) / 2 ||
          !ds_size_mul(cap, sizeof(void *), &b2)) {
        return DS_ERR_OVERFLOW;
      }
      m = (void **)ds_context_realloc(l->config.context, l->mem, b2);
      if (m == NULL) {
        return DS_ERR_ALLOC;
      }
      l->mem = m;
      l->mem_capacity = cap;
    }
    e = (lsm_ent_t *)ds_context_alloc(l->config.context, bytes);
    if (e == NULL) {
      return DS_ERR_ALLOC;
    }
    memmove(l->mem + pos + 1, l->mem + pos,
            (l->mem_count - pos) * sizeof(void *));
    l->mem_count++;
  } else {
    lsm_ent_t *old = mem_at(l, pos);
    e = (lsm_ent_t *)ds_context_alloc(l->config.context, bytes);
    if (e == NULL) {
      return DS_ERR_ALLOC;
    }
    l->mem_bytes -= sizeof(lsm_ent_t) + old->klen + old->vlen;
    ds_context_free(l->config.context, old);
  }
  ent_fill(e, key, klen, value, vlen, tomb);
  l->mem[pos] = e;
  l->mem_bytes += bytes;
  return DS_OK;
}

static ds_status_t lsm_set(ds_lsm_t *l, const void *key, size_t klen,
                           const void *value, size_t vlen, int tomb) {
  size_t bytes;
  size_t mark;
  int logged;
  ds_status_t st;
  if (l == NULL || l->l0 == NULL || (key == NULL && klen > 0) ||
      (value == NULL && vlen > 0)) {
    return DS_ERR_NULL;
  }
  if (l->broken) {
    return DS_ERR_STATE;
  }
  if (!ds_size_add(sizeof(lsm_ent_t), klen, &bytes) ||
      !ds_size_add(bytes, vlen, &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  logged = l->config.store != NULL && !l->replaying;
  mark = l->pending_len;
  if (logged) {
    st = lsm_log_record(l, key, klen, value, vlen, tomb);
    if (st != DS_OK) {
      return st;
    }
  }
  st = lsm_skip(l) ? sl_set(l, key, klen, value, vlen, tomb, bytes)
                   : arr_set(l, key, klen, value, vlen, tomb, bytes);
  if (st != DS_OK) {
    l->pending_len = mark; /* not applied, so not logged */
    return st;
  }
  if (l->replaying) {
    return DS_OK;
  }
  if (l->unsynced != (size_t)-1) {
    l->unsynced++;
  }
  if (l->mem_bytes >= l->config.memtable_bytes) {
    /* An in-memory update can survive a failed automatic compaction.
     * Persistent updates must report failed publication, including an
     * ambiguous slot write that marks the tree broken. */
    st = ds_lsm_flush(l);
    if (logged && st != DS_OK) {
      return st;
    }
  }
  if (logged && l->config.sync_every != 0 &&
      l->unsynced >= l->config.sync_every) {
    return ds_lsm_sync(l);
  }
  return DS_OK;
}

ds_status_t ds_lsm_put(ds_lsm_t *lsm, const void *key, size_t key_len,
                       const void *value, size_t value_len) {
  return lsm_set(lsm, key, key_len, value, value_len, 0);
}

ds_status_t ds_lsm_delete(ds_lsm_t *lsm, const void *key, size_t key_len) {
  return lsm_set(lsm, key, key_len, NULL, 0, 1);
}

static ds_status_t lsm_table_get(ds_lsm_t *l, ds_lsm_table_t *t,
                                 const void *key, size_t klen,
                                 const unsigned char **v, size_t *vl) {
  bool tomb = false;
  ds_status_t st;
  if (t->data == NULL || !ds_sstable_may_contain(&t->table, key, klen)) {
    return DS_NOT_FOUND;
  }
  st = ds_sstable_get(&t->table, key, klen, v, vl, &tomb, l->config.context);
  if (st == DS_OK && tomb) {
    return DS_EMPTY; /* deleted */
  }
  return st;
}

ds_status_t ds_lsm_get(ds_lsm_t *lsm, const void *key, size_t key_len,
                       const unsigned char **out_value, size_t *out_len) {
  const unsigned char *v = NULL;
  size_t vl = 0, i;
  lsm_ent_t *e;
  ds_status_t st = DS_NOT_FOUND;
  if (lsm == NULL || lsm->l0 == NULL || (key == NULL && key_len > 0)) {
    return DS_ERR_NULL;
  }
  e = mem_find(lsm, (const unsigned char *)key, key_len);
  if (e != NULL) {
    if (e->tomb) {
      return DS_NOT_FOUND;
    }
    v = ent_val(e);
    vl = e->vlen;
    st = DS_OK;
  }
  for (i = 0; st == DS_NOT_FOUND && i < lsm->l0_count; i++) {
    st = lsm_table_get(lsm, &lsm->l0[i], key, key_len, &v, &vl);
  }
  for (i = 0; st == DS_NOT_FOUND && i < DS_LSM_MAX_LEVELS; i++) {
    st = lsm_table_get(lsm, &lsm->levels[i], key, key_len, &v, &vl);
  }
  if (st == DS_EMPTY) {
    return DS_NOT_FOUND;
  }
  if (st == DS_OK) {
    if (out_value != NULL) {
      *out_value = v;
    }
    if (out_len != NULL) {
      *out_len = vl;
    }
  }
  return st;
}

ds_status_t ds_lsm_compact_all(ds_lsm_t *lsm) {
  ds_lsm_table_t *srcs[DS_LSM_MAX_LEVELS + 64];
  size_t i, n = 0, target = 0;
  ds_status_t st;
  if (lsm == NULL || lsm->l0 == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_lsm_flush(lsm);
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < lsm->l0_count; i++) {
    srcs[n++] = &lsm->l0[i];
  }
  for (i = 0; i < DS_LSM_MAX_LEVELS; i++) {
    if (lsm->levels[i].data != NULL) {
      srcs[n++] = &lsm->levels[i];
      target = i;
    }
  }
  if (n <= 1 && lsm->l0_count == 0) {
    return DS_OK;
  }
  st = lsm_merge_into(lsm, srcs, n, target);
  if (st == DS_OK) {
    lsm->l0_count = 0;
    st = lsm_publish(lsm);
  }
  return st;
}

size_t ds_lsm_table_count(const ds_lsm_t *lsm) {
  size_t i, n;
  if (lsm == NULL) {
    return 0;
  }
  n = lsm->l0_count;
  for (i = 0; i < DS_LSM_MAX_LEVELS; i++) {
    n += lsm->levels[i].data != NULL ? 1U : 0U;
  }
  return n;
}

/* ---- iterators ---- */

/* source 0 is the memtable, then the tables newest to oldest */
static ds_sstable_t *lsm_source_table(ds_lsm_t *l, size_t k) {
  size_t i;
  if (k < l->l0_count) {
    return &l->l0[k].table;
  }
  k -= l->l0_count;
  for (i = 0; i < DS_LSM_MAX_LEVELS; i++) {
    if (l->levels[i].data != NULL) {
      if (k == 0) {
        return &l->levels[i].table;
      }
      k--;
    }
  }
  return NULL;
}

static void it_mcur(const ds_lsm_iter_t *it, lsm_mcur_t *c) {
  c->pos = it->mem_pos;
  c->node = (lsm_sl_t *)it->mem_node;
}

static void it_set_mcur(ds_lsm_iter_t *it, const lsm_mcur_t *c) {
  it->mem_pos = c->pos;
  it->mem_node = c->node;
}

static bool src_valid(const ds_lsm_iter_t *it, size_t s) {
  if (s == 0) {
    lsm_mcur_t c;
    it_mcur(it, &c);
    return mcur_valid(it->lsm, &c);
  }
  return ds_sstable_iter_valid(&it->iters[s - 1]);
}

static void src_get(const ds_lsm_iter_t *it, size_t s, const unsigned char **k,
                    size_t *kl, const unsigned char **v, size_t *vl,
                    bool *tomb) {
  if (s == 0) {
    lsm_mcur_t c;
    lsm_ent_t *e;
    it_mcur(it, &c);
    e = mcur_entry(it->lsm, &c);
    *k = ent_key(e);
    *kl = e->klen;
    *v = ent_val(e);
    *vl = e->vlen;
    *tomb = e->tomb != 0;
  } else {
    const ds_sstable_iter_t *si = &it->iters[s - 1];
    *k = si->key;
    *kl = si->key_len;
    *v = si->value;
    *vl = si->value_len;
    *tomb = si->tombstone;
  }
}

static void src_next(ds_lsm_iter_t *it, size_t s) {
  if (s == 0) {
    lsm_mcur_t c;
    it_mcur(it, &c);
    mcur_next(it->lsm, &c);
    it_set_mcur(it, &c);
  } else {
    ds_status_t st = ds_sstable_iter_next(&it->iters[s - 1]);
    if (DS_FAILED(st)) {
      it->error = st;
    }
  }
}

/* advance every source positioned at the winner's key */
static void lsm_skip_current(ds_lsm_iter_t *it, size_t win) {
  const unsigned char *wk, *k, *v;
  size_t wkl, kl, vl, s;
  bool tomb;
  unsigned char eq[DS_LSM_MAX_LEVELS + 66];
  src_get(it, win, &wk, &wkl, &v, &vl, &tomb);
  for (s = 0; s < it->sources; s++) {
    eq[s] = 0;
    if (src_valid(it, s)) {
      src_get(it, s, &k, &kl, &v, &vl, &tomb);
      eq[s] = key_cmp(it->lsm, k, kl, wk, wkl) == 0;
    }
  }
  for (s = 0; s < it->sources; s++) {
    if (eq[s]) {
      src_next(it, s);
    }
  }
}

static ds_status_t lsm_select(ds_lsm_iter_t *it) {
  for (;;) {
    size_t s, win = LSM_NONE;
    const unsigned char *wk = NULL, *k, *v;
    size_t wkl = 0, kl, vl;
    bool tomb;
    if (it->error != DS_OK) {
      it->current = LSM_NONE;
      return it->error;
    }
    for (s = 0; s < it->sources; s++) {
      if (!src_valid(it, s)) {
        continue;
      }
      src_get(it, s, &k, &kl, &v, &vl, &tomb);
      if (win == LSM_NONE || key_cmp(it->lsm, k, kl, wk, wkl) < 0) {
        win = s;
        wk = k;
        wkl = kl;
      }
    }
    it->current = win;
    if (win == LSM_NONE) {
      return DS_OK;
    }
    src_get(it, win, &it->key, &it->key_len, &it->value, &it->value_len, &tomb);
    if (!tomb) {
      return DS_OK;
    }
    lsm_skip_current(it, win);
  }
}

ds_status_t ds_lsm_iter_init(ds_lsm_iter_t *it, ds_lsm_t *lsm) {
  size_t n, i;
  ds_status_t st;
  if (it == NULL || lsm == NULL || lsm->l0 == NULL) {
    return DS_ERR_NULL;
  }
  memset(it, 0, sizeof(*it));
  n = ds_lsm_table_count(lsm);
  it->lsm = lsm;
  it->sources = n + 1;
  it->current = LSM_NONE;
  it->error = DS_OK;
  it->iters = (ds_sstable_iter_t *)ds_context_calloc(
      lsm->config.context, n == 0 ? 1 : n, sizeof(ds_sstable_iter_t));
  if (it->iters == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    st = ds_sstable_iter_init(&it->iters[i], lsm_source_table(lsm, i),
                              lsm->config.context);
    if (st != DS_OK) {
      it->sources = i + 1;
      ds_lsm_iter_destroy(it);
      return st;
    }
  }
  return DS_OK;
}

void ds_lsm_iter_destroy(ds_lsm_iter_t *it) {
  size_t i;
  if (it == NULL || it->lsm == NULL) {
    return;
  }
  for (i = 1; i < it->sources; i++) {
    ds_sstable_iter_destroy(&it->iters[i - 1]);
  }
  ds_context_free(it->lsm->config.context, it->iters);
  memset(it, 0, sizeof(*it));
}

ds_status_t ds_lsm_iter_first(ds_lsm_iter_t *it) {
  size_t s;
  if (it == NULL || it->lsm == NULL) {
    return DS_ERR_NULL;
  }
  it->error = DS_OK;
  {
    lsm_mcur_t c;
    mcur_first(it->lsm, &c);
    it_set_mcur(it, &c);
  }
  for (s = 1; s < it->sources; s++) {
    ds_status_t st = ds_sstable_iter_first(&it->iters[s - 1]);
    if (DS_FAILED(st)) {
      it->error = st;
    }
  }
  return lsm_select(it);
}

ds_status_t ds_lsm_iter_seek(ds_lsm_iter_t *it, const void *key,
                             size_t key_len) {
  size_t s;
  if (it == NULL || it->lsm == NULL || (key == NULL && key_len > 0)) {
    return DS_ERR_NULL;
  }
  it->error = DS_OK;
  {
    lsm_mcur_t c;
    mcur_seek(it->lsm, &c, (const unsigned char *)key, key_len);
    it_set_mcur(it, &c);
  }
  for (s = 1; s < it->sources; s++) {
    ds_status_t st = ds_sstable_iter_seek(&it->iters[s - 1], key, key_len);
    if (DS_FAILED(st)) {
      it->error = st;
    }
  }
  return lsm_select(it);
}

ds_status_t ds_lsm_iter_next(ds_lsm_iter_t *it) {
  if (it == NULL || it->lsm == NULL) {
    return DS_ERR_NULL;
  }
  if (it->current == LSM_NONE) {
    return DS_OK;
  }
  lsm_skip_current(it, it->current);
  return lsm_select(it);
}

bool ds_lsm_iter_valid(const ds_lsm_iter_t *it) {
  return it != NULL && it->current != LSM_NONE;
}
