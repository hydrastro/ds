#include "cartesian_tree.h"
#include <string.h>

#define CT_NONE DS_CARTESIAN_NONE

ds_status_t ds_cartesian_tree_build(const void *base, size_t n,
                                    size_t elem_size,
                                    ds_cartesian_compare_func_t compare,
                                    void *user, size_t *parent, size_t *left,
                                    size_t *right, size_t *out_root,
                                    ds_context_t *context) {
  const unsigned char *a = (const unsigned char *)base;
  size_t *stack;
  size_t top = 0;
  size_t i;
  size_t last;
  if ((base == NULL && n > 0) || compare == NULL || left == NULL ||
      right == NULL || out_root == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  *out_root = CT_NONE;
  if (n == 0) {
    return DS_OK;
  }
  if (n > ((size_t)-1) / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  stack = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    left[i] = right[i] = CT_NONE;
    last = CT_NONE;
    /* pop strictly greater elements: equal ones stay ancestors */
    while (top > 0 && compare(a + stack[top - 1] * elem_size, a + i * elem_size,
                              user) > 0) {
      last = stack[--top];
    }
    left[i] = last;
    if (top > 0) {
      right[stack[top - 1]] = i;
    }
    stack[top++] = i;
  }
  *out_root = stack[0];
  if (parent != NULL) {
    for (i = 0; i < n; i++) {
      parent[i] = CT_NONE;
    }
    for (i = 0; i < n; i++) {
      if (left[i] != CT_NONE) {
        parent[left[i]] = i;
      }
      if (right[i] != CT_NONE) {
        parent[right[i]] = i;
      }
    }
  }
  ds_context_free(context, stack);
  return DS_OK;
}

ds_status_t ds_cartesian_tree_rmq(const size_t *parent, size_t n, size_t i,
                                  size_t j, size_t *out_index) {
  size_t a;
  size_t b;
  size_t da = 0;
  size_t db = 0;
  size_t v;
  if (parent == NULL || out_index == NULL) {
    return DS_ERR_NULL;
  }
  if (i > j || j >= n) {
    return DS_ERR_RANGE;
  }
  for (v = i; parent[v] != CT_NONE; v = parent[v]) {
    da++;
  }
  for (v = j; parent[v] != CT_NONE; v = parent[v]) {
    db++;
  }
  a = i;
  b = j;
  while (da > db) {
    a = parent[a];
    da--;
  }
  while (db > da) {
    b = parent[b];
    db--;
  }
  while (a != b) {
    a = parent[a];
    b = parent[b];
  }
  *out_index = a;
  return DS_OK;
}

/* Binary heap of node indices ordered by (value, index): stable. */
typedef struct ct_heap {
  size_t *h;
  size_t n;
  const unsigned char *a;
  size_t es;
  ds_cartesian_compare_func_t cmp;
  void *user;
} ct_heap_t;

static int ct_less(const ct_heap_t *q, size_t x, size_t y) {
  int c = q->cmp(q->a + x * q->es, q->a + y * q->es, q->user);
  return c < 0 || (c == 0 && x < y);
}

static void ct_push(ct_heap_t *q, size_t v) {
  size_t i = q->n++;
  size_t p;
  while (i > 0) {
    p = (i - 1) / 2;
    if (!ct_less(q, v, q->h[p])) {
      break;
    }
    q->h[i] = q->h[p];
    i = p;
  }
  q->h[i] = v;
}

static size_t ct_pop(ct_heap_t *q) {
  size_t top = q->h[0];
  size_t v = q->h[--q->n];
  size_t i = 0;
  size_t c;
  for (;;) {
    c = 2 * i + 1;
    if (c >= q->n) {
      break;
    }
    if (c + 1 < q->n && ct_less(q, q->h[c + 1], q->h[c])) {
      c++;
    }
    if (!ct_less(q, q->h[c], v)) {
      break;
    }
    q->h[i] = q->h[c];
    i = c;
  }
  if (q->n > 0) {
    q->h[i] = v;
  }
  return top;
}

ds_status_t ds_cartesian_tree_sort(void *base, size_t n, size_t elem_size,
                                   ds_cartesian_compare_func_t compare,
                                   void *user, ds_context_t *context) {
  size_t *left;
  size_t *right;
  size_t *heap;
  unsigned char *out;
  size_t root;
  size_t k;
  size_t v;
  ct_heap_t q;
  ds_status_t st;
  if ((base == NULL && n > 0) || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (n < 2) {
    return DS_OK;
  }
  if (n > ((size_t)-1) / (3 * sizeof(size_t)) || n > ((size_t)-1) / elem_size) {
    return DS_ERR_OVERFLOW;
  }
  left = (size_t *)ds_context_alloc(context, 3 * n * sizeof(size_t));
  out = (unsigned char *)ds_context_alloc(context, n * elem_size);
  if (left == NULL || out == NULL) {
    ds_context_free(context, left);
    ds_context_free(context, out);
    return DS_ERR_ALLOC;
  }
  right = left + n;
  heap = right + n;
  st = ds_cartesian_tree_build(base, n, elem_size, compare, user, NULL, left,
                               right, &root, context);
  if (st == DS_OK) {
    q.h = heap;
    q.n = 0;
    q.a = (const unsigned char *)base;
    q.es = elem_size;
    q.cmp = compare;
    q.user = user;
    ct_push(&q, root);
    for (k = 0; k < n; k++) {
      v = ct_pop(&q);
      memcpy(out + k * elem_size, q.a + v * elem_size, elem_size);
      if (left[v] != CT_NONE) {
        ct_push(&q, left[v]);
      }
      if (right[v] != CT_NONE) {
        ct_push(&q, right[v]);
      }
    }
    memcpy(base, out, n * elem_size);
  }
  ds_context_free(context, left);
  ds_context_free(context, out);
  return st;
}
