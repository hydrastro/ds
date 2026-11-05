#include "wb_tree.h"
#include <string.h>

#define WB_NONE DS_WB_NONE

static unsigned char *wb_at(const ds_wb_tree_t *t, size_t v) {
  return t->data + v * t->elem_size;
}

static size_t wb_size(const ds_wb_tree_t *t, size_t v) {
  return v == WB_NONE ? 0 : t->size[v];
}

static size_t wb_weight(const ds_wb_tree_t *t, size_t v) {
  return wb_size(t, v) + 1;
}

ds_status_t ds_wb_tree_init(ds_wb_tree_t *t, size_t elem_size,
                            ds_wb_compare_func_t compare, void *user,
                            size_t delta_num, size_t delta_den,
                            size_t gamma_num, size_t gamma_den,
                            ds_context_t *context) {
  if (t == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (delta_num == 0 && delta_den == 0 && gamma_num == 0 && gamma_den == 0) {
    delta_num = 3;
    gamma_num = 2;
    delta_den = gamma_den = 1;
  }
  if (elem_size == 0 || delta_den == 0 || gamma_den == 0 || delta_num > 1024 ||
      delta_den > 1024 || gamma_num > 1024 || gamma_den > 1024 ||
      delta_num <= delta_den || gamma_num < gamma_den ||
      gamma_num * delta_den >= delta_num * gamma_den) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->root = WB_NONE;
  t->free_list = WB_NONE;
  t->elem_size = elem_size;
  t->delta_num = delta_num;
  t->delta_den = delta_den;
  t->gamma_num = gamma_num;
  t->gamma_den = gamma_den;
  t->compare = compare;
  t->user = user;
  t->context = context;
  return DS_OK;
}

void ds_wb_tree_destroy(ds_wb_tree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->data);
  ds_context_free(t->context, t->left);
  ds_context_free(t->context, t->right);
  ds_context_free(t->context, t->size);
  t->data = NULL;
  t->left = t->right = t->size = NULL;
  t->slots = t->capacity = 0;
  t->root = t->free_list = WB_NONE;
}

size_t ds_wb_tree_size(const ds_wb_tree_t *t) {
  return t == NULL ? 0 : wb_size(t, t->root);
}

static ds_status_t wb_node(ds_wb_tree_t *t, const void *elem, size_t *out) {
  size_t cap;
  void *p;
  size_t v;
  if (t->free_list != WB_NONE) {
    v = t->free_list;
    t->free_list = t->left[v];
  } else {
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
      p = ds_context_realloc(t->context, t->size, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->size = (size_t *)p;
      t->capacity = cap;
    }
    v = t->slots++;
  }
  memcpy(wb_at(t, v), elem, t->elem_size);
  t->left[v] = t->right[v] = WB_NONE;
  t->size[v] = 1;
  *out = v;
  return DS_OK;
}

static void wb_fix(ds_wb_tree_t *t, size_t v) {
  t->size[v] = wb_size(t, t->left[v]) + wb_size(t, t->right[v]) + 1;
}

static size_t wb_rot_left(ds_wb_tree_t *t, size_t v) {
  size_t r = t->right[v];
  t->right[v] = t->left[r];
  t->left[r] = v;
  wb_fix(t, v);
  wb_fix(t, r);
  return r;
}

static size_t wb_rot_right(ds_wb_tree_t *t, size_t v) {
  size_t l = t->left[v];
  t->left[v] = t->right[l];
  t->right[l] = v;
  wb_fix(t, v);
  wb_fix(t, l);
  return l;
}

/* Restore balance at v after one insertion or deletion below it. */
static size_t wb_balance(ds_wb_tree_t *t, size_t v) {
  size_t wl = wb_weight(t, t->left[v]);
  size_t wr = wb_weight(t, t->right[v]);
  size_t c;
  wb_fix(t, v);
  if (wr * t->delta_den > t->delta_num * wl) {
    c = t->right[v];
    if (wb_weight(t, t->left[c]) * t->gamma_den >=
        t->gamma_num * wb_weight(t, t->right[c])) {
      t->right[v] = wb_rot_right(t, c);
    }
    return wb_rot_left(t, v);
  }
  if (wl * t->delta_den > t->delta_num * wr) {
    c = t->left[v];
    if (wb_weight(t, t->right[c]) * t->gamma_den >=
        t->gamma_num * wb_weight(t, t->left[c])) {
      t->left[v] = wb_rot_left(t, c);
    }
    return wb_rot_right(t, v);
  }
  return v;
}

static ds_status_t wb_insert(ds_wb_tree_t *t, size_t v, const void *elem,
                             size_t *out_root) {
  int c;
  size_t sub;
  ds_status_t st;
  if (v == WB_NONE) {
    return wb_node(t, elem, out_root);
  }
  c = t->compare(elem, wb_at(t, v), t->user);
  if (c == 0) {
    *out_root = v;
    return DS_EXISTS;
  }
  st = wb_insert(t, c < 0 ? t->left[v] : t->right[v], elem, &sub);
  if (st != DS_OK) {
    *out_root = v;
    return st;
  }
  if (c < 0) {
    t->left[v] = sub;
  } else {
    t->right[v] = sub;
  }
  *out_root = wb_balance(t, v);
  return DS_OK;
}

ds_status_t ds_wb_tree_insert(ds_wb_tree_t *t, const void *elem) {
  size_t r;
  ds_status_t st;
  if (t == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  st = wb_insert(t, t->root, elem, &r);
  t->root = r;
  return st;
}

/* Detach the minimum of the subtree v: returns the new subtree root. */
static size_t wb_take_min(ds_wb_tree_t *t, size_t v, size_t *out_min) {
  if (t->left[v] == WB_NONE) {
    *out_min = v;
    return t->right[v];
  }
  t->left[v] = wb_take_min(t, t->left[v], out_min);
  return wb_balance(t, v);
}

static size_t wb_remove(ds_wb_tree_t *t, size_t v, const void *key, void *out,
                        bool *found) {
  int c;
  size_t m;
  size_t r;
  if (v == WB_NONE) {
    return WB_NONE;
  }
  c = t->compare(key, wb_at(t, v), t->user);
  if (c < 0) {
    t->left[v] = wb_remove(t, t->left[v], key, out, found);
  } else if (c > 0) {
    t->right[v] = wb_remove(t, t->right[v], key, out, found);
  } else {
    *found = true;
    if (out != NULL) {
      memcpy(out, wb_at(t, v), t->elem_size);
    }
    if (t->left[v] == WB_NONE || t->right[v] == WB_NONE) {
      r = t->left[v] == WB_NONE ? t->right[v] : t->left[v];
    } else {
      t->right[v] = wb_take_min(t, t->right[v], &m);
      t->left[m] = t->left[v];
      t->right[m] = t->right[v];
      r = wb_balance(t, m);
    }
    t->left[v] = t->free_list;
    t->free_list = v;
    return r;
  }
  return *found ? wb_balance(t, v) : v;
}

ds_status_t ds_wb_tree_remove(ds_wb_tree_t *t, const void *key, void *out) {
  bool found = false;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  t->root = wb_remove(t, t->root, key, out, &found);
  return found ? DS_OK : DS_NOT_FOUND;
}

ds_status_t ds_wb_tree_find(const ds_wb_tree_t *t, const void *key, void *out) {
  size_t v;
  int c;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  v = t->root;
  while (v != WB_NONE) {
    c = t->compare(key, wb_at(t, v), t->user);
    if (c == 0) {
      if (out != NULL) {
        memcpy(out, wb_at(t, v), t->elem_size);
      }
      return DS_OK;
    }
    v = c < 0 ? t->left[v] : t->right[v];
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_wb_tree_select(const ds_wb_tree_t *t, size_t k, void *out) {
  size_t v;
  size_t ls;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (k >= ds_wb_tree_size(t)) {
    return DS_ERR_RANGE;
  }
  v = t->root;
  for (;;) {
    ls = wb_size(t, t->left[v]);
    if (k < ls) {
      v = t->left[v];
    } else if (k == ls) {
      break;
    } else {
      k -= ls + 1;
      v = t->right[v];
    }
  }
  if (out != NULL) {
    memcpy(out, wb_at(t, v), t->elem_size);
  }
  return DS_OK;
}

size_t ds_wb_tree_rank(const ds_wb_tree_t *t, const void *key) {
  size_t v;
  size_t r = 0;
  int c;
  if (t == NULL || key == NULL) {
    return 0;
  }
  v = t->root;
  while (v != WB_NONE) {
    c = t->compare(key, wb_at(t, v), t->user);
    if (c <= 0) {
      if (c == 0) {
        return r + wb_size(t, t->left[v]);
      }
      v = t->left[v];
    } else {
      r += wb_size(t, t->left[v]) + 1;
      v = t->right[v];
    }
  }
  return r;
}

double ds_wb_tree_root_balance(const ds_wb_tree_t *t) {
  size_t wl;
  size_t w;
  if (t == NULL || t->root == WB_NONE) {
    return 0.5;
  }
  wl = wb_weight(t, t->left[t->root]);
  w = wb_weight(t, t->root);
  return (double)wl / (double)w;
}

static size_t wb_height(const ds_wb_tree_t *t, size_t v) {
  size_t a;
  size_t b;
  if (v == WB_NONE) {
    return 0;
  }
  a = wb_height(t, t->left[v]);
  b = wb_height(t, t->right[v]);
  return (a > b ? a : b) + 1;
}

size_t ds_wb_tree_height(const ds_wb_tree_t *t) {
  return t == NULL ? 0 : wb_height(t, t->root);
}

static bool wb_each(const ds_wb_tree_t *t, size_t v,
                    bool (*visit)(const void *elem, void *user), void *user) {
  if (v == WB_NONE) {
    return true;
  }
  return wb_each(t, t->left[v], visit, user) && visit(wb_at(t, v), user) &&
         wb_each(t, t->right[v], visit, user);
}

void ds_wb_tree_foreach(const ds_wb_tree_t *t,
                        bool (*visit)(const void *elem, void *user),
                        void *user) {
  if (t != NULL && visit != NULL) {
    (void)wb_each(t, t->root, visit, user);
  }
}

static bool wb_check(const ds_wb_tree_t *t, size_t v, const void *lo,
                     const void *hi) {
  size_t wl;
  size_t wr;
  if (v == WB_NONE) {
    return true;
  }
  if ((lo != NULL && t->compare(wb_at(t, v), lo, t->user) <= 0) ||
      (hi != NULL && t->compare(wb_at(t, v), hi, t->user) >= 0)) {
    return false;
  }
  wl = wb_weight(t, t->left[v]);
  wr = wb_weight(t, t->right[v]);
  if (t->size[v] != wl + wr - 1 || wl * t->delta_den > t->delta_num * wr ||
      wr * t->delta_den > t->delta_num * wl) {
    return false;
  }
  return wb_check(t, t->left[v], lo, wb_at(t, v)) &&
         wb_check(t, t->right[v], wb_at(t, v), hi);
}

bool ds_wb_tree_valid(const ds_wb_tree_t *t) {
  return t != NULL && wb_check(t, t->root, NULL, NULL);
}
