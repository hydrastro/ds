#include "threaded_tree.h"
#include <string.h>

#define TT_NONE DS_THREADED_NONE

static unsigned char *tt_at(const ds_threaded_tree_t *t, size_t v) {
  return t->data + v * t->elem_size;
}

static bool tt_has_left(const ds_threaded_tree_t *t, size_t v) {
  return !t->lthread[v] && t->left[v] != TT_NONE;
}

static bool tt_has_right(const ds_threaded_tree_t *t, size_t v) {
  return !t->rthread[v] && t->right[v] != TT_NONE;
}

/* v's left link becomes the predecessor thread (none when right-threaded) */
static void tt_left_thread(ds_threaded_tree_t *t, size_t v, size_t pred) {
  if (t->mode == DS_THREAD_BOTH) {
    t->left[v] = pred;
    t->lthread[v] = 1;
  } else {
    t->left[v] = TT_NONE;
    t->lthread[v] = 0;
  }
}

ds_status_t ds_threaded_tree_init(ds_threaded_tree_t *t, size_t elem_size,
                                  ds_threaded_compare_func_t compare,
                                  void *user, ds_thread_mode_t mode,
                                  ds_context_t *context) {
  if (t == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 || (int)mode < 0 || mode > DS_THREAD_RIGHT) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->root = TT_NONE;
  t->free_list = TT_NONE;
  t->elem_size = elem_size;
  t->mode = mode;
  t->compare = compare;
  t->user = user;
  t->context = context;
  return DS_OK;
}

void ds_threaded_tree_destroy(ds_threaded_tree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->data);
  ds_context_free(t->context, t->left);
  ds_context_free(t->context, t->right);
  ds_context_free(t->context, t->lthread);
  ds_context_free(t->context, t->rthread);
  t->data = NULL;
  t->left = t->right = NULL;
  t->lthread = t->rthread = NULL;
  t->size = t->slots = t->capacity = 0;
  t->root = t->free_list = TT_NONE;
}

size_t ds_threaded_tree_size(const ds_threaded_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

static ds_status_t tt_node(ds_threaded_tree_t *t, size_t *out) {
  size_t cap;
  void *p;
  if (t->free_list != TT_NONE) {
    *out = t->free_list;
    t->free_list = t->right[*out];
    return DS_OK;
  }
  if (t->slots == t->capacity) {
    cap = t->capacity < 8 ? 8 : t->capacity * 2;
    if (cap <= t->capacity || cap > ((size_t)-1) / t->elem_size ||
        cap > ((size_t)-1) / sizeof(size_t)) {
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
    p = ds_context_realloc(t->context, t->lthread, cap);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    t->lthread = (unsigned char *)p;
    p = ds_context_realloc(t->context, t->rthread, cap);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    t->rthread = (unsigned char *)p;
    t->capacity = cap;
  }
  *out = t->slots++;
  return DS_OK;
}

ds_status_t ds_threaded_tree_insert(ds_threaded_tree_t *t, const void *elem) {
  size_t v;
  size_t p = TT_NONE;
  size_t n;
  int c = 0;
  ds_status_t st;
  if (t == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  v = t->root;
  while (v != TT_NONE) {
    c = t->compare(elem, tt_at(t, v), t->user);
    if (c == 0) {
      return DS_EXISTS;
    }
    p = v;
    if (c < 0) {
      v = tt_has_left(t, v) ? t->left[v] : TT_NONE;
    } else {
      v = tt_has_right(t, v) ? t->right[v] : TT_NONE;
    }
  }
  st = tt_node(t, &n);
  if (st != DS_OK) {
    return st;
  }
  memcpy(tt_at(t, n), elem, t->elem_size);
  if (p == TT_NONE) {
    tt_left_thread(t, n, TT_NONE);
    t->right[n] = TT_NONE;
    t->rthread[n] = 1;
    t->root = n;
  } else if (c < 0) {
    tt_left_thread(t, n, t->mode == DS_THREAD_BOTH ? t->left[p] : TT_NONE);
    t->right[n] = p;
    t->rthread[n] = 1;
    t->left[p] = n;
    t->lthread[p] = 0;
  } else {
    tt_left_thread(t, n, p);
    t->right[n] = t->right[p];
    t->rthread[n] = 1;
    t->right[p] = n;
    t->rthread[p] = 0;
  }
  t->size++;
  return DS_OK;
}

static size_t tt_search(const ds_threaded_tree_t *t, const void *key,
                        size_t *out_parent, int *out_side) {
  size_t v = t->root;
  int c;
  *out_parent = TT_NONE;
  *out_side = 0;
  while (v != TT_NONE) {
    c = t->compare(key, tt_at(t, v), t->user);
    if (c == 0) {
      return v;
    }
    *out_parent = v;
    *out_side = c;
    if (c < 0) {
      v = tt_has_left(t, v) ? t->left[v] : TT_NONE;
    } else {
      v = tt_has_right(t, v) ? t->right[v] : TT_NONE;
    }
  }
  return TT_NONE;
}

ds_status_t ds_threaded_tree_find(const ds_threaded_tree_t *t, const void *key,
                                  size_t *out_node) {
  size_t p;
  size_t v;
  int side;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  v = tt_search(t, key, &p, &side);
  if (v == TT_NONE) {
    return DS_NOT_FOUND;
  }
  if (out_node != NULL) {
    *out_node = v;
  }
  return DS_OK;
}

static void tt_replace(ds_threaded_tree_t *t, size_t par, int side, size_t c) {
  if (par == TT_NONE) {
    t->root = c;
  } else if (side < 0) {
    t->left[par] = c;
  } else {
    t->right[par] = c;
  }
}

ds_status_t ds_threaded_tree_remove(ds_threaded_tree_t *t, const void *key,
                                    void *out) {
  size_t d;
  size_t par;
  size_t s;
  size_t c;
  int side;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  d = tt_search(t, key, &par, &side);
  if (d == TT_NONE) {
    return DS_NOT_FOUND;
  }
  if (out != NULL) {
    memcpy(out, tt_at(t, d), t->elem_size);
  }
  if (tt_has_left(t, d) && tt_has_right(t, d)) {
    /* the successor's element moves up; the successor is deleted */
    par = d;
    side = 1;
    s = t->right[d];
    while (tt_has_left(t, s)) {
      par = s;
      side = -1;
      s = t->left[s];
    }
    memcpy(tt_at(t, d), tt_at(t, s), t->elem_size);
    d = s;
  }
  if (!tt_has_left(t, d) && !tt_has_right(t, d)) {
    if (par == TT_NONE) {
      t->root = TT_NONE;
    } else if (side < 0) {
      tt_left_thread(t, par, t->mode == DS_THREAD_BOTH ? t->left[d] : TT_NONE);
    } else {
      t->right[par] = t->right[d];
      t->rthread[par] = 1;
    }
  } else if (tt_has_left(t, d)) {
    c = t->left[d];
    s = c;
    while (tt_has_right(t, s)) {
      s = t->right[s];
    }
    t->right[s] = t->right[d]; /* predecessor's thread skips d */
    tt_replace(t, par, side, c);
  } else {
    c = t->right[d];
    s = c;
    while (tt_has_left(t, s)) {
      s = t->left[s];
    }
    if (t->mode == DS_THREAD_BOTH) {
      t->left[s] = t->left[d]; /* successor's thread skips d */
    }
    tt_replace(t, par, side, c);
  }
  t->right[d] = t->free_list;
  t->free_list = d;
  t->size--;
  return DS_OK;
}

size_t ds_threaded_tree_first(const ds_threaded_tree_t *t) {
  size_t v;
  if (t == NULL || t->root == TT_NONE) {
    return TT_NONE;
  }
  for (v = t->root; tt_has_left(t, v); v = t->left[v]) {
  }
  return v;
}

size_t ds_threaded_tree_last(const ds_threaded_tree_t *t) {
  size_t v;
  if (t == NULL || t->root == TT_NONE) {
    return TT_NONE;
  }
  for (v = t->root; tt_has_right(t, v); v = t->right[v]) {
  }
  return v;
}

size_t ds_threaded_tree_next(const ds_threaded_tree_t *t, size_t node) {
  size_t v;
  if (t == NULL || node >= t->slots) {
    return TT_NONE;
  }
  if (t->rthread[node]) {
    return t->right[node];
  }
  for (v = t->right[node]; tt_has_left(t, v); v = t->left[v]) {
  }
  return v;
}

size_t ds_threaded_tree_prev(const ds_threaded_tree_t *t, size_t node) {
  size_t v;
  size_t best = TT_NONE;
  if (t == NULL || node >= t->slots) {
    return TT_NONE;
  }
  if (t->mode == DS_THREAD_BOTH) {
    if (t->lthread[node]) {
      return t->left[node];
    }
    for (v = t->left[node]; tt_has_right(t, v); v = t->right[v]) {
    }
    return v;
  }
  if (tt_has_left(t, node)) {
    for (v = t->left[node]; tt_has_right(t, v); v = t->right[v]) {
    }
    return v;
  }
  /* right-threaded: the nearest ancestor we went right from */
  v = t->root;
  while (v != TT_NONE && v != node) {
    if (t->compare(tt_at(t, node), tt_at(t, v), t->user) > 0) {
      best = v;
      v = tt_has_right(t, v) ? t->right[v] : TT_NONE;
    } else {
      v = tt_has_left(t, v) ? t->left[v] : TT_NONE;
    }
  }
  return best;
}

const void *ds_threaded_tree_get(const ds_threaded_tree_t *t, size_t node) {
  if (t == NULL || node >= t->slots) {
    return NULL;
  }
  return tt_at(t, node);
}

/* ---- stackless traversal by temporary threading -------------------------- */

typedef struct cg {
  bool (*visit)(size_t node, void *user);
  void *user;
  bool stopped;
} cg_t;

static void cg_visit(cg_t *g, size_t v) {
  if (!g->stopped && !g->visit(v, g->user)) {
    g->stopped = true;
  }
}

/* Reverse the right links on the path from..to (to's link is NONE). */
static void cg_reverse(size_t *right, size_t from, size_t to) {
  size_t x = from;
  size_t y;
  size_t z;
  if (from == to) {
    return;
  }
  y = right[from];
  for (;;) {
    z = right[y];
    right[y] = x;
    x = y;
    y = z;
    if (x == to) {
      break;
    }
  }
}

static void cg_reverse_visit(cg_t *g, size_t *right, size_t from, size_t to) {
  size_t v;
  cg_reverse(right, from, to);
  v = to;
  for (;;) {
    cg_visit(g, v);
    if (v == from) {
      break;
    }
    v = right[v];
  }
  cg_reverse(right, to, from);
  right[to] = TT_NONE;
}

ds_status_t ds_cupif_giannini_traverse(size_t *left, size_t *right, size_t root,
                                       ds_traversal_order_t order,
                                       bool (*visit)(size_t node, void *user),
                                       void *user) {
  cg_t g;
  size_t cur = root;
  size_t pre;
  if (left == NULL || right == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if ((int)order < 0 || order > DS_TRAVERSE_POSTORDER) {
    return DS_ERR_INVALID;
  }
  g.visit = visit;
  g.user = user;
  g.stopped = false;
  while (cur != TT_NONE) {
    if (left[cur] == TT_NONE) {
      if (order != DS_TRAVERSE_POSTORDER) {
        cg_visit(&g, cur);
      }
      cur = right[cur];
      continue;
    }
    pre = left[cur];
    while (right[pre] != TT_NONE && right[pre] != cur) {
      pre = right[pre];
    }
    if (right[pre] == TT_NONE) {
      right[pre] = cur; /* thread back to cur */
      if (order == DS_TRAVERSE_PREORDER) {
        cg_visit(&g, cur);
      }
      cur = left[cur];
    } else {
      right[pre] = TT_NONE; /* remove the thread */
      if (order == DS_TRAVERSE_INORDER) {
        cg_visit(&g, cur);
      } else if (order == DS_TRAVERSE_POSTORDER) {
        cg_reverse_visit(&g, right, left[cur], pre);
      }
      cur = right[cur];
    }
  }
  if (order == DS_TRAVERSE_POSTORDER && root != TT_NONE) {
    pre = root;
    while (right[pre] != TT_NONE) {
      pre = right[pre];
    }
    cg_reverse_visit(&g, right, root, pre);
  }
  return g.stopped ? DS_STOP : DS_OK;
}
