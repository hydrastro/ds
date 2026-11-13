#include "r_index.h"
#include "checked.h"
#include "suffix_array.h"
#include <string.h>

#define RI_NONE ((size_t) - 1)

/* the run holding row i: the last run starting at or before it */
static size_t ri_run(const ds_rindex_t *r, size_t i) {
  size_t lo = 0;
  size_t hi = r->runs; /* start[lo] <= i < start[hi] */
  while (hi - lo > 1) {
    size_t mid = lo + (hi - lo) / 2;
    if (r->start[mid] <= i) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return lo;
}

/* how many runs of symbol c come before run k */
static size_t ri_before(const ds_rindex_t *r, unsigned c, size_t k) {
  size_t lo = r->sym_off[c];
  size_t hi = r->sym_off[c + 1];
  size_t base = lo;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (r->list[mid] < k) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo - base;
}

/* occurrences of symbol c in rows [0, i) */
static size_t ri_rank(const ds_rindex_t *r, unsigned c, size_t i) {
  size_t k;
  size_t q;
  size_t total;
  if (i == 0) {
    return 0;
  }
  k = ri_run(r, i - 1);
  q = ri_before(r, c, k);
  total = r->C[c + 1] - r->C[c];
  if (r->sym[k] == c) {
    return r->cum[r->sym_off[c] + q] + (i - r->start[k]);
  }
  return r->sym_off[c] + q < r->sym_off[c + 1] ? r->cum[r->sym_off[c] + q]
                                               : total;
}

static void ri_free(ds_rindex_t *r) {
  ds_context_free(r->context, r->start);
  ds_context_free(r->context, r->sym);
  ds_context_free(r->context, r->list);
  ds_context_free(r->context, r->cum);
  ds_context_free(r->context, r->end_sa);
  ds_context_free(r->context, r->phi_pos);
  ds_context_free(r->context, r->phi_val);
  r->start = NULL;
  r->sym = NULL;
  r->list = NULL;
  r->cum = NULL;
  r->end_sa = NULL;
  r->phi_pos = NULL;
  r->phi_val = NULL;
}

void ds_rindex_destroy(ds_rindex_t *r) {
  if (r == NULL) {
    return;
  }
  ri_free(r);
  r->runs = 0;
  r->n = 0;
}

static unsigned ri_symbol(const unsigned char *text, const size_t *sa,
                          size_t row) {
  return sa[row] == 0 ? 0u : (unsigned)text[sa[row] - 1] + 1u;
}

ds_status_t ds_rindex_build(ds_rindex_t *r, const unsigned char *text, size_t n,
                            ds_fm_sa_builder_t builder, ds_context_t *context) {
  size_t *sa;
  size_t rows;
  size_t bytes;
  size_t runs = 0;
  size_t i;
  size_t k;
  unsigned c;
  ds_status_t st;
  if (r == NULL || (n > 0 && text == NULL)) {
    return DS_ERR_NULL;
  }
  memset(r, 0, sizeof(*r));
  r->context = context;
  r->n = n;
  rows = n + 1;
  if (rows == 0 || !ds_size_mul(rows, sizeof(size_t), &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  sa = (size_t *)ds_context_alloc(context, bytes);
  if (sa == NULL) {
    return DS_ERR_ALLOC;
  }
  /* row 0 is the marker suffix, then the text's suffixes in order */
  sa[0] = n;
  st = builder != NULL ? builder(text, n, sa + 1, context)
                       : ds_sa_build(text, n, sa + 1, context);
  if (st != DS_OK) {
    ds_context_free(context, sa);
    return st;
  }
  for (i = 0; i < rows; i++) {
    if (i == 0 || ri_symbol(text, sa, i) != ri_symbol(text, sa, i - 1)) {
      runs++;
    }
  }
  r->runs = runs;
  r->start = (size_t *)ds_context_alloc(context, (runs + 1) * sizeof(size_t));
  r->sym = (unsigned short *)ds_context_alloc(context,
                                              runs * sizeof(unsigned short));
  r->list = (size_t *)ds_context_alloc(context, runs * sizeof(size_t));
  r->cum = (size_t *)ds_context_alloc(context, runs * sizeof(size_t));
  r->end_sa = (size_t *)ds_context_alloc(context, runs * sizeof(size_t));
  r->phi_pos = (size_t *)ds_context_alloc(context, runs * sizeof(size_t));
  r->phi_val = (size_t *)ds_context_alloc(context, runs * sizeof(size_t));
  if (r->start == NULL || r->sym == NULL || r->list == NULL || r->cum == NULL ||
      r->end_sa == NULL || r->phi_pos == NULL || r->phi_val == NULL) {
    ri_free(r);
    ds_context_free(context, sa);
    return DS_ERR_ALLOC;
  }
  /* runs, their symbols, and the samples at their ends */
  k = 0;
  for (i = 0; i < rows; i++) {
    c = ri_symbol(text, sa, i);
    if (i == 0 || c != ri_symbol(text, sa, i - 1)) {
      r->start[k] = i;
      r->sym[k] = (unsigned short)c;
      r->phi_pos[k] = sa[i]; /* sorted below */
      k++;
    }
    r->end_sa[k - 1] = sa[i];
  }
  r->start[runs] = rows;
  r->last_sa = sa[rows - 1];
  /* C over symbols: the marker, then the bytes */
  for (c = 0; c < 258; c++) {
    r->C[c] = 0;
    r->sym_off[c] = 0;
  }
  r->C[1] = 1;
  for (i = 0; i < n; i++) {
    r->C[(unsigned)text[i] + 2]++;
  }
  for (c = 1; c < 257; c++) {
    r->C[c + 1] += r->C[c];
  }
  /* each symbol's runs, in order, with the rows before them */
  for (k = 0; k < runs; k++) {
    r->sym_off[r->sym[k] + 1]++;
  }
  for (c = 0; c < 257; c++) {
    r->sym_off[c + 1] += r->sym_off[c];
  }
  {
    size_t fill[257];
    size_t seen[257];
    for (c = 0; c < 257; c++) {
      fill[c] = r->sym_off[c];
      seen[c] = 0;
    }
    for (k = 0; k < runs; k++) {
      c = r->sym[k];
      r->list[fill[c]] = k;
      r->cum[fill[c]] = seen[c];
      fill[c]++;
      seen[c] += r->start[k + 1] - r->start[k];
    }
  }
  /* phi: values at run starts in text order, each with the value at the
   * row before it (the previous run's end); sa is reused as a map from
   * text position to run */
  for (i = 0; i < rows; i++) {
    sa[i] = RI_NONE;
  }
  for (k = 0; k < runs; k++) {
    sa[r->phi_pos[k]] = k;
  }
  i = 0;
  for (k = 0; k < rows; k++) {
    if (sa[k] != RI_NONE) {
      size_t run = sa[k];
      r->phi_pos[i] = k;
      r->phi_val[i] = run == 0 ? RI_NONE : r->end_sa[run - 1];
      i++;
    }
  }
  ds_context_free(context, sa);
  return DS_OK;
}

size_t ds_rindex_runs(const ds_rindex_t *r) { return r == NULL ? 0 : r->runs; }

size_t ds_rindex_bytes(const ds_rindex_t *r) {
  if (r == NULL) {
    return 0;
  }
  return sizeof(*r) + (r->runs + 1) * sizeof(size_t) +
         r->runs * (sizeof(unsigned short) + 5 * sizeof(size_t));
}

int ds_rindex_bwt(const ds_rindex_t *r, size_t row) {
  unsigned c = r->sym[ri_run(r, row)];
  return c == 0 ? -1 : (int)c - 1;
}

size_t ds_rindex_lf(const ds_rindex_t *r, size_t row) {
  unsigned c = r->sym[ri_run(r, row)];
  return r->C[c] + ri_rank(r, c, row);
}

size_t ds_rindex_phi(const ds_rindex_t *r, size_t x) {
  size_t lo = 0;
  size_t hi = r->runs; /* phi_pos[lo] <= x < phi_pos[hi] */
  while (hi - lo > 1) {
    size_t mid = lo + (hi - lo) / 2;
    if (r->phi_pos[mid] <= x) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return r->phi_val[lo] + (x - r->phi_pos[lo]);
}

/* backward search; with sa_last != NULL also the toehold: the suffix
 * array value of the range's last row */
static size_t ri_search(const ds_rindex_t *r, const unsigned char *p, size_t m,
                        size_t *out_sp, size_t *out_ep, size_t *sa_last) {
  size_t sp = 0;
  size_t ep = r->n + 1;
  size_t last = r->last_sa;
  size_t j;
  for (j = m; j-- > 0;) {
    unsigned c = (unsigned)p[j] + 1u;
    if (r->C[c] == r->C[c + 1]) {
      return 0;
    }
    if (sa_last != NULL) {
      size_t k = ri_run(r, ep - 1);
      if (r->sym[k] == c) {
        last--; /* the last row stays the last after LF */
      } else {
        size_t q = ri_before(r, c, k);
        if (q == 0) {
          return 0; /* no c before the range's end */
        }
        last = r->end_sa[r->list[r->sym_off[c] + q - 1]] - 1;
      }
    }
    sp = r->C[c] + ri_rank(r, c, sp);
    ep = r->C[c] + ri_rank(r, c, ep);
    if (sp >= ep) {
      return 0;
    }
  }
  if (out_sp != NULL) {
    *out_sp = sp;
  }
  if (out_ep != NULL) {
    *out_ep = ep;
  }
  if (sa_last != NULL) {
    *sa_last = last;
  }
  return ep - sp;
}

size_t ds_rindex_count(const ds_rindex_t *r, const unsigned char *p, size_t m,
                       size_t *out_sp, size_t *out_ep) {
  if (r == NULL || (m > 0 && p == NULL)) {
    return 0;
  }
  return ri_search(r, p, m, out_sp, out_ep, NULL);
}

ds_status_t ds_rindex_locate(const ds_rindex_t *r, const unsigned char *p,
                             size_t m, size_t *out, size_t capacity,
                             size_t *out_total) {
  size_t sp = 0;
  size_t ep = 0;
  size_t x = 0;
  size_t total;
  size_t i;
  if (r == NULL || out_total == NULL || (m > 0 && p == NULL) ||
      (capacity > 0 && out == NULL)) {
    return DS_ERR_NULL;
  }
  total = ri_search(r, p, m, &sp, &ep, &x);
  *out_total = total;
  if (total == 0) {
    return DS_OK;
  }
  for (i = 0; i < capacity && i < total; i++) {
    out[i] = x;
    if (i + 1 < total) {
      x = ds_rindex_phi(r, x);
    }
  }
  return DS_OK;
}
