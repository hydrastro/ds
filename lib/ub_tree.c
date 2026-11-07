#include "ub_tree.h"
#include <stdlib.h>
#include <string.h>

#define UB_NONE (~(size_t)0)
#define UB_W32 0xFFFFFFFFUL

/* ---- universal B-tree
 * ---------------------------------------------------------- */

static unsigned long ub_low(size_t n) {
  return n >= 32 ? UB_W32 : (1UL << n) - 1UL;
}

void ds_ub_tree_zaddress(const ds_ub_tree_t *t, const unsigned long *point,
                         unsigned long *out) {
  size_t d;
  size_t total;
  size_t i;
  if (t == NULL || point == NULL || out == NULL) {
    return;
  }
  d = t->dim;
  total = d * t->bits;
  for (i = 0; i < t->words; i++) {
    out[i] = 0;
  }
  for (i = 0; i < total; i++) {
    size_t k = i % d;
    size_t j = t->bits - 1 - i / d;
    if ((point[k] >> j) & 1UL) {
      out[i / 32] |= 1UL << (31 - i % 32);
    }
  }
}

static int ub_cmp(const unsigned long *a, const unsigned long *b, size_t w) {
  size_t i;
  for (i = 0; i < w; i++) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }
  return 0;
}

static unsigned long *ub_key(const ds_ub_tree_t *t, size_t v, size_t i) {
  return t->keys + (v * t->slots + i) * t->words;
}

static unsigned long *ub_pt(const ds_ub_tree_t *t, size_t v, size_t i) {
  return t->pts + (v * t->slots + i) * t->dim;
}

static size_t *ub_ref(const ds_ub_tree_t *t, size_t v, size_t i) {
  return t->ref + v * t->slots + i;
}

static ds_status_t ub_node(ds_ub_tree_t *t, bool leaf, size_t *out) {
  size_t v;
  if (t->free_list != UB_NONE) {
    v = t->free_list;
    t->free_list = t->next[v];
  } else {
    if (t->nodes == t->capacity) {
      size_t nc = t->capacity < 8 ? 8 : t->capacity * 2;
      void *p;
#define UB_RE(field, type, per)                                                \
  p = ds_context_realloc(t->context, t->field, nc * (per) * sizeof(type));     \
  if (p == NULL) {                                                             \
    return DS_ERR_ALLOC;                                                       \
  }                                                                            \
  t->field = (type *)p;
      UB_RE(leaf, unsigned char, 1)
      UB_RE(count, size_t, 1)
      UB_RE(next, size_t, 1)
      UB_RE(prev, size_t, 1)
      UB_RE(parent, size_t, 1)
      UB_RE(keys, unsigned long, t->slots * t->words)
      UB_RE(pts, unsigned long, t->slots * t->dim)
      UB_RE(ref, size_t, t->slots)
#undef UB_RE
      t->capacity = nc;
    }
    v = t->nodes++;
  }
  t->leaf[v] = (unsigned char)leaf;
  t->count[v] = 0;
  t->next[v] = t->prev[v] = UB_NONE;
  t->parent[v] = UB_NONE;
  *out = v;
  return DS_OK;
}

static void ub_free(ds_ub_tree_t *t, size_t v) {
  t->next[v] = t->free_list;
  t->free_list = v;
}

ds_status_t ds_ub_tree_init(ds_ub_tree_t *t, size_t dim, size_t bits,
                            size_t leaf_cap, size_t fanout,
                            ds_context_t *context) {
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (bits == 0) {
    bits = 32;
  }
  if (leaf_cap == 0) {
    leaf_cap = 32;
  }
  if (fanout == 0) {
    fanout = 32;
  }
  if (dim == 0 || dim > DS_UB_MAX_DIM || bits > 32 || leaf_cap < 2 ||
      fanout < 3 || leaf_cap > 4096 || fanout > 4096) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->bits = bits;
  t->words = (dim * bits + 31) / 32;
  t->leaf_cap = leaf_cap;
  t->fanout = fanout;
  t->slots = (leaf_cap > fanout ? leaf_cap : fanout) + 1;
  t->free_list = UB_NONE;
  t->context = context;
  st = ub_node(t, true, &t->root);
  if (st != DS_OK) {
    ds_ub_tree_destroy(t);
    return st;
  }
  t->height = 1;
  return DS_OK;
}

void ds_ub_tree_destroy(ds_ub_tree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->leaf);
  ds_context_free(t->context, t->count);
  ds_context_free(t->context, t->next);
  ds_context_free(t->context, t->prev);
  ds_context_free(t->context, t->parent);
  ds_context_free(t->context, t->keys);
  ds_context_free(t->context, t->pts);
  ds_context_free(t->context, t->ref);
  memset(t, 0, sizeof(*t));
}

size_t ds_ub_tree_size(const ds_ub_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

size_t ds_ub_tree_height(const ds_ub_tree_t *t) {
  return t == NULL ? 0 : t->height;
}

static bool ub_point_ok(const ds_ub_tree_t *t, const unsigned long *p) {
  size_t a;
  for (a = 0; a < t->dim; a++) {
    if (p[a] > ub_low(t->bits)) {
      return false;
    }
  }
  return true;
}

/* The leaf and slot of the first key >= z (strict descent). */
static void ub_lower(const ds_ub_tree_t *t, const unsigned long *z,
                     size_t *out_leaf, size_t *out_pos) {
  size_t v = t->root;
  size_t i;
  while (!t->leaf[v]) {
    size_t c = 0;
    for (i = 1; i < t->count[v]; i++) {
      if (ub_cmp(ub_key(t, v, i), z, t->words) < 0) {
        c = i;
      } else {
        break;
      }
    }
    v = *ub_ref(t, v, c);
  }
  for (i = 0; i < t->count[v] && ub_cmp(ub_key(t, v, i), z, t->words) < 0;
       i++) {
  }
  *out_leaf = v;
  *out_pos = i;
}

static void ub_move(ds_ub_tree_t *t, size_t dv, size_t di, size_t sv, size_t si,
                    size_t n) {
  memmove(ub_key(t, dv, di), ub_key(t, sv, si),
          n * t->words * sizeof(unsigned long));
  if (t->leaf[sv]) {
    memmove(ub_pt(t, dv, di), ub_pt(t, sv, si),
            n * t->dim * sizeof(unsigned long));
  }
  memmove(ub_ref(t, dv, di), ub_ref(t, sv, si), n * sizeof(size_t));
}

static size_t ub_child_slot(const ds_ub_tree_t *t, size_t p, size_t c) {
  size_t i;
  for (i = 0; i < t->count[p]; i++) {
    if (*ub_ref(t, p, i) == c) {
      return i;
    }
  }
  return UB_NONE;
}

ds_status_t ds_ub_tree_insert(ds_ub_tree_t *t, const unsigned long *point,
                              size_t value) {
  unsigned long z[DS_UB_MAX_DIM];
  size_t v;
  size_t i;
  ds_status_t st;
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  if (!ub_point_ok(t, point)) {
    return DS_ERR_RANGE;
  }
  ds_ub_tree_zaddress(t, point, z);
  v = t->root;
  while (!t->leaf[v]) {
    size_t c = 0;
    for (i = 1; i < t->count[v]; i++) {
      if (ub_cmp(ub_key(t, v, i), z, t->words) <= 0) {
        c = i;
      } else {
        break;
      }
    }
    v = *ub_ref(t, v, c);
  }
  for (i = 0; i < t->count[v] && ub_cmp(ub_key(t, v, i), z, t->words) <= 0;
       i++) {
  }
  ub_move(t, v, i + 1, v, i, t->count[v] - i);
  memcpy(ub_key(t, v, i), z, t->words * sizeof(unsigned long));
  memcpy(ub_pt(t, v, i), point, t->dim * sizeof(unsigned long));
  *ub_ref(t, v, i) = value;
  t->count[v]++;
  t->size++;
  /* split upwards */
  while (t->count[v] > (t->leaf[v] ? t->leaf_cap : t->fanout)) {
    size_t w;
    size_t half = t->count[v] / 2;
    size_t p;
    size_t slot;
    st = ub_node(t, t->leaf[v] != 0, &w);
    if (st != DS_OK) {
      return st;
    }
    ub_move(t, w, 0, v, half, t->count[v] - half);
    t->count[w] = t->count[v] - half;
    t->count[v] = half;
    if (t->leaf[v]) {
      t->next[w] = t->next[v];
      t->prev[w] = v;
      if (t->next[v] != UB_NONE) {
        t->prev[t->next[v]] = w;
      }
      t->next[v] = w;
    } else {
      for (i = 0; i < t->count[w]; i++) {
        t->parent[*ub_ref(t, w, i)] = w;
      }
    }
    if (v == t->root) {
      size_t r;
      st = ub_node(t, false, &r);
      if (st != DS_OK) {
        return st;
      }
      memcpy(ub_key(t, r, 0), ub_key(t, v, 0),
             t->words * sizeof(unsigned long));
      *ub_ref(t, r, 0) = v;
      t->count[r] = 1;
      t->parent[v] = r;
      t->root = r;
      t->height++;
    }
    p = t->parent[v];
    slot = ub_child_slot(t, p, v) + 1;
    ub_move(t, p, slot + 1, p, slot, t->count[p] - slot);
    memcpy(ub_key(t, p, slot), ub_key(t, w, 0),
           t->words * sizeof(unsigned long));
    *ub_ref(t, p, slot) = w;
    t->count[p]++;
    t->parent[w] = p;
    v = p;
  }
  return DS_OK;
}

/* the entry (leaf, slot) holding the point with the value */
static bool ub_locate(const ds_ub_tree_t *t, const unsigned long *point,
                      const size_t *value, size_t *out_leaf, size_t *out_pos) {
  unsigned long z[DS_UB_MAX_DIM];
  size_t v;
  size_t i;
  ds_ub_tree_zaddress(t, point, z);
  ub_lower(t, z, &v, &i);
  while (v != UB_NONE) {
    if (i == t->count[v]) {
      v = t->next[v];
      i = 0;
      continue;
    }
    if (ub_cmp(ub_key(t, v, i), z, t->words) != 0) {
      return false;
    }
    if (value == NULL || *ub_ref(t, v, i) == *value) {
      *out_leaf = v;
      *out_pos = i;
      return true;
    }
    i++;
  }
  return false;
}

ds_status_t ds_ub_tree_find(const ds_ub_tree_t *t, const unsigned long *point,
                            size_t *out_value) {
  size_t v;
  size_t i;
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  if (!ub_point_ok(t, point) || !ub_locate(t, point, NULL, &v, &i)) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL) {
    *out_value = *ub_ref(t, v, i);
  }
  return DS_OK;
}

ds_status_t ds_ub_tree_remove(ds_ub_tree_t *t, const unsigned long *point,
                              size_t value) {
  size_t v;
  size_t i;
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  if (!ub_point_ok(t, point) || !ub_locate(t, point, &value, &v, &i)) {
    return DS_NOT_FOUND;
  }
  ub_move(t, v, i, v, i + 1, t->count[v] - i - 1);
  t->count[v]--;
  t->size--;
  /* drop empty nodes */
  while (t->count[v] == 0 && v != t->root) {
    size_t p = t->parent[v];
    size_t slot = ub_child_slot(t, p, v);
    if (t->leaf[v]) {
      if (t->prev[v] != UB_NONE) {
        t->next[t->prev[v]] = t->next[v];
      }
      if (t->next[v] != UB_NONE) {
        t->prev[t->next[v]] = t->prev[v];
      }
    }
    ub_move(t, p, slot, p, slot + 1, t->count[p] - slot - 1);
    t->count[p]--;
    ub_free(t, v);
    v = p;
  }
  while (!t->leaf[t->root] && t->count[t->root] == 1) {
    size_t old = t->root;
    t->root = *ub_ref(t, old, 0);
    t->parent[t->root] = UB_NONE;
    ub_free(t, old);
    t->height--;
  }
  if (!t->leaf[t->root] && t->count[t->root] == 0) {
    t->leaf[t->root] = 1;
    t->next[t->root] = t->prev[t->root] = UB_NONE;
    t->height = 1;
  }
  return DS_OK;
}

/* Tropf-Herzog: the Z-smallest point of the box beyond p (p outside). */
static bool ub_bigmin(const ds_ub_tree_t *t, const unsigned long *p,
                      const unsigned long *lo, const unsigned long *hi,
                      unsigned long *out) {
  unsigned long minc[DS_UB_MAX_DIM];
  unsigned long maxc[DS_UB_MAX_DIM];
  unsigned long best[DS_UB_MAX_DIM];
  bool have = false;
  size_t d = t->dim;
  size_t total = d * t->bits;
  size_t s;
  memcpy(minc, lo, d * sizeof(unsigned long));
  memcpy(maxc, hi, d * sizeof(unsigned long));
  for (s = 0; s < total; s++) {
    size_t k = s % d;
    size_t j = t->bits - 1 - s / d;
    unsigned b = (unsigned)((p[k] >> j) & 1UL);
    unsigned l = (unsigned)((minc[k] >> j) & 1UL);
    unsigned h = (unsigned)((maxc[k] >> j) & 1UL);
    unsigned long keep = ~ub_low(j + 1) & UB_W32;
    if (b == 0 && l == 0 && h == 1) {
      memcpy(best, minc, d * sizeof(unsigned long));
      best[k] = (best[k] & keep) | (1UL << j);
      have = true;
      maxc[k] = (maxc[k] & keep) | ub_low(j);
    } else if (b == 0 && l == 1 && h == 1) {
      memcpy(out, minc, d * sizeof(unsigned long));
      return true;
    } else if (b == 1 && l == 0 && h == 0) {
      if (have) {
        memcpy(out, best, d * sizeof(unsigned long));
      }
      return have;
    } else if (b == 1 && l == 0 && h == 1) {
      minc[k] = (minc[k] & keep) | (1UL << j);
    } else if (l == 1 && h == 0) {
      return false; /* empty box */
    }
  }
  memcpy(out, p, d * sizeof(unsigned long));
  return true;
}

ds_status_t ds_ub_tree_query(const ds_ub_tree_t *t, const unsigned long *box,
                             ds_ub_visit_func_t visit, void *user) {
  unsigned long zmax[DS_UB_MAX_DIM];
  unsigned long z[DS_UB_MAX_DIM];
  size_t d;
  size_t a;
  size_t v;
  size_t i;
  if (t == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  for (a = 0; a < d; a++) {
    if (box[a] > box[d + a] || box[d + a] > ub_low(t->bits)) {
      return box[a] > box[d + a] ? DS_OK : DS_ERR_RANGE;
    }
  }
  ds_ub_tree_zaddress(t, box + d, zmax);
  ds_ub_tree_zaddress(t, box, z);
  ub_lower(t, z, &v, &i);
  while (v != UB_NONE) {
    const unsigned long *p;
    if (i == t->count[v]) {
      v = t->next[v];
      i = 0;
      continue;
    }
    if (ub_cmp(ub_key(t, v, i), zmax, t->words) > 0) {
      break;
    }
    p = ub_pt(t, v, i);
    for (a = 0; a < d && p[a] >= box[a] && p[a] <= box[d + a]; a++) {
    }
    if (a == d) {
      if (!visit(p, *ub_ref(t, v, i), user)) {
        return DS_STOP;
      }
      i++;
      continue;
    }
    {
      unsigned long next[DS_UB_MAX_DIM];
      if (!ub_bigmin(t, p, box, box + d, next)) {
        break;
      }
      ds_ub_tree_zaddress(t, next, z);
      ub_lower(t, z, &v, &i);
    }
  }
  return DS_OK;
}

bool ds_ub_tree_valid(const ds_ub_tree_t *t) {
  size_t v;
  size_t i;
  size_t total = 0;
  unsigned long prev[DS_UB_MAX_DIM];
  bool have = false;
  size_t *stack;
  size_t top = 0;
  bool ok = true;
  if (t == NULL || t->leaf == NULL) {
    return false;
  }
  /* the leftmost leaf, then the chain */
  for (v = t->root; !t->leaf[v]; v = *ub_ref(t, v, 0)) {
  }
  if (t->prev[v] != UB_NONE) {
    return false;
  }
  for (; v != UB_NONE && ok; v = t->next[v]) {
    for (i = 0; i < t->count[v] && ok; i++) {
      unsigned long z[DS_UB_MAX_DIM];
      ds_ub_tree_zaddress(t, ub_pt(t, v, i), z);
      ok = ub_cmp(z, ub_key(t, v, i), t->words) == 0 &&
           (!have || ub_cmp(prev, z, t->words) <= 0);
      memcpy(prev, z, t->words * sizeof(unsigned long));
      have = true;
    }
    if (t->next[v] != UB_NONE && t->prev[t->next[v]] != v) {
      ok = false;
    }
    total += t->count[v];
  }
  if (!ok || total != t->size) {
    return false;
  }
  /* separators bound their children */
  stack =
      (size_t *)ds_context_alloc(t->context, (t->nodes + 1) * sizeof(size_t));
  if (stack == NULL) {
    return false;
  }
  stack[top++] = t->root;
  while (top > 0 && ok) {
    v = stack[--top];
    if (t->leaf[v]) {
      continue;
    }
    for (i = 0; i < t->count[v] && ok; i++) {
      size_t c = *ub_ref(t, v, i);
      size_t u;
      ok = t->parent[c] == v && t->count[c] > 0;
      /* every key below child i is >= separator i */
      for (u = c; ok && !t->leaf[u]; u = *ub_ref(t, u, 0)) {
      }
      if (ok && i > 0) {
        ok = ub_cmp(ub_key(t, u, 0), ub_key(t, v, i), t->words) >= 0;
      }
      stack[top++] = c;
    }
  }
  ds_context_free(t->context, stack);
  return ok;
}

/* ---- unlimited branching tree
 * ------------------------------------------------- */

static ds_status_t ubr_node(ds_ubranch_tree_t *t, size_t elem, size_t parent,
                            size_t *out) {
  size_t v;
  if (t->free_list != UB_NONE) {
    v = t->free_list;
    t->free_list = t->parent[v];
  } else {
    if (t->nodes == t->capacity) {
      size_t nc = t->capacity < 8 ? 16 : t->capacity * 2;
      void *p;
#define UBR_RE(field, type)                                                    \
  p = ds_context_realloc(t->context, t->field, nc * sizeof(type));             \
  if (p == NULL) {                                                             \
    return DS_ERR_ALLOC;                                                       \
  }                                                                            \
  t->field = (type *)p;
      UBR_RE(elem, size_t)
      UBR_RE(parent, size_t)
      UBR_RE(child, size_t *)
      UBR_RE(child_count, size_t)
      UBR_RE(child_capacity, size_t)
      UBR_RE(terminal, unsigned char)
      UBR_RE(value, size_t)
#undef UBR_RE
      t->capacity = nc;
    }
    v = t->nodes++;
  }
  t->elem[v] = elem;
  t->parent[v] = parent;
  t->child[v] = NULL;
  t->child_count[v] = t->child_capacity[v] = 0;
  t->terminal[v] = 0;
  t->value[v] = 0;
  *out = v;
  return DS_OK;
}

ds_status_t ds_ubranch_tree_init(ds_ubranch_tree_t *t, ds_context_t *context) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  t->free_list = UB_NONE;
  t->context = context;
  return DS_OK;
}

void ds_ubranch_tree_destroy(ds_ubranch_tree_t *t) {
  size_t v;
  if (t == NULL) {
    return;
  }
  for (v = 0; v < t->nodes; v++) {
    ds_context_free(t->context, t->child[v]);
  }
  ds_context_free(t->context, t->elem);
  ds_context_free(t->context, t->parent);
  ds_context_free(t->context, t->child);
  ds_context_free(t->context, t->child_count);
  ds_context_free(t->context, t->child_capacity);
  ds_context_free(t->context, t->terminal);
  ds_context_free(t->context, t->value);
  ds_context_free(t->context, t->roots);
  ds_context_free(t->context, t->path);
  memset(t, 0, sizeof(*t));
}

size_t ds_ubranch_tree_size(const ds_ubranch_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

size_t ds_ubranch_tree_nodes(const ds_ubranch_tree_t *t) {
  size_t n = 0;
  size_t v;
  if (t == NULL) {
    return 0;
  }
  n = t->nodes;
  for (v = t->free_list; v != UB_NONE; v = t->parent[v]) {
    n--;
  }
  return n;
}

/* position of elem in an ordered child array (or where it would go) */
static size_t ubr_search(const ds_ubranch_tree_t *t, const size_t *kids,
                         size_t n, size_t elem, bool *found) {
  size_t lo = 0;
  size_t hi = n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (t->elem[kids[mid]] < elem) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  *found = lo < n && t->elem[kids[lo]] == elem;
  return lo;
}

static bool ubr_list_ok(const size_t *list, size_t n) {
  size_t i;
  if (list == NULL || n == 0) {
    return false;
  }
  for (i = 1; i < n; i++) {
    if (list[i] <= list[i - 1]) {
      return false;
    }
  }
  return true;
}

static ds_status_t ubr_insert_kid(ds_ubranch_tree_t *t, size_t **kids,
                                  size_t *count, size_t *cap, size_t pos,
                                  size_t node) {
  if (*count == *cap) {
    size_t nc = *cap < 2 ? 4 : *cap * 2;
    void *p = ds_context_realloc(t->context, *kids, nc * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    *kids = (size_t *)p;
    *cap = nc;
  }
  memmove(*kids + pos + 1, *kids + pos, (*count - pos) * sizeof(size_t));
  (*kids)[pos] = node;
  (*count)++;
  return DS_OK;
}

ds_status_t ds_ubranch_tree_insert(ds_ubranch_tree_t *t, const size_t *list,
                                   size_t n, size_t value) {
  size_t v = UB_NONE;
  size_t i;
  ds_status_t st;
  if (t == NULL || list == NULL) {
    return DS_ERR_NULL;
  }
  if (!ubr_list_ok(list, n)) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    bool found;
    size_t **kids = v == UB_NONE ? &t->roots : &t->child[v];
    size_t *count = v == UB_NONE ? &t->root_count : &t->child_count[v];
    size_t *cap = v == UB_NONE ? &t->root_capacity : &t->child_capacity[v];
    size_t pos = ubr_search(t, *kids, *count, list[i], &found);
    if (found) {
      v = (*kids)[pos];
      continue;
    }
    {
      size_t w;
      st = ubr_node(t, list[i], v, &w);
      if (st != DS_OK) {
        return st;
      }
      /* the arrays may have moved with the node pool */
      kids = v == UB_NONE ? &t->roots : &t->child[v];
      count = v == UB_NONE ? &t->root_count : &t->child_count[v];
      cap = v == UB_NONE ? &t->root_capacity : &t->child_capacity[v];
      st = ubr_insert_kid(t, kids, count, cap, pos, w);
      if (st != DS_OK) {
        t->parent[w] = t->free_list;
        t->free_list = w;
        return st;
      }
      v = w;
    }
  }
  if (t->terminal[v]) {
    t->value[v] = value;
    return DS_EXISTS;
  }
  t->terminal[v] = 1;
  t->value[v] = value;
  t->size++;
  return DS_OK;
}

static size_t ubr_walk(const ds_ubranch_tree_t *t, const size_t *list,
                       size_t n) {
  size_t v = UB_NONE;
  size_t i;
  for (i = 0; i < n; i++) {
    bool found;
    const size_t *kids = v == UB_NONE ? t->roots : t->child[v];
    size_t count = v == UB_NONE ? t->root_count : t->child_count[v];
    size_t pos = ubr_search(t, kids, count, list[i], &found);
    if (!found) {
      return UB_NONE;
    }
    v = kids[pos];
  }
  return v;
}

ds_status_t ds_ubranch_tree_find(const ds_ubranch_tree_t *t, const size_t *list,
                                 size_t n, size_t *out_value) {
  size_t v;
  if (t == NULL || list == NULL) {
    return DS_ERR_NULL;
  }
  if (!ubr_list_ok(list, n)) {
    return DS_NOT_FOUND;
  }
  v = ubr_walk(t, list, n);
  if (v == UB_NONE || !t->terminal[v]) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL) {
    *out_value = t->value[v];
  }
  return DS_OK;
}

ds_status_t ds_ubranch_tree_remove(ds_ubranch_tree_t *t, const size_t *list,
                                   size_t n) {
  size_t v;
  if (t == NULL || list == NULL) {
    return DS_ERR_NULL;
  }
  if (!ubr_list_ok(list, n)) {
    return DS_NOT_FOUND;
  }
  v = ubr_walk(t, list, n);
  if (v == UB_NONE || !t->terminal[v]) {
    return DS_NOT_FOUND;
  }
  t->terminal[v] = 0;
  t->size--;
  /* prune the now useless tail of the path */
  while (v != UB_NONE && !t->terminal[v] && t->child_count[v] == 0) {
    size_t p = t->parent[v];
    size_t **kids = p == UB_NONE ? &t->roots : &t->child[p];
    size_t *count = p == UB_NONE ? &t->root_count : &t->child_count[p];
    bool found;
    size_t pos = ubr_search(t, *kids, *count, t->elem[v], &found);
    memmove(*kids + pos, *kids + pos + 1, (*count - pos - 1) * sizeof(size_t));
    (*count)--;
    ds_context_free(t->context, t->child[v]);
    t->child[v] = NULL;
    t->child_capacity[v] = 0;
    t->parent[v] = t->free_list;
    t->free_list = v;
    v = p;
  }
  return DS_OK;
}

/* depth of node v (number of elements on its path) */
static size_t ubr_depth(const ds_ubranch_tree_t *t, size_t v) {
  size_t k = 0;
  for (; v != UB_NONE; v = t->parent[v]) {
    k++;
  }
  return k;
}

static ds_status_t ubr_report(ds_ubranch_tree_t *t, size_t v,
                              ds_ubranch_visit_func_t visit, void *user) {
  size_t k = ubr_depth(t, v);
  size_t u;
  size_t i = k;
  if (k > t->path_capacity) {
    void *p = ds_context_realloc(t->context, t->path, k * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    t->path = (size_t *)p;
    t->path_capacity = k;
  }
  for (u = v; u != UB_NONE; u = t->parent[u]) {
    t->path[--i] = t->elem[u];
  }
  return visit(t->path, k, t->value[v], user) ? DS_OK : DS_STOP;
}

/* explicit stack of (node, set position) pairs */
typedef struct ubr_frame {
  size_t node;
  size_t pos;
} ubr_frame_t;

ds_status_t ds_ubranch_tree_subsets(ds_ubranch_tree_t *t, const size_t *set,
                                    size_t n, ds_ubranch_visit_func_t visit,
                                    void *user) {
  ubr_frame_t *stack;
  size_t top = 0;
  size_t i;
  ds_status_t st = DS_OK;
  if (t == NULL || visit == NULL || (n > 0 && set == NULL)) {
    return DS_ERR_NULL;
  }
  if (n > 1 && !ubr_list_ok(set, n)) {
    return DS_ERR_INVALID;
  }
  stack = (ubr_frame_t *)ds_context_alloc(t->context,
                                          (t->nodes + 1) * sizeof(ubr_frame_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  /* roots whose element is in the set */
  for (i = 0; i < t->root_count; i++) {
    size_t r = t->roots[i];
    bool found;
    size_t lo = 0;
    size_t hi = n;
    while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2;
      if (set[mid] < t->elem[r]) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    found = lo < n && set[lo] == t->elem[r];
    if (found) {
      stack[top].node = r;
      stack[top++].pos = lo + 1;
    }
  }
  while (top > 0 && st == DS_OK) {
    ubr_frame_t f = stack[--top];
    size_t j = f.pos;
    if (t->terminal[f.node]) {
      st = ubr_report(t, f.node, visit, user);
    }
    /* children and the rest of the set are both increasing: merge */
    for (i = 0; i < t->child_count[f.node] && j < n && st == DS_OK; i++) {
      size_t c = t->child[f.node][i];
      while (j < n && set[j] < t->elem[c]) {
        j++;
      }
      if (j < n && set[j] == t->elem[c]) {
        stack[top].node = c;
        stack[top++].pos = j + 1;
      }
    }
  }
  ds_context_free(t->context, stack);
  return st;
}

ds_status_t ds_ubranch_tree_supersets(ds_ubranch_tree_t *t, const size_t *set,
                                      size_t n, ds_ubranch_visit_func_t visit,
                                      void *user) {
  ubr_frame_t *stack;
  size_t top = 0;
  size_t i;
  ds_status_t st = DS_OK;
  if (t == NULL || visit == NULL || (n > 0 && set == NULL)) {
    return DS_ERR_NULL;
  }
  if (n > 1 && !ubr_list_ok(set, n)) {
    return DS_ERR_INVALID;
  }
  stack = (ubr_frame_t *)ds_context_alloc(t->context,
                                          (t->nodes + 1) * sizeof(ubr_frame_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < t->root_count; i++) {
    size_t r = t->roots[i];
    if (n > 0 && t->elem[r] > set[0]) {
      break; /* paths only increase: set[0] can no longer appear */
    }
    stack[top].node = r;
    stack[top++].pos = n > 0 && t->elem[r] == set[0] ? 1 : 0;
  }
  while (top > 0 && st == DS_OK) {
    ubr_frame_t f = stack[--top];
    if (f.pos == n && t->terminal[f.node]) {
      st = ubr_report(t, f.node, visit, user);
    }
    for (i = 0; i < t->child_count[f.node] && st == DS_OK; i++) {
      size_t c = t->child[f.node][i];
      size_t j = f.pos;
      if (j < n && t->elem[c] > set[j]) {
        break;
      }
      if (j < n && t->elem[c] == set[j]) {
        j++;
      }
      stack[top].node = c;
      stack[top++].pos = j;
    }
  }
  ds_context_free(t->context, stack);
  return st;
}
