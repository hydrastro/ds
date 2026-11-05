#include "scapegoat_tree.h"
#include <string.h>

#define SG_NONE DS_SCAPEGOAT_NONE

static unsigned char *sg_at(const ds_scapegoat_tree_t *t, size_t v) {
  return t->data + v * t->elem_size;
}

ds_status_t ds_scapegoat_tree_init(ds_scapegoat_tree_t *t, size_t elem_size,
                                   ds_scapegoat_compare_func_t compare,
                                   void *user, size_t alpha_num,
                                   size_t alpha_den, ds_context_t *context) {
  if (t == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (alpha_num == 0 && alpha_den == 0) {
    alpha_num = 2;
    alpha_den = 3;
  }
  if (elem_size == 0 || alpha_den == 0 || alpha_den > 1024 ||
      alpha_num >= alpha_den || 2 * alpha_num <= alpha_den) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->root = SG_NONE;
  t->free_list = SG_NONE;
  t->elem_size = elem_size;
  t->alpha_num = alpha_num;
  t->alpha_den = alpha_den;
  t->compare = compare;
  t->user = user;
  t->context = context;
  return DS_OK;
}

void ds_scapegoat_tree_destroy(ds_scapegoat_tree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->data);
  ds_context_free(t->context, t->left);
  ds_context_free(t->context, t->right);
  ds_context_free(t->context, t->path);
  ds_context_free(t->context, t->order);
  t->data = NULL;
  t->left = t->right = t->path = t->order = NULL;
  t->size = t->max_size = t->slots = t->capacity = 0;
  t->root = t->free_list = SG_NONE;
}

size_t ds_scapegoat_tree_size(const ds_scapegoat_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

static ds_status_t sg_grow(ds_scapegoat_tree_t *t) {
  size_t cap = t->capacity < 8 ? 8 : t->capacity * 2;
  void *p;
  if (cap <= t->capacity || cap > ((size_t)-1) / t->elem_size ||
      cap > ((size_t)-1) / sizeof(size_t) - 1) {
    return DS_ERR_OVERFLOW;
  }
  p = ds_context_realloc(t->context, t->data, cap * t->elem_size);
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->data = (unsigned char *)p;
  p = ds_context_realloc(t->context, t->left, cap * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->left = (size_t *)p;
  p = ds_context_realloc(t->context, t->right, cap * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->right = (size_t *)p;
  p = ds_context_realloc(t->context, t->path, (cap + 1) * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->path = (size_t *)p;
  p = ds_context_realloc(t->context, t->order, cap * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->order = (size_t *)p;
  t->capacity = cap;
  return DS_OK;
}

/* (1/alpha)^depth > n, i.e. depth > log_{1/alpha} n. */
static bool sg_too_deep(const ds_scapegoat_tree_t *t, size_t depth, size_t n) {
  double x = 1.0;
  double lim = (double)n;
  double f = (double)t->alpha_den / (double)t->alpha_num;
  size_t i;
  for (i = 0; i < depth; i++) {
    x *= f;
    if (x > lim) {
      return true;
    }
  }
  return false;
}

/* Subtree size with the path scratch as a stack, starting at index
 * `base` (entries below base are kept). */
static size_t sg_count(ds_scapegoat_tree_t *t, size_t v, size_t base) {
  size_t top = base;
  size_t n = 0;
  if (v == SG_NONE) {
    return 0;
  }
  t->path[top++] = v;
  while (top > base) {
    v = t->path[--top];
    n++;
    if (t->left[v] != SG_NONE) {
      t->path[top++] = t->left[v];
    }
    if (t->right[v] != SG_NONE) {
      t->path[top++] = t->right[v];
    }
  }
  return n;
}

/* In-order node list of subtree v into t->order; returns its length. */
static size_t sg_flatten(ds_scapegoat_tree_t *t, size_t v) {
  size_t top = 0;
  size_t n = 0;
  while (v != SG_NONE || top > 0) {
    while (v != SG_NONE) {
      t->path[top++] = v;
      v = t->left[v];
    }
    v = t->path[--top];
    t->order[n++] = v;
    v = t->right[v];
  }
  return n;
}

static size_t sg_build(ds_scapegoat_tree_t *t, size_t lo, size_t hi) {
  size_t mid;
  size_t v;
  if (lo >= hi) {
    return SG_NONE;
  }
  mid = lo + (hi - lo) / 2;
  v = t->order[mid];
  t->left[v] = sg_build(t, lo, mid);
  t->right[v] = sg_build(t, mid + 1, hi);
  return v;
}

static size_t sg_rebuild(ds_scapegoat_tree_t *t, size_t v) {
  size_t n = sg_flatten(t, v);
  t->rebuilds++;
  return sg_build(t, 0, n);
}

ds_status_t ds_scapegoat_tree_insert(ds_scapegoat_tree_t *t, const void *elem) {
  size_t v;
  size_t depth = 0;
  size_t n;
  size_t i;
  size_t child_size;
  size_t size;
  size_t child;
  size_t p;
  size_t g;
  size_t sib;
  size_t nb;
  int c = 0;
  ds_status_t st;
  if (t == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (t->size == t->capacity) {
    st = sg_grow(t);
    if (st != DS_OK) {
      return st;
    }
  }
  v = t->root;
  while (v != SG_NONE) {
    c = t->compare(elem, sg_at(t, v), t->user);
    if (c == 0) {
      return DS_EXISTS;
    }
    t->path[depth++] = v;
    v = c < 0 ? t->left[v] : t->right[v];
  }
  if (t->free_list != SG_NONE) {
    n = t->free_list;
    t->free_list = t->left[n];
  } else {
    if (t->slots == t->capacity) {
      st = sg_grow(t);
      if (st != DS_OK) {
        return st;
      }
    }
    n = t->slots++;
  }
  memcpy(sg_at(t, n), elem, t->elem_size);
  t->left[n] = t->right[n] = SG_NONE;
  if (depth == 0) {
    t->root = n;
  } else if (c < 0) {
    t->left[t->path[depth - 1]] = n;
  } else {
    t->right[t->path[depth - 1]] = n;
  }
  t->size++;
  if (t->size > t->max_size) {
    t->max_size = t->size;
  }
  if (!sg_too_deep(t, depth, t->size)) {
    return DS_OK;
  }
  /* find the scapegoat: the first ancestor out of alpha-balance */
  child = n;
  child_size = 1;
  for (i = depth; i-- > 0;) {
    p = t->path[i];
    sib = t->left[p] == child ? t->right[p] : t->left[p];
    size = child_size + 1 + sg_count(t, sib, depth);
    if (child_size * t->alpha_den > t->alpha_num * size) {
      g = i > 0 ? t->path[i - 1] : SG_NONE;
      nb = sg_rebuild(t, p);
      if (g == SG_NONE) {
        t->root = nb;
      } else if (t->left[g] == p) {
        t->left[g] = nb;
      } else {
        t->right[g] = nb;
      }
      return DS_OK;
    }
    child = p;
    child_size = size;
  }
  return DS_OK;
}

ds_status_t ds_scapegoat_tree_remove(ds_scapegoat_tree_t *t, const void *key,
                                     void *out) {
  size_t v;
  size_t p = SG_NONE;
  size_t s;
  size_t sp;
  size_t r;
  int c;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  v = t->root;
  while (v != SG_NONE) {
    c = t->compare(key, sg_at(t, v), t->user);
    if (c == 0) {
      break;
    }
    p = v;
    v = c < 0 ? t->left[v] : t->right[v];
  }
  if (v == SG_NONE) {
    return DS_NOT_FOUND;
  }
  if (out != NULL) {
    memcpy(out, sg_at(t, v), t->elem_size);
  }
  if (t->left[v] == SG_NONE) {
    r = t->right[v];
  } else if (t->right[v] == SG_NONE) {
    r = t->left[v];
  } else {
    sp = v;
    s = t->right[v];
    while (t->left[s] != SG_NONE) {
      sp = s;
      s = t->left[s];
    }
    if (sp != v) {
      t->left[sp] = t->right[s];
      t->right[s] = t->right[v];
    }
    t->left[s] = t->left[v];
    r = s;
  }
  if (p == SG_NONE) {
    t->root = r;
  } else if (t->left[p] == v) {
    t->left[p] = r;
  } else {
    t->right[p] = r;
  }
  t->left[v] = t->free_list;
  t->free_list = v;
  t->size--;
  if (t->size * t->alpha_den < t->alpha_num * t->max_size) {
    if (t->root != SG_NONE) {
      t->root = sg_rebuild(t, t->root);
    }
    t->max_size = t->size;
  }
  return DS_OK;
}

ds_status_t ds_scapegoat_tree_find(const ds_scapegoat_tree_t *t,
                                   const void *key, void *out) {
  size_t v;
  int c;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  v = t->root;
  while (v != SG_NONE) {
    c = t->compare(key, sg_at(t, v), t->user);
    if (c == 0) {
      if (out != NULL) {
        memcpy(out, sg_at(t, v), t->elem_size);
      }
      return DS_OK;
    }
    v = c < 0 ? t->left[v] : t->right[v];
  }
  return DS_NOT_FOUND;
}

static size_t sg_height(const ds_scapegoat_tree_t *t, size_t v) {
  size_t a;
  size_t b;
  if (v == SG_NONE) {
    return 0;
  }
  a = sg_height(t, t->left[v]);
  b = sg_height(t, t->right[v]);
  return (a > b ? a : b) + 1;
}

size_t ds_scapegoat_tree_height(const ds_scapegoat_tree_t *t) {
  return t == NULL ? 0 : sg_height(t, t->root);
}

size_t ds_scapegoat_tree_rebuilds(const ds_scapegoat_tree_t *t) {
  return t == NULL ? 0 : t->rebuilds;
}

static bool sg_each(const ds_scapegoat_tree_t *t, size_t v,
                    bool (*visit)(const void *elem, void *user), void *user) {
  if (v == SG_NONE) {
    return true;
  }
  return sg_each(t, t->left[v], visit, user) && visit(sg_at(t, v), user) &&
         sg_each(t, t->right[v], visit, user);
}

void ds_scapegoat_tree_foreach(const ds_scapegoat_tree_t *t,
                               bool (*visit)(const void *elem, void *user),
                               void *user) {
  if (t != NULL && visit != NULL) {
    (void)sg_each(t, t->root, visit, user);
  }
}

static bool sg_check(const ds_scapegoat_tree_t *t, size_t v, const void *lo,
                     const void *hi) {
  if (v == SG_NONE) {
    return true;
  }
  if ((lo != NULL && t->compare(sg_at(t, v), lo, t->user) <= 0) ||
      (hi != NULL && t->compare(sg_at(t, v), hi, t->user) >= 0)) {
    return false;
  }
  return sg_check(t, t->left[v], lo, sg_at(t, v)) &&
         sg_check(t, t->right[v], sg_at(t, v), hi);
}

bool ds_scapegoat_tree_valid(const ds_scapegoat_tree_t *t) {
  size_t h;
  if (t == NULL || !sg_check(t, t->root, NULL, NULL)) {
    return false;
  }
  h = ds_scapegoat_tree_height(t);
  /* edges on the longest path at most log_{1/alpha}(max_size) + 1 */
  return h <= 1 || !sg_too_deep(t, h - 2, t->max_size);
}
