#include "hmm.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

static ds_status_t hm_check(const ds_hmm_t *h, const size_t *obs, size_t t) {
  size_t i;
  if (h == NULL || h->pi == NULL || h->a == NULL || h->b == NULL ||
      (t > 0 && obs == NULL)) {
    return DS_ERR_NULL;
  }
  if (h->states == 0 || h->symbols == 0 ||
      h->states > ((size_t)-1) / h->states / sizeof(double) ||
      h->states > ((size_t)-1) / h->symbols / sizeof(double)) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < t; i++) {
    if (obs[i] >= h->symbols) {
      return DS_ERR_RANGE;
    }
  }
  return DS_OK;
}

static bool hm_positive(double x) {
  return x > 0.0;
}

/* Forward pass into caller arrays (alpha T x N, scale T). */
static ds_status_t hm_forward(const ds_hmm_t *h, const size_t *obs, size_t t,
                              double *alpha, double *scale, double *loglik) {
  size_t n = h->states;
  size_t s;
  size_t i;
  size_t j;
  double c;
  double sum;
  double ll = 0.0;
  for (s = 0; s < t; s++) {
    c = 0.0;
    for (j = 0; j < n; j++) {
      if (s == 0) {
        sum = h->pi[j];
      } else {
        sum = 0.0;
        for (i = 0; i < n; i++) {
          sum += alpha[(s - 1) * n + i] * h->a[i * n + j];
        }
      }
      alpha[s * n + j] = sum * h->b[j * h->symbols + obs[s]];
      c += alpha[s * n + j];
    }
    if (!hm_positive(c)) {
      return DS_ERR_INVALID;
    }
    scale[s] = c;
    for (j = 0; j < n; j++) {
      alpha[s * n + j] /= c;
    }
    ll += log(c);
  }
  *loglik = ll;
  return DS_OK;
}

static void hm_backward(const ds_hmm_t *h, const size_t *obs, size_t t,
                        const double *scale, double *beta) {
  size_t n = h->states;
  size_t s;
  size_t i;
  size_t j;
  double sum;
  if (t == 0) {
    return;
  }
  for (i = 0; i < n; i++) {
    beta[(t - 1) * n + i] = 1.0;
  }
  for (s = t - 1; s-- > 0;) {
    for (i = 0; i < n; i++) {
      sum = 0.0;
      for (j = 0; j < n; j++) {
        sum += h->a[i * n + j] * h->b[j * h->symbols + obs[s + 1]] *
               beta[(s + 1) * n + j];
      }
      beta[s * n + i] = sum / scale[s + 1];
    }
  }
}

ds_status_t ds_hmm_forward(const ds_hmm_t *h, const size_t *obs, size_t t,
                           double *alpha, double *scale, double *out_loglik,
                           ds_context_t *context) {
  double *buf = NULL;
  double ll = 0.0;
  ds_status_t st = hm_check(h, obs, t);
  if (st != DS_OK) {
    return st;
  }
  if (out_loglik == NULL) {
    return DS_ERR_NULL;
  }
  if (alpha == NULL || scale == NULL) {
    if (t > ((size_t)-1) / (h->states + 1) / sizeof(double)) {
      return DS_ERR_OVERFLOW;
    }
    buf = (double *)ds_context_alloc(
        context, (t > 0 ? t : 1) * (h->states + 1) * sizeof(double));
    if (buf == NULL) {
      return DS_ERR_ALLOC;
    }
  }
  st = hm_forward(h, obs, t, alpha != NULL ? alpha : buf,
                  scale != NULL ? scale : buf + t * h->states, &ll);
  ds_context_free(context, buf);
  *out_loglik = ll;
  return st;
}

ds_status_t ds_hmm_backward(const ds_hmm_t *h, const size_t *obs, size_t t,
                            const double *scale, double *beta) {
  ds_status_t st = hm_check(h, obs, t);
  if (st != DS_OK) {
    return st;
  }
  if (t > 0 && (scale == NULL || beta == NULL)) {
    return DS_ERR_NULL;
  }
  hm_backward(h, obs, t, scale, beta);
  return DS_OK;
}

ds_status_t ds_hmm_posterior(const ds_hmm_t *h, const size_t *obs, size_t t,
                             double *gamma, double *out_loglik,
                             ds_context_t *context) {
  double *alpha;
  double *beta;
  double *scale;
  double ll = 0.0;
  double sum;
  size_t n;
  size_t s;
  size_t i;
  ds_status_t st = hm_check(h, obs, t);
  if (st != DS_OK) {
    return st;
  }
  if (t > 0 && gamma == NULL) {
    return DS_ERR_NULL;
  }
  n = h->states;
  if (t > ((size_t)-1) / (2 * n + 1) / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  alpha = (double *)ds_context_alloc(context, (t > 0 ? t : 1) * (2 * n + 1) *
                                                  sizeof(double));
  if (alpha == NULL) {
    return DS_ERR_ALLOC;
  }
  beta = alpha + t * n;
  scale = beta + t * n;
  st = hm_forward(h, obs, t, alpha, scale, &ll);
  if (st == DS_OK) {
    hm_backward(h, obs, t, scale, beta);
    for (s = 0; s < t; s++) {
      sum = 0.0;
      for (i = 0; i < n; i++) {
        gamma[s * n + i] = alpha[s * n + i] * beta[s * n + i];
        sum += gamma[s * n + i];
      }
      for (i = 0; i < n; i++) {
        gamma[s * n + i] /= sum;
      }
    }
  }
  ds_context_free(context, alpha);
  if (out_loglik != NULL) {
    *out_loglik = ll;
  }
  return st;
}

static double hm_log(double x) {
  return hm_positive(x) ? log(x) : -HUGE_VAL;
}

ds_status_t ds_hmm_viterbi(const ds_hmm_t *h, const size_t *obs, size_t t,
                           size_t *path, double *out_logprob,
                           ds_context_t *context) {
  double *delta;
  size_t *psi;
  size_t n;
  size_t s;
  size_t i;
  size_t j;
  size_t best;
  double v;
  double bv;
  ds_status_t st = hm_check(h, obs, t);
  if (st != DS_OK) {
    return st;
  }
  if ((t > 0 && path == NULL) || out_logprob == NULL) {
    return DS_ERR_NULL;
  }
  if (t == 0) {
    *out_logprob = 0.0;
    return DS_OK;
  }
  n = h->states;
  if (t > ((size_t)-1) / n / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  delta = (double *)ds_context_alloc(context, 2 * n * sizeof(double));
  psi = (size_t *)ds_context_alloc(context, t * n * sizeof(size_t));
  if (delta == NULL || psi == NULL) {
    ds_context_free(context, delta);
    ds_context_free(context, psi);
    return DS_ERR_ALLOC;
  }
  for (j = 0; j < n; j++) {
    delta[j] = hm_log(h->pi[j]) + hm_log(h->b[j * h->symbols + obs[0]]);
    psi[j] = 0;
  }
  for (s = 1; s < t; s++) {
    double *prev = delta + ((s - 1) % 2) * n;
    double *cur = delta + (s % 2) * n;
    for (j = 0; j < n; j++) {
      best = 0;
      bv = prev[0] + hm_log(h->a[j]);
      for (i = 1; i < n; i++) {
        v = prev[i] + hm_log(h->a[i * n + j]);
        if (v > bv) {
          bv = v;
          best = i;
        }
      }
      cur[j] = bv + hm_log(h->b[j * h->symbols + obs[s]]);
      psi[s * n + j] = best;
    }
  }
  {
    double *last = delta + ((t - 1) % 2) * n;
    best = 0;
    for (j = 1; j < n; j++) {
      if (last[j] > last[best]) {
        best = j;
      }
    }
    *out_logprob = last[best];
  }
  path[t - 1] = best;
  for (s = t - 1; s > 0; s--) {
    path[s - 1] = psi[s * n + path[s]];
  }
  ds_context_free(context, delta);
  ds_context_free(context, psi);
  return *out_logprob > -HUGE_VAL ? DS_OK : DS_ERR_INVALID;
}

ds_status_t ds_hmm_baum_welch(ds_hmm_t *h, const size_t *const *seqs,
                              const size_t *lens, size_t count,
                              size_t max_iterations, double tolerance,
                              double *out_loglik, size_t *out_iterations,
                              ds_context_t *context) {
  size_t n;
  size_t m;
  size_t maxt = 0;
  size_t q;
  size_t it;
  size_t s;
  size_t i;
  size_t j;
  size_t t;
  const size_t *o;
  double *alpha;
  double *beta;
  double *scale;
  double *npi;
  double *na;
  double *nb;
  double *den_a;
  double *den_b;
  double total = 0.0;
  double prev = -HUGE_VAL;
  double ll;
  double g;
  double xi;
  ds_status_t st;
  if (h == NULL || (count > 0 && (seqs == NULL || lens == NULL))) {
    return DS_ERR_NULL;
  }
  for (q = 0; q < count; q++) {
    st = hm_check(h, seqs[q], lens[q]);
    if (st != DS_OK) {
      return st;
    }
    if (lens[q] > maxt) {
      maxt = lens[q];
    }
  }
  if (count == 0 || maxt == 0) {
    return DS_ERR_INVALID;
  }
  n = h->states;
  m = h->symbols;
  if (maxt > ((size_t)-1) / (2 * n + 1) / sizeof(double) / 2) {
    return DS_ERR_OVERFLOW;
  }
  alpha = (double *)ds_context_alloc(
      context,
      (maxt * (2 * n + 1) + n + n * n + n * m + n + n) * sizeof(double));
  if (alpha == NULL) {
    return DS_ERR_ALLOC;
  }
  beta = alpha + maxt * n;
  scale = beta + maxt * n;
  npi = scale + maxt;
  na = npi + n;
  nb = na + n * n;
  den_a = nb + n * m;
  den_b = den_a + n;
  st = DS_OK;
  for (it = 0; it < max_iterations; it++) {
    memset(npi, 0, (n + n * n + n * m + 2 * n) * sizeof(double));
    total = 0.0;
    for (q = 0; q < count && st == DS_OK; q++) {
      o = seqs[q];
      t = lens[q];
      if (t == 0) {
        continue;
      }
      st = hm_forward(h, o, t, alpha, scale, &ll);
      if (st != DS_OK) {
        break;
      }
      total += ll;
      hm_backward(h, o, t, scale, beta);
      for (s = 0; s < t; s++) {
        for (i = 0; i < n; i++) {
          /* scaled alpha * beta is the posterior directly */
          g = alpha[s * n + i] * beta[s * n + i];
          if (s == 0) {
            npi[i] += g;
          }
          nb[i * m + o[s]] += g;
          den_b[i] += g;
          if (s + 1 < t) {
            den_a[i] += g;
            for (j = 0; j < n; j++) {
              xi = alpha[s * n + i] * h->a[i * n + j] * h->b[j * m + o[s + 1]] *
                   beta[(s + 1) * n + j] / scale[s + 1];
              na[i * n + j] += xi;
            }
          }
        }
      }
    }
    if (st != DS_OK) {
      break;
    }
    /* stop on convergence before overwriting the model */
    if (it > 0 && total - prev < tolerance) {
      prev = total;
      break;
    }
    prev = total;
    for (i = 0; i < n; i++) {
      h->pi[i] = npi[i] / (double)count;
      if (hm_positive(den_a[i])) {
        for (j = 0; j < n; j++) {
          h->a[i * n + j] = na[i * n + j] / den_a[i];
        }
      }
      if (hm_positive(den_b[i])) {
        for (j = 0; j < m; j++) {
          h->b[i * m + j] = nb[i * m + j] / den_b[i];
        }
      }
    }
  }
  if (st == DS_OK && it == max_iterations) {
    /* the model changed after the last evaluation: score it */
    prev = 0.0;
    for (q = 0; q < count && st == DS_OK; q++) {
      if (lens[q] > 0) {
        st = hm_forward(h, seqs[q], lens[q], alpha, scale, &ll);
        prev += ll;
      }
    }
  }
  ds_context_free(context, alpha);
  if (out_loglik != NULL) {
    *out_loglik = prev;
  }
  if (out_iterations != NULL) {
    *out_iterations = it;
  }
  return st;
}
