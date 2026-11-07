#include "sstable.h"
#include <string.h>

#define SST_MAGIC 0x53535442UL /* "SSTB" */
#define SST_FOOTER 28
#define SST_MAXLEN 0x7FFFFFFFUL

/* ---- encoding helpers -------------------------------------------------------
 */

static void sst_put_u32(unsigned char *p, unsigned long v) {
  p[0] = (unsigned char)(v & 0xFFUL);
  p[1] = (unsigned char)((v >> 8) & 0xFFUL);
  p[2] = (unsigned char)((v >> 16) & 0xFFUL);
  p[3] = (unsigned char)((v >> 24) & 0xFFUL);
}

static unsigned long sst_get_u32(const unsigned char *p) {
  return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
         ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned long sst_fnv(const unsigned char *p, size_t n) {
  unsigned long h = 2166136261UL;
  size_t i;
  for (i = 0; i < n; i++) {
    h ^= p[i];
    h = (h * 16777619UL) & 0xFFFFFFFFUL;
  }
  return h;
}

static unsigned long sst_key_hash(const unsigned char *p, size_t n) {
  unsigned long h = sst_fnv(p, n);
  h ^= h >> 16;
  h = (h * 0x85EBCA6BUL) & 0xFFFFFFFFUL;
  h ^= h >> 13;
  h = (h * 0xC2B2AE35UL) & 0xFFFFFFFFUL;
  h ^= h >> 16;
  return h;
}

static int sst_cmp(const unsigned char *a, size_t na, const unsigned char *b,
                   size_t nb) {
  size_t n = na < nb ? na : nb;
  int c = n == 0 ? 0 : memcmp(a, b, n);
  if (c != 0) {
    return c;
  }
  return (na > nb) - (na < nb);
}

static ds_status_t sst_grow(ds_context_t *ctx, unsigned char **buf, size_t *cap,
                            size_t need) {
  size_t nc;
  void *q;
  if (need <= *cap) {
    return DS_OK;
  }
  nc = *cap < 256 ? 256 : *cap;
  while (nc < need) {
    nc *= 2;
  }
  q = ds_context_realloc(ctx, *buf, nc);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *buf = (unsigned char *)q;
  *cap = nc;
  return DS_OK;
}

static ds_status_t sst_put(ds_context_t *ctx, unsigned char **buf, size_t *len,
                           size_t *cap, const void *p, size_t n) {
  ds_status_t st = sst_grow(ctx, buf, cap, *len + n);
  if (st != DS_OK) {
    return st;
  }
  if (n > 0) {
    memcpy(*buf + *len, p, n);
  }
  *len += n;
  return DS_OK;
}

static ds_status_t sst_put_varint(ds_context_t *ctx, unsigned char **buf,
                                  size_t *len, size_t *cap, unsigned long v) {
  unsigned char tmp[8];
  size_t n = 0;
  do {
    unsigned char c = (unsigned char)(v & 0x7FUL);
    v >>= 7;
    tmp[n++] = (unsigned char)(c | (v != 0 ? 0x80 : 0));
  } while (v != 0 && n < 8);
  return sst_put(ctx, buf, len, cap, tmp, n);
}

/* decode a varint at *pos below end; false when malformed */
static bool sst_get_varint(const unsigned char *p, size_t end, size_t *pos,
                           unsigned long *out) {
  unsigned long v = 0;
  unsigned shift = 0;
  while (*pos < end && shift < 35) {
    unsigned char c = p[(*pos)++];
    v |= (unsigned long)(c & 0x7F) << shift;
    if ((c & 0x80) == 0) {
      *out = v & 0xFFFFFFFFUL;
      return true;
    }
    shift += 7;
  }
  return false;
}

/* ---- builder ----------------------------------------------------------------
 */

ds_status_t ds_sstable_builder_init(ds_sstable_builder_t *b, size_t block_size,
                                    size_t restart_interval,
                                    size_t bits_per_key,
                                    ds_context_t *context) {
  if (b == NULL) {
    return DS_ERR_NULL;
  }
  if (block_size == 0) {
    block_size = 4096;
  }
  if (restart_interval == 0) {
    restart_interval = 16;
  }
  if (bits_per_key > 32) {
    return DS_ERR_INVALID;
  }
  memset(b, 0, sizeof(*b));
  b->block_size = block_size;
  b->restart_interval = restart_interval;
  b->bits_per_key = bits_per_key;
  b->context = context;
  return DS_OK;
}

void ds_sstable_builder_destroy(ds_sstable_builder_t *b) {
  if (b == NULL) {
    return;
  }
  ds_context_free(b->context, b->buf);
  ds_context_free(b->context, b->restarts);
  ds_context_free(b->context, b->last_key);
  ds_context_free(b->context, b->index);
  ds_context_free(b->context, b->hashes);
  memset(b, 0, sizeof(*b));
}

static ds_status_t sst_flush_block(ds_sstable_builder_t *b) {
  unsigned char u[4];
  size_t i;
  size_t size;
  ds_status_t st = DS_OK;
  if (b->block_entries == 0) {
    return DS_OK;
  }
  for (i = 0; i < b->restart_count && st == DS_OK; i++) {
    sst_put_u32(u, b->restarts[i]);
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, u, 4);
  }
  if (st == DS_OK) {
    sst_put_u32(u, (unsigned long)b->restart_count);
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, u, 4);
  }
  size = b->len - b->block_start;
  /* index entry: full last key, value = offset, size */
  if (st == DS_OK) {
    st = sst_put_varint(b->context, &b->index, &b->index_len, &b->index_cap, 0);
  }
  if (st == DS_OK) {
    st = sst_put_varint(b->context, &b->index, &b->index_len, &b->index_cap,
                        (unsigned long)b->last_len);
  }
  if (st == DS_OK) {
    /* the value: two varints, measured first */
    unsigned char vbuf[16];
    size_t vl = 0;
    unsigned char *vb = vbuf;
    size_t vc = sizeof(vbuf);
    st = sst_put_varint(NULL, &vb, &vl, &vc, (unsigned long)b->block_start);
    if (st == DS_OK) {
      st = sst_put_varint(NULL, &vb, &vl, &vc, (unsigned long)size);
    }
    if (st == DS_OK) {
      st = sst_put_varint(b->context, &b->index, &b->index_len, &b->index_cap,
                          (unsigned long)vl << 1);
    }
    if (st == DS_OK) {
      st = sst_put(b->context, &b->index, &b->index_len, &b->index_cap,
                   b->last_key, b->last_len);
    }
    if (st == DS_OK) {
      st = sst_put(b->context, &b->index, &b->index_len, &b->index_cap, vbuf,
                   vl);
    }
  }
  b->index_entries++;
  b->block_start = b->len;
  b->block_entries = 0;
  b->restart_count = 0;
  return st;
}

static ds_status_t sst_add(ds_sstable_builder_t *b, const void *key,
                           size_t key_len, const void *value, size_t value_len,
                           bool tombstone) {
  const unsigned char *k = (const unsigned char *)key;
  size_t shared = 0;
  ds_status_t st;
  if (b == NULL || (key == NULL && key_len > 0) ||
      (value == NULL && value_len > 0)) {
    return DS_ERR_NULL;
  }
  if (b->error != DS_OK) {
    return b->error;
  }
  if (key_len > SST_MAXLEN / 2 || value_len > SST_MAXLEN / 2 ||
      b->len > SST_MAXLEN - key_len - value_len - 64) {
    return DS_ERR_OVERFLOW;
  }
  if (b->has_last && sst_cmp(b->last_key, b->last_len, k, key_len) >= 0) {
    return DS_ERR_INVALID;
  }
  if (b->block_entries % b->restart_interval == 0) {
    if (b->restart_count == b->restart_cap) {
      size_t nc = b->restart_cap < 8 ? 16 : b->restart_cap * 2;
      void *q = ds_context_realloc(b->context, b->restarts,
                                   nc * sizeof(unsigned long));
      if (q == NULL) {
        return DS_ERR_ALLOC;
      }
      b->restarts = (unsigned long *)q;
      b->restart_cap = nc;
    }
    b->restarts[b->restart_count++] = (unsigned long)(b->len - b->block_start);
  } else {
    size_t lim = b->last_len < key_len ? b->last_len : key_len;
    while (shared < lim && b->last_key[shared] == k[shared]) {
      shared++;
    }
  }
  if (b->hash_count == b->hash_cap && b->bits_per_key > 0) {
    size_t nc = b->hash_cap < 16 ? 64 : b->hash_cap * 2;
    void *q =
        ds_context_realloc(b->context, b->hashes, nc * sizeof(unsigned long));
    if (q == NULL) {
      return DS_ERR_ALLOC;
    }
    b->hashes = (unsigned long *)q;
    b->hash_cap = nc;
  }
  st = sst_put_varint(b->context, &b->buf, &b->len, &b->cap,
                      (unsigned long)shared);
  if (st == DS_OK) {
    st = sst_put_varint(b->context, &b->buf, &b->len, &b->cap,
                        (unsigned long)(key_len - shared));
  }
  if (st == DS_OK) {
    st = sst_put_varint(b->context, &b->buf, &b->len, &b->cap,
                        ((unsigned long)value_len << 1) |
                            (tombstone ? 1UL : 0UL));
  }
  if (st == DS_OK) {
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, k + shared,
                 key_len - shared);
  }
  if (st == DS_OK) {
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, value, value_len);
  }
  if (st == DS_OK) {
    size_t lc = b->last_cap;
    st = sst_grow(b->context, &b->last_key, &lc, key_len + 1);
    b->last_cap = lc;
  }
  if (st != DS_OK) {
    b->error = st;
    return st;
  }
  if (key_len > 0) {
    memcpy(b->last_key, k, key_len);
  }
  b->last_len = key_len;
  b->has_last = true;
  if (b->bits_per_key > 0) {
    b->hashes[b->hash_count++] = sst_key_hash(k, key_len);
  }
  b->entries++;
  b->block_entries++;
  if (b->len - b->block_start >= b->block_size) {
    st = sst_flush_block(b);
    if (st != DS_OK) {
      b->error = st;
    }
  }
  return st;
}

ds_status_t ds_sstable_builder_add(ds_sstable_builder_t *b, const void *key,
                                   size_t key_len, const void *value,
                                   size_t value_len) {
  return sst_add(b, key, key_len, value, value_len, false);
}

ds_status_t ds_sstable_builder_add_tombstone(ds_sstable_builder_t *b,
                                             const void *key, size_t key_len) {
  return sst_add(b, key, key_len, NULL, 0, true);
}

static void sst_bloom_params(size_t bits_per_key, size_t n, size_t *out_k,
                             size_t *out_bits) {
  size_t k = (bits_per_key * 69 + 50) / 100;
  size_t bits = n * bits_per_key;
  if (k < 1) {
    k = 1;
  }
  if (k > 30) {
    k = 30;
  }
  if (bits < 64) {
    bits = 64;
  }
  *out_k = k;
  *out_bits = (bits + 7) / 8 * 8;
}

ds_status_t ds_sstable_builder_finish(ds_sstable_builder_t *b,
                                      unsigned char **out, size_t *out_len) {
  unsigned char foot[SST_FOOTER];
  unsigned char u[4];
  size_t index_off;
  size_t index_len;
  size_t filter_off;
  size_t filter_len = 0;
  size_t pos;
  size_t count = 0;
  ds_status_t st;
  if (b == NULL || out == NULL || out_len == NULL) {
    return DS_ERR_NULL;
  }
  if (b->error != DS_OK) {
    return b->error;
  }
  st = sst_flush_block(b);
  index_off = b->len;
  if (st == DS_OK) {
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, b->index, b->index_len);
  }
  /* every index entry is a restart */
  for (pos = 0; pos < b->index_len && st == DS_OK; count++) {
    unsigned long sh;
    unsigned long un;
    unsigned long vf;
    sst_put_u32(u, (unsigned long)pos);
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, u, 4);
    if (!sst_get_varint(b->index, b->index_len, &pos, &sh) ||
        !sst_get_varint(b->index, b->index_len, &pos, &un) ||
        !sst_get_varint(b->index, b->index_len, &pos, &vf)) {
      st = DS_ERR_INTERNAL;
      break;
    }
    pos += un + (vf >> 1);
  }
  if (st == DS_OK) {
    sst_put_u32(u, (unsigned long)count);
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, u, 4);
  }
  index_len = b->len - index_off;
  filter_off = b->len;
  if (st == DS_OK && b->bits_per_key > 0) {
    size_t k;
    size_t bits;
    size_t i;
    unsigned char kb;
    sst_bloom_params(b->bits_per_key, b->hash_count, &k, &bits);
    kb = (unsigned char)k;
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, &kb, 1);
    if (st == DS_OK) {
      st = sst_grow(b->context, &b->buf, &b->cap, b->len + bits / 8);
    }
    if (st == DS_OK) {
      unsigned char *f = b->buf + b->len;
      memset(f, 0, bits / 8);
      for (i = 0; i < b->hash_count; i++) {
        unsigned long h = b->hashes[i];
        unsigned long delta = ((h >> 17) | (h << 15)) & 0xFFFFFFFFUL;
        size_t j;
        for (j = 0; j < k; j++) {
          size_t bit = (size_t)(h % (unsigned long)bits);
          f[bit / 8] = (unsigned char)(f[bit / 8] | (1U << (bit % 8)));
          h = (h + delta) & 0xFFFFFFFFUL;
        }
      }
      b->len += bits / 8;
    }
    filter_len = b->len - filter_off;
  }
  if (st == DS_OK) {
    sst_put_u32(foot, (unsigned long)index_off);
    sst_put_u32(foot + 4, (unsigned long)index_len);
    sst_put_u32(foot + 8, (unsigned long)filter_off);
    sst_put_u32(foot + 12, (unsigned long)filter_len);
    sst_put_u32(foot + 16, (unsigned long)b->entries);
    sst_put_u32(foot + 20, sst_fnv(b->buf, b->len));
    sst_put_u32(foot + 24, SST_MAGIC);
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, foot, SST_FOOTER);
  }
  if (st != DS_OK) {
    b->error = st;
    return st;
  }
  *out = b->buf;
  *out_len = b->len;
  b->buf = NULL;
  b->len = b->cap = 0;
  b->error = DS_ERR_STATE; /* spent */
  return DS_OK;
}

/* ---- reader -----------------------------------------------------------------
 */

ds_status_t ds_sstable_open(ds_sstable_t *t, const void *data, size_t len) {
  const unsigned char *p = (const unsigned char *)data;
  const unsigned char *f;
  if (t == NULL || data == NULL) {
    return DS_ERR_NULL;
  }
  if (len < SST_FOOTER) {
    return DS_ERR_INVALID;
  }
  f = p + len - SST_FOOTER;
  if (sst_get_u32(f + 24) != SST_MAGIC ||
      sst_get_u32(f + 20) != sst_fnv(p, len - SST_FOOTER)) {
    return DS_ERR_INVALID;
  }
  t->data = p;
  t->len = len;
  t->index_off = sst_get_u32(f);
  t->index_len = sst_get_u32(f + 4);
  t->filter_off = sst_get_u32(f + 8);
  t->filter_len = sst_get_u32(f + 12);
  t->entries = sst_get_u32(f + 16);
  if (t->index_len < 4 || t->index_off + t->index_len > len - SST_FOOTER ||
      t->filter_off + t->filter_len > len - SST_FOOTER) {
    return DS_ERR_INVALID;
  }
  return DS_OK;
}

size_t ds_sstable_entries(const ds_sstable_t *t) {
  return t == NULL ? 0 : t->entries;
}

/* restart array of a block [off, off + size) */
static bool sst_block_restarts(const ds_sstable_t *t, size_t off, size_t size,
                               size_t *out_count, size_t *out_entries_end) {
  unsigned long n;
  if (size < 4 || off + size > t->len) {
    return false;
  }
  n = sst_get_u32(t->data + off + size - 4);
  if (n > (size - 4) / 4) {
    return false;
  }
  *out_count = (size_t)n;
  *out_entries_end = off + size - 4 - 4 * (size_t)n;
  return true;
}

size_t ds_sstable_blocks(const ds_sstable_t *t) {
  size_t n;
  size_t e;
  if (t == NULL || !sst_block_restarts(t, t->index_off, t->index_len, &n, &e)) {
    return 0;
  }
  return n;
}

bool ds_sstable_may_contain(const ds_sstable_t *t, const void *key,
                            size_t key_len) {
  const unsigned char *f;
  size_t k;
  size_t bits;
  unsigned long h;
  unsigned long delta;
  size_t j;
  if (t == NULL || t->filter_len < 2) {
    return true;
  }
  f = t->data + t->filter_off;
  k = f[0];
  bits = (t->filter_len - 1) * 8;
  h = sst_key_hash((const unsigned char *)key, key_len);
  delta = ((h >> 17) | (h << 15)) & 0xFFFFFFFFUL;
  for (j = 0; j < k; j++) {
    size_t bit = (size_t)(h % (unsigned long)bits);
    if ((f[1 + bit / 8] & (1U << (bit % 8))) == 0) {
      return false;
    }
    h = (h + delta) & 0xFFFFFFFFUL;
  }
  return true;
}

/* one entry at *pos (< end): shared, the unshared bytes, the value */
typedef struct sst_entry {
  size_t shared;
  const unsigned char *rest;
  size_t rest_len;
  const unsigned char *value;
  size_t value_len;
  bool tombstone;
} sst_entry_t;

static bool sst_entry(const ds_sstable_t *t, size_t end, size_t *pos,
                      sst_entry_t *e) {
  unsigned long sh;
  unsigned long un;
  unsigned long vf;
  if (!sst_get_varint(t->data, end, pos, &sh) ||
      !sst_get_varint(t->data, end, pos, &un) ||
      !sst_get_varint(t->data, end, pos, &vf)) {
    return false;
  }
  if (un > end - *pos || (vf >> 1) > end - *pos - un) {
    return false;
  }
  e->shared = (size_t)sh;
  e->rest = t->data + *pos;
  e->rest_len = (size_t)un;
  e->value = t->data + *pos + un;
  e->value_len = (size_t)(vf >> 1);
  e->tombstone = (vf & 1UL) != 0;
  *pos += un + (vf >> 1);
  return true;
}

/* index of the first restart whose (full) key is >= key, in a block whose
 * restarts start at r; returns count when all keys are smaller */
static size_t sst_restart_lower(const ds_sstable_t *t, size_t r, size_t count,
                                size_t block_off, size_t entries_end,
                                const unsigned char *key, size_t key_len,
                                bool *bad) {
  size_t lo = 0;
  size_t hi = count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    size_t pos = block_off + sst_get_u32(t->data + r + 4 * mid);
    sst_entry_t e;
    if (pos >= entries_end || !sst_entry(t, entries_end, &pos, &e) ||
        e.shared != 0) {
      *bad = true;
      return count;
    }
    if (sst_cmp(e.rest, e.rest_len, key, key_len) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

/* the data block (offset, size) that may hold key: false when key is past
 * the last block */
static ds_status_t sst_find_block(const ds_sstable_t *t,
                                  const unsigned char *key, size_t key_len,
                                  size_t *out_index, size_t *out_off,
                                  size_t *out_size) {
  size_t n;
  size_t end;
  size_t r;
  size_t i;
  bool bad = false;
  size_t pos;
  sst_entry_t e;
  unsigned long off;
  unsigned long size;
  size_t vp;
  if (!sst_block_restarts(t, t->index_off, t->index_len, &n, &end)) {
    return DS_ERR_INVALID;
  }
  r = end;
  i = sst_restart_lower(t, r, n, t->index_off, end, key, key_len, &bad);
  if (bad) {
    return DS_ERR_INVALID;
  }
  if (i == n) {
    return DS_NOT_FOUND;
  }
  pos = t->index_off + sst_get_u32(t->data + r + 4 * i);
  if (!sst_entry(t, end, &pos, &e)) {
    return DS_ERR_INVALID;
  }
  vp = (size_t)(e.value - t->data);
  if (!sst_get_varint(t->data, vp + e.value_len, &vp, &off) ||
      !sst_get_varint(t->data, vp + e.value_len, &vp, &size) ||
      (size_t)off + (size_t)size > t->index_off) {
    return DS_ERR_INVALID;
  }
  *out_index = i;
  *out_off = (size_t)off;
  *out_size = (size_t)size;
  return DS_OK;
}

ds_status_t ds_sstable_get(const ds_sstable_t *t, const void *key,
                           size_t key_len, const unsigned char **out_value,
                           size_t *out_value_len, bool *out_tombstone,
                           ds_context_t *context) {
  ds_sstable_iter_t it;
  ds_status_t st;
  if (t == NULL || (key == NULL && key_len > 0)) {
    return DS_ERR_NULL;
  }
  if (!ds_sstable_may_contain(t, key, key_len)) {
    return DS_NOT_FOUND;
  }
  st = ds_sstable_iter_init(&it, t, context);
  if (st != DS_OK) {
    return st;
  }
  st = ds_sstable_iter_seek(&it, key, key_len);
  if (st == DS_OK) {
    if (!it.valid ||
        sst_cmp(it.key, it.key_len, (const unsigned char *)key, key_len) != 0) {
      st = DS_NOT_FOUND;
    } else {
      if (out_value != NULL) {
        *out_value = it.value;
      }
      if (out_value_len != NULL) {
        *out_value_len = it.value_len;
      }
      if (out_tombstone != NULL) {
        *out_tombstone = it.tombstone;
      }
    }
  }
  ds_sstable_iter_destroy(&it);
  return st;
}

/* ---- iterator ---------------------------------------------------------------
 */

ds_status_t ds_sstable_iter_init(ds_sstable_iter_t *it, const ds_sstable_t *t,
                                 ds_context_t *context) {
  if (it == NULL || t == NULL) {
    return DS_ERR_NULL;
  }
  memset(it, 0, sizeof(*it));
  it->table = t;
  it->context = context;
  return DS_OK;
}

void ds_sstable_iter_destroy(ds_sstable_iter_t *it) {
  if (it == NULL) {
    return;
  }
  ds_context_free(it->context, it->key);
  it->key = NULL;
  it->valid = false;
}

bool ds_sstable_iter_valid(const ds_sstable_iter_t *it) {
  return it != NULL && it->valid;
}

/* decode the entry at it->pos into the iterator */
static ds_status_t sst_iter_load(ds_sstable_iter_t *it) {
  sst_entry_t e;
  size_t pos = it->pos;
  const ds_sstable_t *t = it->table;
  if (!sst_entry(t, it->block_len, &pos, &e) || e.shared > it->key_len) {
    it->valid = false;
    return DS_ERR_INVALID;
  }
  if (e.shared + e.rest_len + 1 > it->key_cap) {
    size_t nc = (e.shared + e.rest_len + 1) * 2;
    void *q = ds_context_realloc(it->context, it->key, nc);
    if (q == NULL) {
      it->valid = false;
      return DS_ERR_ALLOC;
    }
    it->key = (unsigned char *)q;
    it->key_cap = nc;
  }
  if (e.rest_len > 0) {
    memcpy(it->key + e.shared, e.rest, e.rest_len);
  }
  it->key_len = e.shared + e.rest_len;
  it->value = e.value;
  it->value_len = e.value_len;
  it->tombstone = e.tombstone;
  it->pos = pos;
  it->valid = true;
  return DS_OK;
}

/* position at data block number b (index entry), its first entry */
static ds_status_t sst_iter_block(ds_sstable_iter_t *it, size_t b) {
  const ds_sstable_t *t = it->table;
  size_t n;
  size_t end;
  size_t pos;
  size_t vp;
  sst_entry_t e;
  unsigned long off;
  unsigned long size;
  size_t rc;
  size_t ee;
  if (!sst_block_restarts(t, t->index_off, t->index_len, &n, &end)) {
    return DS_ERR_INVALID;
  }
  if (b >= n) {
    it->valid = false;
    return DS_OK;
  }
  pos = t->index_off + sst_get_u32(t->data + end + 4 * b);
  if (!sst_entry(t, end, &pos, &e)) {
    return DS_ERR_INVALID;
  }
  vp = (size_t)(e.value - t->data);
  if (!sst_get_varint(t->data, vp + e.value_len, &vp, &off) ||
      !sst_get_varint(t->data, vp + e.value_len, &vp, &size) ||
      !sst_block_restarts(t, (size_t)off, (size_t)size, &rc, &ee)) {
    return DS_ERR_INVALID;
  }
  it->block = b;
  it->block_off = (size_t)off;
  it->block_len = ee;
  it->pos = (size_t)off;
  it->key_len = 0;
  if (it->pos >= it->block_len) {
    return sst_iter_block(it, b + 1);
  }
  return sst_iter_load(it);
}

ds_status_t ds_sstable_iter_first(ds_sstable_iter_t *it) {
  if (it == NULL) {
    return DS_ERR_NULL;
  }
  it->valid = false;
  return sst_iter_block(it, 0);
}

ds_status_t ds_sstable_iter_next(ds_sstable_iter_t *it) {
  if (it == NULL) {
    return DS_ERR_NULL;
  }
  if (!it->valid) {
    return DS_OK;
  }
  if (it->pos >= it->block_len) {
    return sst_iter_block(it, it->block + 1);
  }
  return sst_iter_load(it);
}

ds_status_t ds_sstable_iter_seek(ds_sstable_iter_t *it, const void *key,
                                 size_t key_len) {
  const ds_sstable_t *t;
  size_t b;
  size_t off;
  size_t size;
  size_t rc;
  size_t ee;
  size_t i;
  bool bad = false;
  ds_status_t st;
  if (it == NULL || (key == NULL && key_len > 0)) {
    return DS_ERR_NULL;
  }
  t = it->table;
  it->valid = false;
  st = sst_find_block(t, (const unsigned char *)key, key_len, &b, &off, &size);
  if (st == DS_NOT_FOUND) {
    return DS_OK; /* past the end */
  }
  if (st != DS_OK) {
    return st;
  }
  if (!sst_block_restarts(t, off, size, &rc, &ee)) {
    return DS_ERR_INVALID;
  }
  /* the last restart below key, then scan */
  i = sst_restart_lower(t, ee, rc, off, ee, (const unsigned char *)key, key_len,
                        &bad);
  if (bad) {
    return DS_ERR_INVALID;
  }
  it->block = b;
  it->block_off = off;
  it->block_len = ee;
  it->key_len = 0;
  it->pos = off + (i == 0 ? 0 : sst_get_u32(t->data + ee + 4 * (i - 1)));
  if (it->pos >= ee) {
    return sst_iter_block(it, b + 1);
  }
  st = sst_iter_load(it);
  while (st == DS_OK && it->valid &&
         sst_cmp(it->key, it->key_len, (const unsigned char *)key, key_len) <
             0) {
    st = ds_sstable_iter_next(it);
  }
  return st;
}

/* ---- merge ------------------------------------------------------------------
 */

ds_status_t ds_sstable_merge(const ds_sstable_t *tables, size_t n,
                             bool drop_tombstones, size_t block_size,
                             size_t restart_interval, size_t bits_per_key,
                             unsigned char **out, size_t *out_len,
                             ds_context_t *context) {
  ds_sstable_iter_t *its;
  ds_sstable_builder_t b;
  size_t i;
  ds_status_t st;
  if ((n > 0 && tables == NULL) || out == NULL || out_len == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_sstable_builder_init(&b, block_size, restart_interval, bits_per_key,
                               context);
  if (st != DS_OK) {
    return st;
  }
  its = (ds_sstable_iter_t *)ds_context_calloc(context, n + 1,
                                               sizeof(ds_sstable_iter_t));
  if (its == NULL) {
    ds_sstable_builder_destroy(&b);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n && st == DS_OK; i++) {
    st = ds_sstable_iter_init(&its[i], &tables[i], context);
    if (st == DS_OK) {
      st = ds_sstable_iter_first(&its[i]);
    }
  }
  while (st == DS_OK) {
    size_t best = n;
    for (i = 0; i < n; i++) {
      if (its[i].valid &&
          (best == n || sst_cmp(its[i].key, its[i].key_len, its[best].key,
                                its[best].key_len) < 0)) {
        best = i; /* equal keys keep the newest (lowest i) */
      }
    }
    if (best == n) {
      break;
    }
    if (!(its[best].tombstone && drop_tombstones)) {
      st = its[best].tombstone
               ? ds_sstable_builder_add_tombstone(&b, its[best].key,
                                                  its[best].key_len)
               : ds_sstable_builder_add(&b, its[best].key, its[best].key_len,
                                        its[best].value, its[best].value_len);
    }
    /* advance every table past this key */
    for (i = 0; i < n && st == DS_OK; i++) {
      if (i != best && its[i].valid &&
          sst_cmp(its[i].key, its[i].key_len, its[best].key,
                  its[best].key_len) == 0) {
        st = ds_sstable_iter_next(&its[i]);
      }
    }
    if (st == DS_OK) {
      st = ds_sstable_iter_next(&its[best]);
    }
  }
  if (st == DS_OK) {
    st = ds_sstable_builder_finish(&b, out, out_len);
  }
  for (i = 0; i < n; i++) {
    ds_sstable_iter_destroy(&its[i]);
  }
  ds_context_free(context, its);
  ds_sstable_builder_destroy(&b);
  return st;
}
