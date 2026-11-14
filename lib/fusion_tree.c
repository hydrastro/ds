#include "fusion_tree.h"
#include "checked.h"
#include <string.h>

/*
 * Words have FZ_W bits; a node's sketches use fields of FZ_K bits, the top
 * one the sentinel, so a sketch has at most FZ_K - 1 bits, one per
 * distinguishing position, and FZ_K fields fit a word.
 */
#define FZ_K DS_FUSION_K
#define FZ_MIN DS_FUSION_MIN
#define FZ_W (sizeof(unsigned long) * CHAR_BIT)

typedef struct fz_step {
  ds_fusion_node_t *node;
  size_t index; /* the child taken, or the key position at the end */
} fz_step_t;

/* ---- word operations ----------------------------------------------------- */

/* the n low bits */
static unsigned long fz_low(size_t n) {
  return n >= FZ_W ? ~0ul : (1ul << n) - 1ul;
}

/* a one at the bottom of each of c fields */
static unsigned long fz_ones(size_t c) {
  return fz_low(c * FZ_K) / fz_low(FZ_K);
}

/* the highest set bit of x != 0, by halving */
static size_t fz_msb(unsigned long x) {
  size_t r = 0;
  while ((x >> 16) >> 16 != 0) {
    x = (x >> 16) >> 16;
    r += 32;
  }
  if (x >> 16 != 0) {
    x >>= 16;
    r += 16;
  }
  if (x >> 8 != 0) {
    x >>= 8;
    r += 8;
  }
  if (x >> 4 != 0) {
    x >>= 4;
    r += 4;
  }
  if (x >> 2 != 0) {
    x >>= 2;
    r += 2;
  }
  if (x >> 1 != 0) {
    r += 1;
  }
  return r;
}

/* q's bits at the node's distinguishing positions, most significant first */
static unsigned long fz_sketch(const ds_fusion_node_t *x, unsigned long q) {
  unsigned long s = 0;
  size_t i;
  for (i = 0; i < x->bits; i++) {
    s = (s << 1) | ((q >> x->bit[i]) & 1ul);
  }
  return s;
}

/* how many of the node's sketches are smaller than s: s is copied into
 * every field and subtracted, a field keeps its sentinel when its sketch
 * is not smaller, and the sentinels are summed by one multiplication */
static size_t fz_below(const ds_fusion_node_t *x, unsigned long s) {
  size_t c = x->count;
  unsigned long ones = fz_ones(c);
  unsigned long kept = (x->packed - s * ones) & (ones << (FZ_K - 1));
  unsigned long sum = ((kept >> (FZ_K - 1)) * ones) >> ((c - 1) * FZ_K);
  return c - (size_t)(sum & fz_low(FZ_K));
}

size_t ds_fusion_node_rank(const ds_fusion_node_t *x, unsigned long q,
                           bool *out_equal) {
  size_t c = x->count;
  size_t j;
  size_t yi;
  size_t p;
  unsigned long y;
  unsigned long s;
  bool equal = false;
  size_t rank;
  if (c == 0) {
    rank = 0;
  } else {
    j = fz_below(x, fz_sketch(x, q));
    /* of the sketch neighbours, the key sharing the longer prefix with q */
    if (j == 0) {
      yi = 0;
    } else if (j == c) {
      yi = c - 1;
    } else {
      yi = (q ^ x->key[j - 1]) < (q ^ x->key[j]) ? j - 1 : j;
    }
    y = x->key[yi];
    if (q == y) {
      equal = true;
      rank = yi + 1;
    } else {
      p = fz_msb(q ^ y);
      if (q > y) { /* q passes every key with the shared prefix */
        s = fz_sketch(x, (q | fz_low(p)) & ~(1ul << p));
        rank = fz_below(x, s);
        if (rank < c && x->sketch[rank] == s) {
          rank++;
        }
      } else { /* q precedes every key with the shared prefix */
        s = fz_sketch(x, (q & ~fz_low(p)) | (1ul << p));
        rank = fz_below(x, s);
      }
    }
  }
  if (out_equal != NULL) {
    *out_equal = equal;
  }
  return rank;
}

/* distinguishing positions, sketches and the packed word of a node with
 * at most K keys */
static void fz_rebuild(ds_fusion_node_t *x) {
  size_t c = x->count;
  size_t n = 0;
  size_t i;
  size_t k;
  for (i = 0; i + 1 < c; i++) {
    size_t b = fz_msb(x->key[i] ^ x->key[i + 1]);
    for (k = 0; k < n && x->bit[k] > b; k++) {
    }
    if (k < n && x->bit[k] == b) {
      continue;
    }
    memmove(&x->bit[k + 1], &x->bit[k], n - k);
    x->bit[k] = (unsigned char)b;
    n++;
  }
  x->bits = (unsigned char)n;
  x->packed = 0;
  for (i = 0; i < c; i++) {
    unsigned long s = fz_sketch(x, x->key[i]);
    x->sketch[i] = (unsigned char)s;
    x->packed |= (s | (1ul << (FZ_K - 1))) << (i * FZ_K);
  }
}

/* ---- nodes --------------------------------------------------------------- */

static unsigned char *fz_value(const ds_fusion_tree_t *t, ds_fusion_node_t *x,
                               size_t i) {
  return (unsigned char *)(void *)x + t->value_at + i * t->value_size;
}

/* what callers see of entry i: its value, or for a set its stored key */
static void *fz_slot(const ds_fusion_tree_t *t, ds_fusion_node_t *x, size_t i) {
  return t->value_size == 0 ? (void *)&x->key[i] : (void *)fz_value(t, x, i);
}

static ds_fusion_node_t *fz_new(ds_fusion_tree_t *t) {
  ds_fusion_node_t *x = (ds_fusion_node_t *)ds_context_alloc(
      t->context, t->value_at + (FZ_K + 1) * t->value_size);
  size_t i;
  if (x == NULL) {
    return NULL;
  }
  for (i = 0; i < FZ_K + 2; i++) {
    x->child[i] = NULL;
  }
  x->count = 0;
  x->bits = 0;
  x->size = 0;
  x->packed = 0;
  return x;
}

static bool fz_leaf(const ds_fusion_node_t *x) { return x->child[0] == NULL; }

/* move keys (with values, and the children to their right when moving
 * within one node is not wanted) */
static void fz_copy_keys(const ds_fusion_tree_t *t, ds_fusion_node_t *dst,
                         size_t at, ds_fusion_node_t *src, size_t from,
                         size_t n) {
  memmove(&dst->key[at], &src->key[from], n * sizeof(unsigned long));
  if (t->value_size != 0) {
    memmove(fz_value(t, dst, at), fz_value(t, src, from), n * t->value_size);
  }
}

static void fz_set(const ds_fusion_tree_t *t, ds_fusion_node_t *x, size_t i,
                   unsigned long key, const void *value) {
  x->key[i] = key;
  if (t->value_size != 0) {
    if (value != NULL) {
      memcpy(fz_value(t, x, i), value, t->value_size);
    } else {
      memset(fz_value(t, x, i), 0, t->value_size);
    }
  }
}

static void fz_out(const ds_fusion_tree_t *t, ds_fusion_node_t *x, size_t i,
                   unsigned long *out_key, void *out_value) {
  if (out_key != NULL) {
    *out_key = x->key[i];
  }
  if (out_value != NULL && t->value_size != 0) {
    memcpy(out_value, fz_value(t, x, i), t->value_size);
  }
}

static size_t fz_child_size(const ds_fusion_node_t *x, size_t i) {
  return x->child[i] == NULL ? 0 : x->child[i]->size;
}

static void fz_resize(ds_fusion_node_t *x) {
  size_t s = x->count;
  size_t i;
  if (!fz_leaf(x)) {
    for (i = 0; i <= x->count; i++) {
      s += x->child[i]->size;
    }
  }
  x->size = s;
}

/* ---- lifetime ------------------------------------------------------------ */

ds_status_t ds_fusion_init(ds_fusion_tree_t *t, size_t value_size,
                           ds_context_t *context) {
  size_t at = (sizeof(ds_fusion_node_t) + DS_MAX_ALIGN - 1) / DS_MAX_ALIGN *
              DS_MAX_ALIGN;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  t->root = NULL;
  t->value_size = 0;
  t->value_at = at;
  t->context = context;
  if (value_size > ((size_t)-1 - at) / (FZ_K + 1)) {
    return DS_ERR_INVALID;
  }
  t->value_size = value_size;
  return DS_OK;
}

static void fz_free_all(ds_fusion_tree_t *t, ds_fusion_node_t *root) {
  fz_step_t stack[DS_FUSION_MAX_HEIGHT];
  size_t depth = 0;
  if (root == NULL) {
    return;
  }
  stack[0].node = root;
  stack[0].index = 0;
  depth = 1;
  while (depth > 0) {
    fz_step_t *s = &stack[depth - 1];
    if (!fz_leaf(s->node) && s->index <= s->node->count &&
        depth < DS_FUSION_MAX_HEIGHT) {
      ds_fusion_node_t *c = s->node->child[s->index++];
      stack[depth].node = c;
      stack[depth].index = 0;
      depth++;
    } else {
      ds_context_free(t->context, s->node);
      depth--;
    }
  }
}

void ds_fusion_clear(ds_fusion_tree_t *t) {
  if (t == NULL) {
    return;
  }
  fz_free_all(t, t->root);
  t->root = NULL;
}

void ds_fusion_destroy(ds_fusion_tree_t *t) { ds_fusion_clear(t); }

/* ---- bulk loading -------------------------------------------------------- */

/* the most keys a tree of height h holds, saturating */
static size_t fz_cap(size_t h) {
  size_t c = 0;
  size_t i;
  for (i = 0; i < h; i++) {
    if (c > ((size_t)-1 - FZ_K) / (FZ_K + 1)) {
      return (size_t)-1;
    }
    c = c * (FZ_K + 1) + FZ_K;
  }
  return c;
}

/* a subtree of height h over keys[lo, lo + n); NULL when out of memory */
static ds_fusion_node_t *fz_build(ds_fusion_tree_t *t,
                                  const unsigned long *keys,
                                  const unsigned char *values, size_t lo,
                                  size_t n, size_t h, bool root) {
  ds_fusion_node_t *x = fz_new(t);
  size_t c;
  size_t base;
  size_t extra;
  size_t i;
  size_t at = lo;
  size_t below;
  if (x == NULL) {
    return NULL;
  }
  if (h == 1) {
    for (i = 0; i < n; i++) {
      fz_set(t, x, i, keys[lo + i],
             values != NULL ? values + (lo + i) * t->value_size : NULL);
    }
    x->count = (unsigned char)n;
    x->size = n;
    fz_rebuild(x);
    return x;
  }
  below = fz_cap(h - 1);
  c = below == (size_t)-1 ? 2 : n / (below + 1) + 1; /* ceil((n+1)/(below+1)) */
  if (c < (root ? 2u : FZ_MIN + 1u)) {
    c = root ? 2 : FZ_MIN + 1;
  }
  base = (n - (c - 1)) / c;
  extra = (n - (c - 1)) % c;
  for (i = 0; i < c; i++) {
    size_t m = base + (i < extra ? 1 : 0);
    x->child[i] = fz_build(t, keys, values, at, m, h - 1, false);
    if (x->child[i] == NULL) { /* free the children built so far */
      if (i > 0) {
        x->count = (unsigned char)(i - 1);
        fz_free_all(t, x);
      } else {
        ds_context_free(t->context, x);
      }
      return NULL;
    }
    at += m;
    if (i + 1 < c) {
      fz_set(t, x, i, keys[at],
             values != NULL ? values + at * t->value_size : NULL);
      at++;
    }
  }
  x->count = (unsigned char)(c - 1);
  fz_resize(x);
  fz_rebuild(x);
  return x;
}

ds_status_t ds_fusion_build(ds_fusion_tree_t *t, const unsigned long *keys,
                            const void *values, size_t n) {
  ds_fusion_node_t *root = NULL;
  size_t h = 1;
  size_t i;
  if (t == NULL || (keys == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  for (i = 1; i < n; i++) {
    if (keys[i - 1] >= keys[i]) {
      return DS_ERR_INVALID;
    }
  }
  if (n > 0) {
    while (fz_cap(h) < n) {
      h++;
    }
    root = fz_build(t, keys, (const unsigned char *)values, 0, n, h, true);
    if (root == NULL) {
      return DS_ERR_ALLOC;
    }
  }
  ds_fusion_clear(t);
  t->root = root;
  return DS_OK;
}

/* ---- updates ------------------------------------------------------------- */

/* x holds K + 1 keys: the upper half moves to y; the median stays in x
 * just past its new count */
static void fz_split(ds_fusion_tree_t *t, ds_fusion_node_t *x,
                     ds_fusion_node_t *y) {
  size_t h = (FZ_K + 1) / 2;
  size_t i;
  size_t moved = FZ_K - h;
  fz_copy_keys(t, y, 0, x, h + 1, moved);
  if (!fz_leaf(x)) {
    for (i = 0; i <= moved; i++) {
      y->child[i] = x->child[h + 1 + i];
      x->child[h + 1 + i] = NULL;
    }
  }
  y->count = (unsigned char)moved;
  x->count = (unsigned char)h;
  fz_resize(x);
  fz_resize(y);
}

static ds_status_t fz_insert(ds_fusion_tree_t *t, unsigned long key,
                             const void *value, bool overwrite) {
  fz_step_t path[DS_FUSION_MAX_HEIGHT];
  ds_fusion_node_t *spare[DS_FUSION_MAX_HEIGHT + 1];
  size_t depth = 0;
  size_t need = 0;
  size_t used = 0;
  size_t i;
  size_t level;
  ds_fusion_node_t *x = t->root;
  bool equal;
  if (x == NULL) {
    x = fz_new(t);
    if (x == NULL) {
      return DS_ERR_ALLOC;
    }
    fz_set(t, x, 0, key, value);
    x->count = 1;
    x->size = 1;
    fz_rebuild(x);
    t->root = x;
    return DS_OK;
  }
  for (;;) {
    size_t r = ds_fusion_node_rank(x, key, &equal);
    if (equal) {
      if (!overwrite) {
        return DS_EXISTS;
      }
      fz_set(t, x, r - 1, key, value);
      return DS_OK;
    }
    if (depth == DS_FUSION_MAX_HEIGHT) {
      return DS_ERR_RANGE; /* beyond any height n allows */
    }
    path[depth].node = x;
    path[depth].index = r;
    depth++;
    if (fz_leaf(x)) {
      break;
    }
    x = x->child[r];
  }
  /* every full node from the leaf up splits, and a full root adds a level:
   * the nodes are taken first so a failure changes nothing */
  for (level = depth; level > 0 && path[level - 1].node->count == FZ_K;
       level--) {
    need++;
  }
  if (level == 0) {
    need++;
  }
  for (i = 0; i < need; i++) {
    spare[i] = fz_new(t);
    if (spare[i] == NULL) {
      while (i > 0) {
        ds_context_free(t->context, spare[--i]);
      }
      return DS_ERR_ALLOC;
    }
  }
  for (i = 0; i < depth; i++) {
    path[i].node->size++;
  }
  /* into the leaf */
  level = depth - 1;
  x = path[level].node;
  i = path[level].index;
  fz_copy_keys(t, x, i + 1, x, i, x->count - i);
  fz_set(t, x, i, key, value);
  x->count++;
  while (x->count > FZ_K) {
    ds_fusion_node_t *y = spare[used++];
    size_t h = (FZ_K + 1) / 2;
    ds_fusion_node_t *p;
    size_t pi;
    fz_split(t, x, y);
    /* the median is x->key[h] (with its value), still in x past count */
    fz_rebuild(x);
    fz_rebuild(y);
    if (level == 0) {
      p = spare[used++];
      p->child[0] = x;
      pi = 0;
      p->count = 0;
      t->root = p;
    } else {
      level--;
      p = path[level].node;
      pi = path[level].index;
      fz_copy_keys(t, p, pi + 1, p, pi, p->count - pi);
      memmove(&p->child[pi + 2], &p->child[pi + 1],
              (p->count - pi) * sizeof(ds_fusion_node_t *));
    }
    fz_copy_keys(t, p, pi, x, h, 1);
    p->child[pi + 1] = y;
    p->count++;
    if (p->count == 1) {
      fz_resize(p); /* a new root */
    }
    x = p;
  }
  fz_rebuild(x);
  return DS_OK;
}

ds_status_t ds_fusion_insert(ds_fusion_tree_t *t, unsigned long key,
                             const void *value) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  return fz_insert(t, key, value, false);
}

ds_status_t ds_fusion_put(ds_fusion_tree_t *t, unsigned long key,
                          const void *value) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  return fz_insert(t, key, value, true);
}

/* x (child ci of p) holds MIN - 1 keys: borrow from a sibling or merge
 * with one; returns true when p lost a key */
static bool fz_fix(ds_fusion_tree_t *t, ds_fusion_node_t *p, size_t ci) {
  ds_fusion_node_t *x = p->child[ci];
  ds_fusion_node_t *l = ci > 0 ? p->child[ci - 1] : NULL;
  ds_fusion_node_t *r = ci < p->count ? p->child[ci + 1] : NULL;
  bool leaf = fz_leaf(x);
  size_t moved;
  if (l != NULL && l->count > FZ_MIN) { /* rotate right */
    fz_copy_keys(t, x, 1, x, 0, x->count);
    fz_copy_keys(t, x, 0, p, ci - 1, 1);
    if (!leaf) {
      memmove(&x->child[1], &x->child[0],
              (x->count + 1) * sizeof(ds_fusion_node_t *));
      x->child[0] = l->child[l->count];
      l->child[l->count] = NULL;
    }
    x->count++;
    fz_copy_keys(t, p, ci - 1, l, l->count - 1, 1);
    l->count--;
    moved = 1 + fz_child_size(x, 0) * (leaf ? 0u : 1u);
    x->size += moved;
    l->size -= moved;
    fz_rebuild(l);
    fz_rebuild(x);
    fz_rebuild(p);
    return false;
  }
  if (r != NULL && r->count > FZ_MIN) { /* rotate left */
    fz_copy_keys(t, x, x->count, p, ci, 1);
    if (!leaf) {
      x->child[x->count + 1] = r->child[0];
      memmove(&r->child[0], &r->child[1],
              r->count * sizeof(ds_fusion_node_t *));
      r->child[r->count] = NULL;
    }
    x->count++;
    fz_copy_keys(t, p, ci, r, 0, 1);
    fz_copy_keys(t, r, 0, r, 1, r->count - 1u);
    r->count--;
    moved = 1 + (leaf ? 0u : x->child[x->count]->size);
    x->size += moved;
    r->size -= moved;
    fz_rebuild(r);
    fz_rebuild(x);
    fz_rebuild(p);
    return false;
  }
  if (l != NULL) { /* merge into the left sibling */
    r = x;
    x = l;
    ci--;
  } else {
    r = p->child[ci + 1];
  }
  /* x, the separator p->key[ci] and r become x */
  fz_copy_keys(t, x, x->count, p, ci, 1);
  fz_copy_keys(t, x, x->count + 1u, r, 0, r->count);
  if (!leaf) {
    size_t i;
    for (i = 0; i <= r->count; i++) {
      x->child[x->count + 1 + i] = r->child[i];
    }
  }
  x->count = (unsigned char)(x->count + 1 + r->count);
  x->size += 1 + r->size;
  fz_copy_keys(t, p, ci, p, ci + 1, p->count - ci - 1u);
  memmove(&p->child[ci + 1], &p->child[ci + 2],
          (p->count - ci - 1u) * sizeof(ds_fusion_node_t *));
  p->child[p->count] = NULL;
  p->count--;
  ds_context_free(t->context, r);
  fz_rebuild(x);
  fz_rebuild(p);
  return true;
}

ds_status_t ds_fusion_remove(ds_fusion_tree_t *t, unsigned long key,
                             void *out_value) {
  fz_step_t path[DS_FUSION_MAX_HEIGHT];
  size_t depth = 0;
  ds_fusion_node_t *x;
  ds_fusion_node_t *hit = NULL;
  size_t hit_at = 0;
  size_t level;
  size_t i;
  bool equal;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  x = t->root;
  while (x != NULL && depth < DS_FUSION_MAX_HEIGHT) {
    size_t r = ds_fusion_node_rank(x, key, &equal);
    if (equal) {
      hit = x;
      hit_at = r - 1;
      path[depth].node = x;
      path[depth].index = r - 1; /* the left subtree of the key */
      depth++;
      break;
    }
    path[depth].node = x;
    path[depth].index = r;
    depth++;
    x = fz_leaf(x) ? NULL : x->child[r];
  }
  if (hit == NULL) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && t->value_size != 0) {
    memcpy(out_value, fz_value(t, hit, hit_at), t->value_size);
  }
  if (!fz_leaf(hit)) { /* the predecessor takes the key's place */
    x = hit->child[hit_at];
    while (depth < DS_FUSION_MAX_HEIGHT) {
      path[depth].node = x;
      path[depth].index = x->count;
      depth++;
      if (fz_leaf(x)) {
        break;
      }
      x = x->child[x->count];
    }
    path[depth - 1].index = x->count - 1u;
    fz_copy_keys(t, hit, hit_at, x, x->count - 1u, 1);
    fz_rebuild(hit);
  }
  for (i = 0; i < depth; i++) {
    path[i].node->size--;
  }
  /* out of the leaf */
  level = depth - 1;
  x = path[level].node;
  i = path[level].index;
  fz_copy_keys(t, x, i, x, i + 1, x->count - i - 1u);
  x->count--;
  fz_rebuild(x);
  while (level > 0 && x->count < FZ_MIN) {
    ds_fusion_node_t *p = path[level - 1].node;
    if (!fz_fix(t, p, path[level - 1].index)) {
      break;
    }
    level--;
    x = p;
  }
  x = t->root;
  if (x->count == 0) {
    t->root = fz_leaf(x) ? NULL : x->child[0];
    ds_context_free(t->context, x);
  }
  return DS_OK;
}

/* ---- queries ------------------------------------------------------------- */

/* the node holding key and its position, or NULL */
static ds_fusion_node_t *fz_find(const ds_fusion_tree_t *t, unsigned long key,
                                 size_t *out_at) {
  ds_fusion_node_t *x = t == NULL ? NULL : t->root;
  while (x != NULL) {
    bool equal;
    size_t r = ds_fusion_node_rank(x, key, &equal);
    if (equal) {
      *out_at = r - 1;
      return x;
    }
    x = fz_leaf(x) ? NULL : x->child[r];
  }
  return NULL;
}

bool ds_fusion_contains(const ds_fusion_tree_t *t, unsigned long key) {
  size_t at;
  return fz_find(t, key, &at) != NULL;
}

void *ds_fusion_get(const ds_fusion_tree_t *t, unsigned long key) {
  size_t at = 0;
  ds_fusion_node_t *x = fz_find(t, key, &at);
  return x == NULL ? NULL : fz_slot(t, x, at);
}

/* the largest key <= q (below: < q) */
static ds_status_t fz_down(const ds_fusion_tree_t *t, unsigned long q,
                           bool below, unsigned long *out_key,
                           void *out_value) {
  ds_fusion_node_t *x;
  ds_fusion_node_t *best = NULL;
  size_t best_at = 0;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  x = t->root;
  while (x != NULL) {
    bool equal;
    size_t r = ds_fusion_node_rank(x, q, &equal);
    if (equal && !below) {
      fz_out(t, x, r - 1, out_key, out_value);
      return DS_OK;
    }
    if (equal) {
      r--; /* the keys < q, and the subtree left of q */
    }
    if (r > 0) {
      best = x;
      best_at = r - 1;
    }
    x = fz_leaf(x) ? NULL : x->child[r];
  }
  if (best == NULL) {
    return DS_NOT_FOUND;
  }
  fz_out(t, best, best_at, out_key, out_value);
  return DS_OK;
}

/* the smallest key >= q (above: > q) */
static ds_status_t fz_up(const ds_fusion_tree_t *t, unsigned long q, bool above,
                         unsigned long *out_key, void *out_value) {
  ds_fusion_node_t *x;
  ds_fusion_node_t *best = NULL;
  size_t best_at = 0;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  x = t->root;
  while (x != NULL) {
    bool equal;
    size_t r = ds_fusion_node_rank(x, q, &equal);
    if (equal && !above) {
      fz_out(t, x, r - 1, out_key, out_value);
      return DS_OK;
    }
    if (r < x->count) {
      best = x;
      best_at = r;
    }
    x = fz_leaf(x) ? NULL : x->child[r];
  }
  if (best == NULL) {
    return DS_NOT_FOUND;
  }
  fz_out(t, best, best_at, out_key, out_value);
  return DS_OK;
}

ds_status_t ds_fusion_predecessor(const ds_fusion_tree_t *t, unsigned long q,
                                  unsigned long *out_key, void *out_value) {
  return fz_down(t, q, true, out_key, out_value);
}

ds_status_t ds_fusion_successor(const ds_fusion_tree_t *t, unsigned long q,
                                unsigned long *out_key, void *out_value) {
  return fz_up(t, q, true, out_key, out_value);
}

ds_status_t ds_fusion_floor(const ds_fusion_tree_t *t, unsigned long q,
                            unsigned long *out_key, void *out_value) {
  return fz_down(t, q, false, out_key, out_value);
}

ds_status_t ds_fusion_ceiling(const ds_fusion_tree_t *t, unsigned long q,
                              unsigned long *out_key, void *out_value) {
  return fz_up(t, q, false, out_key, out_value);
}

ds_status_t ds_fusion_min(const ds_fusion_tree_t *t, unsigned long *out_key,
                          void *out_value) {
  ds_fusion_node_t *x;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  x = t->root;
  if (x == NULL) {
    return DS_EMPTY;
  }
  while (!fz_leaf(x)) {
    x = x->child[0];
  }
  fz_out(t, x, 0, out_key, out_value);
  return DS_OK;
}

ds_status_t ds_fusion_max(const ds_fusion_tree_t *t, unsigned long *out_key,
                          void *out_value) {
  ds_fusion_node_t *x;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  x = t->root;
  if (x == NULL) {
    return DS_EMPTY;
  }
  while (!fz_leaf(x)) {
    x = x->child[x->count];
  }
  fz_out(t, x, x->count - 1u, out_key, out_value);
  return DS_OK;
}

size_t ds_fusion_rank(const ds_fusion_tree_t *t, unsigned long q) {
  ds_fusion_node_t *x = t == NULL ? NULL : t->root;
  size_t total = 0;
  while (x != NULL) {
    bool equal;
    size_t r = ds_fusion_node_rank(x, q, &equal);
    size_t i;
    /* the keys of x below q, and the subtrees left of them (with the one
     * left of q itself when q is a key); otherwise go on in child r */
    size_t whole = r;
    total += equal ? r - 1 : r;
    if (fz_leaf(x)) {
      break;
    }
    for (i = 0; i < whole; i++) {
      total += x->child[i]->size;
    }
    if (equal) {
      break;
    }
    x = x->child[r];
  }
  return total;
}

ds_status_t ds_fusion_select(const ds_fusion_tree_t *t, size_t i,
                             unsigned long *out_key, void *out_value) {
  ds_fusion_node_t *x;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  x = t->root;
  if (x == NULL || i >= x->size) {
    return DS_ERR_RANGE;
  }
  for (;;) {
    size_t j;
    if (fz_leaf(x)) {
      fz_out(t, x, i, out_key, out_value);
      return DS_OK;
    }
    for (j = 0; j <= x->count; j++) {
      size_t s = x->child[j]->size;
      if (i < s) {
        break;
      }
      i -= s;
      if (j < x->count) {
        if (i == 0) {
          fz_out(t, x, j, out_key, out_value);
          return DS_OK;
        }
        i--;
      }
    }
    x = x->child[j];
  }
}

size_t ds_fusion_size(const ds_fusion_tree_t *t) {
  return t == NULL || t->root == NULL ? 0 : t->root->size;
}

size_t ds_fusion_height(const ds_fusion_tree_t *t) {
  size_t h = 0;
  ds_fusion_node_t *x = t == NULL ? NULL : t->root;
  while (x != NULL) {
    h++;
    x = x->child[0];
  }
  return h;
}

int ds_fusion_foreach(const ds_fusion_tree_t *t, ds_fusion_visit_fn visit,
                      void *user) {
  fz_step_t stack[DS_FUSION_MAX_HEIGHT];
  size_t depth = 0;
  if (t == NULL || visit == NULL || t->root == NULL) {
    return 0;
  }
  stack[0].node = t->root;
  stack[0].index = 0;
  depth = 1;
  while (depth > 0) {
    fz_step_t *s = &stack[depth - 1];
    ds_fusion_node_t *x = s->node;
    if (fz_leaf(x)) {
      size_t i;
      for (i = 0; i < x->count; i++) {
        int rc = visit(x->key[i], fz_slot(t, x, i), user);
        if (rc != 0) {
          return rc;
        }
      }
      depth--;
      continue;
    }
    /* index 2i: descend into child i; 2i + 1: visit key i */
    if (s->index > 2u * x->count) {
      depth--;
      continue;
    }
    if (s->index % 2 == 0) {
      size_t c = s->index / 2;
      s->index++;
      if (depth < DS_FUSION_MAX_HEIGHT) {
        stack[depth].node = x->child[c];
        stack[depth].index = 0;
        depth++;
      }
    } else {
      size_t k = s->index / 2;
      int rc;
      s->index++;
      rc = visit(x->key[k], fz_slot(t, x, k), user);
      if (rc != 0) {
        return rc;
      }
    }
  }
  return 0;
}

/* ---- checking ------------------------------------------------------------ */

/* a node's sketch data against a fresh computation, and its search against
 * a plain one at and next to every key */
static bool fz_check_node(const ds_fusion_node_t *x) {
  ds_fusion_node_t copy = *x;
  size_t i;
  size_t j;
  if (x->count > FZ_K) {
    return false;
  }
  for (i = 0; i + 1 < x->count; i++) {
    if (x->key[i] >= x->key[i + 1]) {
      return false;
    }
  }
  fz_rebuild(&copy);
  if (copy.bits != x->bits || copy.packed != x->packed ||
      memcmp(copy.bit, x->bit, x->bits) != 0 ||
      memcmp(copy.sketch, x->sketch, x->count) != 0) {
    return false;
  }
  for (i = 0; i < x->count; i++) {
    unsigned long probe[3];
    probe[0] = x->key[i];
    probe[1] = x->key[i] - 1ul;
    probe[2] = x->key[i] + 1ul;
    for (j = 0; j < 3; j++) {
      size_t plain = 0;
      size_t k;
      bool equal;
      bool plain_equal = false;
      for (k = 0; k < x->count; k++) {
        if (x->key[k] <= probe[j]) {
          plain++;
          plain_equal = plain_equal || x->key[k] == probe[j];
        }
      }
      if (ds_fusion_node_rank(x, probe[j], &equal) != plain ||
          equal != plain_equal) {
        return false;
      }
    }
  }
  return true;
}

ds_status_t ds_fusion_check(const ds_fusion_tree_t *t, bool *out_valid) {
  fz_step_t stack[DS_FUSION_MAX_HEIGHT];
  size_t depth;
  size_t leaf_depth = 0;
  bool have_last = false;
  unsigned long last = 0;
  bool ok = true;
  if (t == NULL || out_valid == NULL) {
    return DS_ERR_NULL;
  }
  *out_valid = false;
  if (t->root == NULL) {
    *out_valid = true;
    return DS_OK;
  }
  if (t->root->count == 0) {
    return DS_OK;
  }
  stack[0].node = t->root;
  stack[0].index = 0;
  depth = 1;
  while (depth > 0 && ok) {
    fz_step_t *s = &stack[depth - 1];
    ds_fusion_node_t *x = s->node;
    size_t i;
    if (s->index == 0) { /* first visit */
      if (!fz_check_node(x) || (depth > 1 && x->count < FZ_MIN) ||
          x->count == 0) {
        ok = false;
        break;
      }
      if (fz_leaf(x)) {
        for (i = 1; i <= FZ_K + 1; i++) {
          if (x->child[i] != NULL) {
            ok = false;
          }
        }
        if (leaf_depth == 0) {
          leaf_depth = depth;
        } else if (leaf_depth != depth) {
          ok = false;
        }
        for (i = 0; i < x->count && ok; i++) {
          if (have_last && last >= x->key[i]) {
            ok = false;
          }
          last = x->key[i];
          have_last = true;
        }
        if (x->size != x->count) {
          ok = false;
        }
        depth--;
        continue;
      }
      for (i = 0; i <= x->count; i++) {
        if (x->child[i] == NULL) {
          ok = false;
        }
      }
      for (i = x->count + 1u; i < FZ_K + 2; i++) {
        if (x->child[i] != NULL) {
          ok = false;
        }
      }
      if (!ok) {
        break;
      }
    }
    if (s->index > 2u * x->count) { /* done: children were checked */
      size_t sum = x->count;
      for (i = 0; i <= x->count; i++) {
        sum += x->child[i]->size;
      }
      if (sum != x->size) {
        ok = false;
      }
      depth--;
      continue;
    }
    if (s->index % 2 == 0) {
      if (depth == DS_FUSION_MAX_HEIGHT) {
        ok = false;
        break;
      }
      stack[depth].node = x->child[s->index / 2];
      stack[depth].index = 0;
      s->index++;
      depth++;
    } else {
      unsigned long k = x->key[s->index / 2];
      if (have_last && last >= k) {
        ok = false;
      }
      last = k;
      have_last = true;
      s->index++;
    }
  }
  *out_valid = ok;
  return DS_OK;
}
