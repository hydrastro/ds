#include "k2_tree.h"
#include <stdlib.h>
#include <string.h>

static unsigned k2_pop32(unsigned long x) {
  unsigned c = 0;
  x &= 0xFFFFFFFFUL;
  while (x != 0) {
    x &= x - 1;
    c++;
  }
  return c;
}

static bool k2_bit(const unsigned long *v, size_t i) {
  return ((v[i / 32] >> (i % 32)) & 1UL) != 0;
}

/* ones in T[0..i) */
static size_t k2_rank(const ds_k2_tree_t *t, size_t i) {
  size_t w = i / 32;
  size_t r = t->t_rank[w];
  if (i % 32 != 0) {
    r += k2_pop32(t->t[w] & ((1UL << (i % 32)) - 1UL));
  }
  return r;
}

typedef struct k2_cell {
  size_t r;
  size_t c;
} k2_cell_t;

typedef struct k2_node {
  size_t lo; /* range of cells */
  size_t hi;
  size_t r; /* submatrix origin */
  size_t c;
} k2_node_t;

static ds_status_t k2_push_bit(ds_context_t *ctx, unsigned long **v,
                               size_t *bits, size_t *cap, bool one) {
  if (*bits == *cap * 32) {
    size_t nc = *cap < 4 ? 8 : *cap * 2;
    void *p = ds_context_realloc(ctx, *v, nc * sizeof(unsigned long));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    *v = (unsigned long *)p;
    memset(*v + *cap, 0, (nc - *cap) * sizeof(unsigned long));
    *cap = nc;
  }
  if (one) {
    (*v)[*bits / 32] |= 1UL << (*bits % 32);
  }
  (*bits)++;
  return DS_OK;
}

ds_status_t ds_k2_tree_build(ds_k2_tree_t *t, size_t n, size_t k,
                             const size_t *rows, const size_t *cols, size_t m,
                             ds_context_t *context) {
  k2_cell_t *cells = NULL;
  k2_cell_t *tmp = NULL;
  k2_node_t *q = NULL;
  k2_node_t *nq = NULL;
  size_t qn;
  size_t *cnt = NULL;
  size_t tcap = 0;
  size_t lcap = 0;
  size_t i;
  size_t level;
  size_t sub;
  size_t kk;
  ds_status_t st = DS_OK;
  if (t == NULL || (m > 0 && (rows == NULL || cols == NULL))) {
    return DS_ERR_NULL;
  }
  if (k == 0) {
    k = 2;
  }
  if (n == 0 || k < 2 || k > 64) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < m; i++) {
    if (rows[i] >= n || cols[i] >= n) {
      return DS_ERR_RANGE;
    }
  }
  memset(t, 0, sizeof(*t));
  t->n = n;
  t->k = k;
  t->context = context;
  kk = k * k;
  t->side = k;
  t->height = 1;
  while (t->side < n) {
    if (t->side > ((size_t)-1) / k) {
      return DS_ERR_OVERFLOW;
    }
    t->side *= k;
    t->height++;
  }
  /* sort and deduplicate the cells */
  cells = (k2_cell_t *)ds_context_alloc(context, (m + 1) * sizeof(k2_cell_t));
  tmp = (k2_cell_t *)ds_context_alloc(context, (m + 1) * sizeof(k2_cell_t));
  cnt = (size_t *)ds_context_alloc(context, (kk + 1) * sizeof(size_t));
  q = (k2_node_t *)ds_context_alloc(context, (m + 1) * sizeof(k2_node_t));
  nq = (k2_node_t *)ds_context_alloc(context, (m + 1) * sizeof(k2_node_t));
  if (cells == NULL || tmp == NULL || cnt == NULL || q == NULL || nq == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (i = 0; i < m; i++) {
    cells[i].r = rows[i];
    cells[i].c = cols[i];
  }
  qn = 0;
  if (m > 0) {
    q[0].lo = 0;
    q[0].hi = m;
    q[0].r = 0;
    q[0].c = 0;
    qn = 1;
  }
  sub = t->side;
  for (level = 0; level < t->height && st == DS_OK; level++) {
    size_t nn = 0;
    bool last = level + 1 == t->height;
    sub /= k;
    for (i = 0; i < qn && st == DS_OK; i++) {
      k2_node_t nd = q[i];
      size_t j;
      size_t ch;
      size_t start;
      /* counting sort of the node's cells by child */
      for (ch = 0; ch <= kk; ch++) {
        cnt[ch] = 0;
      }
      for (j = nd.lo; j < nd.hi; j++) {
        size_t cr = (cells[j].r - nd.r) / sub;
        size_t cc = (cells[j].c - nd.c) / sub;
        cnt[cr * k + cc + 1]++;
      }
      for (ch = 1; ch <= kk; ch++) {
        cnt[ch] += cnt[ch - 1];
      }
      for (j = nd.lo; j < nd.hi; j++) {
        size_t cr = (cells[j].r - nd.r) / sub;
        size_t cc = (cells[j].c - nd.c) / sub;
        tmp[nd.lo + cnt[cr * k + cc]++] = cells[j];
      }
      memcpy(cells + nd.lo, tmp + nd.lo, (nd.hi - nd.lo) * sizeof(k2_cell_t));
      start = nd.lo;
      for (ch = 0; ch < kk && st == DS_OK; ch++) {
        size_t end = nd.lo + cnt[ch];
        bool one = end > start;
        st = last ? k2_push_bit(context, &t->l, &t->l_bits, &lcap, one)
                  : k2_push_bit(context, &t->t, &t->t_bits, &tcap, one);
        if (one && !last) {
          nq[nn].lo = start;
          nq[nn].hi = end;
          nq[nn].r = nd.r + (ch / k) * sub;
          nq[nn].c = nd.c + (ch % k) * sub;
          nn++;
        }
        if (one && last) {
          t->ones++;
        }
        start = end;
      }
    }
    {
      k2_node_t *sw = q;
      q = nq;
      nq = sw;
      qn = nn;
    }
  }
  if (st != DS_OK) {
    goto done;
  }
  /* rank directory over T */
  t->t_rank = (size_t *)ds_context_alloc(context,
                                         (t->t_bits / 32 + 2) * sizeof(size_t));
  if (t->t_rank == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  t->t_rank[0] = 0;
  for (i = 0; i <= t->t_bits / 32; i++) {
    t->t_rank[i + 1] =
        t->t_rank[i] + (i * 32 < t->t_bits ? k2_pop32(t->t[i]) : 0U);
  }
done:
  ds_context_free(context, cells);
  ds_context_free(context, tmp);
  ds_context_free(context, cnt);
  ds_context_free(context, q);
  ds_context_free(context, nq);
  if (st != DS_OK) {
    ds_k2_tree_destroy(t);
  }
  return st;
}

void ds_k2_tree_destroy(ds_k2_tree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->t);
  ds_context_free(t->context, t->t_rank);
  ds_context_free(t->context, t->l);
  memset(t, 0, sizeof(*t));
}

size_t ds_k2_tree_ones(const ds_k2_tree_t *t) {
  return t == NULL ? 0 : t->ones;
}

size_t ds_k2_tree_bits(const ds_k2_tree_t *t) {
  return t == NULL ? 0 : t->t_bits + t->l_bits;
}

bool ds_k2_tree_get(const ds_k2_tree_t *t, size_t row, size_t col) {
  size_t sub;
  size_t base = 0;
  size_t kk;
  if (t == NULL || row >= t->n || col >= t->n || t->ones == 0) {
    return false;
  }
  kk = t->k * t->k;
  sub = t->side;
  for (;;) {
    size_t pos;
    sub /= t->k;
    pos = base + (row / sub) * t->k + col / sub;
    row %= sub;
    col %= sub;
    if (pos >= t->t_bits) {
      return k2_bit(t->l, pos - t->t_bits);
    }
    if (!k2_bit(t->t, pos)) {
      return false;
    }
    base = k2_rank(t, pos + 1) * kk;
  }
}

typedef struct k2_frame {
  size_t base; /* first child position */
  size_t r;    /* submatrix origin */
  size_t c;
  size_t sub; /* child side */
} k2_frame_t;

ds_status_t ds_k2_tree_range(const ds_k2_tree_t *t, size_t r0, size_t r1,
                             size_t c0, size_t c1, ds_k2_visit_func_t visit,
                             void *user) {
  k2_frame_t *stack;
  size_t top = 0;
  size_t kk;
  ds_status_t st = DS_OK;
  if (t == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (r0 > r1 || c0 > c1 || r0 >= t->n || c0 >= t->n || t->ones == 0) {
    return DS_OK;
  }
  if (r1 >= t->n) {
    r1 = t->n - 1;
  }
  if (c1 >= t->n) {
    c1 = t->n - 1;
  }
  kk = t->k * t->k;
  stack = (k2_frame_t *)ds_context_alloc(t->context, (t->height * kk + 1) *
                                                         sizeof(k2_frame_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  stack[top].base = 0;
  stack[top].r = 0;
  stack[top].c = 0;
  stack[top].sub = t->side / t->k;
  top++;
  while (top > 0 && st == DS_OK) {
    k2_frame_t f = stack[--top];
    size_t a;
    size_t b;
    size_t alo = r0 > f.r ? (r0 - f.r) / f.sub : 0;
    size_t ahi = (r1 - f.r) / f.sub;
    size_t blo = c0 > f.c ? (c0 - f.c) / f.sub : 0;
    size_t bhi = (c1 - f.c) / f.sub;
    if (ahi >= t->k) {
      ahi = t->k - 1;
    }
    if (bhi >= t->k) {
      bhi = t->k - 1;
    }
    /* push in reverse so that children come out in order */
    for (a = ahi + 1; a-- > alo && st == DS_OK;) {
      for (b = bhi + 1; b-- > blo && st == DS_OK;) {
        size_t pos = f.base + a * t->k + b;
        size_t rr = f.r + a * f.sub;
        size_t cc = f.c + b * f.sub;
        if (pos >= t->t_bits) {
          /* leaves: reported directly, in reverse order within this
           * frame; collect them in order instead */
          continue;
        }
        if (k2_bit(t->t, pos)) {
          stack[top].base = k2_rank(t, pos + 1) * kk;
          stack[top].r = rr;
          stack[top].c = cc;
          stack[top].sub = f.sub / t->k;
          top++;
        }
      }
    }
    if (f.base >= t->t_bits) {
      for (a = alo; a <= ahi && st == DS_OK; a++) {
        for (b = blo; b <= bhi && st == DS_OK; b++) {
          size_t pos = f.base + a * t->k + b;
          if (k2_bit(t->l, pos - t->t_bits) && !visit(f.r + a, f.c + b, user)) {
            st = DS_STOP;
          }
        }
      }
    }
  }
  ds_context_free(t->context, stack);
  return st;
}

ds_status_t ds_k2_tree_successors(const ds_k2_tree_t *t, size_t row,
                                  ds_k2_visit_func_t visit, void *user) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  return ds_k2_tree_range(t, row, row, 0, t->n - 1, visit, user);
}

ds_status_t ds_k2_tree_predecessors(const ds_k2_tree_t *t, size_t col,
                                    ds_k2_visit_func_t visit, void *user) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  return ds_k2_tree_range(t, 0, t->n - 1, col, col, visit, user);
}
