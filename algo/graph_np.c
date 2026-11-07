#include "graph_np.h"
#include <string.h>

#define NP_NONE (~(size_t)0)

static bool np_adj(const unsigned char *adj, size_t n, size_t u, size_t v) {
  return adj[u * n + v] != 0;
}

static ds_status_t np_mul(size_t a, size_t b, size_t *out) {
  if (b != 0 && a > ((size_t)-1) / b) {
    return DS_ERR_OVERFLOW;
  }
  *out = a * b;
  return DS_OK;
}

/* ---- Bron-Kerbosch with pivoting, iterative ---------------------------------
 */

typedef struct np_bk {
  const unsigned char *adj;
  size_t n;
  size_t *buf; /* per level: P (n), X (n), C (n) */
  size_t *pl;  /* sizes per level */
  size_t *xl;
  size_t *cl;
  size_t *ci;  /* next candidate per level */
  size_t *r;   /* current clique */
  size_t best; /* for maximum clique search (0: enumerate all) */
  bool bound;
  size_t *best_set;
  bool (*visit)(const size_t *clique, size_t size, void *user);
  void *user;
} np_bk_t;

static size_t *bk_p(np_bk_t *b, size_t l) {
  return b->buf + l * 3 * b->n;
}

static size_t *bk_x(np_bk_t *b, size_t l) {
  return b->buf + l * 3 * b->n + b->n;
}

static size_t *bk_c(np_bk_t *b, size_t l) {
  return b->buf + l * 3 * b->n + 2 * b->n;
}

/* Candidates of level l: P minus the neighbours of the pivot (the vertex
 * of P u X with most neighbours in P). */
static void bk_candidates(np_bk_t *b, size_t l) {
  size_t *p = bk_p(b, l);
  size_t *x = bk_x(b, l);
  size_t *c = bk_c(b, l);
  size_t i;
  size_t j;
  size_t u;
  size_t cnt;
  size_t best = NP_NONE;
  size_t bestc = 0;
  for (i = 0; i < b->pl[l] + b->xl[l]; i++) {
    u = i < b->pl[l] ? p[i] : x[i - b->pl[l]];
    cnt = 0;
    for (j = 0; j < b->pl[l]; j++) {
      cnt += np_adj(b->adj, b->n, u, p[j]);
    }
    if (best == NP_NONE || cnt > bestc) {
      best = u;
      bestc = cnt;
    }
  }
  b->cl[l] = 0;
  for (i = 0; i < b->pl[l]; i++) {
    if (best == NP_NONE || !np_adj(b->adj, b->n, best, p[i])) {
      c[b->cl[l]++] = p[i];
    }
  }
  b->ci[l] = 0;
}

static void bk_remove(size_t *arr, size_t *len, size_t v) {
  size_t i;
  for (i = 0; i < *len; i++) {
    if (arr[i] == v) {
      arr[i] = arr[--*len];
      return;
    }
  }
}

/* Returns false when the visitor stopped. */
static bool bk_run(np_bk_t *b) {
  size_t l = 0;
  size_t i;
  size_t v;
  size_t *p;
  size_t *x;
  size_t *np2;
  size_t *nx;
  b->pl[0] = b->n;
  b->xl[0] = 0;
  for (i = 0; i < b->n; i++) {
    bk_p(b, 0)[i] = i;
  }
  bk_candidates(b, 0);
  for (;;) {
    if (b->pl[l] == 0 && b->xl[l] == 0) {
      /* R is a maximal clique */
      if (b->bound) {
        if (l > b->best) {
          b->best = l;
          memcpy(b->best_set, b->r, l * sizeof(size_t));
        }
      } else if (b->visit != NULL && !b->visit(b->r, l, b->user)) {
        return false;
      }
      b->ci[l] = b->cl[l]; /* nothing left here */
    }
    if (b->ci[l] < b->cl[l] && (!b->bound || l + b->pl[l] > b->best)) {
      v = bk_c(b, l)[b->ci[l]++];
      b->r[l] = v;
      p = bk_p(b, l);
      x = bk_x(b, l);
      np2 = bk_p(b, l + 1);
      nx = bk_x(b, l + 1);
      b->pl[l + 1] = 0;
      b->xl[l + 1] = 0;
      for (i = 0; i < b->pl[l]; i++) {
        if (np_adj(b->adj, b->n, v, p[i])) {
          np2[b->pl[l + 1]++] = p[i];
        }
      }
      for (i = 0; i < b->xl[l]; i++) {
        if (np_adj(b->adj, b->n, v, x[i])) {
          nx[b->xl[l + 1]++] = x[i];
        }
      }
      l++;
      bk_candidates(b, l);
      continue;
    }
    /* level exhausted: return to the parent, moving its vertex P -> X */
    if (l == 0) {
      return true;
    }
    l--;
    v = b->r[l];
    bk_remove(bk_p(b, l), &b->pl[l], v);
    bk_x(b, l)[b->xl[l]++] = v;
  }
}

static ds_status_t bk_alloc(np_bk_t *b, const unsigned char *adj, size_t n,
                            void **block, ds_context_t *context) {
  size_t cells;
  ds_status_t st = np_mul(n + 1, 3 * n + 6, &cells);
  if (st != DS_OK || cells > ((size_t)-1) / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  *block = ds_context_alloc(context, (cells + 1) * sizeof(size_t));
  if (*block == NULL) {
    return DS_ERR_ALLOC;
  }
  memset(b, 0, sizeof(*b));
  b->adj = adj;
  b->n = n;
  b->buf = (size_t *)*block;
  b->pl = b->buf + (n + 1) * 3 * n;
  b->xl = b->pl + n + 1;
  b->cl = b->xl + n + 1;
  b->ci = b->cl + n + 1;
  b->r = b->ci + n + 1;
  b->best_set = b->r + n + 1;
  return DS_OK;
}

ds_status_t ds_maximal_cliques(const unsigned char *adj, size_t n,
                               bool (*visit)(const size_t *clique, size_t size,
                                             void *user),
                               void *user, ds_context_t *context) {
  np_bk_t b;
  void *block;
  bool done;
  ds_status_t st;
  if ((n > 0 && adj == NULL) || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (n == 0) {
    return DS_OK;
  }
  st = bk_alloc(&b, adj, n, &block, context);
  if (st != DS_OK) {
    return st;
  }
  b.visit = visit;
  b.user = user;
  done = bk_run(&b);
  ds_context_free(context, block);
  return done ? DS_OK : DS_STOP;
}

ds_status_t ds_max_clique(const unsigned char *adj, size_t n, size_t *out,
                          size_t *out_size, ds_context_t *context) {
  np_bk_t b;
  void *block;
  ds_status_t st;
  if ((n > 0 && (adj == NULL || out == NULL)) || out_size == NULL) {
    return DS_ERR_NULL;
  }
  *out_size = 0;
  if (n == 0) {
    return DS_OK;
  }
  st = bk_alloc(&b, adj, n, &block, context);
  if (st != DS_OK) {
    return st;
  }
  b.bound = true;
  (void)bk_run(&b);
  memcpy(out, b.best_set, b.best * sizeof(size_t));
  *out_size = b.best;
  ds_context_free(context, block);
  return DS_OK;
}

static unsigned char *np_complement(const unsigned char *adj, size_t n,
                                    ds_context_t *context) {
  size_t i;
  size_t j;
  unsigned char *c = (unsigned char *)ds_context_alloc(context, n * n);
  if (c == NULL) {
    return NULL;
  }
  for (i = 0; i < n; i++) {
    for (j = 0; j < n; j++) {
      c[i * n + j] =
          (unsigned char)(i != j && !adj[i * n + j] && !adj[j * n + i]);
    }
  }
  return c;
}

ds_status_t ds_max_independent_set(const unsigned char *adj, size_t n,
                                   size_t *out, size_t *out_size,
                                   ds_context_t *context) {
  unsigned char *c;
  size_t sq;
  ds_status_t st;
  if ((n > 0 && (adj == NULL || out == NULL)) || out_size == NULL) {
    return DS_ERR_NULL;
  }
  if (np_mul(n, n, &sq) != DS_OK) {
    return DS_ERR_OVERFLOW;
  }
  *out_size = 0;
  if (n == 0) {
    return DS_OK;
  }
  c = np_complement(adj, n, context);
  if (c == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ds_max_clique(c, n, out, out_size, context);
  ds_context_free(context, c);
  return st;
}

ds_status_t ds_maximal_independent_set(const unsigned char *adj, size_t n,
                                       ds_mis_method_t method,
                                       unsigned long seed,
                                       unsigned char *in_set,
                                       size_t *out_rounds,
                                       ds_context_t *context) {
  unsigned char *alive;
  unsigned long *val;
  size_t i;
  size_t j;
  size_t rounds = 0;
  size_t best;
  size_t bd;
  size_t d;
  size_t left = n;
  unsigned long x = seed == 0 ? 88172645UL : seed;
  bool join;
  if (n > 0 && (adj == NULL || in_set == NULL)) {
    return DS_ERR_NULL;
  }
  alive = (unsigned char *)ds_context_alloc(context, n > 0 ? n : 1);
  val = (unsigned long *)ds_context_alloc(context, (n > 0 ? n : 1) *
                                                       sizeof(unsigned long));
  if (alive == NULL || val == NULL) {
    ds_context_free(context, alive);
    ds_context_free(context, val);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    alive[i] = 1;
    in_set[i] = 0;
  }
  while (left > 0) {
    rounds++;
    if (method == DS_MIS_GREEDY) {
      /* the live vertex of minimum live degree */
      best = NP_NONE;
      bd = 0;
      for (i = 0; i < n; i++) {
        if (!alive[i]) {
          continue;
        }
        d = 0;
        for (j = 0; j < n; j++) {
          d += alive[j] && j != i && np_adj(adj, n, i, j);
        }
        if (best == NP_NONE || d < bd) {
          best = i;
          bd = d;
        }
      }
      in_set[best] = 1;
      alive[best] = 0;
      left--;
      for (j = 0; j < n; j++) {
        if (alive[j] && np_adj(adj, n, best, j)) {
          alive[j] = 0;
          left--;
        }
      }
      continue;
    }
    /* Luby: a vertex joins when its random value beats its live
     * neighbours' (ties by index) */
    for (i = 0; i < n; i++) {
      x ^= (x << 13) & 0xFFFFFFFFUL;
      x ^= x >> 17;
      x ^= (x << 5) & 0xFFFFFFFFUL;
      val[i] = x;
    }
    for (i = 0; i < n; i++) {
      if (!alive[i]) {
        continue;
      }
      join = true;
      for (j = 0; j < n && join; j++) {
        if (j != i && alive[j] && np_adj(adj, n, i, j) &&
            (val[j] < val[i] || (val[j] == val[i] && j < i))) {
          join = false;
        }
      }
      if (join) {
        in_set[i] = 2; /* joined this round */
      }
    }
    for (i = 0; i < n; i++) {
      if (in_set[i] == 2) {
        in_set[i] = 1;
        if (alive[i]) {
          alive[i] = 0;
          left--;
        }
        for (j = 0; j < n; j++) {
          if (alive[j] && np_adj(adj, n, i, j)) {
            alive[j] = 0;
            left--;
          }
        }
      }
    }
  }
  ds_context_free(context, alive);
  ds_context_free(context, val);
  if (out_rounds != NULL) {
    *out_rounds = rounds;
  }
  return DS_OK;
}

ds_status_t ds_min_vertex_cover(const unsigned char *adj, size_t n, size_t *out,
                                size_t *out_size, ds_context_t *context) {
  size_t *is;
  size_t k;
  size_t i;
  size_t c = 0;
  unsigned char *mark;
  ds_status_t st;
  if ((n > 0 && (adj == NULL || out == NULL)) || out_size == NULL) {
    return DS_ERR_NULL;
  }
  *out_size = 0;
  if (n == 0) {
    return DS_OK;
  }
  is = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  mark = (unsigned char *)ds_context_calloc(context, n, 1);
  if (is == NULL || mark == NULL) {
    ds_context_free(context, is);
    ds_context_free(context, mark);
    return DS_ERR_ALLOC;
  }
  st = ds_max_independent_set(adj, n, is, &k, context);
  if (st == DS_OK) {
    for (i = 0; i < k; i++) {
      mark[is[i]] = 1;
    }
    for (i = 0; i < n; i++) {
      if (!mark[i]) {
        out[c++] = i;
      }
    }
    *out_size = c;
  }
  ds_context_free(context, is);
  ds_context_free(context, mark);
  return st;
}

ds_status_t ds_vertex_cover_approx(const unsigned char *adj, size_t n,
                                   unsigned char *in_cover, size_t *out_size) {
  size_t i;
  size_t j;
  size_t c = 0;
  if ((n > 0 && (adj == NULL || in_cover == NULL)) || out_size == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n; i++) {
    in_cover[i] = 0;
  }
  /* both ends of every edge of a maximal matching */
  for (i = 0; i < n; i++) {
    for (j = i + 1; j < n && !in_cover[i]; j++) {
      if (!in_cover[j] && (np_adj(adj, n, i, j) || np_adj(adj, n, j, i))) {
        in_cover[i] = in_cover[j] = 1;
        c += 2;
      }
    }
  }
  *out_size = c;
  return DS_OK;
}

/* ---- vertex colouring -------------------------------------------------------
 */

/* DSATUR order (and the greedy colouring it produces). */
static size_t np_dsatur(const unsigned char *adj, size_t n, size_t *color,
                        size_t *order, unsigned char *used /* n x n */) {
  size_t step;
  size_t i;
  size_t j;
  size_t best;
  size_t bs;
  size_t bd;
  size_t s;
  size_t d;
  size_t c;
  size_t colors = 0;
  for (i = 0; i < n; i++) {
    color[i] = NP_NONE;
  }
  memset(used, 0, n * n);
  for (step = 0; step < n; step++) {
    best = NP_NONE;
    bs = 0;
    bd = 0;
    for (i = 0; i < n; i++) {
      if (color[i] != NP_NONE) {
        continue;
      }
      s = 0;
      d = 0;
      for (c = 0; c < n; c++) {
        s += used[i * n + c];
      }
      for (j = 0; j < n; j++) {
        d += color[j] == NP_NONE && j != i && np_adj(adj, n, i, j);
      }
      if (best == NP_NONE || s > bs || (s == bs && d > bd)) {
        best = i;
        bs = s;
        bd = d;
      }
    }
    for (c = 0; used[best * n + c]; c++) {
    }
    color[best] = c;
    if (c + 1 > colors) {
      colors = c + 1;
    }
    if (order != NULL) {
      order[step] = best;
    }
    for (j = 0; j < n; j++) {
      if (np_adj(adj, n, best, j) || np_adj(adj, n, j, best)) {
        used[j * n + c] = 1;
      }
    }
  }
  return colors;
}

ds_status_t ds_color_greedy(const unsigned char *adj, size_t n,
                            ds_color_order_t order, size_t *color,
                            size_t *out_colors, ds_context_t *context) {
  unsigned char *used;
  size_t i;
  size_t j;
  size_t c;
  size_t colors = 0;
  size_t sq;
  if ((n > 0 && (adj == NULL || color == NULL)) || out_colors == NULL) {
    return DS_ERR_NULL;
  }
  if (np_mul(n, n, &sq) != DS_OK) {
    return DS_ERR_OVERFLOW;
  }
  *out_colors = 0;
  if (n == 0) {
    return DS_OK;
  }
  used = (unsigned char *)ds_context_alloc(context, sq);
  if (used == NULL) {
    return DS_ERR_ALLOC;
  }
  if (order == DS_COLOR_DSATUR) {
    colors = np_dsatur(adj, n, color, NULL, used);
  } else {
    for (i = 0; i < n; i++) {
      memset(used, 0, n);
      for (j = 0; j < i; j++) {
        if (np_adj(adj, n, i, j) || np_adj(adj, n, j, i)) {
          used[color[j]] = 1;
        }
      }
      for (c = 0; used[c]; c++) {
      }
      color[i] = c;
      if (c + 1 > colors) {
        colors = c + 1;
      }
    }
  }
  ds_context_free(context, used);
  *out_colors = colors;
  return DS_OK;
}

/* Backtracking k-colouring along a fixed order; colours of earlier
 * vertices only, symmetric colours broken by "at most max so far + 1". */
static ds_status_t np_kcolor(const unsigned char *adj, size_t n, size_t k,
                             const size_t *order, size_t *color,
                             size_t *budget) {
  size_t *next; /* next colour to try per position */
  size_t *maxc; /* colours used before position */
  size_t pos = 0;
  size_t v;
  size_t c;
  size_t i;
  bool ok;
  ds_status_t st = DS_NOT_FOUND;
  next = color + n; /* scratch laid out by the caller */
  maxc = next + n;
  for (i = 0; i < n; i++) {
    color[i] = NP_NONE;
  }
  next[0] = 0;
  maxc[0] = 0;
  for (;;) {
    if (pos == n) {
      return DS_OK;
    }
    v = order[pos];
    ok = false;
    while (next[pos] < k && next[pos] <= maxc[pos]) {
      c = next[pos]++;
      if (*budget != 0 && --*budget == 0) {
        return DS_STOP;
      }
      ok = true;
      for (i = 0; i < pos && ok; i++) {
        size_t u = order[i];
        if (color[u] == c && (np_adj(adj, n, u, v) || np_adj(adj, n, v, u))) {
          ok = false;
        }
      }
      if (ok) {
        color[v] = c;
        break;
      }
    }
    if (ok) {
      pos++;
      if (pos < n) {
        next[pos] = 0;
        maxc[pos] = maxc[pos - 1] > color[v] + 1 ? maxc[pos - 1] : color[v] + 1;
      }
      continue;
    }
    color[v] = NP_NONE;
    if (pos == 0) {
      return st;
    }
    pos--;
    color[order[pos]] = NP_NONE;
  }
}

ds_status_t ds_k_coloring(const unsigned char *adj, size_t n, size_t k,
                          size_t *color, size_t budget, ds_context_t *context) {
  size_t *scratch;
  unsigned char *used;
  size_t sq;
  ds_status_t st;
  size_t b = budget;
  if (n > 0 && (adj == NULL || color == NULL)) {
    return DS_ERR_NULL;
  }
  if (n == 0) {
    return DS_OK;
  }
  if (k == 0) {
    return DS_NOT_FOUND;
  }
  if (np_mul(n, n, &sq) != DS_OK || n > ((size_t)-1) / (4 * sizeof(size_t))) {
    return DS_ERR_OVERFLOW;
  }
  scratch = (size_t *)ds_context_alloc(context, 4 * n * sizeof(size_t));
  used = (unsigned char *)ds_context_alloc(context, sq);
  if (scratch == NULL || used == NULL) {
    ds_context_free(context, scratch);
    ds_context_free(context, used);
    return DS_ERR_ALLOC;
  }
  /* DSATUR gives the order (scratch + 3n) and a quick answer */
  if (np_dsatur(adj, n, scratch, scratch + 3 * n, used) <= k) {
    memcpy(color, scratch, n * sizeof(size_t));
    st = DS_OK;
  } else {
    st = np_kcolor(adj, n, k, scratch + 3 * n, scratch, &b);
    if (st == DS_OK) {
      memcpy(color, scratch, n * sizeof(size_t));
    }
  }
  ds_context_free(context, scratch);
  ds_context_free(context, used);
  return st;
}

ds_status_t ds_chromatic_number(const unsigned char *adj, size_t n,
                                size_t *color, size_t *out_chi, size_t budget,
                                ds_context_t *context) {
  size_t *clique;
  size_t lb;
  size_t ub;
  size_t k;
  ds_status_t st;
  if ((n > 0 && (adj == NULL || color == NULL)) || out_chi == NULL) {
    return DS_ERR_NULL;
  }
  *out_chi = 0;
  if (n == 0) {
    return DS_OK;
  }
  st = ds_color_greedy(adj, n, DS_COLOR_DSATUR, color, &ub, context);
  if (st != DS_OK) {
    return st;
  }
  clique = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  if (clique == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ds_max_clique(adj, n, clique, &lb, context);
  ds_context_free(context, clique);
  if (st != DS_OK) {
    return st;
  }
  for (k = lb; k < ub; k++) {
    st = ds_k_coloring(adj, n, k, color, budget, context);
    if (st == DS_OK) {
      *out_chi = k;
      return DS_OK;
    }
    if (st != DS_NOT_FOUND) {
      /* budget exhausted: the DSATUR colouring is still valid */
      (void)ds_color_greedy(adj, n, DS_COLOR_DSATUR, color, &ub, context);
      *out_chi = ub;
      return st;
    }
  }
  st = ds_color_greedy(adj, n, DS_COLOR_DSATUR, color, &ub, context);
  *out_chi = ub;
  return st;
}

/* ---- edge colouring
 * ---------------------------------------------------------- */

typedef struct np_ec {
  size_t n;
  size_t colors; /* Delta + 1 */
  size_t *col;   /* n x n edge colours, 0 = uncoloured */
  size_t *at;    /* n x (colors + 1): neighbour using colour c, or NONE */
} np_ec_t;

static bool ec_free(const np_ec_t *e, size_t x, size_t c) {
  return e->at[x * (e->colors + 1) + c] == NP_NONE;
}

static void ec_set(np_ec_t *e, size_t u, size_t v, size_t c) {
  size_t old = e->col[u * e->n + v];
  if (old != 0) {
    e->at[u * (e->colors + 1) + old] = NP_NONE;
    e->at[v * (e->colors + 1) + old] = NP_NONE;
  }
  e->col[u * e->n + v] = e->col[v * e->n + u] = c;
  if (c != 0) {
    e->at[u * (e->colors + 1) + c] = v;
    e->at[v * (e->colors + 1) + c] = u;
  }
}

static size_t ec_some_free(const np_ec_t *e, size_t x) {
  size_t c;
  for (c = 1; c <= e->colors; c++) {
    if (ec_free(e, x, c)) {
      return c;
    }
  }
  return 0;
}

/* Swap colours c and d along the maximal c/d path starting at u (u has c
 * free, so the path starts with a d edge). */
static void ec_invert(np_ec_t *e, size_t u, size_t c, size_t d, size_t *path) {
  size_t len = 0;
  size_t x = u;
  size_t want = d;
  size_t y;
  size_t i;
  while ((y = e->at[x * (e->colors + 1) + want]) != NP_NONE) {
    path[len++] = x;
    x = y;
    want = want == d ? c : d;
    if (len > e->n) {
      break;
    }
  }
  path[len] = x;
  /* clear then recolour so the colour tables never clash */
  for (i = 0; i < len; i++) {
    ec_set(e, path[i], path[i + 1], 0);
  }
  want = c;
  for (i = 0; i < len; i++) {
    ec_set(e, path[i], path[i + 1], want);
    want = want == d ? c : d;
  }
}

ds_status_t ds_edge_color(const unsigned char *adj, size_t n, size_t *ecolor,
                          size_t *out_colors, ds_context_t *context) {
  np_ec_t e;
  size_t *fan;
  size_t *path;
  unsigned char *infan;
  size_t delta = 0;
  size_t u;
  size_t v;
  size_t i;
  size_t k;
  size_t c;
  size_t d;
  size_t w;
  size_t deg;
  size_t used = 0;
  size_t sq;
  bool grown;
  if ((n > 0 && (adj == NULL || ecolor == NULL)) || out_colors == NULL) {
    return DS_ERR_NULL;
  }
  if (np_mul(n, n, &sq) != DS_OK) {
    return DS_ERR_OVERFLOW;
  }
  *out_colors = 0;
  if (n == 0) {
    return DS_OK;
  }
  for (u = 0; u < n; u++) {
    deg = 0;
    for (v = 0; v < n; v++) {
      deg += v != u && (adj[u * n + v] || adj[v * n + u]);
    }
    if (deg > delta) {
      delta = deg;
    }
  }
  e.n = n;
  e.colors = delta + 1;
  e.col = ecolor;
  e.at =
      (size_t *)ds_context_alloc(context, n * (e.colors + 1) * sizeof(size_t));
  fan = (size_t *)ds_context_alloc(context, (2 * n + 2) * sizeof(size_t));
  infan = (unsigned char *)ds_context_alloc(context, n);
  if (e.at == NULL || fan == NULL || infan == NULL) {
    ds_context_free(context, e.at);
    ds_context_free(context, fan);
    ds_context_free(context, infan);
    return DS_ERR_ALLOC;
  }
  path = fan + n;
  for (i = 0; i < sq; i++) {
    ecolor[i] = 0;
  }
  for (i = 0; i < n * (e.colors + 1); i++) {
    e.at[i] = NP_NONE;
  }
  for (u = 0; u < n; u++) {
    for (v = u + 1; v < n; v++) {
      if (!(adj[u * n + v] || adj[v * n + u])) {
        continue;
      }
      /* maximal fan of u starting at v */
      memset(infan, 0, n);
      k = 0;
      fan[k++] = v;
      infan[v] = 1;
      do {
        grown = false;
        for (w = 0; w < n; w++) {
          size_t cw = ecolor[u * n + w];
          if (!infan[w] && cw != 0 && ec_free(&e, fan[k - 1], cw)) {
            fan[k++] = w;
            infan[w] = 1;
            grown = true;
            break;
          }
        }
      } while (grown);
      c = ec_some_free(&e, u);
      d = ec_some_free(&e, fan[k - 1]);
      if (c != d && !ec_free(&e, u, d)) {
        ec_invert(&e, u, c, d, path);
      }
      /* the first fan vertex w with d free (the prefix stays a fan) */
      for (i = 0; i < k; i++) {
        if (ec_free(&e, fan[i], d)) {
          break;
        }
        if (i + 1 < k && !ec_free(&e, fan[i], ecolor[u * n + fan[i + 1]])) {
          i = k;
          break;
        }
      }
      if (i >= k) {
        i = k - 1;
      }
      /* rotate the fan prefix fan[0..i] and colour (u, fan[i]) with d */
      for (w = 0; w < i; w++) {
        size_t nc = ecolor[u * n + fan[w + 1]];
        ec_set(&e, u, fan[w + 1], 0);
        ec_set(&e, u, fan[w], nc);
      }
      ec_set(&e, u, fan[i], d);
    }
  }
  for (i = 0; i < sq; i++) {
    if (ecolor[i] > used) {
      used = ecolor[i];
    }
  }
  ds_context_free(context, e.at);
  ds_context_free(context, fan);
  ds_context_free(context, infan);
  *out_colors = used;
  return DS_OK;
}

ds_status_t ds_chromatic_index(const unsigned char *adj, size_t n,
                               size_t *ecolor, size_t *out_index, size_t budget,
                               ds_context_t *context) {
  size_t *eu;
  size_t *ev;
  size_t *next;
  size_t m = 0;
  size_t delta = 0;
  size_t u;
  size_t v;
  size_t deg;
  size_t pos;
  size_t i;
  size_t c;
  size_t used;
  size_t sq;
  bool ok;
  ds_status_t st;
  if ((n > 0 && (adj == NULL || ecolor == NULL)) || out_index == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_edge_color(adj, n, ecolor, &used, context);
  if (st != DS_OK || n == 0) {
    *out_index = used;
    return st;
  }
  sq = n * n;
  for (u = 0; u < n; u++) {
    deg = 0;
    for (v = 0; v < n; v++) {
      deg += v != u && (adj[u * n + v] || adj[v * n + u]);
    }
    if (deg > delta) {
      delta = deg;
    }
    for (v = u + 1; v < n; v++) {
      m += adj[u * n + v] || adj[v * n + u];
    }
  }
  if (used <= delta) {
    *out_index = used;
    return DS_OK;
  }
  /* try Delta colours by backtracking over the edges */
  eu = (size_t *)ds_context_alloc(context, (3 * m + 1) * sizeof(size_t));
  if (eu == NULL) {
    return DS_ERR_ALLOC;
  }
  ev = eu + m;
  next = ev + m;
  m = 0;
  for (u = 0; u < n; u++) {
    for (v = u + 1; v < n; v++) {
      if (adj[u * n + v] || adj[v * n + u]) {
        eu[m] = u;
        ev[m++] = v;
      }
    }
  }
  {
    size_t *col = (size_t *)ds_context_calloc(context, m + 1, sizeof(size_t));
    if (col == NULL) {
      ds_context_free(context, eu);
      return DS_ERR_ALLOC;
    }
    pos = 0;
    next[0] = 1;
    st = DS_NOT_FOUND;
    for (;;) {
      if (pos == m) {
        st = DS_OK;
        break;
      }
      ok = false;
      while (next[pos] <= delta) {
        c = next[pos]++;
        if (budget != 0 && --budget == 0) {
          st = DS_STOP;
          break;
        }
        ok = true;
        for (i = 0; i < pos && ok; i++) {
          if (col[i] == c && (eu[i] == eu[pos] || eu[i] == ev[pos] ||
                              ev[i] == eu[pos] || ev[i] == ev[pos])) {
            ok = false;
          }
        }
        if (ok) {
          col[pos] = c;
          break;
        }
      }
      if (st == DS_STOP) {
        break;
      }
      if (ok) {
        pos++;
        if (pos < m) {
          next[pos] = 1;
        }
        continue;
      }
      if (pos == 0 || pos == 1) {
        /* the first edge's colour is arbitrary by symmetry */
        break;
      }
      pos--;
    }
    if (st == DS_OK) {
      for (i = 0; i < sq; i++) {
        ecolor[i] = 0;
      }
      for (i = 0; i < m; i++) {
        ecolor[eu[i] * n + ev[i]] = ecolor[ev[i] * n + eu[i]] = col[i];
      }
      *out_index = delta;
    } else {
      *out_index = used; /* Delta + 1, the Misra-Gries colouring */
      if (st == DS_NOT_FOUND) {
        st = DS_OK;
      }
    }
    ds_context_free(context, col);
  }
  ds_context_free(context, eu);
  return st;
}

/* ---- Hamiltonian paths and cycles ------------------------------------------
 */

static ds_status_t np_ham_dp(const unsigned char *adj, size_t n, bool cycle,
                             size_t *order, ds_context_t *context) {
  unsigned long *dp; /* per mask: set of possible end vertices */
  unsigned long full;
  unsigned long mask;
  unsigned long ends;
  size_t v;
  size_t u;
  size_t i;
  size_t pos;
  if (n > 20) {
    return DS_ERR_UNSUPPORTED;
  }
  full = (1UL << n) - 1UL;
  dp = (unsigned long *)ds_context_calloc(context, (size_t)full + 1,
                                          sizeof(unsigned long));
  if (dp == NULL) {
    return DS_ERR_ALLOC;
  }
  for (v = 0; v < n; v++) {
    if (!cycle || v == 0) {
      dp[1UL << v] = 1UL << v;
    }
  }
  for (mask = 1; mask <= full; mask++) {
    if (dp[mask] == 0) {
      continue;
    }
    for (u = 0; u < n; u++) {
      if (!(dp[mask] & (1UL << u))) {
        continue;
      }
      for (v = 0; v < n; v++) {
        if (!(mask & (1UL << v)) && adj[u * n + v]) {
          dp[mask | (1UL << v)] |= 1UL << v;
        }
      }
    }
  }
  ends = dp[full];
  if (cycle) {
    unsigned long ok = 0;
    for (v = 0; v < n; v++) {
      if ((ends & (1UL << v)) && (n == 1 || adj[v * n + 0])) {
        ok |= 1UL << v;
      }
    }
    ends = ok;
  }
  if (ends == 0) {
    ds_context_free(context, dp);
    return DS_NOT_FOUND;
  }
  /* walk back */
  for (v = 0; !(ends & (1UL << v)); v++) {
  }
  mask = full;
  for (pos = n; pos-- > 0;) {
    order[pos] = v;
    if (pos == 0) {
      break;
    }
    mask &= ~(1UL << v);
    for (i = 0; i < n; i++) {
      if ((dp[mask] & (1UL << i)) && adj[i * n + v]) {
        break;
      }
    }
    v = i;
  }
  ds_context_free(context, dp);
  return DS_OK;
}

static ds_status_t np_ham_bt(const unsigned char *adj, size_t n, bool cycle,
                             size_t *order, size_t budget,
                             ds_context_t *context) {
  size_t *next; /* candidate index per position */
  unsigned char *used;
  size_t pos;
  size_t start;
  size_t v;
  size_t c;
  ds_status_t st = DS_NOT_FOUND;
  next = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  used = (unsigned char *)ds_context_calloc(context, n, 1);
  if (next == NULL || used == NULL) {
    ds_context_free(context, next);
    ds_context_free(context, used);
    return DS_ERR_ALLOC;
  }
  for (start = 0; start < (cycle ? 1 : n) && st == DS_NOT_FOUND; start++) {
    memset(used, 0, n);
    order[0] = start;
    used[start] = 1;
    pos = 1;
    if (n == 1) {
      st = DS_OK;
      break;
    }
    next[1] = 0;
    for (;;) {
      if (pos == n) {
        if (!cycle || adj[order[n - 1] * n + order[0]]) {
          st = DS_OK;
          break;
        }
        pos--;
        used[order[pos]] = 0;
        continue;
      }
      v = order[pos - 1];
      c = NP_NONE;
      while (next[pos] < n) {
        size_t w = next[pos]++;
        if (!used[w] && adj[v * n + w]) {
          c = w;
          break;
        }
      }
      if (budget != 0 && --budget == 0) {
        st = DS_STOP;
        break;
      }
      if (c != NP_NONE) {
        order[pos] = c;
        used[c] = 1;
        pos++;
        if (pos < n) {
          next[pos] = 0;
        }
        continue;
      }
      if (pos == 1) {
        break;
      }
      pos--;
      used[order[pos]] = 0;
    }
  }
  ds_context_free(context, next);
  ds_context_free(context, used);
  return st;
}

ds_status_t ds_hamiltonian(const unsigned char *adj, size_t n, bool cycle,
                           ds_ham_method_t method, size_t *order, size_t budget,
                           ds_context_t *context) {
  if (n > 0 && (adj == NULL || order == NULL)) {
    return DS_ERR_NULL;
  }
  if (n == 0) {
    return DS_NOT_FOUND;
  }
  if (method == DS_HAM_DP) {
    return np_ham_dp(adj, n, cycle, order, context);
  }
  if (method == DS_HAM_BACKTRACK) {
    return np_ham_bt(adj, n, cycle, order, budget, context);
  }
  return DS_ERR_INVALID;
}

/* ---- path cover of a DAG
 * ----------------------------------------------------- */

/* Kahn's algorithm on the (directed) graph without removed vertices. */
static bool np_acyclic_directed(const unsigned char *adj, size_t n,
                                const unsigned char *removed, size_t *indeg,
                                size_t *queue) {
  size_t i;
  size_t j;
  size_t head = 0;
  size_t tail = 0;
  size_t live = 0;
  for (j = 0; j < n; j++) {
    indeg[j] = 0;
  }
  for (i = 0; i < n; i++) {
    if (removed != NULL && removed[i]) {
      continue;
    }
    live++;
    for (j = 0; j < n; j++) {
      if ((removed == NULL || !removed[j]) && adj[i * n + j]) {
        indeg[j]++;
      }
    }
  }
  for (i = 0; i < n; i++) {
    if ((removed == NULL || !removed[i]) && indeg[i] == 0) {
      queue[tail++] = i;
    }
  }
  while (head < tail) {
    i = queue[head++];
    for (j = 0; j < n; j++) {
      if ((removed == NULL || !removed[j]) && adj[i * n + j] &&
          --indeg[j] == 0) {
        queue[tail++] = j;
      }
    }
  }
  return tail == live;
}

ds_status_t ds_path_cover(const unsigned char *adj, size_t n, size_t *path_id,
                          size_t *next, size_t *out_paths,
                          ds_context_t *context) {
  size_t *mr; /* right vertex -> matched left */
  size_t *ml; /* left vertex -> matched right */
  size_t *prev;
  size_t *queue;
  unsigned char *seen;
  size_t u;
  size_t v;
  size_t head;
  size_t tail;
  size_t paths = 0;
  size_t x;
  ds_status_t st = DS_OK;
  if ((n > 0 && (adj == NULL || path_id == NULL || next == NULL)) ||
      out_paths == NULL) {
    return DS_ERR_NULL;
  }
  *out_paths = 0;
  if (n == 0) {
    return DS_OK;
  }
  mr = (size_t *)ds_context_alloc(context, 5 * n * sizeof(size_t));
  seen = (unsigned char *)ds_context_alloc(context, n);
  if (mr == NULL || seen == NULL) {
    ds_context_free(context, mr);
    ds_context_free(context, seen);
    return DS_ERR_ALLOC;
  }
  ml = mr + n;
  prev = ml + n;
  queue = prev + n;
  if (!np_acyclic_directed(adj, n, NULL, queue + n, queue)) {
    st = DS_ERR_INVALID;
    goto out;
  }
  for (v = 0; v < n; v++) {
    mr[v] = ml[v] = NP_NONE;
  }
  /* augmenting paths by BFS over left vertices (Kuhn, iterative) */
  for (u = 0; u < n; u++) {
    memset(seen, 0, n);
    head = tail = 0;
    queue[tail++] = u;
    x = NP_NONE;
    while (head < tail && x == NP_NONE) {
      size_t l = queue[head++];
      for (v = 0; v < n; v++) {
        if (!adj[l * n + v] || seen[v]) {
          continue;
        }
        seen[v] = 1;
        prev[v] = l;
        if (mr[v] == NP_NONE) {
          x = v;
          break;
        }
        queue[tail++] = mr[v];
      }
    }
    /* flip the alternating path ending at right vertex x */
    while (x != NP_NONE) {
      size_t l = prev[x];
      size_t old = ml[l];
      mr[x] = l;
      ml[l] = x;
      x = l == u ? NP_NONE : old;
    }
  }
  /* paths start at vertices nobody is matched into */
  for (v = 0; v < n; v++) {
    next[v] = ml[v];
    path_id[v] = NP_NONE;
  }
  for (v = 0; v < n; v++) {
    if (mr[v] == NP_NONE) {
      for (x = v; x != NP_NONE; x = next[x]) {
        path_id[x] = paths;
      }
      paths++;
    }
  }
  *out_paths = paths;
out:
  ds_context_free(context, mr);
  ds_context_free(context, seen);
  return st;
}

/* ---- feedback vertex set ----------------------------------------------------
 */

static size_t np_uf_find(size_t *uf, size_t x) {
  while (uf[x] != x) {
    uf[x] = uf[uf[x]];
    x = uf[x];
  }
  return x;
}

static bool np_acyclic_undirected(const unsigned char *adj, size_t n,
                                  const unsigned char *removed, size_t *uf) {
  size_t i;
  size_t j;
  size_t a;
  size_t b;
  for (i = 0; i < n; i++) {
    uf[i] = i;
  }
  for (i = 0; i < n; i++) {
    if (removed[i]) {
      continue;
    }
    if (adj[i * n + i]) {
      return false; /* a loop */
    }
    for (j = i + 1; j < n; j++) {
      if (removed[j] || !(adj[i * n + j] || adj[j * n + i])) {
        continue;
      }
      a = np_uf_find(uf, i);
      b = np_uf_find(uf, j);
      if (a == b) {
        return false;
      }
      uf[a] = b;
    }
  }
  return true;
}

static bool np_acyclic(const unsigned char *adj, size_t n, bool directed,
                       const unsigned char *removed, size_t *scratch) {
  return directed ? np_acyclic_directed(adj, n, removed, scratch, scratch + n)
                  : np_acyclic_undirected(adj, n, removed, scratch);
}

ds_status_t ds_feedback_vertex_set(const unsigned char *adj, size_t n,
                                   bool directed, ds_fvs_method_t method,
                                   unsigned char *in_set, size_t *out_size,
                                   ds_context_t *context) {
  size_t *scratch;
  size_t *comb;
  size_t k;
  size_t i;
  size_t j;
  size_t size = 0;
  bool found = false;
  ds_status_t st = DS_OK;
  if ((n > 0 && (adj == NULL || in_set == NULL)) || out_size == NULL) {
    return DS_ERR_NULL;
  }
  *out_size = 0;
  if (n == 0) {
    return DS_OK;
  }
  scratch = (size_t *)ds_context_alloc(context, 3 * n * sizeof(size_t));
  if (scratch == NULL) {
    return DS_ERR_ALLOC;
  }
  comb = scratch + 2 * n;
  memset(in_set, 0, n);
  if (method == DS_FVS_EXACT) {
    /* every subset of size k = 0, 1, ... in lexicographic order */
    for (k = 0; k <= n && !found; k++) {
      for (i = 0; i < k; i++) {
        comb[i] = i;
      }
      for (;;) {
        memset(in_set, 0, n);
        for (i = 0; i < k; i++) {
          in_set[comb[i]] = 1;
        }
        if (np_acyclic(adj, n, directed, in_set, scratch)) {
          found = true;
          size = k;
          break;
        }
        /* next combination */
        for (i = k; i-- > 0;) {
          if (comb[i] < n - k + i) {
            break;
          }
        }
        if (i == (size_t)-1 || k == 0) {
          break;
        }
        comb[i]++;
        for (j = i + 1; j < k; j++) {
          comb[j] = comb[j - 1] + 1;
        }
      }
    }
  } else if (method == DS_FVS_GREEDY) {
    unsigned char *gone = (unsigned char *)ds_context_calloc(context, n, 1);
    if (gone == NULL) {
      ds_context_free(context, scratch);
      return DS_ERR_ALLOC;
    }
    for (;;) {
      bool pruned = true;
      size_t best = NP_NONE;
      size_t bs = 0;
      /* drop vertices that cannot lie on a cycle */
      while (pruned) {
        pruned = false;
        for (i = 0; i < n; i++) {
          size_t din = 0;
          size_t dout = 0;
          if (gone[i]) {
            continue;
          }
          for (j = 0; j < n; j++) {
            if (gone[j] || j == i) {
              continue;
            }
            if (directed) {
              din += adj[j * n + i] != 0;
              dout += adj[i * n + j] != 0;
            } else {
              dout += adj[i * n + j] || adj[j * n + i];
            }
          }
          if (adj[i * n + i]) {
            continue; /* a loop keeps it */
          }
          if (directed ? (din == 0 || dout == 0) : dout <= 1) {
            gone[i] = 1;
            pruned = true;
          }
        }
      }
      for (i = 0; i < n; i++) {
        size_t din = 0;
        size_t dout = 0;
        size_t score;
        if (gone[i]) {
          continue;
        }
        for (j = 0; j < n; j++) {
          if (!gone[j]) {
            din += adj[j * n + i] != 0;
            dout += adj[i * n + j] != 0;
          }
        }
        score = directed ? din * dout : din + dout;
        if (adj[i * n + i]) {
          score = (size_t)-1;
        }
        if (best == NP_NONE || score > bs) {
          best = i;
          bs = score;
        }
      }
      if (best == NP_NONE) {
        break;
      }
      in_set[best] = 1;
      gone[best] = 1;
      size++;
    }
    /* drop redundant choices */
    for (i = 0; i < n; i++) {
      if (in_set[i]) {
        in_set[i] = 0;
        if (np_acyclic(adj, n, directed, in_set, scratch)) {
          size--;
        } else {
          in_set[i] = 1;
        }
      }
    }
    ds_context_free(context, gone);
    found = true;
  } else {
    st = DS_ERR_INVALID;
  }
  ds_context_free(context, scratch);
  *out_size = found ? size : 0;
  return st;
}

/* ---- isomorphism ------------------------------------------------------------
 */

typedef struct np_iso {
  const unsigned char *p;
  size_t np;
  const unsigned char *t;
  size_t nt;
  bool induced;
  bool exact; /* graph isomorphism: degrees equal */
  size_t *order;
  size_t *pdeg;
  size_t *tdeg;
} np_iso_t;

static bool iso_ok(const np_iso_t *s, size_t depth, size_t pv, size_t tv,
                   const size_t *map) {
  size_t i;
  size_t pu;
  size_t tu;
  if (s->exact ? s->pdeg[pv] != s->tdeg[tv] : s->pdeg[pv] > s->tdeg[tv]) {
    return false;
  }
  if ((s->p[pv * s->np + pv] != 0) != (s->t[tv * s->nt + tv] != 0) &&
      (s->induced || s->p[pv * s->np + pv])) {
    return false;
  }
  for (i = 0; i < depth; i++) {
    pu = s->order[i];
    tu = map[pu];
    if (s->p[pv * s->np + pu] && !s->t[tv * s->nt + tu]) {
      return false;
    }
    if (s->p[pu * s->np + pv] && !s->t[tu * s->nt + tv]) {
      return false;
    }
    if (s->induced && ((!s->p[pv * s->np + pu] && s->t[tv * s->nt + tu]) ||
                       (!s->p[pu * s->np + pv] && s->t[tu * s->nt + tv]))) {
      return false;
    }
  }
  return true;
}

static ds_status_t np_iso_run(np_iso_t *s, size_t *map, size_t budget,
                              ds_context_t *context) {
  size_t *next;
  unsigned char *used;
  unsigned char *placed;
  size_t depth = 0;
  size_t i;
  size_t j;
  size_t c;
  ds_status_t st = DS_NOT_FOUND;
  if (s->np == 0) {
    return DS_OK;
  }
  next =
      (size_t *)ds_context_alloc(context, (3 * s->np + s->nt) * sizeof(size_t));
  used = (unsigned char *)ds_context_calloc(context, s->nt + s->np, 1);
  if (next == NULL || used == NULL) {
    ds_context_free(context, next);
    ds_context_free(context, used);
    return DS_ERR_ALLOC;
  }
  s->order = next + s->np;
  s->pdeg = s->order + s->np;
  s->tdeg = s->pdeg + s->np;
  placed = used + s->nt;
  for (i = 0; i < s->np; i++) {
    s->pdeg[i] = 0;
    for (j = 0; j < s->np; j++) {
      s->pdeg[i] += (j != i) && (s->p[i * s->np + j] || s->p[j * s->np + i]);
    }
  }
  for (i = 0; i < s->nt; i++) {
    s->tdeg[i] = 0;
    for (j = 0; j < s->nt; j++) {
      s->tdeg[i] += (j != i) && (s->t[i * s->nt + j] || s->t[j * s->nt + i]);
    }
  }
  /* order: repeatedly the unplaced vertex with most placed neighbours
   * (ties: highest degree), keeping the search connected */
  for (i = 0; i < s->np; i++) {
    size_t best = NP_NONE;
    size_t bc = 0;
    for (j = 0; j < s->np; j++) {
      size_t conn = 0;
      size_t k;
      if (placed[j]) {
        continue;
      }
      for (k = 0; k < i; k++) {
        conn += s->p[j * s->np + s->order[k]] || s->p[s->order[k] * s->np + j];
      }
      if (best == NP_NONE || conn > bc ||
          (conn == bc && s->pdeg[j] > s->pdeg[best])) {
        best = j;
        bc = conn;
      }
    }
    s->order[i] = best;
    placed[best] = 1;
  }
  next[0] = 0;
  for (;;) {
    size_t pv;
    if (depth == s->np) {
      st = DS_OK;
      break;
    }
    pv = s->order[depth];
    c = NP_NONE;
    while (next[depth] < s->nt) {
      size_t tv = next[depth]++;
      if (budget != 0 && --budget == 0) {
        st = DS_STOP;
        break;
      }
      if (!used[tv] && iso_ok(s, depth, pv, tv, map)) {
        c = tv;
        break;
      }
    }
    if (st == DS_STOP) {
      break;
    }
    if (c != NP_NONE) {
      map[pv] = c;
      used[c] = 1;
      depth++;
      if (depth < s->np) {
        next[depth] = 0;
      }
      continue;
    }
    if (depth == 0) {
      break;
    }
    depth--;
    used[map[s->order[depth]]] = 0;
  }
  ds_context_free(context, next);
  ds_context_free(context, used);
  return st;
}

ds_status_t ds_graph_isomorphism(const unsigned char *a, const unsigned char *b,
                                 size_t n, size_t *map, size_t budget,
                                 ds_context_t *context) {
  np_iso_t s;
  size_t ea = 0;
  size_t eb = 0;
  size_t i;
  if (n > 0 && (a == NULL || b == NULL || map == NULL)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n * n; i++) {
    ea += a[i] != 0;
    eb += b[i] != 0;
  }
  if (ea != eb) {
    return DS_NOT_FOUND;
  }
  s.p = a;
  s.np = n;
  s.t = b;
  s.nt = n;
  s.induced = true;
  s.exact = true;
  return np_iso_run(&s, map, budget, context);
}

ds_status_t ds_subgraph_isomorphism(const unsigned char *pattern, size_t np,
                                    const unsigned char *target, size_t nt,
                                    bool induced, size_t *map, size_t budget,
                                    ds_context_t *context) {
  np_iso_t s;
  if ((np > 0 && (pattern == NULL || map == NULL)) ||
      (nt > 0 && target == NULL)) {
    return DS_ERR_NULL;
  }
  if (np > nt) {
    return DS_NOT_FOUND;
  }
  s.p = pattern;
  s.np = np;
  s.t = target;
  s.nt = nt;
  s.induced = induced;
  s.exact = false;
  return np_iso_run(&s, map, budget, context);
}

/* ---- bisection --------------------------------------------------------------
 */

static double np_cut(const double *w, size_t n, const unsigned char *side) {
  size_t i;
  size_t j;
  double c = 0.0;
  for (i = 0; i < n; i++) {
    for (j = i + 1; j < n; j++) {
      if (side[i] != side[j]) {
        c += w[i * n + j];
      }
    }
  }
  return c;
}

ds_status_t ds_graph_bisection(const double *w, size_t n,
                               ds_bisect_method_t method, unsigned long seed,
                               unsigned char *side, double *out_cut,
                               ds_context_t *context) {
  size_t i;
  size_t j;
  size_t h;
  if ((n > 0 && (w == NULL || side == NULL)) || out_cut == NULL) {
    return DS_ERR_NULL;
  }
  *out_cut = 0.0;
  if (n == 0) {
    return DS_OK;
  }
  h = n / 2;
  if (method == DS_BISECT_EXACT) {
    size_t comb[32];
    unsigned char *cur;
    double best = 0.0;
    bool any = false;
    double c;
    if (n > 30) {
      return DS_ERR_UNSUPPORTED;
    }
    cur = (unsigned char *)ds_context_alloc(context, n);
    if (cur == NULL) {
      return DS_ERR_ALLOC;
    }
    for (i = 0; i < h; i++) {
      comb[i] = i;
    }
    for (;;) {
      memset(cur, 0, n);
      for (i = 0; i < h; i++) {
        cur[comb[i]] = 1;
      }
      c = np_cut(w, n, cur);
      if (!any || c < best) {
        best = c;
        any = true;
        memcpy(side, cur, n);
      }
      for (i = h; i-- > 0;) {
        if (comb[i] < n - h + i) {
          break;
        }
      }
      if (h == 0 || i == (size_t)-1) {
        break;
      }
      comb[i]++;
      for (j = i + 1; j < h; j++) {
        comb[j] = comb[j - 1] + 1;
      }
    }
    ds_context_free(context, cur);
    *out_cut = best;
    return DS_OK;
  }
  if (method == DS_BISECT_KERNIGHAN_LIN) {
    double *d;
    size_t *sa;
    size_t *sb;
    double *gain;
    unsigned char *locked;
    unsigned long x = seed == 0 ? 2463534242UL : seed;
    size_t *perm;
    size_t steps;
    size_t s;
    size_t best_k;
    double best_g;
    double run;
    bool improved = true;
    d = (double *)ds_context_alloc(context, 2 * n * sizeof(double));
    perm = (size_t *)ds_context_alloc(context, 3 * n * sizeof(size_t));
    locked = (unsigned char *)ds_context_alloc(context, n);
    if (d == NULL || perm == NULL || locked == NULL) {
      ds_context_free(context, d);
      ds_context_free(context, perm);
      ds_context_free(context, locked);
      return DS_ERR_ALLOC;
    }
    gain = d + n;
    sa = perm + n;
    sb = sa + n;
    /* random balanced start */
    for (i = 0; i < n; i++) {
      perm[i] = i;
    }
    for (i = n; i > 1; i--) {
      x ^= (x << 13) & 0xFFFFFFFFUL;
      x ^= x >> 17;
      x ^= (x << 5) & 0xFFFFFFFFUL;
      j = (size_t)(x % i);
      s = perm[i - 1];
      perm[i - 1] = perm[j];
      perm[j] = s;
    }
    for (i = 0; i < n; i++) {
      side[perm[i]] = (unsigned char)(i < h);
    }
    while (improved) {
      improved = false;
      memset(locked, 0, n);
      steps = h < n - h ? h : n - h;
      /* D = external - internal cost */
      for (i = 0; i < n; i++) {
        d[i] = 0.0;
        for (j = 0; j < n; j++) {
          if (j != i) {
            d[i] += side[i] != side[j] ? w[i * n + j] : -w[i * n + j];
          }
        }
      }
      for (s = 0; s < steps; s++) {
        size_t ba = NP_NONE;
        size_t bb = NP_NONE;
        double bg = 0.0;
        for (i = 0; i < n; i++) {
          if (locked[i] || side[i] != 1) {
            continue;
          }
          for (j = 0; j < n; j++) {
            double g;
            if (locked[j] || side[j] != 0) {
              continue;
            }
            g = d[i] + d[j] - 2.0 * w[i * n + j];
            if (ba == NP_NONE || g > bg) {
              ba = i;
              bb = j;
              bg = g;
            }
          }
        }
        sa[s] = ba;
        sb[s] = bb;
        gain[s] = bg;
        locked[ba] = locked[bb] = 1;
        /* update D as if a and b had swapped */
        for (i = 0; i < n; i++) {
          if (locked[i]) {
            continue;
          }
          if (side[i] == 1) {
            d[i] += 2.0 * w[i * n + ba] - 2.0 * w[i * n + bb];
          } else {
            d[i] += 2.0 * w[i * n + bb] - 2.0 * w[i * n + ba];
          }
        }
      }
      best_k = 0;
      best_g = 0.0;
      run = 0.0;
      for (s = 0; s < steps; s++) {
        run += gain[s];
        if (run > best_g + 1e-12) {
          best_g = run;
          best_k = s + 1;
        }
      }
      if (best_k > 0) {
        for (s = 0; s < best_k; s++) {
          side[sa[s]] = 0;
          side[sb[s]] = 1;
        }
        improved = true;
      }
    }
    ds_context_free(context, d);
    ds_context_free(context, perm);
    ds_context_free(context, locked);
    *out_cut = np_cut(w, n, side);
    return DS_OK;
  }
  return DS_ERR_INVALID;
}
