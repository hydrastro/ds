#include "binary_tree_repr.h"
#include <string.h>

#define LC_NONE DS_LCRS_NONE

/* ---- first child-next sibling trees -------------------------------------- */

ds_status_t ds_lcrs_tree_init(ds_lcrs_tree_t *t, size_t elem_size,
                              ds_context_t *context) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->root = LC_NONE;
  t->free_list = LC_NONE;
  t->elem_size = elem_size;
  t->context = context;
  return DS_OK;
}

void ds_lcrs_tree_destroy(ds_lcrs_tree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->data);
  ds_context_free(t->context, t->child);
  ds_context_free(t->context, t->sibling);
  ds_context_free(t->context, t->parent);
  ds_context_free(t->context, t->stack);
  t->data = NULL;
  t->child = t->sibling = t->parent = t->stack = NULL;
  t->size = t->slots = t->capacity = t->stack_capacity = 0;
  t->root = t->free_list = LC_NONE;
}

void ds_lcrs_tree_clear(ds_lcrs_tree_t *t) {
  if (t != NULL) {
    t->size = t->slots = 0;
    t->root = t->free_list = LC_NONE;
  }
}

size_t ds_lcrs_tree_size(const ds_lcrs_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

static bool lc_valid(const ds_lcrs_tree_t *t, size_t v) {
  return v < t->slots && (v == t->root || t->parent[v] != LC_NONE);
}

static ds_status_t lc_reserve(ds_lcrs_tree_t *t, size_t need) {
  size_t cap;
  void *p;
  if (need <= t->capacity) {
    return DS_OK;
  }
  cap = t->capacity < 8 ? 8 : t->capacity;
  while (cap < need) {
    if (cap > ((size_t)-1) / 2) {
      return DS_ERR_OVERFLOW;
    }
    cap *= 2;
  }
  if (cap > ((size_t)-1) / t->elem_size ||
      cap > ((size_t)-1) / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  p = ds_context_realloc(t->context, t->data, cap * t->elem_size);
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->data = (unsigned char *)p;
  p = ds_context_realloc(t->context, t->child, cap * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->child = (size_t *)p;
  p = ds_context_realloc(t->context, t->sibling, cap * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->sibling = (size_t *)p;
  p = ds_context_realloc(t->context, t->parent, cap * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->parent = (size_t *)p;
  t->capacity = cap;
  return DS_OK;
}

static ds_status_t lc_new(ds_lcrs_tree_t *t, const void *elem, size_t parent,
                          size_t *out) {
  size_t v;
  ds_status_t st;
  if (t->free_list != LC_NONE) {
    v = t->free_list;
    t->free_list = t->sibling[v];
  } else {
    st = lc_reserve(t, t->slots + 1);
    if (st != DS_OK) {
      return st;
    }
    v = t->slots++;
  }
  memcpy(t->data + v * t->elem_size, elem, t->elem_size);
  t->child[v] = LC_NONE;
  t->sibling[v] = LC_NONE;
  t->parent[v] = parent;
  t->size++;
  *out = v;
  return DS_OK;
}

ds_status_t ds_lcrs_tree_set_root(ds_lcrs_tree_t *t, const void *elem,
                                  size_t *out_node) {
  size_t v;
  ds_status_t st;
  if (t == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root != LC_NONE) {
    return DS_ERR_STATE;
  }
  st = lc_new(t, elem, LC_NONE, &v);
  if (st != DS_OK) {
    return st;
  }
  t->root = v;
  if (out_node != NULL) {
    *out_node = v;
  }
  return DS_OK;
}

ds_status_t ds_lcrs_tree_add_first_child(ds_lcrs_tree_t *t, size_t parent,
                                         const void *elem, size_t *out_node) {
  size_t v;
  ds_status_t st;
  if (t == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (!lc_valid(t, parent)) {
    return DS_ERR_RANGE;
  }
  st = lc_new(t, elem, parent, &v);
  if (st != DS_OK) {
    return st;
  }
  t->sibling[v] = t->child[parent];
  t->child[parent] = v;
  if (out_node != NULL) {
    *out_node = v;
  }
  return DS_OK;
}

static void lc_append(ds_lcrs_tree_t *t, size_t parent, size_t v) {
  size_t c = t->child[parent];
  t->parent[v] = parent;
  t->sibling[v] = LC_NONE;
  if (c == LC_NONE) {
    t->child[parent] = v;
    return;
  }
  while (t->sibling[c] != LC_NONE) {
    c = t->sibling[c];
  }
  t->sibling[c] = v;
}

ds_status_t ds_lcrs_tree_add_last_child(ds_lcrs_tree_t *t, size_t parent,
                                        const void *elem, size_t *out_node) {
  size_t v;
  ds_status_t st;
  if (t == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (!lc_valid(t, parent)) {
    return DS_ERR_RANGE;
  }
  st = lc_new(t, elem, parent, &v);
  if (st != DS_OK) {
    return st;
  }
  lc_append(t, parent, v);
  if (out_node != NULL) {
    *out_node = v;
  }
  return DS_OK;
}

ds_status_t ds_lcrs_tree_add_sibling_after(ds_lcrs_tree_t *t, size_t node,
                                           const void *elem, size_t *out_node) {
  size_t v;
  ds_status_t st;
  if (t == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (!lc_valid(t, node) || node == t->root) {
    return DS_ERR_RANGE;
  }
  st = lc_new(t, elem, t->parent[node], &v);
  if (st != DS_OK) {
    return st;
  }
  t->sibling[v] = t->sibling[node];
  t->sibling[node] = v;
  if (out_node != NULL) {
    *out_node = v;
  }
  return DS_OK;
}

static void lc_detach(ds_lcrs_tree_t *t, size_t v) {
  size_t p = t->parent[v];
  size_t c;
  if (p == LC_NONE) {
    t->root = LC_NONE;
    return;
  }
  c = t->child[p];
  if (c == v) {
    t->child[p] = t->sibling[v];
  } else {
    while (t->sibling[c] != v) {
      c = t->sibling[c];
    }
    t->sibling[c] = t->sibling[v];
  }
  t->sibling[v] = LC_NONE;
}

ds_status_t ds_lcrs_tree_remove_subtree(ds_lcrs_tree_t *t, size_t node) {
  size_t v;
  size_t l;
  size_t r;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (!lc_valid(t, node)) {
    return DS_ERR_RANGE;
  }
  lc_detach(t, node);
  /* free the binary tree (child = left, sibling = right) by rotations */
  v = node;
  while (v != LC_NONE) {
    if (t->child[v] != LC_NONE) {
      l = t->child[v];
      t->child[v] = t->sibling[l];
      t->sibling[l] = v;
      v = l;
    } else {
      r = t->sibling[v];
      t->parent[v] = LC_NONE;
      t->sibling[v] = t->free_list;
      t->free_list = v;
      t->size--;
      v = r;
    }
  }
  return DS_OK;
}

ds_status_t ds_lcrs_tree_move_subtree(ds_lcrs_tree_t *t, size_t node,
                                      size_t new_parent) {
  size_t a;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (!lc_valid(t, node) || !lc_valid(t, new_parent) || node == t->root) {
    return DS_ERR_RANGE;
  }
  for (a = new_parent; a != LC_NONE; a = t->parent[a]) {
    if (a == node) {
      return DS_ERR_INVALID;
    }
  }
  lc_detach(t, node);
  lc_append(t, new_parent, node);
  return DS_OK;
}

void *ds_lcrs_tree_get(const ds_lcrs_tree_t *t, size_t node) {
  if (t == NULL || !lc_valid(t, node)) {
    return NULL;
  }
  return t->data + node * t->elem_size;
}

size_t ds_lcrs_tree_root(const ds_lcrs_tree_t *t) {
  return t == NULL ? LC_NONE : t->root;
}

size_t ds_lcrs_tree_parent(const ds_lcrs_tree_t *t, size_t node) {
  return t == NULL || !lc_valid(t, node) ? LC_NONE : t->parent[node];
}

size_t ds_lcrs_tree_left(const ds_lcrs_tree_t *t, size_t node) {
  return t == NULL || !lc_valid(t, node) ? LC_NONE : t->child[node];
}

size_t ds_lcrs_tree_right(const ds_lcrs_tree_t *t, size_t node) {
  return t == NULL || !lc_valid(t, node) ? LC_NONE : t->sibling[node];
}

size_t ds_lcrs_tree_degree(const ds_lcrs_tree_t *t, size_t node) {
  size_t c;
  size_t d = 0;
  if (t == NULL || !lc_valid(t, node)) {
    return 0;
  }
  for (c = t->child[node]; c != LC_NONE; c = t->sibling[c]) {
    d++;
  }
  return d;
}

size_t ds_lcrs_tree_depth(const ds_lcrs_tree_t *t, size_t node) {
  size_t d = 0;
  if (t == NULL || !lc_valid(t, node)) {
    return 0;
  }
  while (t->parent[node] != LC_NONE) {
    node = t->parent[node];
    d++;
  }
  return d;
}

ds_status_t ds_lcrs_tree_preorder(ds_lcrs_tree_t *t, ds_lcrs_visit_func_t f,
                                  void *user) {
  size_t v;
  size_t d = 0;
  if (t == NULL || f == NULL) {
    return DS_ERR_NULL;
  }
  v = t->root;
  while (v != LC_NONE) {
    if (!f(v, t->data + v * t->elem_size, d, user)) {
      return DS_STOP;
    }
    if (t->child[v] != LC_NONE) {
      v = t->child[v];
      d++;
      continue;
    }
    while (v != t->root && t->sibling[v] == LC_NONE) {
      v = t->parent[v];
      d--;
    }
    v = v == t->root ? LC_NONE : t->sibling[v];
  }
  return DS_OK;
}

ds_status_t ds_lcrs_tree_postorder(ds_lcrs_tree_t *t, ds_lcrs_visit_func_t f,
                                   void *user) {
  size_t v;
  size_t d = 0;
  if (t == NULL || f == NULL) {
    return DS_ERR_NULL;
  }
  v = t->root;
  if (v == LC_NONE) {
    return DS_OK;
  }
  while (t->child[v] != LC_NONE) {
    v = t->child[v];
    d++;
  }
  for (;;) {
    if (!f(v, t->data + v * t->elem_size, d, user)) {
      return DS_STOP;
    }
    if (v == t->root) {
      return DS_OK;
    }
    if (t->sibling[v] != LC_NONE) {
      v = t->sibling[v];
      while (t->child[v] != LC_NONE) {
        v = t->child[v];
        d++;
      }
    } else {
      v = t->parent[v];
      d--;
    }
  }
}

ds_status_t ds_lcrs_tree_levelorder(ds_lcrs_tree_t *t, ds_lcrs_visit_func_t f,
                                    void *user) {
  size_t head = 0;
  size_t tail = 0;
  size_t level_end;
  size_t d = 0;
  size_t v;
  size_t c;
  void *p;
  if (t == NULL || f == NULL) {
    return DS_ERR_NULL;
  }
  if (t->root == LC_NONE) {
    return DS_OK;
  }
  if (t->stack_capacity < t->size) {
    p = ds_context_realloc(t->context, t->stack, t->size * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    t->stack = (size_t *)p;
    t->stack_capacity = t->size;
  }
  t->stack[tail++] = t->root;
  level_end = tail;
  while (head < tail) {
    v = t->stack[head++];
    if (!f(v, t->data + v * t->elem_size, d, user)) {
      return DS_STOP;
    }
    for (c = t->child[v]; c != LC_NONE; c = t->sibling[c]) {
      t->stack[tail++] = c;
    }
    if (head == level_end) {
      level_end = tail;
      d++;
    }
  }
  return DS_OK;
}

ds_status_t ds_lcrs_tree_from_parents(ds_lcrs_tree_t *t, const size_t *parents,
                                      const void *elems, size_t n) {
  size_t i;
  size_t root = LC_NONE;
  size_t seen;
  size_t v;
  ds_status_t st;
  if (t == NULL || (n > 0 && (parents == NULL || elems == NULL))) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n; i++) {
    if (parents[i] == LC_NONE) {
      if (root != LC_NONE) {
        return DS_ERR_INVALID;
      }
      root = i;
    } else if (parents[i] >= n || parents[i] == i) {
      return DS_ERR_INVALID;
    }
  }
  if (n > 0 && root == LC_NONE) {
    return DS_ERR_INVALID;
  }
  ds_lcrs_tree_clear(t);
  st = lc_reserve(t, n);
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < n; i++) {
    memcpy(t->data + i * t->elem_size,
           (const unsigned char *)elems + i * t->elem_size, t->elem_size);
    t->child[i] = LC_NONE;
    t->parent[i] = parents[i];
    t->sibling[i] = LC_NONE;
  }
  /* prepend from the back so children end up in index order */
  for (i = n; i-- > 0;) {
    if (parents[i] != LC_NONE) {
      t->sibling[i] = t->child[parents[i]];
      t->child[parents[i]] = i;
    }
  }
  t->slots = t->size = n;
  t->root = root;
  t->free_list = LC_NONE;
  /* reject cycles: the root must reach every node */
  seen = 0;
  v = root;
  while (v != LC_NONE && seen <= n) {
    seen++;
    if (t->child[v] != LC_NONE) {
      v = t->child[v];
      continue;
    }
    while (v != root && t->sibling[v] == LC_NONE) {
      v = t->parent[v];
    }
    v = v == root ? LC_NONE : t->sibling[v];
  }
  if (seen != n) {
    ds_lcrs_tree_clear(t);
    return DS_ERR_INVALID;
  }
  return DS_OK;
}

/* ---- doubly-chained tree (de la Briandais trie) -------------------------- */

#define DC_NONE (~(size_t)0)

ds_status_t ds_dc_trie_init(ds_dc_trie_t *t, ds_context_t *context) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  t->context = context;
  t->free_list = DC_NONE;
  return DS_OK;
}

void ds_dc_trie_destroy(ds_dc_trie_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->symbol);
  ds_context_free(t->context, t->child);
  ds_context_free(t->context, t->sibling);
  ds_context_free(t->context, t->value);
  ds_context_free(t->context, t->terminal);
  memset(t, 0, sizeof(*t));
  t->free_list = DC_NONE;
}

size_t ds_dc_trie_size(const ds_dc_trie_t *t) {
  return t == NULL ? 0 : t->keys;
}

static ds_status_t dc_new(ds_dc_trie_t *t, unsigned char sym, size_t *out) {
  size_t v;
  size_t cap;
  void *p;
  if (t->free_list != DC_NONE) {
    v = t->free_list;
    t->free_list = t->sibling[v];
  } else {
    if (t->nodes == t->capacity) {
      cap = t->capacity < 16 ? 16 : t->capacity * 2;
      if (cap <= t->capacity || cap > ((size_t)-1) / sizeof(size_t)) {
        return DS_ERR_OVERFLOW;
      }
      p = ds_context_realloc(t->context, t->symbol, cap);
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->symbol = (unsigned char *)p;
      p = ds_context_realloc(t->context, t->terminal, cap);
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->terminal = (unsigned char *)p;
      p = ds_context_realloc(t->context, t->child, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->child = (size_t *)p;
      p = ds_context_realloc(t->context, t->sibling, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->sibling = (size_t *)p;
      p = ds_context_realloc(t->context, t->value, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->value = (size_t *)p;
      t->capacity = cap;
    }
    v = t->nodes++;
  }
  t->symbol[v] = sym;
  t->terminal[v] = 0;
  t->child[v] = DC_NONE;
  t->sibling[v] = DC_NONE;
  t->value[v] = 0;
  *out = v;
  return DS_OK;
}

static ds_status_t dc_root(ds_dc_trie_t *t) {
  size_t r;
  if (t->nodes > 0) {
    return DS_OK;
  }
  return dc_new(t, 0, &r);
}

/* Child of v with symbol c, or DC_NONE (sorted chain). */
static size_t dc_child(const ds_dc_trie_t *t, size_t v, unsigned char c) {
  size_t w;
  for (w = t->child[v]; w != DC_NONE && t->symbol[w] < c; w = t->sibling[w]) {
  }
  return w != DC_NONE && t->symbol[w] == c ? w : DC_NONE;
}

ds_status_t ds_dc_trie_insert(ds_dc_trie_t *t, const unsigned char *key,
                              size_t len, size_t value) {
  size_t v = 0;
  size_t w;
  size_t prev;
  size_t n;
  size_t i;
  ds_status_t st;
  if (t == NULL || (key == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  st = dc_root(t);
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < len; i++) {
    prev = DC_NONE;
    for (w = t->child[v]; w != DC_NONE && t->symbol[w] < key[i];
         w = t->sibling[w]) {
      prev = w;
    }
    if (w == DC_NONE || t->symbol[w] != key[i]) {
      st = dc_new(t, key[i], &n);
      if (st != DS_OK) {
        return st;
      }
      t->sibling[n] = w;
      if (prev == DC_NONE) {
        t->child[v] = n;
      } else {
        t->sibling[prev] = n;
      }
      w = n;
    }
    v = w;
  }
  t->value[v] = value;
  if (t->terminal[v]) {
    return DS_EXISTS;
  }
  t->terminal[v] = 1;
  t->keys++;
  return DS_OK;
}

static size_t dc_walk(const ds_dc_trie_t *t, const unsigned char *key,
                      size_t len) {
  size_t v = 0;
  size_t i;
  if (t->nodes == 0) {
    return DC_NONE;
  }
  for (i = 0; i < len && v != DC_NONE; i++) {
    v = dc_child(t, v, key[i]);
  }
  return v;
}

ds_status_t ds_dc_trie_find(const ds_dc_trie_t *t, const unsigned char *key,
                            size_t len, size_t *out_value) {
  size_t v;
  if (t == NULL || (key == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  v = dc_walk(t, key, len);
  if (v == DC_NONE || !t->terminal[v]) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL) {
    *out_value = t->value[v];
  }
  return DS_OK;
}

ds_status_t ds_dc_trie_remove(ds_dc_trie_t *t, const unsigned char *key,
                              size_t len) {
  size_t v;
  size_t p;
  size_t w;
  size_t cut_parent = DC_NONE;
  size_t cut = DC_NONE;
  size_t i;
  if (t == NULL || (key == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  v = dc_walk(t, key, len);
  if (v == DC_NONE || !t->terminal[v]) {
    return DS_NOT_FOUND;
  }
  t->terminal[v] = 0;
  t->keys--;
  if (t->child[v] != DC_NONE || len == 0) {
    return DS_OK;
  }
  /* the topmost node below which the path is a prunable chain */
  p = 0;
  for (i = 0; i < len; i++) {
    w = dc_child(t, p, key[i]);
    if (p == 0 || t->terminal[p] || t->child[p] != w ||
        t->sibling[w] != DC_NONE) {
      cut_parent = p;
      cut = w;
    }
    p = w;
  }
  /* unlink cut from its parent's chain */
  if (t->child[cut_parent] == cut) {
    t->child[cut_parent] = t->sibling[cut];
  } else {
    for (w = t->child[cut_parent]; t->sibling[w] != cut; w = t->sibling[w]) {
    }
    t->sibling[w] = t->sibling[cut];
  }
  while (cut != DC_NONE) {
    w = t->child[cut];
    t->sibling[cut] = t->free_list;
    t->free_list = cut;
    cut = w;
  }
  return DS_OK;
}

ds_status_t ds_dc_trie_prefix(const ds_dc_trie_t *t,
                              const unsigned char *prefix, size_t len,
                              unsigned char *buf, size_t buf_len,
                              ds_dc_trie_visit_func_t visit, void *user) {
  size_t v;
  size_t *stack;
  size_t top;
  size_t cap;
  ds_status_t st = DS_OK;
  if (t == NULL || visit == NULL || (prefix == NULL && len > 0) ||
      (buf == NULL && buf_len > 0)) {
    return DS_ERR_NULL;
  }
  if (len > buf_len) {
    return DS_ERR_RANGE;
  }
  v = dc_walk(t, prefix, len);
  if (v == DC_NONE) {
    return DS_OK;
  }
  if (len > 0) {
    memmove(buf, prefix, len);
  }
  if (t->terminal[v] && !visit(buf, len, t->value[v], user)) {
    return DS_STOP;
  }
  if (t->child[v] == DC_NONE) {
    return DS_OK;
  }
  cap = buf_len - len + 1;
  stack = (size_t *)ds_context_alloc(t->context, cap * sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  top = 0;
  stack[0] = t->child[v];
  for (;;) {
    v = stack[top];
    if (len + top >= buf_len) {
      st = DS_ERR_RANGE;
      break;
    }
    buf[len + top] = t->symbol[v];
    if (t->terminal[v] && !visit(buf, len + top + 1, t->value[v], user)) {
      st = DS_STOP;
      break;
    }
    if (t->child[v] != DC_NONE) {
      stack[++top] = t->child[v];
      continue;
    }
    while (t->sibling[stack[top]] == DC_NONE) {
      if (top == 0) {
        goto done;
      }
      top--;
    }
    stack[top] = t->sibling[stack[top]];
  }
done:
  ds_context_free(t->context, stack);
  return st;
}

/* ---- stack trees ------------------------------------------------------------
 */

bool ds_binary_tree_is_stack_tree(const size_t *left, const size_t *right,
                                  size_t n, size_t root,
                                  ds_context_t *context) {
  size_t none = ~(size_t)0;
  size_t *stack;
  unsigned char *seen;
  size_t top = 0;
  bool ok = true;
  if (left == NULL || right == NULL || root >= n) {
    return false;
  }
  stack = (size_t *)ds_context_alloc(context, (n + 1) * sizeof(size_t));
  seen = (unsigned char *)ds_context_calloc(context, n, 1);
  if (stack == NULL || seen == NULL) {
    ds_context_free(context, stack);
    ds_context_free(context, seen);
    return false;
  }
  stack[top++] = root;
  while (top > 0 && ok) {
    size_t v = stack[--top];
    size_t c[2];
    size_t inner = 0;
    size_t k;
    if (v >= n || seen[v]) {
      ok = false; /* out of range or shared: not a tree */
      break;
    }
    seen[v] = 1;
    c[0] = left[v];
    c[1] = right[v];
    for (k = 0; k < 2; k++) {
      if (c[k] == none) {
        continue;
      }
      if (c[k] >= n) {
        ok = false;
        break;
      }
      if (left[c[k]] != none || right[c[k]] != none) {
        inner++;
      }
      stack[top++] = c[k];
    }
    if (v != root && inner > 1) {
      ok = false;
    }
  }
  ds_context_free(context, stack);
  ds_context_free(context, seen);
  return ok;
}

ds_status_t ds_stack_tree_build(size_t internal, size_t *left, size_t *right) {
  size_t none = ~(size_t)0;
  size_t total;
  size_t i;
  size_t next;
  size_t spine[2];
  size_t remaining;
  size_t side = 0;
  if (left == NULL || right == NULL) {
    return DS_ERR_NULL;
  }
  if (internal == 0 || internal > (none - 1) / 2) {
    return DS_ERR_INVALID;
  }
  total = 2 * internal + 1;
  for (i = 0; i < total; i++) {
    left[i] = right[i] = none;
  }
  /* node 0 is the root; its children start two spines, alternately
   * extended; every spine node gets one leaf and one spine child */
  next = 1;
  remaining = internal - 1;
  spine[0] = spine[1] = 0;
  if (remaining == 0) {
    left[0] = next++;
    right[0] = next++;
    return DS_OK;
  }
  left[0] = next++;
  right[0] = next++;
  spine[0] = left[0];
  spine[1] = right[0];
  /* the two children are internal or leaves: give the rest out */
  while (remaining > 0) {
    size_t v = spine[side];
    left[v] = next++;  /* a leaf */
    right[v] = next++; /* the spine goes on (a leaf if it ends here) */
    spine[side] = right[v];
    remaining--;
    side = remaining > 0 && spine[1 - side] != 0 ? 1 - side : side;
  }
  return DS_OK;
}
