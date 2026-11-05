#include "self_org_list.h"
#include <string.h>

/* ---- self-organizing list ------------------------------------------------ */

static unsigned char *sol_at(const ds_self_org_list_t *l, size_t i) {
  return l->data + i * l->elem_size;
}

ds_status_t ds_self_org_list_init(ds_self_org_list_t *list, size_t elem_size,
                                  ds_self_org_compare_func_t compare,
                                  void *user, ds_sol_policy_t policy,
                                  ds_context_t *context) {
  if (list == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 || (int)policy < 0 || policy >= DS_SOL_CUSTOM) {
    return DS_ERR_INVALID;
  }
  memset(list, 0, sizeof(*list));
  list->elem_size = elem_size;
  list->compare = compare;
  list->user = user;
  list->policy = policy;
  list->k = 1;
  list->context = context;
  list->swap = (unsigned char *)ds_context_alloc(context, elem_size);
  return list->swap == NULL ? DS_ERR_ALLOC : DS_OK;
}

ds_status_t ds_self_org_list_init_buffer(ds_self_org_list_t *list,
                                         size_t elem_size,
                                         ds_self_org_compare_func_t compare,
                                         void *user, ds_sol_policy_t policy,
                                         void *buffer, size_t *counts,
                                         size_t capacity) {
  if (list == NULL || compare == NULL || buffer == NULL || counts == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 || (int)policy < 0 || policy >= DS_SOL_CUSTOM) {
    return DS_ERR_INVALID;
  }
  memset(list, 0, sizeof(*list));
  list->elem_size = elem_size;
  list->compare = compare;
  list->user = user;
  list->policy = policy;
  list->k = 1;
  list->data = (unsigned char *)buffer;
  list->swap = list->data + capacity * elem_size;
  list->counts = counts;
  list->capacity = capacity;
  list->fixed = true;
  return DS_OK;
}

void ds_self_org_list_destroy(ds_self_org_list_t *list) {
  if (list == NULL) {
    return;
  }
  if (!list->fixed) {
    ds_context_free(list->context, list->data);
    ds_context_free(list->context, list->counts);
    ds_context_free(list->context, list->swap);
  }
  list->data = NULL;
  list->counts = NULL;
  list->swap = NULL;
  list->size = list->capacity = 0;
}

void ds_self_org_list_clear(ds_self_org_list_t *list) {
  if (list != NULL) {
    list->size = 0;
    list->probes = 0;
  }
}

ds_status_t ds_self_org_list_set_policy(ds_self_org_list_t *list,
                                        ds_sol_policy_t policy, size_t k,
                                        ds_sol_policy_func_t func,
                                        void *func_user) {
  size_t i;
  if (list == NULL) {
    return DS_ERR_NULL;
  }
  if ((int)policy < 0 || policy > DS_SOL_CUSTOM ||
      (policy == DS_SOL_CUSTOM && func == NULL) ||
      (policy == DS_SOL_MOVE_AHEAD_K && k == 0)) {
    return DS_ERR_INVALID;
  }
  if (policy == DS_SOL_COUNT && list->policy != DS_SOL_COUNT) {
    /* restart the counts so the order invariant holds */
    for (i = 0; i < list->size; i++) {
      list->counts[i] = 0;
    }
  }
  list->policy = policy;
  list->k = k;
  list->custom = func;
  list->custom_user = func_user;
  return DS_OK;
}

size_t ds_self_org_list_size(const ds_self_org_list_t *list) {
  return list == NULL ? 0 : list->size;
}

static ds_status_t sol_reserve(ds_self_org_list_t *l, size_t need) {
  size_t cap;
  unsigned char *d;
  size_t *c;
  if (need <= l->capacity) {
    return DS_OK;
  }
  if (l->fixed) {
    return DS_ERR_OVERFLOW;
  }
  cap = l->capacity < 8 ? 8 : l->capacity;
  while (cap < need) {
    if (cap > ((size_t)-1) / 2) {
      return DS_ERR_OVERFLOW;
    }
    cap *= 2;
  }
  if (cap > ((size_t)-1) / l->elem_size ||
      cap > ((size_t)-1) / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  d = (unsigned char *)ds_context_realloc(l->context, l->data,
                                          cap * l->elem_size);
  if (d == NULL) {
    return DS_ERR_ALLOC;
  }
  l->data = d;
  c = (size_t *)ds_context_realloc(l->context, l->counts, cap * sizeof(size_t));
  if (c == NULL) {
    return DS_ERR_ALLOC;
  }
  l->counts = c;
  l->capacity = cap;
  return DS_OK;
}

ds_status_t ds_self_org_list_insert(ds_self_org_list_t *list,
                                    const void *elem) {
  ds_status_t st;
  if (list == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  st = sol_reserve(list, list->size + 1);
  if (st != DS_OK) {
    return st;
  }
  memcpy(sol_at(list, list->size), elem, list->elem_size);
  list->counts[list->size] = 0;
  list->size++;
  return DS_OK;
}

/* Move the element at i to j <= i, shifting [j, i) back by one. */
static void sol_move(ds_self_org_list_t *l, size_t i, size_t j) {
  size_t c;
  if (j >= i) {
    return;
  }
  memcpy(l->swap, sol_at(l, i), l->elem_size);
  memmove(sol_at(l, j + 1), sol_at(l, j), (i - j) * l->elem_size);
  memcpy(sol_at(l, j), l->swap, l->elem_size);
  c = l->counts[i];
  memmove(l->counts + j + 1, l->counts + j, (i - j) * sizeof(size_t));
  l->counts[j] = c;
}

static ds_status_t sol_find(const ds_self_org_list_t *l, const void *key,
                            size_t *out, size_t *probes) {
  size_t i;
  for (i = 0; i < l->size; i++) {
    (*probes)++;
    if (l->compare(sol_at(l, i), key, l->user) == 0) {
      *out = i;
      return DS_OK;
    }
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_self_org_list_search(ds_self_org_list_t *list, const void *key,
                                    void *out, size_t *out_index) {
  size_t i;
  size_t j;
  ds_status_t st;
  if (list == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  st = sol_find(list, key, &i, &list->probes);
  if (st != DS_OK) {
    return st;
  }
  if (list->counts[i] != (size_t)-1) {
    list->counts[i]++;
  }
  switch (list->policy) {
  case DS_SOL_MOVE_TO_FRONT:
    j = 0;
    break;
  case DS_SOL_TRANSPOSE:
    j = i == 0 ? 0 : i - 1;
    break;
  case DS_SOL_COUNT:
    for (j = i; j > 0 && list->counts[j - 1] < list->counts[i]; j--) {
    }
    break;
  case DS_SOL_MOVE_AHEAD_K:
    j = i > list->k ? i - list->k : 0;
    break;
  case DS_SOL_CUSTOM:
    j = list->custom(i, list->counts, list->size, list->custom_user);
    if (j > i) {
      j = i;
    }
    break;
  case DS_SOL_NONE:
  default:
    j = i;
    break;
  }
  sol_move(list, i, j);
  if (out != NULL) {
    memcpy(out, sol_at(list, j), list->elem_size);
  }
  if (out_index != NULL) {
    *out_index = j;
  }
  return DS_OK;
}

ds_status_t ds_self_org_list_find(const ds_self_org_list_t *list,
                                  const void *key, size_t *out_index) {
  size_t probes = 0;
  size_t i;
  ds_status_t st;
  if (list == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  st = sol_find(list, key, &i, &probes);
  if (st == DS_OK && out_index != NULL) {
    *out_index = i;
  }
  return st;
}

ds_status_t ds_self_org_list_remove(ds_self_org_list_t *list, const void *key,
                                    void *out) {
  size_t probes = 0;
  size_t i;
  ds_status_t st;
  if (list == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  st = sol_find(list, key, &i, &probes);
  if (st != DS_OK) {
    return st;
  }
  if (out != NULL) {
    memcpy(out, sol_at(list, i), list->elem_size);
  }
  memmove(sol_at(list, i), sol_at(list, i + 1),
          (list->size - i - 1) * list->elem_size);
  memmove(list->counts + i, list->counts + i + 1,
          (list->size - i - 1) * sizeof(size_t));
  list->size--;
  return DS_OK;
}

const void *ds_self_org_list_at(const ds_self_org_list_t *list, size_t index) {
  if (list == NULL || index >= list->size) {
    return NULL;
  }
  return sol_at(list, index);
}

size_t ds_self_org_list_count_at(const ds_self_org_list_t *list, size_t index) {
  if (list == NULL || index >= list->size) {
    return 0;
  }
  return list->counts[index];
}

size_t ds_self_org_list_probes(const ds_self_org_list_t *list) {
  return list == NULL ? 0 : list->probes;
}

/* ---- move-to-root tree --------------------------------------------------- */

#define MTR_NONE DS_MTR_NONE

static unsigned char *mtr_at(const ds_mtr_tree_t *t, size_t v) {
  return t->data + v * t->elem_size;
}

ds_status_t ds_mtr_tree_init(ds_mtr_tree_t *t, size_t elem_size,
                             ds_self_org_compare_func_t compare, void *user,
                             ds_mtr_policy_t policy, ds_context_t *context) {
  if (t == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 || (int)policy < 0 || policy > DS_MTR_STATIC) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->root = MTR_NONE;
  t->free_list = MTR_NONE;
  t->elem_size = elem_size;
  t->policy = policy;
  t->compare = compare;
  t->user = user;
  t->context = context;
  return DS_OK;
}

void ds_mtr_tree_destroy(ds_mtr_tree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->data);
  ds_context_free(t->context, t->left);
  ds_context_free(t->context, t->right);
  ds_context_free(t->context, t->parent);
  t->data = NULL;
  t->left = t->right = t->parent = NULL;
  t->size = t->slots = t->capacity = 0;
  t->root = t->free_list = MTR_NONE;
}

size_t ds_mtr_tree_size(const ds_mtr_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

static ds_status_t mtr_grow(ds_mtr_tree_t *t) {
  size_t cap = t->capacity < 8 ? 8 : t->capacity * 2;
  void *p;
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
  p = ds_context_realloc(t->context, t->parent, cap * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->parent = (size_t *)p;
  t->capacity = cap;
  return DS_OK;
}

/* Rotate x above its parent. */
static void mtr_rotate(ds_mtr_tree_t *t, size_t x) {
  size_t p = t->parent[x];
  size_t g = t->parent[p];
  size_t b;
  if (t->left[p] == x) {
    b = t->right[x];
    t->left[p] = b;
    t->right[x] = p;
  } else {
    b = t->left[x];
    t->right[p] = b;
    t->left[x] = p;
  }
  if (b != MTR_NONE) {
    t->parent[b] = p;
  }
  t->parent[p] = x;
  t->parent[x] = g;
  if (g == MTR_NONE) {
    t->root = x;
  } else if (t->left[g] == p) {
    t->left[g] = x;
  } else {
    t->right[g] = x;
  }
}

static void mtr_access(ds_mtr_tree_t *t, size_t x) {
  if (t->policy == DS_MTR_MOVE_TO_ROOT) {
    while (t->parent[x] != MTR_NONE) {
      mtr_rotate(t, x);
    }
  } else if (t->policy == DS_MTR_SIMPLE_EXCHANGE && t->parent[x] != MTR_NONE) {
    mtr_rotate(t, x);
  }
}

/* Node with key, or MTR_NONE and the would-be parent in *parent. */
static size_t mtr_locate(const ds_mtr_tree_t *t, const void *key,
                         size_t *parent, int *side, size_t *probes) {
  size_t v = t->root;
  int c;
  *parent = MTR_NONE;
  *side = 0;
  while (v != MTR_NONE) {
    (*probes)++;
    c = t->compare(key, mtr_at(t, v), t->user);
    if (c == 0) {
      return v;
    }
    *parent = v;
    *side = c;
    v = c < 0 ? t->left[v] : t->right[v];
  }
  return MTR_NONE;
}

ds_status_t ds_mtr_tree_insert(ds_mtr_tree_t *t, const void *elem) {
  size_t parent;
  size_t v;
  int side;
  ds_status_t st;
  if (t == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  v = mtr_locate(t, elem, &parent, &side, &t->probes);
  if (v != MTR_NONE) {
    mtr_access(t, v);
    return DS_EXISTS;
  }
  if (t->free_list != MTR_NONE) {
    v = t->free_list;
    t->free_list = t->left[v];
  } else {
    if (t->slots == t->capacity) {
      st = mtr_grow(t);
      if (st != DS_OK) {
        return st;
      }
    }
    v = t->slots++;
  }
  memcpy(mtr_at(t, v), elem, t->elem_size);
  t->left[v] = t->right[v] = MTR_NONE;
  t->parent[v] = parent;
  if (parent == MTR_NONE) {
    t->root = v;
  } else if (side < 0) {
    t->left[parent] = v;
  } else {
    t->right[parent] = v;
  }
  t->size++;
  mtr_access(t, v);
  return DS_OK;
}

ds_status_t ds_mtr_tree_search(ds_mtr_tree_t *t, const void *key, void *out) {
  size_t parent;
  size_t v;
  int side;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  v = mtr_locate(t, key, &parent, &side, &t->probes);
  if (v == MTR_NONE) {
    return DS_NOT_FOUND;
  }
  if (out != NULL) {
    memcpy(out, mtr_at(t, v), t->elem_size);
  }
  mtr_access(t, v);
  return DS_OK;
}

static void mtr_replace(ds_mtr_tree_t *t, size_t u, size_t v) {
  size_t p = t->parent[u];
  if (p == MTR_NONE) {
    t->root = v;
  } else if (t->left[p] == u) {
    t->left[p] = v;
  } else {
    t->right[p] = v;
  }
  if (v != MTR_NONE) {
    t->parent[v] = p;
  }
}

ds_status_t ds_mtr_tree_remove(ds_mtr_tree_t *t, const void *key, void *out) {
  size_t parent;
  size_t v;
  size_t s;
  size_t probes = 0;
  int side;
  if (t == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  v = mtr_locate(t, key, &parent, &side, &probes);
  if (v == MTR_NONE) {
    return DS_NOT_FOUND;
  }
  if (out != NULL) {
    memcpy(out, mtr_at(t, v), t->elem_size);
  }
  if (t->left[v] == MTR_NONE) {
    mtr_replace(t, v, t->right[v]);
  } else if (t->right[v] == MTR_NONE) {
    mtr_replace(t, v, t->left[v]);
  } else {
    s = t->right[v];
    while (t->left[s] != MTR_NONE) {
      s = t->left[s];
    }
    if (t->parent[s] != v) {
      mtr_replace(t, s, t->right[s]);
      t->right[s] = t->right[v];
      t->parent[t->right[s]] = s;
    }
    mtr_replace(t, v, s);
    t->left[s] = t->left[v];
    t->parent[t->left[s]] = s;
  }
  t->left[v] = t->free_list;
  t->free_list = v;
  t->size--;
  return DS_OK;
}

ds_status_t ds_mtr_tree_depth(const ds_mtr_tree_t *t, const void *key,
                              size_t *out_depth) {
  size_t parent;
  size_t v;
  size_t probes = 0;
  int side;
  if (t == NULL || key == NULL || out_depth == NULL) {
    return DS_ERR_NULL;
  }
  v = mtr_locate(t, key, &parent, &side, &probes);
  if (v == MTR_NONE) {
    return DS_NOT_FOUND;
  }
  *out_depth = probes - 1;
  return DS_OK;
}

void ds_mtr_tree_foreach(const ds_mtr_tree_t *t,
                         bool (*visit)(const void *elem, void *user),
                         void *user) {
  size_t v;
  size_t p;
  if (t == NULL || visit == NULL || t->root == MTR_NONE) {
    return;
  }
  v = t->root;
  while (t->left[v] != MTR_NONE) {
    v = t->left[v];
  }
  while (v != MTR_NONE) {
    if (!visit(mtr_at(t, v), user)) {
      return;
    }
    if (t->right[v] != MTR_NONE) {
      v = t->right[v];
      while (t->left[v] != MTR_NONE) {
        v = t->left[v];
      }
    } else {
      p = t->parent[v];
      while (p != MTR_NONE && t->right[p] == v) {
        v = p;
        p = t->parent[p];
      }
      v = p;
    }
  }
}

size_t ds_mtr_tree_probes(const ds_mtr_tree_t *t) {
  return t == NULL ? 0 : t->probes;
}
