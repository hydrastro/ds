#include "tree_edit.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

typedef struct te_tree {
  size_t n;    /* real nodes; node n + 1 (1-based) is a virtual root */
  size_t *l;   /* leftmost leaf, 1-based postorder */
  size_t *pre; /* postorder (1-based) -> preorder index */
  const unsigned char *labels;
} te_tree_t;

typedef struct te_ctx {
  te_tree_t a;
  te_tree_t b;
  size_t elem;
  const ds_tree_edit_costs_t *costs;
  double *td; /* (na + 2) x (nb + 2) */
  double *fd;
  size_t w; /* row width of td */
} te_ctx_t;

static ds_status_t te_prepare(const ds_tree_edit_forest_t *f, size_t elem,
                              te_tree_t *t, ds_context_t *ctx) {
  size_t n = f->nodes;
  size_t *parent;
  size_t *rem;
  size_t *stack;
  size_t *size;
  size_t *depth;
  size_t sp = 0;
  size_t i;
  if (n > 0 && (f->degree == NULL || f->labels == NULL)) {
    return DS_ERR_NULL;
  }
  if (n > (size_t)-1 / 8 - 4) {
    return DS_ERR_OVERFLOW;
  }
  (void)elem;
  t->n = n;
  t->labels = (const unsigned char *)f->labels;
  t->l = (size_t *)ds_context_alloc(ctx, 2 * (n + 2) * sizeof(size_t));
  parent = (size_t *)ds_context_alloc(ctx, 5 * (n + 1) * sizeof(size_t));
  if (t->l == NULL || parent == NULL) {
    ds_context_free(ctx, t->l);
    ds_context_free(ctx, parent);
    t->l = NULL;
    return DS_ERR_ALLOC;
  }
  t->pre = t->l + n + 2;
  rem = parent + n + 1;
  stack = rem + n + 1;
  size = stack + n + 1;
  depth = size + n + 1;
  for (i = 0; i < n; i++) {
    if (sp > 0) {
      size_t p = stack[sp - 1];
      parent[i] = p;
      depth[i] = depth[p] + 1;
      if (--rem[p] == 0) {
        sp--;
      }
    } else {
      parent[i] = DS_TREE_EDIT_NONE;
      depth[i] = 0;
    }
    if (f->degree[i] > 0) {
      if (f->degree[i] > n) {
        break;
      }
      rem[i] = f->degree[i];
      stack[sp++] = i;
    }
  }
  if (i < n || sp != 0) {
    ds_context_free(ctx, t->l);
    ds_context_free(ctx, parent);
    t->l = NULL;
    return DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    size[i] = 1;
  }
  for (i = n; i-- > 0;) {
    if (parent[i] != DS_TREE_EDIT_NONE) {
      size[parent[i]] += size[i];
    }
  }
  for (i = 0; i < n; i++) {
    size_t post = i - depth[i] + size[i]; /* 1-based */
    t->pre[post] = i;
    t->l[post] = post - size[i] + 1;
  }
  t->l[n + 1] = 1; /* virtual root over the whole forest */
  t->pre[n + 1] = DS_TREE_EDIT_NONE;
  ds_context_free(ctx, parent);
  return DS_OK;
}

static const void *te_label(const te_ctx_t *c, const te_tree_t *t, size_t k) {
  return t->labels + t->pre[k] * c->elem;
}

static double te_del(const te_ctx_t *c, size_t x) {
  if (x == c->a.n + 1) {
    return HUGE_VAL;
  }
  if (c->costs != NULL && c->costs->remove != NULL) {
    return c->costs->remove(te_label(c, &c->a, x), c->costs->user);
  }
  return 1.0;
}

static double te_ins(const te_ctx_t *c, size_t y) {
  if (y == c->b.n + 1) {
    return HUGE_VAL;
  }
  if (c->costs != NULL && c->costs->insert != NULL) {
    return c->costs->insert(te_label(c, &c->b, y), c->costs->user);
  }
  return 1.0;
}

static double te_ren(const te_ctx_t *c, size_t x, size_t y) {
  bool vx = x == c->a.n + 1;
  bool vy = y == c->b.n + 1;
  if (vx || vy) {
    return vx && vy ? 0.0 : HUGE_VAL;
  }
  if (c->costs != NULL && c->costs->relabel != NULL) {
    return c->costs->relabel(te_label(c, &c->a, x), te_label(c, &c->b, y),
                             c->costs->user);
  }
  return memcmp(te_label(c, &c->a, x), te_label(c, &c->b, y), c->elem) == 0
             ? 0.0
             : 1.0;
}

static double te_min3(double a, double b, double d) {
  double m = a < b ? a : b;
  return d < m ? d : m;
}

/* forest distance table for subtrees i (of a) and j (of b); fills td for
 * the pairs on the leftmost paths. fd[(x - li + 1) * fw + (y - lj + 1)] */
static size_t te_forest(te_ctx_t *c, size_t i, size_t j) {
  size_t li = c->a.l[i];
  size_t lj = c->b.l[j];
  size_t fw = j - lj + 2;
  size_t x;
  size_t y;
  double *fd = c->fd;
  fd[0] = 0.0;
  for (x = li; x <= i; x++) {
    fd[(x - li + 1) * fw] = fd[(x - li) * fw] + te_del(c, x);
  }
  for (y = lj; y <= j; y++) {
    fd[y - lj + 1] = fd[y - lj] + te_ins(c, y);
  }
  for (x = li; x <= i; x++) {
    for (y = lj; y <= j; y++) {
      size_t at = (x - li + 1) * fw + (y - lj + 1);
      double del = fd[at - fw] + te_del(c, x);
      double ins = fd[at - 1] + te_ins(c, y);
      if (c->a.l[x] == li && c->b.l[y] == lj) {
        fd[at] = te_min3(del, ins, fd[at - fw - 1] + te_ren(c, x, y));
        c->td[x * c->w + y] = fd[at];
      } else {
        size_t back = (c->a.l[x] - li) * fw + (c->b.l[y] - lj);
        fd[at] = te_min3(del, ins, fd[back] + c->td[x * c->w + y]);
      }
    }
  }
  return fw;
}

static bool te_near(double a, double b) {
  return fabs(a - b) <= 1e-9 * (1.0 + fabs(a));
}

ds_status_t ds_tree_edit_distance(const ds_tree_edit_forest_t *a,
                                  const ds_tree_edit_forest_t *b,
                                  size_t elem_size,
                                  const ds_tree_edit_costs_t *costs,
                                  double *out_distance, size_t *map,
                                  ds_context_t *context) {
  te_ctx_t c;
  size_t na;
  size_t nb;
  unsigned char *seen = NULL;
  size_t *stack = NULL;
  size_t i;
  size_t j;
  ds_status_t st;
  if (a == NULL || b == NULL || out_distance == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(&c, 0, sizeof(c));
  c.elem = elem_size;
  c.costs = costs;
  st = te_prepare(a, elem_size, &c.a, context);
  if (st != DS_OK) {
    return st;
  }
  st = te_prepare(b, elem_size, &c.b, context);
  if (st != DS_OK) {
    ds_context_free(context, c.a.l);
    return st;
  }
  na = c.a.n + 1;
  nb = c.b.n + 1;
  c.w = nb + 1;
  if (na + 1 > (size_t)-1 / sizeof(double) / (nb + 1)) {
    st = DS_ERR_OVERFLOW;
    goto done;
  }
  c.td =
      (double *)ds_context_alloc(context, (na + 1) * (nb + 1) * sizeof(double));
  c.fd =
      (double *)ds_context_alloc(context, (na + 1) * (nb + 1) * sizeof(double));
  seen = (unsigned char *)ds_context_calloc(context, 2 * (na + nb + 2), 1);
  if (c.td == NULL || c.fd == NULL || seen == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  /* keyroots: the highest node of each leftmost leaf; processed in
   * increasing order. seen[0..na] / seen[na+1..] mark leftmost leaves,
   * seen[na+nb+2 + k] flags keyroots of a, then of b */
  for (i = na; i >= 1; i--) {
    if (!seen[c.a.l[i]]) {
      seen[c.a.l[i]] = 1;
      seen[na + nb + 2 + i] = 1;
    }
  }
  for (j = nb; j >= 1; j--) {
    if (!seen[na + 1 + c.b.l[j]]) {
      seen[na + 1 + c.b.l[j]] = 1;
      seen[2 * na + nb + 3 + j] = 1;
    }
  }
  for (i = 1; i <= na; i++) {
    if (!seen[na + nb + 2 + i]) {
      continue;
    }
    for (j = 1; j <= nb; j++) {
      if (seen[2 * na + nb + 3 + j]) {
        (void)te_forest(&c, i, j);
      }
    }
  }
  *out_distance = c.td[na * c.w + nb];
  if (map != NULL) {
    size_t sp = 0;
    for (i = 0; i < c.a.n; i++) {
      map[i] = DS_TREE_EDIT_NONE;
    }
    stack = (size_t *)ds_context_alloc(context, 2 * (na + 1) * sizeof(size_t));
    if (stack == NULL) {
      st = DS_ERR_ALLOC;
      goto done;
    }
    stack[sp++] = na;
    stack[sp++] = nb;
    while (sp > 0) {
      size_t ti;
      size_t tj;
      size_t li;
      size_t lj;
      size_t fw;
      size_t x;
      size_t y;
      tj = stack[--sp];
      ti = stack[--sp];
      fw = te_forest(&c, ti, tj);
      li = c.a.l[ti];
      lj = c.b.l[tj];
      x = ti;
      y = tj;
      while (x >= li || y >= lj) {
        size_t at = (x + 1 - li) * fw + (y + 1 - lj);
        double here = c.fd[at];
        if (x >= li && te_near(here, c.fd[at - fw] + te_del(&c, x))) {
          x--;
        } else if (y >= lj && te_near(here, c.fd[at - 1] + te_ins(&c, y))) {
          y--;
        } else if (c.a.l[x] == li && c.b.l[y] == lj) {
          if (x <= c.a.n && y <= c.b.n) {
            map[c.a.pre[x]] = c.b.pre[y];
          }
          x--;
          y--;
        } else {
          stack[sp++] = x;
          stack[sp++] = y;
          x = c.a.l[x] - 1;
          y = c.b.l[y] - 1;
        }
      }
    }
  }
done:
  ds_context_free(context, c.a.l);
  ds_context_free(context, c.b.l);
  ds_context_free(context, c.td);
  ds_context_free(context, c.fd);
  ds_context_free(context, seen);
  ds_context_free(context, stack);
  return st;
}
