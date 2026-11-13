#include "louds.h"
#include "checked.h"
#include <limits.h>
#include <string.h>

#define LD_W (sizeof(unsigned long) * CHAR_BIT)

/* the bits for degrees in level order (validated by the callers) */
static ds_status_t ld_build(ds_louds_t *t, const size_t *degree, size_t n,
                            const ds_rs_ops_t *ops, ds_context_t *ctx) {
  size_t bits;
  size_t pos = 2;
  size_t v;
  unsigned long *w;
  ds_status_t st;
  if (!ds_size_mul(n, 2, &bits) || !ds_size_add(bits, 1, &bits)) {
    return DS_ERR_OVERFLOW;
  }
  w = (unsigned long *)ds_context_calloc(ctx, bits / LD_W + 1,
                                         sizeof(unsigned long));
  if (w == NULL) {
    return DS_ERR_ALLOC;
  }
  w[0] = 1UL; /* the virtual root's "10" */
  for (v = 0; v < n; v++) {
    size_t d;
    for (d = 0; d < degree[v]; d++) {
      w[pos / LD_W] |= 1UL << (pos % LD_W);
      pos++;
    }
    pos++;
  }
  st = ds_rs_build(&t->bits, ops, w, bits, ctx);
  ds_context_free(ctx, w);
  if (st != DS_OK) {
    return st;
  }
  t->n = n;
  t->context = ctx;
  return DS_OK;
}

/* every node reached before its degree is read, n - 1 children in all */
static bool ld_valid_degrees(const size_t *degree, size_t n) {
  size_t reached = 1;
  size_t v;
  if (n == 0) {
    return true;
  }
  for (v = 0; v < n; v++) {
    if (v >= reached || degree[v] > n - reached) {
      return false;
    }
    reached += degree[v];
  }
  return reached == n;
}

ds_status_t ds_louds_from_degrees(ds_louds_t *t, const size_t *degree, size_t n,
                                  const ds_rs_ops_t *ops,
                                  ds_context_t *context) {
  if (t == NULL || (degree == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (!ld_valid_degrees(degree, n)) {
    return DS_ERR_INVALID;
  }
  return ld_build(t, degree, n, ops, context);
}

ds_status_t ds_louds_from_parents(ds_louds_t *t, const size_t *parent, size_t n,
                                  size_t *out_number, const ds_rs_ops_t *ops,
                                  ds_context_t *context) {
  size_t *start = NULL; /* n + 1: children of v at kids[start[v] ..) */
  size_t *kids = NULL;  /* n */
  size_t *queue = NULL; /* n: input nodes in level order */
  size_t *degree = NULL;
  size_t root = DS_LOUDS_NONE;
  size_t v;
  size_t head = 0;
  size_t tail = 0;
  ds_status_t st = DS_ERR_ALLOC;
  if (t == NULL || (parent == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (n == 0) {
    return ld_build(t, NULL, 0, ops, context);
  }
  for (v = 0; v < n; v++) {
    if (parent[v] == DS_LOUDS_NONE) {
      if (root != DS_LOUDS_NONE) {
        return DS_ERR_INVALID; /* two roots */
      }
      root = v;
    } else if (parent[v] >= n || parent[v] == v) {
      return DS_ERR_INVALID;
    }
  }
  if (root == DS_LOUDS_NONE) {
    return DS_ERR_INVALID;
  }
  /* n entries of parent exist, so these sizes fit */
  start = (size_t *)ds_context_calloc(context, n + 1, sizeof(size_t));
  kids = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  queue = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  degree = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  if (start == NULL || kids == NULL || queue == NULL || degree == NULL) {
    goto done;
  }
  /* children by counting sort on the parent, stable in input order */
  for (v = 0; v < n; v++) {
    if (v != root) {
      start[parent[v] + 1]++;
    }
  }
  for (v = 0; v < n; v++) {
    start[v + 1] += start[v];
  }
  for (v = 0; v < n; v++) {
    if (v != root) {
      kids[start[parent[v]]++] = v;
    }
  }
  /* start[p] now ends p's children; shift back to starts */
  for (v = n; v > 0; v--) {
    start[v] = start[v - 1];
  }
  start[0] = 0;
  /* breadth first from the root; a cycle leaves nodes unreached */
  queue[tail++] = root;
  while (head < tail) {
    size_t u = queue[head];
    size_t k;
    degree[head] = start[u + 1] - start[u];
    head++;
    for (k = start[u]; k < start[u + 1]; k++) {
      queue[tail++] = kids[k];
    }
  }
  if (tail != n) {
    st = DS_ERR_INVALID;
    goto done;
  }
  st = ld_build(t, degree, n, ops, context);
  if (st == DS_OK && out_number != NULL) {
    for (v = 0; v < n; v++) {
      out_number[queue[v]] = v;
    }
  }
done:
  ds_context_free(context, start);
  ds_context_free(context, kids);
  ds_context_free(context, queue);
  ds_context_free(context, degree);
  return st;
}

void ds_louds_destroy(ds_louds_t *t) {
  if (t == NULL) {
    return;
  }
  ds_rs_destroy(&t->bits);
  memset(t, 0, sizeof(*t));
}

size_t ds_louds_size(const ds_louds_t *t) { return t == NULL ? 0 : t->n; }

/* the 0 ending v's list of children */
static size_t ld_end(const ds_louds_t *t, size_t v) {
  return ds_rs_select0(&t->bits, v + 1);
}

size_t ds_louds_degree(const ds_louds_t *t, size_t v) {
  if (t == NULL || v >= t->n) {
    return 0;
  }
  return ld_end(t, v) - ds_rs_select0(&t->bits, v) - 1;
}

bool ds_louds_is_leaf(const ds_louds_t *t, size_t v) {
  if (t == NULL || v >= t->n) {
    return true;
  }
  return !ds_rs_access(&t->bits, ds_rs_select0(&t->bits, v) + 1);
}

size_t ds_louds_child(const ds_louds_t *t, size_t v, size_t j) {
  size_t s;
  if (t == NULL || v >= t->n) {
    return DS_LOUDS_NONE;
  }
  s = ds_rs_select0(&t->bits, v);
  if (j >= ld_end(t, v) - s - 1) {
    return DS_LOUDS_NONE;
  }
  return s - v + j;
}

size_t ds_louds_first_child(const ds_louds_t *t, size_t v) {
  size_t s;
  if (t == NULL || v >= t->n) {
    return DS_LOUDS_NONE;
  }
  s = ds_rs_select0(&t->bits, v);
  return ds_rs_access(&t->bits, s + 1) ? s - v : DS_LOUDS_NONE;
}

size_t ds_louds_last_child(const ds_louds_t *t, size_t v) {
  size_t e;
  if (t == NULL || v >= t->n) {
    return DS_LOUDS_NONE;
  }
  e = ld_end(t, v);
  /* the bit before the ending 0 is a child's 1, unless the list is empty */
  return ds_rs_access(&t->bits, e - 1) ? e - v - 2 : DS_LOUDS_NONE;
}

size_t ds_louds_parent(const ds_louds_t *t, size_t v) {
  if (t == NULL || v == 0 || v >= t->n) {
    return DS_LOUDS_NONE;
  }
  return ds_rs_select1(&t->bits, v) - v - 1;
}

size_t ds_louds_next_sibling(const ds_louds_t *t, size_t v) {
  if (t == NULL || v == 0 || v >= t->n) {
    return DS_LOUDS_NONE;
  }
  return ds_rs_access(&t->bits, ds_rs_select1(&t->bits, v) + 1) ? v + 1
                                                                : DS_LOUDS_NONE;
}

size_t ds_louds_prev_sibling(const ds_louds_t *t, size_t v) {
  if (t == NULL || v == 0 || v >= t->n) {
    return DS_LOUDS_NONE;
  }
  return ds_rs_access(&t->bits, ds_rs_select1(&t->bits, v) - 1) ? v - 1
                                                                : DS_LOUDS_NONE;
}

size_t ds_louds_child_rank(const ds_louds_t *t, size_t v) {
  size_t p;
  if (t == NULL || v == 0 || v >= t->n) {
    return 0;
  }
  p = ds_rs_select1(&t->bits, v);
  return p - ds_rs_select0(&t->bits, p - v - 1) - 1;
}

size_t ds_louds_depth(const ds_louds_t *t, size_t v) {
  size_t d = 0;
  if (t == NULL || v >= t->n) {
    return 0;
  }
  while (v != 0) {
    v = ds_rs_select1(&t->bits, v) - v - 1;
    d++;
  }
  return d;
}

size_t ds_louds_child_label(const ds_louds_t *t, size_t v,
                            const unsigned char *labels, unsigned char label) {
  size_t s;
  size_t lo;
  size_t hi;
  size_t end;
  if (t == NULL || labels == NULL || v >= t->n) {
    return DS_LOUDS_NONE;
  }
  s = ds_rs_select0(&t->bits, v);
  lo = s - v;                        /* the first child */
  end = lo + (ld_end(t, v) - s - 1); /* past the last */
  hi = end;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (labels[mid] < label) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo < end && labels[lo] == label ? lo : DS_LOUDS_NONE;
}

size_t ds_louds_bytes(const ds_louds_t *t) {
  return t == NULL ? 0 : sizeof(*t) + ds_rs_bytes(&t->bits);
}
