#include "markov.h"
#include <math.h>
#include <string.h>

#define MK_NONE (~(size_t)0)

static void *mk_alloc(ds_context_t *ctx, size_t count, size_t size) {
  if (count != 0 && size > (size_t)-1 / count) {
    return NULL;
  }
  return ds_context_alloc(ctx, count * size == 0 ? 1 : count * size);
}

ds_status_t ds_markov_init(ds_markov_chain_t *chain, size_t states,
                           ds_context_t *context) {
  size_t k;
  if (chain == NULL) {
    return DS_ERR_NULL;
  }
  if (states == 0) {
    return DS_ERR_INVALID;
  }
  if (states > (size_t)-1 / states) {
    return DS_ERR_OVERFLOW;
  }
  chain->p = (double *)mk_alloc(context, states * states, sizeof(double));
  if (chain->p == NULL) {
    return DS_ERR_ALLOC;
  }
  for (k = 0; k < states * states; k++) {
    chain->p[k] = 0.0;
  }
  chain->states = states;
  chain->context = context;
  return DS_OK;
}

ds_status_t ds_markov_init_matrix(ds_markov_chain_t *chain, size_t states,
                                  const double *matrix, ds_context_t *context) {
  ds_status_t st;
  if (matrix == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_markov_init(chain, states, context);
  if (st == DS_OK) {
    memcpy(chain->p, matrix, states * states * sizeof(double));
  }
  return st;
}

void ds_markov_destroy(ds_markov_chain_t *chain) {
  if (chain == NULL) {
    return;
  }
  ds_context_free(chain->context, chain->p);
  chain->p = NULL;
  chain->states = 0;
}

ds_status_t ds_markov_set(ds_markov_chain_t *chain, size_t from, size_t to,
                          double probability) {
  if (chain == NULL) {
    return DS_ERR_NULL;
  }
  if (from >= chain->states || to >= chain->states) {
    return DS_ERR_RANGE;
  }
  if (!(probability >= 0.0) || probability > 1.0) {
    return DS_ERR_INVALID;
  }
  chain->p[from * chain->states + to] = probability;
  return DS_OK;
}

double ds_markov_get(const ds_markov_chain_t *chain, size_t from, size_t to) {
  if (chain == NULL || from >= chain->states || to >= chain->states) {
    return 0.0;
  }
  return chain->p[from * chain->states + to];
}

ds_status_t ds_markov_validate(const ds_markov_chain_t *chain, double tol) {
  size_t i;
  size_t j;
  size_t n;
  if (chain == NULL) {
    return DS_ERR_NULL;
  }
  n = chain->states;
  for (i = 0; i < n; i++) {
    double sum = 0.0;
    for (j = 0; j < n; j++) {
      double v = chain->p[i * n + j];
      if (!(v >= 0.0)) {
        return DS_ERR_INVALID;
      }
      sum += v;
    }
    if (fabs(sum - 1.0) > tol) {
      return DS_ERR_INVALID;
    }
  }
  return DS_OK;
}

ds_status_t ds_markov_fit(ds_markov_chain_t *chain, const size_t *const *seqs,
                          const size_t *lengths, size_t count, double alpha) {
  size_t n;
  size_t i;
  size_t j;
  size_t k;
  if (chain == NULL || (count > 0 && (seqs == NULL || lengths == NULL))) {
    return DS_ERR_NULL;
  }
  if (!(alpha >= 0.0)) {
    return DS_ERR_INVALID;
  }
  n = chain->states;
  for (k = 0; k < count; k++) {
    if (lengths[k] > 0 && seqs[k] == NULL) {
      return DS_ERR_NULL;
    }
    for (i = 0; i < lengths[k]; i++) {
      if (seqs[k][i] >= n) {
        return DS_ERR_RANGE;
      }
    }
  }
  for (i = 0; i < n * n; i++) {
    chain->p[i] = 0.0;
  }
  for (k = 0; k < count; k++) {
    for (i = 1; i < lengths[k]; i++) {
      chain->p[seqs[k][i - 1] * n + seqs[k][i]] += 1.0;
    }
  }
  for (i = 0; i < n; i++) {
    double total = 0.0;
    double *row = chain->p + i * n;
    for (j = 0; j < n; j++) {
      total += row[j];
    }
    total += alpha * (double)n;
    if (!(total > 0.0)) {
      row[i] = 1.0;
      continue;
    }
    for (j = 0; j < n; j++) {
      row[j] = (row[j] + alpha) / total;
    }
  }
  return DS_OK;
}

ds_status_t ds_markov_step(const ds_markov_chain_t *chain, size_t state,
                           ds_rng_t *rng, size_t *out) {
  const double *row;
  double u;
  double acc = 0.0;
  size_t j;
  size_t last = MK_NONE;
  if (chain == NULL || rng == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (state >= chain->states) {
    return DS_ERR_RANGE;
  }
  row = chain->p + state * chain->states;
  u = ds_rng_double(rng);
  for (j = 0; j < chain->states; j++) {
    if (row[j] > 0.0) {
      acc += row[j];
      last = j;
      if (u < acc) {
        *out = j;
        return DS_OK;
      }
    }
  }
  if (last == MK_NONE) {
    return DS_ERR_STATE;
  }
  *out = last; /* rounding: the row summed to slightly below u */
  return DS_OK;
}

ds_status_t ds_markov_walk(const ds_markov_chain_t *chain, size_t start,
                           size_t steps, ds_rng_t *rng, size_t *out) {
  size_t t;
  if (chain == NULL || rng == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (start >= chain->states) {
    return DS_ERR_RANGE;
  }
  out[0] = start;
  for (t = 0; t < steps; t++) {
    ds_status_t st = ds_markov_step(chain, out[t], rng, &out[t + 1]);
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
}

ds_status_t ds_markov_evolve(const ds_markov_chain_t *chain, const double *in,
                             size_t steps, double *out) {
  double *tmp;
  size_t n;
  size_t t;
  size_t i;
  size_t j;
  if (chain == NULL || in == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  n = chain->states;
  tmp = (double *)mk_alloc(chain->context, n, sizeof(double));
  if (tmp == NULL) {
    return DS_ERR_ALLOC;
  }
  memmove(out, in, n * sizeof(double));
  for (t = 0; t < steps; t++) {
    for (j = 0; j < n; j++) {
      tmp[j] = 0.0;
    }
    for (i = 0; i < n; i++) {
      double w = out[i];
      const double *row = chain->p + i * n;
      if (!(w > 0.0) && !(w < 0.0)) {
        continue;
      }
      for (j = 0; j < n; j++) {
        tmp[j] += w * row[j];
      }
    }
    memcpy(out, tmp, n * sizeof(double));
  }
  ds_context_free(chain->context, tmp);
  return DS_OK;
}

static void mk_matmul(const double *a, const double *b, double *c, size_t n) {
  size_t i;
  size_t j;
  size_t k;
  for (i = 0; i < n * n; i++) {
    c[i] = 0.0;
  }
  for (i = 0; i < n; i++) {
    for (k = 0; k < n; k++) {
      double v = a[i * n + k];
      if (!(v > 0.0) && !(v < 0.0)) {
        continue;
      }
      for (j = 0; j < n; j++) {
        c[i * n + j] += v * b[k * n + j];
      }
    }
  }
}

ds_status_t ds_markov_power(const ds_markov_chain_t *chain, unsigned long k,
                            double *out) {
  double *base;
  double *tmp;
  size_t n;
  size_t i;
  if (chain == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  n = chain->states;
  base = (double *)mk_alloc(chain->context, 2 * n * n, sizeof(double));
  if (base == NULL) {
    return DS_ERR_ALLOC;
  }
  tmp = base + n * n;
  memcpy(base, chain->p, n * n * sizeof(double));
  for (i = 0; i < n * n; i++) {
    out[i] = i % (n + 1) == 0 ? 1.0 : 0.0;
  }
  while (k > 0) {
    if (k & 1ul) {
      mk_matmul(out, base, tmp, n);
      memcpy(out, tmp, n * n * sizeof(double));
    }
    k >>= 1;
    if (k > 0) {
      mk_matmul(base, base, tmp, n);
      memcpy(base, tmp, n * n * sizeof(double));
    }
  }
  ds_context_free(chain->context, base);
  return DS_OK;
}

static size_t mk_gcd(size_t a, size_t b) {
  while (b != 0) {
    size_t t = a % b;
    a = b;
    b = t;
  }
  return a;
}

ds_status_t ds_markov_classes(const ds_markov_chain_t *chain, size_t *class_of,
                              size_t *out_classes, bool *closed,
                              size_t *period) {
  size_t n;
  size_t *index;
  size_t *low;
  size_t *stack;
  size_t *cs_v;
  size_t *cs_j;
  unsigned char *on;
  size_t counter = 0;
  size_t sp = 0;
  size_t classes = 0;
  size_t root;
  size_t i;
  size_t j;
  if (chain == NULL || class_of == NULL || out_classes == NULL) {
    return DS_ERR_NULL;
  }
  n = chain->states;
  index = (size_t *)mk_alloc(chain->context, 5 * n, sizeof(size_t));
  on = (unsigned char *)mk_alloc(chain->context, n, 1);
  if (index == NULL || on == NULL) {
    ds_context_free(chain->context, index);
    ds_context_free(chain->context, on);
    return DS_ERR_ALLOC;
  }
  low = index + n;
  stack = low + n;
  cs_v = stack + n;
  cs_j = cs_v + n;
  for (i = 0; i < n; i++) {
    index[i] = MK_NONE;
    on[i] = 0;
  }
  for (root = 0; root < n; root++) {
    size_t csp = 0;
    if (index[root] != MK_NONE) {
      continue;
    }
    cs_v[csp] = root;
    cs_j[csp++] = 0;
    index[root] = low[root] = counter++;
    stack[sp++] = root;
    on[root] = 1;
    while (csp > 0) {
      size_t v = cs_v[csp - 1];
      bool pushed = false;
      while (cs_j[csp - 1] < n) {
        size_t w = cs_j[csp - 1]++;
        if (!(chain->p[v * n + w] > 0.0)) {
          continue;
        }
        if (index[w] == MK_NONE) {
          index[w] = low[w] = counter++;
          stack[sp++] = w;
          on[w] = 1;
          cs_v[csp] = w;
          cs_j[csp++] = 0;
          pushed = true;
          break;
        }
        if (on[w] && index[w] < low[v]) {
          low[v] = index[w];
        }
      }
      if (pushed) {
        continue;
      }
      if (low[v] == index[v]) {
        size_t w;
        do {
          w = stack[--sp];
          on[w] = 0;
          class_of[w] = classes;
        } while (w != v);
        classes++;
      }
      csp--;
      if (csp > 0) {
        size_t u = cs_v[csp - 1];
        if (low[v] < low[u]) {
          low[u] = low[v];
        }
      }
    }
  }
  *out_classes = classes;
  if (closed != NULL) {
    size_t c;
    for (c = 0; c < classes; c++) {
      closed[c] = true;
    }
    for (i = 0; i < n; i++) {
      for (j = 0; j < n; j++) {
        if (chain->p[i * n + j] > 0.0 && class_of[i] != class_of[j]) {
          closed[class_of[i]] = false;
        }
      }
    }
  }
  if (period != NULL) {
    /* BFS levels inside each class from its first state; the period is
     * the gcd of level[u] + 1 - level[v] over in-class edges u -> v. */
    size_t *level = index;
    size_t *queue = stack;
    size_t c;
    for (c = 0; c < classes; c++) {
      period[c] = 0;
    }
    for (i = 0; i < n; i++) {
      level[i] = MK_NONE;
    }
    for (root = 0; root < n; root++) {
      size_t head = 0;
      size_t tail = 0;
      size_t g = 0;
      if (level[root] != MK_NONE) {
        continue;
      }
      level[root] = 0;
      queue[tail++] = root;
      while (head < tail) {
        size_t u = queue[head++];
        for (j = 0; j < n; j++) {
          size_t d;
          if (!(chain->p[u * n + j] > 0.0) || class_of[j] != class_of[u]) {
            continue;
          }
          if (level[j] == MK_NONE) {
            level[j] = level[u] + 1;
            queue[tail++] = j;
          }
          d = level[u] + 1 >= level[j] ? level[u] + 1 - level[j]
                                       : level[j] - level[u] - 1;
          g = mk_gcd(g, d);
        }
      }
      period[class_of[root]] = g;
    }
  }
  ds_context_free(chain->context, index);
  ds_context_free(chain->context, on);
  return DS_OK;
}

static bool mk_class_info(const ds_markov_chain_t *chain, size_t *classes,
                          size_t *period0) {
  size_t *cls;
  size_t *per;
  bool ok;
  if (chain == NULL || chain->states == 0) {
    return false;
  }
  cls = (size_t *)mk_alloc(chain->context, 2 * chain->states, sizeof(size_t));
  if (cls == NULL) {
    return false;
  }
  per = cls + chain->states;
  ok = ds_markov_classes(chain, cls, classes, NULL, per) == DS_OK;
  *period0 = per[0];
  ds_context_free(chain->context, cls);
  return ok;
}

bool ds_markov_is_irreducible(const ds_markov_chain_t *chain) {
  size_t classes = 0;
  size_t per = 0;
  return mk_class_info(chain, &classes, &per) && classes == 1;
}

bool ds_markov_is_ergodic(const ds_markov_chain_t *chain) {
  size_t classes = 0;
  size_t per = 0;
  return mk_class_info(chain, &classes, &per) && classes == 1 && per == 1;
}

/* Gaussian elimination with partial pivoting on an n x (n + m) row-major
 * system; on success the last m columns hold the solutions. */
static ds_status_t mk_solve(double *a, size_t n, size_t m) {
  size_t w = n + m;
  size_t col;
  size_t r;
  size_t c;
  for (col = 0; col < n; col++) {
    size_t piv = col;
    double best = fabs(a[col * w + col]);
    for (r = col + 1; r < n; r++) {
      double v = fabs(a[r * w + col]);
      if (v > best) {
        best = v;
        piv = r;
      }
    }
    if (!(best > 1e-300)) {
      return DS_ERR_STATE;
    }
    if (piv != col) {
      for (c = 0; c < w; c++) {
        double t = a[col * w + c];
        a[col * w + c] = a[piv * w + c];
        a[piv * w + c] = t;
      }
    }
    for (r = 0; r < n; r++) {
      double f;
      if (r == col) {
        continue;
      }
      f = a[r * w + col] / a[col * w + col];
      if (!(f > 0.0) && !(f < 0.0)) {
        continue;
      }
      for (c = col; c < w; c++) {
        a[r * w + c] -= f * a[col * w + c];
      }
    }
  }
  for (r = 0; r < n; r++) {
    double d = a[r * w + r];
    for (c = n; c < w; c++) {
      a[r * w + c] /= d;
    }
  }
  return DS_OK;
}

ds_status_t ds_markov_stationary(const ds_markov_chain_t *chain, double *out) {
  size_t n;
  size_t *cls;
  bool *closed;
  size_t *member;
  double *a;
  size_t classes = 0;
  size_t target = MK_NONE;
  size_t m = 0;
  size_t i;
  size_t j;
  size_t c;
  ds_status_t st;
  if (chain == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  n = chain->states;
  cls = (size_t *)mk_alloc(chain->context, 2 * n, sizeof(size_t));
  closed = (bool *)mk_alloc(chain->context, n, sizeof(bool));
  if (cls == NULL || closed == NULL) {
    ds_context_free(chain->context, cls);
    ds_context_free(chain->context, closed);
    return DS_ERR_ALLOC;
  }
  member = cls + n;
  st = ds_markov_classes(chain, cls, &classes, closed, NULL);
  for (c = 0; st == DS_OK && c < classes; c++) {
    if (closed[c]) {
      if (target != MK_NONE) {
        st = DS_ERR_STATE;
      }
      target = c;
    }
  }
  if (st == DS_OK) {
    for (i = 0; i < n; i++) {
      if (cls[i] == target) {
        member[m++] = i;
      }
    }
    /* pi restricted to the closed class: (P_C^T - I) pi = 0 with the last
     * equation replaced by sum(pi) = 1 */
    a = (double *)mk_alloc(chain->context, m * (m + 1), sizeof(double));
    if (a == NULL) {
      st = DS_ERR_ALLOC;
    } else {
      for (i = 0; i < m; i++) {
        for (j = 0; j < m; j++) {
          a[i * (m + 1) + j] =
              chain->p[member[j] * n + member[i]] - (i == j ? 1.0 : 0.0);
        }
        a[i * (m + 1) + m] = 0.0;
      }
      for (j = 0; j <= m; j++) {
        a[(m - 1) * (m + 1) + j] = 1.0;
      }
      st = mk_solve(a, m, 1);
      if (st == DS_OK) {
        for (i = 0; i < n; i++) {
          out[i] = 0.0;
        }
        for (i = 0; i < m; i++) {
          double v = a[i * (m + 1) + m];
          out[member[i]] = v < 0.0 ? 0.0 : v;
        }
      }
      ds_context_free(chain->context, a);
    }
  }
  ds_context_free(chain->context, cls);
  ds_context_free(chain->context, closed);
  return st;
}

ds_status_t ds_markov_absorption(const ds_markov_chain_t *chain, double *absorb,
                                 double *steps) {
  size_t n;
  size_t *cls;
  size_t *tindex;
  size_t *tlist;
  bool *closed;
  double *a = NULL;
  size_t classes = 0;
  size_t t = 0;
  size_t w;
  size_t i;
  size_t j;
  size_t c;
  ds_status_t st;
  if (chain == NULL) {
    return DS_ERR_NULL;
  }
  n = chain->states;
  cls = (size_t *)mk_alloc(chain->context, 3 * n, sizeof(size_t));
  closed = (bool *)mk_alloc(chain->context, n, sizeof(bool));
  if (cls == NULL || closed == NULL) {
    ds_context_free(chain->context, cls);
    ds_context_free(chain->context, closed);
    return DS_ERR_ALLOC;
  }
  tindex = cls + n;
  tlist = tindex + n;
  st = ds_markov_classes(chain, cls, &classes, closed, NULL);
  if (st != DS_OK) {
    goto done;
  }
  for (i = 0; i < n; i++) {
    tindex[i] = MK_NONE;
    if (!closed[cls[i]]) {
      tindex[i] = t;
      tlist[t++] = i;
    }
  }
  /* [I - Q | R | 1] over the transient states */
  w = t + classes + 1;
  if (t > 0) {
    a = (double *)mk_alloc(chain->context, t * w, sizeof(double));
    if (a == NULL) {
      st = DS_ERR_ALLOC;
      goto done;
    }
    for (i = 0; i < t * w; i++) {
      a[i] = 0.0;
    }
    for (i = 0; i < t; i++) {
      size_t s = tlist[i];
      a[i * w + i] = 1.0;
      for (j = 0; j < n; j++) {
        double v = chain->p[s * n + j];
        if (!(v > 0.0)) {
          continue;
        }
        if (tindex[j] != MK_NONE) {
          a[i * w + tindex[j]] -= v;
        } else {
          a[i * w + t + cls[j]] += v;
        }
      }
      a[i * w + t + classes] = 1.0;
    }
    st = mk_solve(a, t, classes + 1);
    if (st != DS_OK) {
      goto done;
    }
  }
  for (i = 0; i < n; i++) {
    if (absorb != NULL) {
      for (c = 0; c < classes; c++) {
        absorb[i * classes + c] = tindex[i] == MK_NONE
                                      ? (cls[i] == c ? 1.0 : 0.0)
                                      : a[tindex[i] * w + t + c];
      }
    }
    if (steps != NULL) {
      steps[i] = tindex[i] == MK_NONE ? 0.0 : a[tindex[i] * w + t + classes];
    }
  }
done:
  ds_context_free(chain->context, a);
  ds_context_free(chain->context, cls);
  ds_context_free(chain->context, closed);
  return st;
}

/* backward reachability: mark[v] |= 1 for every v reaching a marked seed
 * through states with block[v] == 0 (seeds themselves are given in mark) */
static void mk_backward(const ds_markov_chain_t *chain, unsigned char *mark,
                        const unsigned char *block, size_t *queue) {
  size_t n = chain->states;
  size_t head = 0;
  size_t tail = 0;
  size_t i;
  for (i = 0; i < n; i++) {
    if (mark[i]) {
      queue[tail++] = i;
    }
  }
  while (head < tail) {
    size_t v = queue[head++];
    for (i = 0; i < n; i++) {
      if (!mark[i] && !block[i] && chain->p[i * n + v] > 0.0) {
        mark[i] = 1;
        queue[tail++] = i;
      }
    }
  }
}

static ds_status_t mk_hit_system(const ds_markov_chain_t *chain,
                                 const unsigned char *unknown,
                                 const unsigned char *in_a, bool times,
                                 double *out) {
  size_t n = chain->states;
  size_t *idx;
  size_t *list;
  double *a;
  size_t u = 0;
  size_t i;
  size_t j;
  ds_status_t st;
  idx = (size_t *)mk_alloc(chain->context, 2 * n, sizeof(size_t));
  if (idx == NULL) {
    return DS_ERR_ALLOC;
  }
  list = idx + n;
  for (i = 0; i < n; i++) {
    idx[i] = MK_NONE;
    if (unknown[i]) {
      idx[i] = u;
      list[u++] = i;
    }
  }
  if (u == 0) {
    ds_context_free(chain->context, idx);
    return DS_OK;
  }
  a = (double *)mk_alloc(chain->context, u * (u + 1), sizeof(double));
  if (a == NULL) {
    ds_context_free(chain->context, idx);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < u * (u + 1); i++) {
    a[i] = 0.0;
  }
  for (i = 0; i < u; i++) {
    size_t s = list[i];
    a[i * (u + 1) + i] = 1.0;
    a[i * (u + 1) + u] = times ? 1.0 : 0.0;
    for (j = 0; j < n; j++) {
      double v = chain->p[s * n + j];
      if (!(v > 0.0)) {
        continue;
      }
      if (idx[j] != MK_NONE) {
        a[i * (u + 1) + idx[j]] -= v;
      } else if (!times && in_a[j]) {
        a[i * (u + 1) + u] += v;
      }
    }
  }
  st = mk_solve(a, u, 1);
  if (st == DS_OK) {
    for (i = 0; i < u; i++) {
      out[list[i]] = a[i * (u + 1) + u];
    }
  }
  ds_context_free(chain->context, a);
  ds_context_free(chain->context, idx);
  return st;
}

ds_status_t ds_markov_hitting(const ds_markov_chain_t *chain,
                              const bool *target, double *prob, double *time) {
  size_t n;
  unsigned char *in_a;
  unsigned char *reach;
  unsigned char *risky;
  unsigned char *unknown;
  unsigned char *none;
  size_t *queue;
  size_t i;
  ds_status_t st = DS_OK;
  if (chain == NULL || target == NULL) {
    return DS_ERR_NULL;
  }
  n = chain->states;
  in_a = (unsigned char *)mk_alloc(chain->context, 5 * n, 1);
  queue = (size_t *)mk_alloc(chain->context, n, sizeof(size_t));
  if (in_a == NULL || queue == NULL) {
    ds_context_free(chain->context, in_a);
    ds_context_free(chain->context, queue);
    return DS_ERR_ALLOC;
  }
  reach = in_a + n;
  risky = reach + n;
  unknown = risky + n;
  none = unknown + n;
  for (i = 0; i < n; i++) {
    in_a[i] = target[i] ? 1 : 0;
    reach[i] = in_a[i];
    none[i] = 0;
  }
  /* states that can reach A at all */
  mk_backward(chain, reach, none, queue);
  /* states that can, avoiding A, reach a state that never reaches A */
  for (i = 0; i < n; i++) {
    risky[i] = reach[i] ? 0 : 1;
  }
  mk_backward(chain, risky, in_a, queue);
  if (prob != NULL) {
    for (i = 0; i < n; i++) {
      prob[i] = in_a[i] ? 1.0 : 0.0;
      unknown[i] = 0;
      if (!in_a[i] && reach[i]) {
        if (risky[i]) {
          unknown[i] = 1;
        } else {
          prob[i] = 1.0;
        }
      }
    }
    st = mk_hit_system(chain, unknown, in_a, false, prob);
  }
  if (st == DS_OK && time != NULL) {
    for (i = 0; i < n; i++) {
      unknown[i] = !in_a[i] && reach[i] && !risky[i] ? 1 : 0;
      time[i] = in_a[i] ? 0.0 : unknown[i] ? 0.0 : HUGE_VAL;
    }
    st = mk_hit_system(chain, unknown, in_a, true, time);
  }
  ds_context_free(chain->context, in_a);
  ds_context_free(chain->context, queue);
  return st;
}

ds_status_t ds_markov_entropy_rate(const ds_markov_chain_t *chain,
                                   double *out) {
  double *pi;
  double h = 0.0;
  size_t n;
  size_t i;
  size_t j;
  ds_status_t st;
  if (chain == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  n = chain->states;
  pi = (double *)mk_alloc(chain->context, n, sizeof(double));
  if (pi == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ds_markov_stationary(chain, pi);
  if (st == DS_OK) {
    for (i = 0; i < n; i++) {
      for (j = 0; j < n; j++) {
        double v = chain->p[i * n + j];
        if (v > 0.0) {
          h -= pi[i] * v * log(v);
        }
      }
    }
    *out = h;
  }
  ds_context_free(chain->context, pi);
  return st;
}
