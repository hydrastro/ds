#include "dynamic_hash.h"
#include <string.h>

#define DH_NONE (~(size_t)0)

static size_t dh_mix(size_t h) {
  h ^= h >> 16;
  h *= (size_t)0x45d9f3bUL;
  h ^= h >> 16;
  h *= (size_t)0x45d9f3bUL;
  h ^= h >> 16;
  return h;
}

static bool dh_args(size_t key_size, ds_dyn_hash_func_t hash,
                    ds_dyn_equal_func_t equal) {
  return key_size > 0 && hash != NULL && equal != NULL;
}

/* ---- virtual hashing ------------------------------------------------------
 */

static unsigned char *vh_key(const ds_virtual_hash_t *h, size_t b, size_t i) {
  return h->bkeys[b] + i * h->key_size;
}

static unsigned char *vh_val(const ds_virtual_hash_t *h, size_t b, size_t i) {
  return h->bvals[b] + i * h->value_size;
}

static ds_status_t vh_alloc(ds_virtual_hash_t *h, size_t buckets) {
  size_t i;
  void *p;
  if (buckets > ((size_t)-1) / sizeof(void *)) {
    return DS_ERR_OVERFLOW;
  }
#define VH_GROW(field, type)                                                   \
  p = ds_context_realloc(h->context, (void *)h->field,                         \
                         buckets * sizeof(type));                              \
  if (p == NULL) {                                                             \
    return DS_ERR_ALLOC;                                                       \
  }                                                                            \
  h->field = (type *)p;
  VH_GROW(bkeys, unsigned char *)
  VH_GROW(bvals, unsigned char *)
  VH_GROW(bcap, size_t)
  VH_GROW(load, size_t)
  VH_GROW(in_use, unsigned char)
  VH_GROW(level, unsigned char)
#undef VH_GROW
  for (i = h->buckets; i < buckets; i++) {
    h->bkeys[i] = NULL;
    h->bvals[i] = NULL;
    h->bcap[i] = 0;
    h->load[i] = 0;
    h->in_use[i] = 0;
    h->level[i] = 0;
  }
  h->buckets = buckets;
  return DS_OK;
}

ds_status_t ds_virtual_hash_init(ds_virtual_hash_t *h, size_t key_size,
                                 size_t value_size, ds_dyn_hash_func_t hash,
                                 ds_dyn_equal_func_t equal, void *user,
                                 size_t n0, size_t bucket_size,
                                 ds_context_t *context) {
  size_t i;
  ds_status_t st;
  if (h == NULL || hash == NULL || equal == NULL) {
    return DS_ERR_NULL;
  }
  if (n0 == 0) {
    n0 = 4;
  }
  if (bucket_size == 0) {
    bucket_size = 4;
  }
  if (!dh_args(key_size, hash, equal) || n0 > 65536 || bucket_size > 65536) {
    return DS_ERR_INVALID;
  }
  memset(h, 0, sizeof(*h));
  h->n0 = n0;
  h->bucket_size = bucket_size;
  h->key_size = key_size;
  h->value_size = value_size;
  h->hash = hash;
  h->equal = equal;
  h->user = user;
  h->context = context;
  st = vh_alloc(h, n0);
  if (st != DS_OK) {
    ds_virtual_hash_destroy(h);
    return st;
  }
  for (i = 0; i < n0; i++) {
    h->in_use[i] = 1;
  }
  return DS_OK;
}

void ds_virtual_hash_destroy(ds_virtual_hash_t *h) {
  size_t i;
  if (h == NULL) {
    return;
  }
  for (i = 0; i < h->buckets; i++) {
    ds_context_free(h->context, h->bkeys[i]);
    ds_context_free(h->context, h->bvals[i]);
  }
  ds_context_free(h->context, (void *)h->bkeys);
  ds_context_free(h->context, (void *)h->bvals);
  ds_context_free(h->context, h->bcap);
  ds_context_free(h->context, h->load);
  ds_context_free(h->context, h->in_use);
  ds_context_free(h->context, h->level);
  h->bkeys = h->bvals = NULL;
  h->in_use = h->level = NULL;
  h->load = h->bcap = NULL;
  h->buckets = h->size = 0;
}

size_t ds_virtual_hash_size(const ds_virtual_hash_t *h) {
  return h == NULL ? 0 : h->size;
}

/* Bucket of a hash value: the highest level whose bucket is in use. */
static size_t vh_bucket(const ds_virtual_hash_t *h, size_t hv) {
  size_t j = h->max_level + 1;
  size_t m;
  while (j-- > 0) {
    m = hv % (h->n0 << j);
    if (h->in_use[m]) {
      return m;
    }
  }
  return hv % h->n0;
}

static bool vh_find(const ds_virtual_hash_t *h, const void *key, size_t *ob,
                    size_t *oi) {
  size_t b = vh_bucket(h, dh_mix(h->hash(key, h->user)));
  size_t i;
  for (i = 0; i < h->load[b]; i++) {
    if (h->equal(vh_key(h, b, i), key, h->user)) {
      *ob = b;
      *oi = i;
      return true;
    }
  }
  *ob = b;
  return false;
}

/* Bucket b able to hold n entries. */
static ds_status_t vh_reserve(ds_virtual_hash_t *h, size_t b, size_t n) {
  size_t cap;
  void *p;
  if (n <= h->bcap[b]) {
    return DS_OK;
  }
  cap = h->bcap[b] == 0 ? h->bucket_size : h->bcap[b];
  while (cap < n) {
    if (cap > ((size_t)-1) / 2) {
      return DS_ERR_OVERFLOW;
    }
    cap *= 2;
  }
  if (cap > ((size_t)-1) / h->key_size ||
      (h->value_size > 0 && cap > ((size_t)-1) / h->value_size)) {
    return DS_ERR_OVERFLOW;
  }
  p = ds_context_realloc(h->context, h->bkeys[b], cap * h->key_size);
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  h->bkeys[b] = (unsigned char *)p;
  p = ds_context_realloc(h->context, h->bvals[b],
                         h->value_size > 0 ? cap * h->value_size : 1);
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  h->bvals[b] = (unsigned char *)p;
  h->bcap[b] = cap;
  return DS_OK;
}

static ds_status_t vh_place(ds_virtual_hash_t *h, size_t b, const void *key,
                            const void *value) {
  size_t i;
  ds_status_t st = vh_reserve(h, b, h->load[b] + 1);
  if (st != DS_OK) {
    return st;
  }
  i = h->load[b]++;
  memcpy(vh_key(h, b, i), key, h->key_size);
  if (h->value_size > 0) {
    memcpy(vh_val(h, b, i), value, h->value_size);
  }
  return DS_OK;
}

/* May bucket m (at its level) split? The address space is kept within a
 * constant factor of the number of keys. */
static bool vh_may_split(const ds_virtual_hash_t *h, size_t m) {
  size_t j = h->level[m];
  size_t need;
  if (j >= 40 || (h->n0 << (j + 1)) >> (j + 1) != h->n0) {
    return false;
  }
  need = h->n0 << (j + 1);
  return need <= h->buckets || need <= 8 * (h->size + h->n0 + 16);
}

/* Split bucket m: its keys spread over m and m + n0 * 2^j. */
static ds_status_t vh_split(ds_virtual_hash_t *h, size_t m) {
  size_t j = h->level[m];
  size_t m2 = m + (h->n0 << j);
  size_t i;
  size_t hv;
  ds_status_t st;
  if (m2 >= h->buckets) {
    st = vh_alloc(h, h->n0 << (j + 1));
    if (st != DS_OK) {
      return st;
    }
    h->max_level = j + 1;
  }
  /* reserve the new bucket first so moving cannot fail halfway */
  st = vh_reserve(h, m2, h->load[m]);
  if (st != DS_OK) {
    return st;
  }
  h->in_use[m2] = 1;
  h->level[m2] = (unsigned char)(j + 1);
  h->level[m] = (unsigned char)(j + 1);
  i = 0;
  while (i < h->load[m]) {
    hv = dh_mix(h->hash(vh_key(h, m, i), h->user));
    if (hv % (h->n0 << (j + 1)) == m2) {
      (void)vh_place(h, m2, vh_key(h, m, i), vh_val(h, m, i));
      h->load[m]--;
      if (i != h->load[m]) {
        memcpy(vh_key(h, m, i), vh_key(h, m, h->load[m]), h->key_size);
        if (h->value_size > 0) {
          memcpy(vh_val(h, m, i), vh_val(h, m, h->load[m]), h->value_size);
        }
      }
    } else {
      i++;
    }
  }
  return DS_OK;
}

ds_status_t ds_virtual_hash_put(ds_virtual_hash_t *h, const void *key,
                                const void *value) {
  size_t b;
  size_t i;
  ds_status_t st;
  if (h == NULL || key == NULL || (value == NULL && h->value_size > 0)) {
    return DS_ERR_NULL;
  }
  if (vh_find(h, key, &b, &i)) {
    if (h->value_size > 0) {
      memcpy(vh_val(h, b, i), value, h->value_size);
    }
    return DS_EXISTS;
  }
  while (h->load[b] >= h->bucket_size && vh_may_split(h, b)) {
    st = vh_split(h, b);
    if (st != DS_OK) {
      return st;
    }
    b = vh_bucket(h, dh_mix(h->hash(key, h->user)));
  }
  /* past the split limit the bucket overflows its nominal size */
  st = vh_place(h, b, key, value);
  if (st != DS_OK) {
    return st;
  }
  h->size++;
  return DS_OK;
}

void *ds_virtual_hash_get(const ds_virtual_hash_t *h, const void *key) {
  size_t b;
  size_t i;
  if (h == NULL || key == NULL || !vh_find(h, key, &b, &i)) {
    return NULL;
  }
  return vh_val(h, b, i);
}

ds_status_t ds_virtual_hash_remove(ds_virtual_hash_t *h, const void *key,
                                   void *out_value) {
  size_t b;
  size_t i;
  size_t last;
  if (h == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  if (!vh_find(h, key, &b, &i)) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && h->value_size > 0) {
    memcpy(out_value, vh_val(h, b, i), h->value_size);
  }
  last = --h->load[b];
  if (i != last) {
    memcpy(vh_key(h, b, i), vh_key(h, b, last), h->key_size);
    if (h->value_size > 0) {
      memcpy(vh_val(h, b, i), vh_val(h, b, last), h->value_size);
    }
  }
  h->size--;
  return DS_OK;
}

size_t ds_virtual_hash_buckets_in_use(const ds_virtual_hash_t *h) {
  size_t i;
  size_t n = 0;
  if (h == NULL) {
    return 0;
  }
  for (i = 0; i < h->buckets; i++) {
    n += h->in_use[i];
  }
  return n;
}

void ds_virtual_hash_foreach(const ds_virtual_hash_t *h, ds_dyn_visit_func_t f,
                             void *user) {
  size_t b;
  size_t i;
  if (h == NULL || f == NULL) {
    return;
  }
  for (b = 0; b < h->buckets; b++) {
    for (i = 0; i < h->load[b]; i++) {
      if (!f(vh_key(h, b, i), vh_val(h, b, i), user)) {
        return;
      }
    }
  }
}

/* ---- expandable hashing ---------------------------------------------------
 */

#define EH_BITS (sizeof(size_t) * 8)

static unsigned char *eh_key(const ds_expandable_hash_t *h, size_t b,
                             size_t i) {
  return h->keys + (b * h->bucket_size + i) * h->key_size;
}

static unsigned char *eh_val(const ds_expandable_hash_t *h, size_t b,
                             size_t i) {
  return h->values + (b * h->bucket_size + i) * h->value_size;
}

static ds_status_t eh_new_node(ds_expandable_hash_t *h, size_t *out) {
  size_t cap;
  void *p;
  if (h->node_free != DH_NONE) {
    *out = h->node_free;
    h->node_free = h->left[*out];
    return DS_OK;
  }
  if (h->nodes == h->node_capacity) {
    cap = h->node_capacity < 8 ? 8 : h->node_capacity * 2;
    if (cap <= h->node_capacity || cap > ((size_t)-1) / sizeof(size_t)) {
      return DS_ERR_OVERFLOW;
    }
    p = ds_context_realloc(h->context, h->left, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    h->left = (size_t *)p;
    p = ds_context_realloc(h->context, h->right, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    h->right = (size_t *)p;
    p = ds_context_realloc(h->context, h->bucket, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    h->bucket = (size_t *)p;
    h->node_capacity = cap;
  }
  *out = h->nodes++;
  return DS_OK;
}

static ds_status_t eh_new_bucket(ds_expandable_hash_t *h, size_t *out) {
  size_t cap;
  size_t cells;
  void *p;
  if (h->bucket_free != DH_NONE) {
    *out = h->bucket_free;
    h->bucket_free = h->load[*out];
    h->load[*out] = 0;
    return DS_OK;
  }
  if (h->bucket_count == h->bucket_capacity) {
    cap = h->bucket_capacity < 4 ? 4 : h->bucket_capacity * 2;
    if (cap <= h->bucket_capacity || cap > ((size_t)-1) / h->bucket_size) {
      return DS_ERR_OVERFLOW;
    }
    cells = cap * h->bucket_size;
    if (cells > ((size_t)-1) / h->key_size ||
        (h->value_size > 0 && cells > ((size_t)-1) / h->value_size) ||
        cap > ((size_t)-1) / sizeof(size_t)) {
      return DS_ERR_OVERFLOW;
    }
    p = ds_context_realloc(h->context, h->keys, cells * h->key_size);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    h->keys = (unsigned char *)p;
    p = ds_context_realloc(h->context, h->values,
                           h->value_size > 0 ? cells * h->value_size : 1);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    h->values = (unsigned char *)p;
    p = ds_context_realloc(h->context, h->load, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    h->load = (size_t *)p;
    h->bucket_capacity = cap;
  }
  *out = h->bucket_count++;
  h->load[*out] = 0;
  return DS_OK;
}

ds_status_t ds_expandable_hash_init(ds_expandable_hash_t *h, size_t key_size,
                                    size_t value_size, ds_dyn_hash_func_t hash,
                                    ds_dyn_equal_func_t equal, void *user,
                                    size_t bucket_size, ds_context_t *context) {
  size_t b = 0;
  ds_status_t st;
  if (h == NULL || hash == NULL || equal == NULL) {
    return DS_ERR_NULL;
  }
  if (bucket_size == 0) {
    bucket_size = 4;
  }
  if (!dh_args(key_size, hash, equal) || bucket_size > 65536) {
    return DS_ERR_INVALID;
  }
  memset(h, 0, sizeof(*h));
  h->node_free = DH_NONE;
  h->bucket_free = DH_NONE;
  h->bucket_size = bucket_size;
  h->key_size = key_size;
  h->value_size = value_size;
  h->hash = hash;
  h->equal = equal;
  h->user = user;
  h->context = context;
  st = eh_new_node(h, &h->root);
  if (st == DS_OK) {
    st = eh_new_bucket(h, &b);
  }
  if (st != DS_OK) {
    ds_expandable_hash_destroy(h);
    return st;
  }
  h->left[h->root] = h->right[h->root] = DH_NONE;
  h->bucket[h->root] = b;
  return DS_OK;
}

void ds_expandable_hash_destroy(ds_expandable_hash_t *h) {
  if (h == NULL) {
    return;
  }
  ds_context_free(h->context, h->left);
  ds_context_free(h->context, h->right);
  ds_context_free(h->context, h->bucket);
  ds_context_free(h->context, h->keys);
  ds_context_free(h->context, h->values);
  ds_context_free(h->context, h->load);
  h->left = h->right = h->bucket = h->load = NULL;
  h->keys = h->values = NULL;
  h->nodes = h->bucket_count = h->size = 0;
}

size_t ds_expandable_hash_size(const ds_expandable_hash_t *h) {
  return h == NULL ? 0 : h->size;
}

/* Leaf for hash value hv; path (up to EH_BITS + 1 nodes) if wanted. */
static size_t eh_leaf(const ds_expandable_hash_t *h, size_t hv, size_t *path,
                      size_t *depth) {
  size_t v = h->root;
  size_t d = 0;
  while (h->left[v] != DH_NONE) {
    if (path != NULL) {
      path[d] = v;
    }
    v = ((hv >> d) & 1u) ? h->right[v] : h->left[v];
    d++;
  }
  if (path != NULL) {
    path[d] = v;
  }
  if (depth != NULL) {
    *depth = d;
  }
  return v;
}

static bool eh_find(const ds_expandable_hash_t *h, const void *key, size_t *ob,
                    size_t *oi) {
  size_t b = h->bucket[eh_leaf(h, dh_mix(h->hash(key, h->user)), NULL, NULL)];
  size_t i;
  for (i = 0; i < h->load[b]; i++) {
    if (h->equal(eh_key(h, b, i), key, h->user)) {
      *ob = b;
      *oi = i;
      return true;
    }
  }
  return false;
}

static void eh_move(ds_expandable_hash_t *h, size_t from, size_t i, size_t to) {
  size_t j = h->load[to]++;
  memcpy(eh_key(h, to, j), eh_key(h, from, i), h->key_size);
  if (h->value_size > 0) {
    memcpy(eh_val(h, to, j), eh_val(h, from, i), h->value_size);
  }
  h->load[from]--;
  if (i != h->load[from]) {
    memcpy(eh_key(h, from, i), eh_key(h, from, h->load[from]), h->key_size);
    if (h->value_size > 0) {
      memcpy(eh_val(h, from, i), eh_val(h, from, h->load[from]), h->value_size);
    }
  }
}

ds_status_t ds_expandable_hash_put(ds_expandable_hash_t *h, const void *key,
                                   const void *value) {
  size_t hv;
  size_t v;
  size_t d;
  size_t b;
  size_t i;
  size_t l;
  size_t r;
  size_t nb;
  ds_status_t st;
  if (h == NULL || key == NULL || (value == NULL && h->value_size > 0)) {
    return DS_ERR_NULL;
  }
  if (eh_find(h, key, &b, &i)) {
    if (h->value_size > 0) {
      memcpy(eh_val(h, b, i), value, h->value_size);
    }
    return DS_EXISTS;
  }
  hv = dh_mix(h->hash(key, h->user));
  for (;;) {
    v = eh_leaf(h, hv, NULL, &d);
    b = h->bucket[v];
    if (h->load[b] < h->bucket_size) {
      break;
    }
    if (d >= EH_BITS) {
      return DS_ERR_OVERFLOW;
    }
    /* split the leaf on bit d */
    st = eh_new_node(h, &l);
    if (st != DS_OK) {
      return st;
    }
    st = eh_new_node(h, &r);
    if (st != DS_OK) {
      h->left[l] = h->node_free;
      h->node_free = l;
      return st;
    }
    st = eh_new_bucket(h, &nb);
    if (st != DS_OK) {
      h->left[r] = h->node_free;
      h->left[l] = r;
      h->node_free = l;
      return st;
    }
    h->left[l] = h->right[l] = DH_NONE;
    h->left[r] = h->right[r] = DH_NONE;
    h->bucket[l] = b;
    h->bucket[r] = nb;
    h->left[v] = l;
    h->right[v] = r;
    i = 0;
    while (i < h->load[b]) {
      if ((dh_mix(h->hash(eh_key(h, b, i), h->user)) >> d) & 1u) {
        eh_move(h, b, i, nb);
      } else {
        i++;
      }
    }
  }
  i = h->load[b]++;
  memcpy(eh_key(h, b, i), key, h->key_size);
  if (h->value_size > 0) {
    memcpy(eh_val(h, b, i), value, h->value_size);
  }
  h->size++;
  return DS_OK;
}

void *ds_expandable_hash_get(const ds_expandable_hash_t *h, const void *key) {
  size_t b;
  size_t i;
  if (h == NULL || key == NULL || !eh_find(h, key, &b, &i)) {
    return NULL;
  }
  return eh_val(h, b, i);
}

ds_status_t ds_expandable_hash_remove(ds_expandable_hash_t *h, const void *key,
                                      void *out_value) {
  size_t path[EH_BITS + 1];
  size_t hv;
  size_t d;
  size_t v;
  size_t b;
  size_t i;
  size_t p;
  size_t l;
  size_t r;
  size_t bl;
  size_t br;
  if (h == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  hv = dh_mix(h->hash(key, h->user));
  v = eh_leaf(h, hv, path, &d);
  b = h->bucket[v];
  for (i = 0; i < h->load[b]; i++) {
    if (h->equal(eh_key(h, b, i), key, h->user)) {
      break;
    }
  }
  if (i == h->load[b]) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && h->value_size > 0) {
    memcpy(out_value, eh_val(h, b, i), h->value_size);
  }
  h->load[b]--;
  if (i != h->load[b]) {
    memcpy(eh_key(h, b, i), eh_key(h, b, h->load[b]), h->key_size);
    if (h->value_size > 0) {
      memcpy(eh_val(h, b, i), eh_val(h, b, h->load[b]), h->value_size);
    }
  }
  h->size--;
  /* merge sibling leaves that fit in one bucket, upwards */
  while (d > 0) {
    p = path[d - 1];
    l = h->left[p];
    r = h->right[p];
    if (h->left[l] != DH_NONE || h->left[r] != DH_NONE) {
      break;
    }
    bl = h->bucket[l];
    br = h->bucket[r];
    if (h->load[bl] + h->load[br] > h->bucket_size) {
      break;
    }
    while (h->load[br] > 0) {
      eh_move(h, br, h->load[br] - 1, bl);
    }
    h->load[br] = h->bucket_free;
    h->bucket_free = br;
    h->left[r] = h->node_free;
    h->left[l] = r;
    h->node_free = l;
    h->left[p] = h->right[p] = DH_NONE;
    h->bucket[p] = bl;
    d--;
  }
  return DS_OK;
}

size_t ds_expandable_hash_buckets(const ds_expandable_hash_t *h) {
  size_t n = 0;
  size_t b;
  if (h == NULL) {
    return 0;
  }
  for (b = h->bucket_free; b != DH_NONE; b = h->load[b]) {
    n++;
  }
  return h->bucket_count - n;
}

void ds_expandable_hash_foreach(const ds_expandable_hash_t *h,
                                ds_dyn_visit_func_t f, void *user) {
  size_t stack[EH_BITS + 2];
  size_t top = 0;
  size_t v;
  size_t i;
  size_t b;
  if (h == NULL || f == NULL || h->left == NULL) {
    return;
  }
  stack[top++] = h->root;
  while (top > 0) {
    v = stack[--top];
    if (h->left[v] != DH_NONE) {
      stack[top++] = h->right[v];
      stack[top++] = h->left[v];
      continue;
    }
    b = h->bucket[v];
    for (i = 0; i < h->load[b]; i++) {
      if (!f(eh_key(h, b, i), eh_val(h, b, i), user)) {
        return;
      }
    }
  }
}

/* ---- hashbelt -------------------------------------------------------------
 */

static unsigned char *hb_key(const ds_hashbelt_t *b, const ds_hashbelt_gen_t *g,
                             size_t i) {
  return g->keys + i * b->key_size;
}

static unsigned char *hb_val(const ds_hashbelt_t *b, const ds_hashbelt_gen_t *g,
                             size_t i) {
  return g->values + i * b->value_size;
}

ds_status_t ds_hashbelt_init(ds_hashbelt_t *b, size_t key_size,
                             size_t value_size, ds_dyn_hash_func_t hash,
                             ds_dyn_equal_func_t equal, void *user,
                             size_t generations, bool refresh,
                             ds_context_t *context) {
  if (b == NULL || hash == NULL || equal == NULL) {
    return DS_ERR_NULL;
  }
  if (!dh_args(key_size, hash, equal) || generations < 2 ||
      generations > 4096) {
    return DS_ERR_INVALID;
  }
  memset(b, 0, sizeof(*b));
  b->gen = (ds_hashbelt_gen_t *)ds_context_calloc(context, generations,
                                                  sizeof(ds_hashbelt_gen_t));
  if (b->gen == NULL) {
    return DS_ERR_ALLOC;
  }
  b->generations = generations;
  b->refresh = refresh;
  b->key_size = key_size;
  b->value_size = value_size;
  b->hash = hash;
  b->equal = equal;
  b->user = user;
  b->context = context;
  return DS_OK;
}

static void hb_gen_free(ds_hashbelt_t *b, ds_hashbelt_gen_t *g) {
  ds_context_free(b->context, g->keys);
  ds_context_free(b->context, g->values);
  ds_context_free(b->context, g->used);
  memset(g, 0, sizeof(*g));
}

void ds_hashbelt_destroy(ds_hashbelt_t *b) {
  size_t i;
  if (b == NULL || b->gen == NULL) {
    return;
  }
  for (i = 0; i < b->generations; i++) {
    hb_gen_free(b, &b->gen[i]);
  }
  ds_context_free(b->context, b->gen);
  b->gen = NULL;
  b->size = 0;
}

size_t ds_hashbelt_size(const ds_hashbelt_t *b) {
  return b == NULL ? 0 : b->size;
}

static size_t hb_home(const ds_hashbelt_t *b, const ds_hashbelt_gen_t *g,
                      const void *key) {
  return dh_mix(b->hash(key, b->user)) & (g->slots - 1);
}

static size_t hb_slot(const ds_hashbelt_t *b, const ds_hashbelt_gen_t *g,
                      const void *key) {
  size_t i = hb_home(b, g, key);
  while (g->used[i] && !b->equal(hb_key(b, g, i), key, b->user)) {
    i = (i + 1) & (g->slots - 1);
  }
  return i;
}

static ds_status_t hb_gen_insert(ds_hashbelt_t *b, ds_hashbelt_gen_t *g,
                                 const void *key, const void *value);

static ds_status_t hb_gen_grow(ds_hashbelt_t *b, ds_hashbelt_gen_t *g) {
  ds_hashbelt_gen_t old = *g;
  size_t ns = g->slots == 0 ? 16 : g->slots * 2;
  size_t i;
  ds_status_t st;
  if (ns <= g->slots || ns > ((size_t)-1) / b->key_size ||
      (b->value_size > 0 && ns > ((size_t)-1) / b->value_size)) {
    return DS_ERR_OVERFLOW;
  }
  g->keys = (unsigned char *)ds_context_alloc(b->context, ns * b->key_size);
  g->values = (unsigned char *)ds_context_alloc(
      b->context, b->value_size > 0 ? ns * b->value_size : 1);
  g->used = (unsigned char *)ds_context_calloc(b->context, ns, 1);
  if (g->keys == NULL || g->values == NULL || g->used == NULL) {
    ds_context_free(b->context, g->keys);
    ds_context_free(b->context, g->values);
    ds_context_free(b->context, g->used);
    *g = old;
    return DS_ERR_ALLOC;
  }
  g->slots = ns;
  g->size = 0;
  for (i = 0; i < old.slots; i++) {
    if (old.used[i]) {
      st = hb_gen_insert(b, g, old.keys + i * b->key_size,
                         old.values + i * b->value_size);
      (void)st; /* room is guaranteed */
    }
  }
  ds_context_free(b->context, old.keys);
  ds_context_free(b->context, old.values);
  ds_context_free(b->context, old.used);
  return DS_OK;
}

static ds_status_t hb_gen_insert(ds_hashbelt_t *b, ds_hashbelt_gen_t *g,
                                 const void *key, const void *value) {
  size_t i;
  ds_status_t st;
  if (g->slots == 0 || (g->size + 1) * 4 > g->slots * 3) {
    st = hb_gen_grow(b, g);
    if (st != DS_OK) {
      return st;
    }
  }
  i = hb_slot(b, g, key);
  memcpy(hb_key(b, g, i), key, b->key_size);
  if (b->value_size > 0) {
    memcpy(hb_val(b, g, i), value, b->value_size);
  }
  if (!g->used[i]) {
    g->used[i] = 1;
    g->size++;
  }
  return DS_OK;
}

/* Backward-shift deletion of slot i. */
static void hb_gen_erase(ds_hashbelt_t *b, ds_hashbelt_gen_t *g, size_t i) {
  size_t j = i;
  size_t h;
  size_t mask = g->slots - 1;
  g->used[i] = 0;
  g->size--;
  for (;;) {
    j = (j + 1) & mask;
    if (!g->used[j]) {
      return;
    }
    h = hb_home(b, g, hb_key(b, g, j));
    if ((i < j && (h <= i || h > j)) || (i > j && h <= i && h > j)) {
      memcpy(hb_key(b, g, i), hb_key(b, g, j), b->key_size);
      if (b->value_size > 0) {
        memcpy(hb_val(b, g, i), hb_val(b, g, j), b->value_size);
      }
      g->used[i] = 1;
      g->used[j] = 0;
      i = j;
    }
  }
}

/* Generation index and slot holding key, newest first. */
static bool hb_locate(const ds_hashbelt_t *b, const void *key, size_t *og,
                      size_t *oi) {
  size_t a;
  size_t gi;
  size_t i;
  const ds_hashbelt_gen_t *g;
  for (a = 0; a < b->generations; a++) {
    gi = (b->newest + b->generations - a) % b->generations;
    g = &b->gen[gi];
    if (g->size == 0) {
      continue;
    }
    i = hb_slot(b, g, key);
    if (g->used[i]) {
      *og = gi;
      *oi = i;
      return true;
    }
  }
  return false;
}

ds_status_t ds_hashbelt_put(ds_hashbelt_t *b, const void *key,
                            const void *value) {
  size_t gi;
  size_t i;
  bool had;
  ds_status_t st;
  if (b == NULL || key == NULL || (value == NULL && b->value_size > 0)) {
    return DS_ERR_NULL;
  }
  had = hb_locate(b, key, &gi, &i);
  if (had && gi != b->newest) {
    /* insert into the newest first so a failure loses nothing */
    st = hb_gen_insert(b, &b->gen[b->newest], key, value);
    if (st != DS_OK) {
      return st;
    }
    hb_gen_erase(b, &b->gen[gi], hb_slot(b, &b->gen[gi], key));
    return DS_EXISTS;
  }
  st = hb_gen_insert(b, &b->gen[b->newest], key, value);
  if (st != DS_OK) {
    return st;
  }
  if (had) {
    return DS_EXISTS;
  }
  b->size++;
  return DS_OK;
}

ds_status_t ds_hashbelt_get(ds_hashbelt_t *b, const void *key, void *out) {
  size_t gi;
  size_t i;
  ds_hashbelt_gen_t *g;
  if (b == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  if (!hb_locate(b, key, &gi, &i)) {
    return DS_NOT_FOUND;
  }
  g = &b->gen[gi];
  if (out != NULL && b->value_size > 0) {
    memcpy(out, hb_val(b, g, i), b->value_size);
  }
  if (b->refresh && gi != b->newest) {
    /* copy out before erasing: the slot may be reused by the shift */
    unsigned char *tmp = (unsigned char *)ds_context_alloc(
        b->context, b->key_size + b->value_size);
    if (tmp == NULL) {
      return DS_OK; /* found; refresh skipped */
    }
    memcpy(tmp, hb_key(b, g, i), b->key_size);
    if (b->value_size > 0) {
      memcpy(tmp + b->key_size, hb_val(b, g, i), b->value_size);
    }
    if (hb_gen_insert(b, &b->gen[b->newest], tmp, tmp + b->key_size) == DS_OK) {
      hb_gen_erase(b, g, hb_slot(b, g, tmp));
    }
    ds_context_free(b->context, tmp);
  }
  return DS_OK;
}

ds_status_t ds_hashbelt_remove(ds_hashbelt_t *b, const void *key, void *out) {
  size_t gi;
  size_t i;
  if (b == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  if (!hb_locate(b, key, &gi, &i)) {
    return DS_NOT_FOUND;
  }
  if (out != NULL && b->value_size > 0) {
    memcpy(out, hb_val(b, &b->gen[gi], i), b->value_size);
  }
  hb_gen_erase(b, &b->gen[gi], i);
  b->size--;
  return DS_OK;
}

ds_status_t ds_hashbelt_age(const ds_hashbelt_t *b, const void *key,
                            size_t *out_age) {
  size_t gi;
  size_t i;
  if (b == NULL || key == NULL || out_age == NULL) {
    return DS_ERR_NULL;
  }
  if (!hb_locate(b, key, &gi, &i)) {
    return DS_NOT_FOUND;
  }
  *out_age = (b->newest + b->generations - gi) % b->generations;
  return DS_OK;
}

ds_status_t ds_hashbelt_rotate(ds_hashbelt_t *b, ds_dyn_visit_func_t expired,
                               void *user) {
  size_t oldest;
  size_t i;
  ds_hashbelt_gen_t *g;
  if (b == NULL) {
    return DS_ERR_NULL;
  }
  oldest = (b->newest + 1) % b->generations;
  g = &b->gen[oldest];
  for (i = 0; i < g->slots; i++) {
    if (g->used[i]) {
      if (expired != NULL) {
        (void)expired(hb_key(b, g, i), hb_val(b, g, i), user);
      }
      g->used[i] = 0;
    }
  }
  b->size -= g->size;
  g->size = 0;
  b->newest = oldest;
  return DS_OK;
}
