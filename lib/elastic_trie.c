#include "elastic_trie.h"
#include <string.h>

#define ET_NONE (~(size_t)0)
#define ET_END ((size_t)0) /* sorts before every byte (symbols b + 1) */

/* ---- key store ------------------------------------------------------------
 */

static void tk_init(ds_trie_keys_t *k) {
  memset(k, 0, sizeof(*k));
  k->free_list = ET_NONE;
}

static void tk_destroy(ds_trie_keys_t *k, ds_context_t *ctx) {
  size_t i;
  for (i = 0; i < k->slots; i++) {
    if (k->bytes[i] != NULL) {
      ds_context_free(ctx, k->bytes[i]);
    }
  }
  ds_context_free(ctx, (void *)k->bytes);
  ds_context_free(ctx, k->len);
  ds_context_free(ctx, k->value);
  tk_init(k);
}

static ds_status_t tk_add(ds_trie_keys_t *k, ds_context_t *ctx,
                          const unsigned char *key, size_t len, size_t value,
                          size_t *out) {
  size_t i;
  size_t cap;
  void *p;
  unsigned char *copy =
      (unsigned char *)ds_context_alloc(ctx, len > 0 ? len : 1);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  if (len > 0) {
    memcpy(copy, key, len);
  }
  if (k->free_list != ET_NONE) {
    i = k->free_list;
    k->free_list = k->len[i];
  } else {
    if (k->slots == k->capacity) {
      cap = k->capacity < 16 ? 16 : k->capacity * 2;
      if (cap <= k->capacity || cap > ((size_t)-1) / sizeof(void *)) {
        ds_context_free(ctx, copy);
        return DS_ERR_OVERFLOW;
      }
      p = ds_context_realloc(ctx, (void *)k->bytes,
                             cap * sizeof(unsigned char *));
      if (p == NULL) {
        ds_context_free(ctx, copy);
        return DS_ERR_ALLOC;
      }
      k->bytes = (unsigned char **)p;
      p = ds_context_realloc(ctx, k->len, cap * sizeof(size_t));
      if (p == NULL) {
        ds_context_free(ctx, copy);
        return DS_ERR_ALLOC;
      }
      k->len = (size_t *)p;
      p = ds_context_realloc(ctx, k->value, cap * sizeof(size_t));
      if (p == NULL) {
        ds_context_free(ctx, copy);
        return DS_ERR_ALLOC;
      }
      k->value = (size_t *)p;
      k->capacity = cap;
    }
    i = k->slots++;
  }
  k->bytes[i] = copy;
  k->len[i] = len;
  k->value[i] = value;
  *out = i;
  return DS_OK;
}

static void tk_free(ds_trie_keys_t *k, ds_context_t *ctx, size_t i) {
  ds_context_free(ctx, k->bytes[i]);
  k->bytes[i] = NULL;
  k->len[i] = k->free_list;
  k->free_list = i;
}

static bool tk_equal(const ds_trie_keys_t *k, size_t i,
                     const unsigned char *key, size_t len) {
  return k->len[i] == len && (len == 0 || memcmp(k->bytes[i], key, len) == 0);
}

static int tk_cmp(const ds_trie_keys_t *k, size_t a, size_t b) {
  size_t m = k->len[a] < k->len[b] ? k->len[a] : k->len[b];
  int c = m > 0 ? memcmp(k->bytes[a], k->bytes[b], m) : 0;
  if (c != 0) {
    return c;
  }
  return k->len[a] < k->len[b] ? -1 : k->len[a] > k->len[b];
}

static size_t et_sym(const unsigned char *key, size_t len, size_t d) {
  return d < len ? (size_t)key[d] + 1 : ET_END;
}

/* ---- bucket trie ----------------------------------------------------------
 */

ds_status_t ds_bucket_trie_init(ds_bucket_trie_t *t, size_t b, bool elastic,
                                ds_context_t *context) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (b == 0 || b > 65536) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->root = ET_NONE;
  t->node_free = ET_NONE;
  t->b = b;
  t->elastic = elastic;
  t->context = context;
  tk_init(&t->keys);
  return DS_OK;
}

void ds_bucket_trie_destroy(ds_bucket_trie_t *t) {
  size_t v;
  if (t == NULL) {
    return;
  }
  for (v = 0; v < t->node_slots; v++) {
    if (t->bucket[v] != NULL) {
      ds_context_free(t->context, t->bucket[v]);
    }
  }
  ds_context_free(t->context, t->leaf);
  ds_context_free(t->context, t->depth);
  ds_context_free(t->context, t->parent);
  ds_context_free(t->context, t->child);
  ds_context_free(t->context, t->sibling);
  ds_context_free(t->context, t->label);
  ds_context_free(t->context, t->count);
  ds_context_free(t->context, (void *)t->bucket);
  ds_context_free(t->context, t->bucket_cap);
  tk_destroy(&t->keys, t->context);
  ds_bucket_trie_init(t, t->b, t->elastic, t->context);
}

size_t ds_bucket_trie_size(const ds_bucket_trie_t *t) {
  return t == NULL ? 0 : t->size;
}

static ds_status_t bt_node(ds_bucket_trie_t *t, size_t *out) {
  size_t cap;
  size_t v;
  void *p;
  if (t->node_free != ET_NONE) {
    v = t->node_free;
    t->node_free = t->sibling[v];
  } else {
    if (t->node_slots == t->node_capacity) {
      cap = t->node_capacity < 16 ? 16 : t->node_capacity * 2;
      if (cap <= t->node_capacity || cap > ((size_t)-1) / sizeof(size_t)) {
        return DS_ERR_OVERFLOW;
      }
#define BT_GROW(field, type)                                                   \
  p = ds_context_realloc(t->context, (void *)t->field, cap * sizeof(type));    \
  if (p == NULL) {                                                             \
    return DS_ERR_ALLOC;                                                       \
  }                                                                            \
  t->field = (type *)p;
      BT_GROW(leaf, unsigned char)
      BT_GROW(depth, size_t)
      BT_GROW(parent, size_t)
      BT_GROW(child, size_t)
      BT_GROW(sibling, size_t)
      BT_GROW(label, size_t)
      BT_GROW(count, size_t)
      BT_GROW(bucket, size_t *)
      BT_GROW(bucket_cap, size_t)
#undef BT_GROW
      t->node_capacity = cap;
    }
    v = t->node_slots++;
  }
  t->leaf[v] = 1;
  t->depth[v] = 0;
  t->parent[v] = ET_NONE;
  t->child[v] = ET_NONE;
  t->sibling[v] = ET_NONE;
  t->label[v] = 0;
  t->count[v] = 0;
  t->bucket[v] = NULL;
  t->bucket_cap[v] = 0;
  *out = v;
  return DS_OK;
}

static void bt_free_node(ds_bucket_trie_t *t, size_t v) {
  if (t->bucket[v] != NULL) {
    ds_context_free(t->context, t->bucket[v]);
    t->bucket[v] = NULL;
  }
  t->bucket_cap[v] = 0;
  t->parent[v] = ET_NONE;
  t->sibling[v] = t->node_free;
  t->node_free = v;
}

/* Bucket of leaf v able to hold n keys. */
static ds_status_t bt_reserve(ds_bucket_trie_t *t, size_t v, size_t n) {
  size_t cap;
  void *p;
  if (n <= t->bucket_cap[v]) {
    return DS_OK;
  }
  if (t->elastic) {
    cap = t->bucket_cap[v] == 0 ? 1 : t->bucket_cap[v];
    while (cap < n) {
      cap *= 2;
    }
    if (cap > t->b) {
      cap = t->b;
    }
  } else {
    cap = t->b;
  }
  p = ds_context_realloc(t->context, t->bucket[v], cap * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->bucket[v] = (size_t *)p;
  t->bucket_cap[v] = cap;
  return DS_OK;
}

static void bt_shrink(ds_bucket_trie_t *t, size_t v) {
  size_t cap;
  void *p;
  if (!t->elastic || t->bucket_cap[v] <= 1 ||
      t->count[v] * 4 > t->bucket_cap[v]) {
    return;
  }
  cap = t->bucket_cap[v] / 2;
  if (cap < t->count[v]) {
    return;
  }
  p = ds_context_realloc(t->context, t->bucket[v], cap * sizeof(size_t));
  if (p != NULL) {
    t->bucket[v] = (size_t *)p;
    t->bucket_cap[v] = cap;
  }
}

static size_t bt_find_child(const ds_bucket_trie_t *t, size_t v, size_t sym) {
  size_t c;
  for (c = t->child[v]; c != ET_NONE && t->label[c] < sym; c = t->sibling[c]) {
  }
  return c != ET_NONE && t->label[c] == sym ? c : ET_NONE;
}

static void bt_link_child(ds_bucket_trie_t *t, size_t v, size_t c) {
  size_t prev = ET_NONE;
  size_t x;
  for (x = t->child[v]; x != ET_NONE && t->label[x] < t->label[c];
       x = t->sibling[x]) {
    prev = x;
  }
  t->sibling[c] = x;
  if (prev == ET_NONE) {
    t->child[v] = c;
  } else {
    t->sibling[prev] = c;
  }
  t->parent[c] = v;
}

static void bt_unlink_child(ds_bucket_trie_t *t, size_t c) {
  size_t v = t->parent[c];
  size_t x;
  if (v == ET_NONE) {
    t->root = ET_NONE;
    return;
  }
  if (t->child[v] == c) {
    t->child[v] = t->sibling[c];
  } else {
    for (x = t->child[v]; t->sibling[x] != c; x = t->sibling[x]) {
    }
    t->sibling[x] = t->sibling[c];
  }
}

static size_t bt_leaf_for(const ds_bucket_trie_t *t, const unsigned char *key,
                          size_t len) {
  size_t v = t->root;
  while (v != ET_NONE && !t->leaf[v]) {
    v = bt_find_child(t, v, et_sym(key, len, t->depth[v]));
  }
  return v;
}

ds_status_t ds_bucket_trie_find(const ds_bucket_trie_t *t,
                                const unsigned char *key, size_t len,
                                size_t *out_value) {
  size_t v;
  size_t i;
  if (t == NULL || (key == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  v = bt_leaf_for(t, key, len);
  if (v == ET_NONE) {
    return DS_NOT_FOUND;
  }
  for (i = 0; i < t->count[v]; i++) {
    if (tk_equal(&t->keys, t->bucket[v][i], key, len)) {
      if (out_value != NULL) {
        *out_value = t->keys.value[t->bucket[v][i]];
      }
      return DS_OK;
    }
  }
  return DS_NOT_FOUND;
}

static void bt_count_up(ds_bucket_trie_t *t, size_t v, bool inc) {
  for (v = t->parent[v]; v != ET_NONE; v = t->parent[v]) {
    if (inc) {
      t->count[v]++;
    } else {
      t->count[v]--;
    }
  }
}

/* Leaf v holds keys ks[0..n) with n = b + 1: split it. */
static ds_status_t bt_split(ds_bucket_trie_t *t, size_t v, size_t *ks,
                            size_t n) {
  size_t i;
  size_t j;
  size_t g;
  size_t x;
  size_t c;
  size_t d;
  size_t sym;
  ds_status_t st;
  for (;;) {
    d = t->depth[v];
    /* sort ks by symbol at depth d (n is small) */
    for (i = 1; i < n; i++) {
      x = ks[i];
      sym = et_sym(t->keys.bytes[x], t->keys.len[x], d);
      for (j = i; j > 0 && et_sym(t->keys.bytes[ks[j - 1]],
                                  t->keys.len[ks[j - 1]], d) > sym;
           j--) {
        ks[j] = ks[j - 1];
      }
      ks[j] = x;
    }
    if (t->bucket[v] != NULL) {
      ds_context_free(t->context, t->bucket[v]);
      t->bucket[v] = NULL;
    }
    t->bucket_cap[v] = 0;
    t->leaf[v] = 0;
    t->count[v] = n;
    for (i = 0; i < n; i = g) {
      sym = et_sym(t->keys.bytes[ks[i]], t->keys.len[ks[i]], d);
      for (g = i + 1;
           g < n && et_sym(t->keys.bytes[ks[g]], t->keys.len[ks[g]], d) == sym;
           g++) {
      }
      st = bt_node(t, &c);
      if (st != DS_OK) {
        return st;
      }
      t->depth[c] = d + 1;
      t->label[c] = sym;
      bt_link_child(t, v, c);
      if (g - i > t->b) {
        /* everything went one way: split again one level down */
        v = c;
        break;
      }
      st = bt_reserve(t, c, g - i);
      if (st != DS_OK) {
        return st;
      }
      memcpy(t->bucket[c], ks + i, (g - i) * sizeof(size_t));
      t->count[c] = g - i;
    }
    if (i >= n) {
      return DS_OK;
    }
  }
}

ds_status_t ds_bucket_trie_insert(ds_bucket_trie_t *t, const unsigned char *key,
                                  size_t len, size_t value) {
  size_t v;
  size_t c;
  size_t i;
  size_t k;
  size_t *ks;
  ds_status_t st;
  if (t == NULL || (key == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  if (t->root == ET_NONE) {
    st = bt_node(t, &v);
    if (st != DS_OK) {
      return st;
    }
    t->root = v;
  }
  v = t->root;
  while (!t->leaf[v]) {
    c = bt_find_child(t, v, et_sym(key, len, t->depth[v]));
    if (c == ET_NONE) {
      st = bt_node(t, &c);
      if (st != DS_OK) {
        return st;
      }
      t->depth[c] = t->depth[v] + 1;
      t->label[c] = et_sym(key, len, t->depth[v]);
      bt_link_child(t, v, c);
    }
    v = c;
  }
  for (i = 0; i < t->count[v]; i++) {
    if (tk_equal(&t->keys, t->bucket[v][i], key, len)) {
      t->keys.value[t->bucket[v][i]] = value;
      return DS_EXISTS;
    }
  }
  st = tk_add(&t->keys, t->context, key, len, value, &k);
  if (st != DS_OK) {
    return st;
  }
  if (t->count[v] < t->b) {
    st = bt_reserve(t, v, t->count[v] + 1);
    if (st != DS_OK) {
      tk_free(&t->keys, t->context, k);
      return st;
    }
    t->bucket[v][t->count[v]++] = k;
  } else {
    ks = (size_t *)ds_context_alloc(t->context, (t->b + 1) * sizeof(size_t));
    if (ks == NULL) {
      tk_free(&t->keys, t->context, k);
      return DS_ERR_ALLOC;
    }
    memcpy(ks, t->bucket[v], t->b * sizeof(size_t));
    ks[t->b] = k;
    st = bt_split(t, v, ks, t->b + 1);
    ds_context_free(t->context, ks);
    if (st != DS_OK) {
      return st;
    }
  }
  bt_count_up(t, v, true);
  t->size++;
  return DS_OK;
}

/* Turn the internal node u (count <= b) back into one bucket. */
static ds_status_t bt_collapse(ds_bucket_trie_t *t, size_t u) {
  size_t *ks;
  size_t n = 0;
  size_t v;
  size_t next;
  size_t i;
  ds_status_t st;
  ks = (size_t *)ds_context_alloc(
      t->context, (t->count[u] > 0 ? t->count[u] : 1) * sizeof(size_t));
  if (ks == NULL) {
    return DS_ERR_ALLOC;
  }
  /* gather and free every descendant (stackless walk using parents) */
  v = t->child[u];
  while (v != ET_NONE) {
    if (!t->leaf[v] && t->child[v] != ET_NONE) {
      v = t->child[v];
      continue;
    }
    if (t->leaf[v]) {
      for (i = 0; i < t->count[v]; i++) {
        ks[n++] = t->bucket[v][i];
      }
    }
    /* free v and climb while it was the last child */
    for (;;) {
      next = t->sibling[v];
      i = t->parent[v];
      bt_free_node(t, v);
      if (next != ET_NONE) {
        v = next;
        break;
      }
      if (i == u) {
        v = ET_NONE;
        break;
      }
      t->child[i] = ET_NONE; /* all its children are gone */
      v = i;
      /* i now has no children: it is freed on the next round */
      if (!t->leaf[v]) {
        t->leaf[v] = 1;
        t->count[v] = 0;
      }
    }
  }
  t->child[u] = ET_NONE;
  t->leaf[u] = 1;
  t->count[u] = n;
  st = bt_reserve(t, u, n > 0 ? n : 1);
  if (st == DS_OK && n > 0) {
    memcpy(t->bucket[u], ks, n * sizeof(size_t));
  }
  ds_context_free(t->context, ks);
  return st;
}

ds_status_t ds_bucket_trie_remove(ds_bucket_trie_t *t, const unsigned char *key,
                                  size_t len) {
  size_t v;
  size_t u;
  size_t top;
  size_t i;
  if (t == NULL || (key == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  v = bt_leaf_for(t, key, len);
  if (v == ET_NONE) {
    return DS_NOT_FOUND;
  }
  for (i = 0; i < t->count[v]; i++) {
    if (tk_equal(&t->keys, t->bucket[v][i], key, len)) {
      break;
    }
  }
  if (i == t->count[v]) {
    return DS_NOT_FOUND;
  }
  tk_free(&t->keys, t->context, t->bucket[v][i]);
  t->bucket[v][i] = t->bucket[v][--t->count[v]];
  bt_count_up(t, v, false);
  t->size--;
  bt_shrink(t, v);
  /* the topmost ancestor small enough to be one bucket again */
  top = ET_NONE;
  for (u = t->parent[v]; u != ET_NONE; u = t->parent[u]) {
    if (t->count[u] <= t->b) {
      top = u;
    }
  }
  if (top != ET_NONE) {
    if (bt_collapse(t, top) != DS_OK) {
      return DS_OK; /* still consistent, just not compacted */
    }
    v = top;
  }
  if (t->count[v] == 0) {
    bt_unlink_child(t, v);
    bt_free_node(t, v);
  }
  return DS_OK;
}

ds_status_t ds_bucket_trie_foreach(ds_bucket_trie_t *t,
                                   ds_trie_visit_func_t visit, void *user) {
  size_t v;
  size_t *ks;
  size_t i;
  size_t j;
  size_t x;
  if (t == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root == ET_NONE) {
    return DS_OK;
  }
  ks = (size_t *)ds_context_alloc(t->context, t->b * sizeof(size_t));
  if (ks == NULL) {
    return DS_ERR_ALLOC;
  }
  v = t->root;
  while (v != ET_NONE) {
    if (!t->leaf[v] && t->child[v] != ET_NONE) {
      v = t->child[v];
      continue;
    }
    if (t->leaf[v]) {
      memcpy(ks, t->bucket[v], t->count[v] * sizeof(size_t));
      for (i = 1; i < t->count[v]; i++) {
        x = ks[i];
        for (j = i; j > 0 && tk_cmp(&t->keys, ks[j - 1], x) > 0; j--) {
          ks[j] = ks[j - 1];
        }
        ks[j] = x;
      }
      for (i = 0; i < t->count[v]; i++) {
        x = ks[i];
        if (!visit(t->keys.bytes[x], t->keys.len[x], t->keys.value[x], user)) {
          ds_context_free(t->context, ks);
          return DS_STOP;
        }
      }
    }
    while (v != t->root && t->sibling[v] == ET_NONE) {
      v = t->parent[v];
    }
    v = v == t->root ? ET_NONE : t->sibling[v];
  }
  ds_context_free(t->context, ks);
  return DS_OK;
}

size_t ds_bucket_trie_nodes(const ds_bucket_trie_t *t) {
  size_t n = 0;
  size_t v;
  if (t == NULL) {
    return 0;
  }
  for (v = t->node_free; v != ET_NONE; v = t->sibling[v]) {
    n++;
  }
  return t->node_slots - n;
}

size_t ds_bucket_trie_reserved(const ds_bucket_trie_t *t) {
  size_t v;
  size_t r = 0;
  if (t == NULL) {
    return 0;
  }
  for (v = 0; v < t->node_slots; v++) {
    r += t->bucket_cap[v];
  }
  return r;
}

/* ---- blind trie -----------------------------------------------------------
 */

ds_status_t ds_blind_trie_init(ds_blind_trie_t *t, ds_context_t *context) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  t->root = ET_NONE;
  t->node_free = ET_NONE;
  t->context = context;
  tk_init(&t->keys);
  return DS_OK;
}

void ds_blind_trie_destroy(ds_blind_trie_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->leaf);
  ds_context_free(t->context, t->pos);
  ds_context_free(t->context, t->parent);
  ds_context_free(t->context, t->child);
  ds_context_free(t->context, t->sibling);
  ds_context_free(t->context, t->label);
  tk_destroy(&t->keys, t->context);
  ds_blind_trie_init(t, t->context);
}

size_t ds_blind_trie_size(const ds_blind_trie_t *t) {
  return t == NULL ? 0 : t->size;
}

static ds_status_t bl_node(ds_blind_trie_t *t, size_t *out) {
  size_t cap;
  size_t v;
  void *p;
  if (t->node_free != ET_NONE) {
    v = t->node_free;
    t->node_free = t->sibling[v];
  } else {
    if (t->node_slots == t->node_capacity) {
      cap = t->node_capacity < 16 ? 16 : t->node_capacity * 2;
      if (cap <= t->node_capacity || cap > ((size_t)-1) / sizeof(size_t)) {
        return DS_ERR_OVERFLOW;
      }
#define BL_GROW(field, type)                                                   \
  p = ds_context_realloc(t->context, (void *)t->field, cap * sizeof(type));    \
  if (p == NULL) {                                                             \
    return DS_ERR_ALLOC;                                                       \
  }                                                                            \
  t->field = (type *)p;
      BL_GROW(leaf, unsigned char)
      BL_GROW(pos, size_t)
      BL_GROW(parent, size_t)
      BL_GROW(child, size_t)
      BL_GROW(sibling, size_t)
      BL_GROW(label, size_t)
#undef BL_GROW
      t->node_capacity = cap;
    }
    v = t->node_slots++;
  }
  t->leaf[v] = 1;
  t->pos[v] = 0;
  t->parent[v] = ET_NONE;
  t->child[v] = ET_NONE;
  t->sibling[v] = ET_NONE;
  t->label[v] = 0;
  *out = v;
  return DS_OK;
}

static size_t bl_find_child(const ds_blind_trie_t *t, size_t v, size_t sym) {
  size_t c;
  for (c = t->child[v]; c != ET_NONE && t->label[c] < sym; c = t->sibling[c]) {
  }
  return c != ET_NONE && t->label[c] == sym ? c : ET_NONE;
}

static void bl_link_child(ds_blind_trie_t *t, size_t v, size_t c) {
  size_t prev = ET_NONE;
  size_t x;
  for (x = t->child[v]; x != ET_NONE && t->label[x] < t->label[c];
       x = t->sibling[x]) {
    prev = x;
  }
  t->sibling[c] = x;
  if (prev == ET_NONE) {
    t->child[v] = c;
  } else {
    t->sibling[prev] = c;
  }
  t->parent[c] = v;
}

/* Replace child old of its parent by new (same label slot). */
static void bl_replace(ds_blind_trie_t *t, size_t old, size_t nw) {
  size_t p = t->parent[old];
  size_t x;
  t->parent[nw] = p;
  t->label[nw] = t->label[old];
  t->sibling[nw] = t->sibling[old];
  if (p == ET_NONE) {
    t->root = nw;
    return;
  }
  if (t->child[p] == old) {
    t->child[p] = nw;
  } else {
    for (x = t->child[p]; t->sibling[x] != old; x = t->sibling[x]) {
    }
    t->sibling[x] = nw;
  }
}

/* Blind descent: follow matching edges, any edge otherwise. */
static size_t bl_blind(const ds_blind_trie_t *t, const unsigned char *key,
                       size_t len) {
  size_t v = t->root;
  size_t c;
  while (v != ET_NONE && !t->leaf[v]) {
    c = bl_find_child(t, v, et_sym(key, len, t->pos[v]));
    v = c != ET_NONE ? c : t->child[v];
  }
  return v;
}

static size_t bl_lcp(const ds_blind_trie_t *t, size_t k,
                     const unsigned char *key, size_t len) {
  size_t m = t->keys.len[k] < len ? t->keys.len[k] : len;
  size_t i = 0;
  while (i < m && t->keys.bytes[k][i] == key[i]) {
    i++;
  }
  return i;
}

ds_status_t ds_blind_trie_find(const ds_blind_trie_t *t,
                               const unsigned char *key, size_t len,
                               size_t *out_value) {
  size_t v;
  if (t == NULL || (key == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  v = bl_blind(t, key, len);
  if (v == ET_NONE || !tk_equal(&t->keys, t->pos[v], key, len)) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL) {
    *out_value = t->keys.value[t->pos[v]];
  }
  return DS_OK;
}

size_t ds_blind_trie_lcp(const ds_blind_trie_t *t, const unsigned char *key,
                         size_t len) {
  size_t v;
  if (t == NULL || (key == NULL && len > 0)) {
    return 0;
  }
  v = bl_blind(t, key, len);
  return v == ET_NONE ? 0 : bl_lcp(t, t->pos[v], key, len);
}

ds_status_t ds_blind_trie_insert(ds_blind_trie_t *t, const unsigned char *key,
                                 size_t len, size_t value) {
  size_t leaf;
  size_t l;
  size_t v;
  size_t w;
  size_t rep;
  size_t k;
  size_t nl;
  ds_status_t st;
  if (t == NULL || (key == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  if (t->root == ET_NONE) {
    st = tk_add(&t->keys, t->context, key, len, value, &k);
    if (st != DS_OK) {
      return st;
    }
    st = bl_node(t, &v);
    if (st != DS_OK) {
      tk_free(&t->keys, t->context, k);
      return st;
    }
    t->pos[v] = k;
    t->root = v;
    t->size = 1;
    return DS_OK;
  }
  leaf = bl_blind(t, key, len);
  if (tk_equal(&t->keys, t->pos[leaf], key, len)) {
    t->keys.value[t->pos[leaf]] = value;
    return DS_EXISTS;
  }
  l = bl_lcp(t, t->pos[leaf], key, len);
  /* the first node on the key's path branching at l or later */
  v = t->root;
  while (!t->leaf[v] && t->pos[v] < l) {
    v = bl_find_child(t, v, et_sym(key, len, t->pos[v]));
  }
  st = tk_add(&t->keys, t->context, key, len, value, &k);
  if (st != DS_OK) {
    return st;
  }
  st = bl_node(t, &nl);
  if (st != DS_OK) {
    tk_free(&t->keys, t->context, k);
    return st;
  }
  t->pos[nl] = k;
  t->label[nl] = et_sym(key, len, l);
  if (!t->leaf[v] && t->pos[v] == l) {
    bl_link_child(t, v, nl);
  } else {
    st = bl_node(t, &w);
    if (st != DS_OK) {
      t->sibling[nl] = t->node_free;
      t->node_free = nl;
      tk_free(&t->keys, t->context, k);
      return st;
    }
    /* any key below v shares its first l + 1 bytes' symbol at l */
    for (rep = v; !t->leaf[rep]; rep = t->child[rep]) {
    }
    rep = t->pos[rep];
    bl_replace(t, v, w);
    t->leaf[w] = 0;
    t->pos[w] = l;
    t->child[w] = ET_NONE;
    t->label[v] = et_sym(t->keys.bytes[rep], t->keys.len[rep], l);
    bl_link_child(t, w, v);
    bl_link_child(t, w, nl);
  }
  t->size++;
  return DS_OK;
}

ds_status_t ds_blind_trie_remove(ds_blind_trie_t *t, const unsigned char *key,
                                 size_t len) {
  size_t v;
  size_t p;
  size_t x;
  size_t only;
  if (t == NULL || (key == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  v = bl_blind(t, key, len);
  if (v == ET_NONE || !tk_equal(&t->keys, t->pos[v], key, len)) {
    return DS_NOT_FOUND;
  }
  tk_free(&t->keys, t->context, t->pos[v]);
  p = t->parent[v];
  if (p == ET_NONE) {
    t->root = ET_NONE;
  } else {
    if (t->child[p] == v) {
      t->child[p] = t->sibling[v];
    } else {
      for (x = t->child[p]; t->sibling[x] != v; x = t->sibling[x]) {
      }
      t->sibling[x] = t->sibling[v];
    }
    only = t->child[p];
    if (t->sibling[only] == ET_NONE) {
      /* a unary internal node is spliced out */
      bl_replace(t, p, only);
      t->sibling[p] = t->node_free;
      t->node_free = p;
    }
  }
  t->sibling[v] = t->node_free;
  t->node_free = v;
  t->size--;
  return DS_OK;
}
