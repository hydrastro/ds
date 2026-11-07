#include "parallel_sim.h"
#include <string.h>

#define PR_NONE DS_PRAM_NONE

static size_t pr_log_bound(size_t n) {
  size_t r = 1;
  size_t x = 1;
  while (x < n) {
    x <<= 1;
    r++;
  }
  return r + 1;
}

static ds_status_t pr_check_next(const size_t *next, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    if (next[i] != PR_NONE && (next[i] >= n || next[i] == i)) {
      return DS_ERR_INVALID;
    }
  }
  return DS_OK;
}

ds_status_t ds_pram_list_rank(const size_t *next, size_t n, size_t *rank,
                              size_t *out_rounds, ds_context_t *context) {
  size_t *buf;
  size_t *nx;
  size_t *nx2;
  size_t *r2;
  size_t *t;
  size_t i;
  size_t rounds = 0;
  size_t limit = pr_log_bound(n);
  int active = 1;
  ds_status_t st;
  if (n > 0 && (next == NULL || rank == NULL)) {
    return DS_ERR_NULL;
  }
  st = pr_check_next(next, n);
  if (st != DS_OK) {
    return st;
  }
  if (n > ((size_t)-1) / (3 * sizeof(size_t))) {
    return DS_ERR_OVERFLOW;
  }
  buf =
      (size_t *)ds_context_alloc(context, (n > 0 ? n : 1) * 3 * sizeof(size_t));
  if (buf == NULL) {
    return DS_ERR_ALLOC;
  }
  nx = buf;
  nx2 = nx + n;
  r2 = nx2 + n;
  for (i = 0; i < n; i++) {
    nx[i] = next[i];
    rank[i] = next[i] == PR_NONE ? 0 : 1;
  }
  while (active) {
    active = 0;
    for (i = 0; i < n; i++) {
      if (nx[i] != PR_NONE) {
        r2[i] = rank[i] + rank[nx[i]];
        nx2[i] = nx[nx[i]];
        active |= nx2[i] != PR_NONE;
      } else {
        r2[i] = rank[i];
        nx2[i] = PR_NONE;
      }
    }
    memcpy(rank, r2, n * sizeof(size_t));
    t = nx;
    nx = nx2;
    nx2 = t;
    rounds++;
    if (rounds > limit) {
      ds_context_free(context, buf);
      return DS_ERR_INVALID; /* a cycle */
    }
  }
  ds_context_free(context, buf);
  if (out_rounds != NULL) {
    *out_rounds = rounds;
  }
  return DS_OK;
}

ds_status_t ds_pram_list_scan(const size_t *next, size_t n, const void *values,
                              size_t elem_size, ds_pram_op_func_t op,
                              void *user, void *out, size_t *out_rounds,
                              ds_context_t *context) {
  size_t *nx;
  size_t *nx2;
  size_t *t;
  unsigned char *v2;
  unsigned char *o = (unsigned char *)out;
  size_t i;
  size_t rounds = 0;
  size_t limit = pr_log_bound(n);
  int active = 1;
  ds_status_t st;
  if (n > 0 && (next == NULL || values == NULL || out == NULL || op == NULL)) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  st = pr_check_next(next, n);
  if (st != DS_OK) {
    return st;
  }
  if (n > ((size_t)-1) / (2 * sizeof(size_t)) || n > ((size_t)-1) / elem_size) {
    return DS_ERR_OVERFLOW;
  }
  nx =
      (size_t *)ds_context_alloc(context, (n > 0 ? n : 1) * 2 * sizeof(size_t));
  v2 = (unsigned char *)ds_context_alloc(context, (n > 0 ? n : 1) * elem_size);
  if (nx == NULL || v2 == NULL) {
    ds_context_free(context, nx);
    ds_context_free(context, v2);
    return DS_ERR_ALLOC;
  }
  nx2 = nx + n;
  memcpy(nx, next, n * sizeof(size_t));
  if (n > 0) {
    memmove(o, values, n * elem_size);
  }
  while (active && n > 0) {
    active = 0;
    for (i = 0; i < n; i++) {
      if (nx[i] != PR_NONE) {
        op(o + i * elem_size, o + nx[i] * elem_size, v2 + i * elem_size, user);
        nx2[i] = nx[nx[i]];
        active |= nx2[i] != PR_NONE;
      } else {
        memcpy(v2 + i * elem_size, o + i * elem_size, elem_size);
        nx2[i] = PR_NONE;
      }
    }
    memcpy(o, v2, n * elem_size);
    t = nx;
    nx = nx2;
    nx2 = t;
    rounds++;
    if (rounds > limit) {
      ds_context_free(context, nx < nx2 ? nx : nx2);
      ds_context_free(context, v2);
      return DS_ERR_INVALID;
    }
  }
  ds_context_free(context, nx < nx2 ? nx : nx2);
  ds_context_free(context, v2);
  if (out_rounds != NULL) {
    *out_rounds = rounds;
  }
  return DS_OK;
}

ds_status_t ds_pram_find_roots(const size_t *parent, size_t n, size_t *root,
                               size_t *out_rounds, ds_context_t *context) {
  size_t *p2;
  size_t i;
  size_t rounds = 0;
  size_t limit = pr_log_bound(n);
  int changed = 1;
  if (n > 0 && (parent == NULL || root == NULL)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n; i++) {
    if (parent[i] != PR_NONE && parent[i] >= n) {
      return DS_ERR_INVALID;
    }
  }
  p2 = (size_t *)ds_context_alloc(context, (n > 0 ? n : 1) * sizeof(size_t));
  if (p2 == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    root[i] = parent[i] == PR_NONE ? i : parent[i];
  }
  while (changed) {
    changed = 0;
    for (i = 0; i < n; i++) {
      p2[i] = root[root[i]];
      changed |= p2[i] != root[i];
    }
    memcpy(root, p2, n * sizeof(size_t));
    if (changed) {
      rounds++;
    }
    if (rounds > limit) {
      ds_context_free(context, p2);
      return DS_ERR_INVALID; /* a cycle without a root */
    }
  }
  ds_context_free(context, p2);
  if (out_rounds != NULL) {
    *out_rounds = rounds;
  }
  return DS_OK;
}

ds_status_t ds_pram_list_contract_rank(const size_t *next, size_t n,
                                       size_t *rank, unsigned long seed,
                                       size_t *out_rounds,
                                       ds_context_t *context) {
  size_t *succ;
  size_t *pred;
  size_t *w;
  size_t *order; /* spliced nodes in removal order */
  size_t *osucc; /* their successor when spliced */
  size_t *ow;    /* their distance to it */
  unsigned char *coin;
  unsigned char *sel;
  unsigned char *alive;
  size_t removed = 0;
  size_t i;
  size_t rounds = 0;
  size_t limit = 64 * pr_log_bound(n) + 64;
  size_t p;
  size_t s;
  size_t v;
  unsigned long x = seed == 0 ? 2463534242UL : seed;
  int interior;
  ds_status_t st;
  if (n > 0 && (next == NULL || rank == NULL)) {
    return DS_ERR_NULL;
  }
  st = pr_check_next(next, n);
  if (st != DS_OK) {
    return st;
  }
  if (n > ((size_t)-1) / (6 * sizeof(size_t))) {
    return DS_ERR_OVERFLOW;
  }
  succ =
      (size_t *)ds_context_alloc(context, (n > 0 ? n : 1) * 6 * sizeof(size_t));
  coin = (unsigned char *)ds_context_alloc(context, (n > 0 ? n : 1) * 3);
  if (succ == NULL || coin == NULL) {
    ds_context_free(context, succ);
    ds_context_free(context, coin);
    return DS_ERR_ALLOC;
  }
  pred = succ + n;
  w = pred + n;
  order = w + n;
  osucc = order + n;
  ow = osucc + n;
  sel = coin + n;
  alive = sel + n;
  for (i = 0; i < n; i++) {
    pred[i] = PR_NONE;
    alive[i] = 1;
  }
  st = DS_OK;
  for (i = 0; i < n; i++) {
    succ[i] = next[i];
    w[i] = next[i] == PR_NONE ? 0 : 1;
    if (next[i] != PR_NONE) {
      if (pred[next[i]] != PR_NONE) {
        st = DS_ERR_INVALID; /* two predecessors */
      }
      pred[next[i]] = i;
    }
  }
  /* contraction: each round splices the interior nodes that flipped heads
   * while their predecessor flipped tails (an independent set) */
  for (;;) {
    interior = 0;
    for (i = 0; i < n; i++) {
      x ^= (x << 13) & 0xFFFFFFFFUL;
      x ^= x >> 17;
      x ^= (x << 5) & 0xFFFFFFFFUL;
      coin[i] = (unsigned char)(x & 1UL);
      sel[i] = 0;
      if (alive[i] && pred[i] != PR_NONE && succ[i] != PR_NONE) {
        interior = 1;
      }
    }
    if (!interior || st != DS_OK) {
      break;
    }
    if (++rounds > limit) {
      st = DS_ERR_INVALID; /* a cycle */
      break;
    }
    for (i = 0; i < n; i++) {
      if (alive[i] && pred[i] != PR_NONE && succ[i] != PR_NONE && coin[i] &&
          !coin[pred[i]]) {
        sel[i] = 1;
      }
    }
    for (i = 0; i < n; i++) {
      if (!sel[i]) {
        continue;
      }
      p = pred[i];
      s = succ[i];
      order[removed] = i;
      osucc[removed] = s;
      ow[removed++] = w[i];
      succ[p] = s;
      w[p] += w[i];
      pred[s] = p;
      alive[i] = 0;
    }
  }
  if (st == DS_OK) {
    /* survivors: tails rank 0, a head ranks its distance to its tail */
    for (i = 0; i < n; i++) {
      if (alive[i]) {
        rank[i] = succ[i] == PR_NONE ? 0 : w[i];
      }
    }
    /* expansion in reverse removal order */
    for (i = removed; i-- > 0;) {
      v = order[i];
      rank[v] = rank[osucc[i]] + ow[i];
    }
  }
  ds_context_free(context, succ);
  ds_context_free(context, coin);
  if (st == DS_OK && out_rounds != NULL) {
    *out_rounds = rounds;
  }
  return st;
}

ds_status_t ds_pram_expr_eval(const size_t *left, const size_t *right,
                              const ds_pram_expr_op_t *op, const double *value,
                              size_t n, size_t root, double *out_value,
                              size_t *out_rounds, ds_context_t *context) {
  size_t *l;
  size_t *r;
  size_t *par;
  size_t *leaves;
  size_t *stack;
  double *fa;
  double *fb;
  size_t nl = 0;
  size_t top = 0;
  size_t i;
  size_t k;
  size_t phase;
  size_t rounds = 0;
  size_t v;
  size_t u;
  size_t p;
  size_t s;
  size_t g;
  double c;
  double ga;
  double gb;
  if (n == 0 || left == NULL || right == NULL || op == NULL || value == NULL ||
      out_value == NULL) {
    return n == 0 ? DS_ERR_INVALID : DS_ERR_NULL;
  }
  if (root >= n || n > ((size_t)-1) / (6 * sizeof(size_t))) {
    return DS_ERR_RANGE;
  }
  for (i = 0; i < n; i++) {
    if (op[i] == DS_PRAM_LEAF) {
      if (left[i] != PR_NONE || right[i] != PR_NONE) {
        return DS_ERR_INVALID;
      }
    } else if ((op[i] != DS_PRAM_ADD && op[i] != DS_PRAM_MUL) || left[i] >= n ||
               right[i] >= n) {
      return DS_ERR_INVALID;
    }
  }
  l = (size_t *)ds_context_alloc(context, 6 * n * sizeof(size_t));
  fa = (double *)ds_context_alloc(context, 2 * n * sizeof(double));
  if (l == NULL || fa == NULL) {
    ds_context_free(context, l);
    ds_context_free(context, fa);
    return DS_ERR_ALLOC;
  }
  r = l + n;
  par = r + n;
  leaves = par + n;
  stack = leaves + n;
  fb = fa + n;
  for (i = 0; i < n; i++) {
    l[i] = left[i];
    r[i] = right[i];
    par[i] = PR_NONE;
    fa[i] = 1.0;
    fb[i] = 0.0;
  }
  /* leaves left to right (and parent links), checking it is a tree */
  stack[top++] = root;
  while (top > 0) {
    v = stack[--top];
    if (op[v] == DS_PRAM_LEAF) {
      leaves[nl++] = v;
      continue;
    }
    if (par[l[v]] != PR_NONE || par[r[v]] != PR_NONE || l[v] == root ||
        r[v] == root || top + 2 > n) {
      ds_context_free(context, l);
      ds_context_free(context, fa);
      return DS_ERR_INVALID;
    }
    par[l[v]] = v;
    par[r[v]] = v;
    stack[top++] = r[v];
    stack[top++] = l[v];
  }
  /* rake rounds: odd-numbered left leaves, then odd-numbered right leaves */
  while (nl > 1) {
    for (phase = 0; phase < 2; phase++) {
      for (k = 0; k < nl; k += 2) {
        u = leaves[k];
        p = par[u];
        if (p == PR_NONE || (phase == 0) != (l[p] == u)) {
          continue;
        }
        s = l[p] == u ? r[p] : l[p];
        c = fa[u] * value[u] + fb[u];
        if (op[p] == DS_PRAM_ADD) {
          ga = fa[s];
          gb = fb[s] + c;
        } else {
          ga = c * fa[s];
          gb = c * fb[s];
        }
        /* s takes p's place with label f_p o g */
        fb[s] = fa[p] * gb + fb[p];
        fa[s] = fa[p] * ga;
        g = par[p];
        par[s] = g;
        if (g != PR_NONE) {
          if (l[g] == p) {
            l[g] = s;
          } else {
            r[g] = s;
          }
        } else {
          root = s;
        }
        par[u] = PR_NONE;
        leaves[k] = PR_NONE; /* raked */
      }
    }
    /* keep the survivors in order */
    i = 0;
    for (k = 0; k < nl; k++) {
      if (leaves[k] != PR_NONE) {
        leaves[i++] = leaves[k];
      }
    }
    nl = i;
    rounds++;
  }
  *out_value = fa[root] * value[root] + fb[root];
  ds_context_free(context, l);
  ds_context_free(context, fa);
  if (out_rounds != NULL) {
    *out_rounds = rounds;
  }
  return DS_OK;
}
