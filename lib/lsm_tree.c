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
  ds_context_free(l->config.context, t->data);
  memset(t, 0, sizeof(*t));
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
  if (lsm->config.bits_per_key == 0) {
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
  ds_lsm_table_t out;
  size_t i;
  ds_status_t st;
  if (n > sizeof(tables) / sizeof(tables[0])) {
    return DS_ERR_RANGE;
  }
  for (i = 0; i < n; i++) {
    tables[i] = srcs[i]->table;
  }
  memset(&out, 0, sizeof(out));
  st = ds_sstable_merge(tables, n, lsm_deeper_empty(l, target),
                        l->config.block_size, l->config.restart_interval,
                        l->config.bits_per_key, &out.data, &out.len,
                        l->config.context);
  if (st != DS_OK) {
    return st;
  }
  st = ds_sstable_open_cmp(&out.table, out.data, out.len, &l->cmp);
  if (st != DS_OK) {
    ds_context_free(l->config.context, out.data);
    return st;
  }
  for (i = 0; i < n; i++) {
    lsm_free_table(l, srcs[i]);
  }
  if (ds_sstable_entries(&out.table) == 0) {
    ds_context_free(l->config.context, out.data);
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
  ds_lsm_table_t t;
  ds_status_t st;
  lsm_mcur_t c;
  if (lsm == NULL || lsm->l0 == NULL) {
    return DS_ERR_NULL;
  }
  if (lsm->mem_count == 0) {
    return DS_OK;
  }
  if (lsm->l0_count > lsm->config.l0_tables) {
    /* an earlier compaction failed: retry it before adding a table */
    st = lsm_compact(lsm);
    if (st != DS_OK) {
      return st;
    }
  }
  st = ds_sstable_builder_init_cmp(
      &b, lsm->config.block_size, lsm->config.restart_interval,
      lsm->config.bits_per_key, &lsm->cmp, lsm->config.context);
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
  st = ds_sstable_open_cmp(&t.table, t.data, t.len, &lsm->cmp);
  if (st != DS_OK) {
    ds_context_free(lsm->config.context, t.data);
    return st;
  }
  memmove(lsm->l0 + 1, lsm->l0, lsm->l0_count * sizeof(ds_lsm_table_t));
  lsm->l0[0] = t;
  lsm->l0_count++;
  lsm_free_mem(lsm);
  lsm->flushes++;
  /* the data is safe in level 0 even if a compaction fails now */
  (void)lsm_compact(lsm);
  return DS_OK;
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
  ds_status_t st;
  if (l == NULL || l->l0 == NULL || (key == NULL && klen > 0) ||
      (value == NULL && vlen > 0)) {
    return DS_ERR_NULL;
  }
  if (!ds_size_add(sizeof(lsm_ent_t), klen, &bytes) ||
      !ds_size_add(bytes, vlen, &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  st = lsm_skip(l) ? sl_set(l, key, klen, value, vlen, tomb, bytes)
                   : arr_set(l, key, klen, value, vlen, tomb, bytes);
  if (st != DS_OK) {
    return st;
  }
  if (l->mem_bytes >= l->config.memtable_bytes) {
    /* a failed flush leaves the entry in the memtable; the next
     * modification retries */
    (void)ds_lsm_flush(l);
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
