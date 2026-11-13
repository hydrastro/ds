#include "suffix_array.h"
#include "checked.h"
#include <stdbool.h>
#include <string.h>

/*
 * SA-IS over a string with a virtual sentinel at position n, smaller than
 * every symbol: sa has n + 1 rows and sa[0] = n. Types: t[i] = 1 for S
 * (the suffix at i is smaller than the one at i + 1), 0 for L; the
 * sentinel is S and position n - 1 is L. Position i is LMS when t[i] = 1
 * and t[i - 1] = 0; LMS positions are at least two apart, the sentinel
 * included.
 */

#define SA_EMPTY ((size_t) - 1)

typedef struct sa_text {
  const unsigned char *b; /* bytes, or */
  const size_t *w;        /* integers */
} sa_text_t;

static size_t sa_sym(const sa_text_t *s, size_t i) {
  return s->b != NULL ? (size_t)s->b[i] : s->w[i];
}

static bool sa_lms(const unsigned char *t, size_t i) {
  return i > 0 && t[i] && !t[i - 1];
}

/* the first row of each bucket (row 0 is the sentinel's) */
static void sa_heads(const size_t *cnt, size_t k, size_t *bkt) {
  size_t i;
  size_t sum = 1;
  for (i = 0; i < k; i++) {
    bkt[i] = sum;
    sum += cnt[i];
  }
}

/* one past the last row of each bucket */
static void sa_tails(const size_t *cnt, size_t k, size_t *bkt) {
  size_t i;
  size_t sum = 1;
  for (i = 0; i < k; i++) {
    sum += cnt[i];
    bkt[i] = sum;
  }
}

/* L suffixes left to right from what is placed, then S right to left */
static void sa_induce(const sa_text_t *s, size_t n, size_t k,
                      const unsigned char *t, const size_t *cnt, size_t *bkt,
                      size_t *sa) {
  size_t i;
  sa_heads(cnt, k, bkt);
  for (i = 0; i <= n; i++) {
    size_t j = sa[i];
    if (j != SA_EMPTY && j > 0 && !t[j - 1]) {
      sa[bkt[sa_sym(s, j - 1)]++] = j - 1;
    }
  }
  sa_tails(cnt, k, bkt);
  for (i = n + 1; i-- > 0;) {
    size_t j = sa[i];
    if (j != SA_EMPTY && j > 0 && t[j - 1]) {
      sa[--bkt[sa_sym(s, j - 1)]] = j - 1;
    }
  }
}

/* whether the LMS substrings at a and b (both below n) are equal */
static bool sa_same(const sa_text_t *s, size_t n, const unsigned char *t,
                    size_t a, size_t b) {
  size_t d;
  for (d = 0;; d++) {
    if (a + d == n || b + d == n) {
      return false;
    }
    if (sa_sym(s, a + d) != sa_sym(s, b + d) || t[a + d] != t[b + d]) {
      return false;
    }
    if (d > 0 && sa_lms(t, a + d)) {
      return true; /* types agree so far: b + d is LMS too */
    }
  }
}

static ds_status_t sa_core(const sa_text_t *s, size_t n, size_t k, size_t *sa,
                           ds_context_t *ctx) {
  unsigned char *t;
  size_t *cnt;
  size_t *bkt;
  size_t *name;
  size_t i;
  size_t j;
  size_t n1 = 0; /* LMS positions, the sentinel included */
  size_t m1;     /* the reduced string's length */
  size_t names = 0;
  size_t prev = SA_EMPTY;
  ds_status_t st = DS_OK;
  if (n == 0) {
    sa[0] = 0;
    return DS_OK;
  }
  if (k == 0 || n > ((size_t)-1) / 4 || k > ((size_t)-1) / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  t = (unsigned char *)ds_context_alloc(ctx, n + 1);
  cnt = (size_t *)ds_context_alloc(ctx, k * sizeof(size_t));
  bkt = (size_t *)ds_context_alloc(ctx, k * sizeof(size_t));
  name = (size_t *)ds_context_alloc(ctx, (n / 2 + 1) * sizeof(size_t));
  if (t == NULL || cnt == NULL || bkt == NULL || name == NULL) {
    ds_context_free(ctx, t);
    ds_context_free(ctx, cnt);
    ds_context_free(ctx, bkt);
    ds_context_free(ctx, name);
    return DS_ERR_ALLOC;
  }
  t[n] = 1;
  t[n - 1] = 0;
  for (i = n - 1; i-- > 0;) {
    size_t a = sa_sym(s, i);
    size_t b = sa_sym(s, i + 1);
    t[i] = (unsigned char)(a < b || (a == b && t[i + 1]) ? 1 : 0);
  }
  for (i = 0; i < k; i++) {
    cnt[i] = 0;
  }
  for (i = 0; i < n; i++) {
    cnt[sa_sym(s, i)]++;
  }
  /* stage 1: LMS substrings sorted by one induced sort */
  for (i = 0; i <= n; i++) {
    sa[i] = SA_EMPTY;
  }
  sa[0] = n;
  sa_tails(cnt, k, bkt);
  for (i = 1; i < n; i++) {
    if (sa_lms(t, i)) {
      sa[--bkt[sa_sym(s, i)]] = i;
    }
  }
  sa_induce(s, n, k, t, cnt, bkt, sa);
  for (i = 0; i <= n; i++) {
    size_t p = sa[i];
    if (p == n || sa_lms(t, p)) {
      sa[n1++] = p;
    }
  }
  /* names: the sentinel 0, then one per distinct LMS substring */
  for (i = 0; i < n1; i++) {
    size_t p = sa[i];
    if (p == n || prev == n || prev == SA_EMPTY || !sa_same(s, n, t, p, prev)) {
      names++;
    }
    prev = p;
    if (p != n) {
      name[p / 2] = names - 1;
    }
  }
  m1 = n1 - 1;
  /* the reduced string in the tail of sa, its suffix array in the head */
  j = n + 1 - m1;
  for (i = 1; i < n; i++) {
    if (sa_lms(t, i)) {
      sa[j++] = name[i / 2] - 1;
    }
  }
  if (names - 1 == m1) { /* all distinct: the names are the ranks */
    sa[0] = m1;
    for (i = 0; i < m1; i++) {
      sa[1 + sa[n + 1 - m1 + i]] = i;
    }
  } else {
    sa_text_t s1;
    s1.b = NULL;
    s1.w = sa + (n + 1 - m1);
    st = sa_core(&s1, m1, names - 1, sa, ctx);
  }
  if (st == DS_OK) {
    /* back to positions: the LMS list replaces the reduced string */
    j = n + 1 - m1;
    for (i = 1; i < n; i++) {
      if (sa_lms(t, i)) {
        sa[j++] = i;
      }
    }
    for (i = 0; i <= m1; i++) {
      sa[i] = sa[i] == m1 ? n : sa[n + 1 - m1 + sa[i]];
    }
    for (i = m1 + 1; i <= n; i++) {
      sa[i] = SA_EMPTY;
    }
    /* stage 3: the sorted LMS suffixes at their bucket tails, the largest
     * first (each moves right, onto a free row), then one induced sort */
    sa_tails(cnt, k, bkt);
    for (i = m1; i >= 1; i--) {
      size_t p = sa[i];
      sa[i] = SA_EMPTY;
      sa[--bkt[sa_sym(s, p)]] = p;
    }
    sa_induce(s, n, k, t, cnt, bkt, sa);
  }
  ds_context_free(ctx, t);
  ds_context_free(ctx, cnt);
  ds_context_free(ctx, bkt);
  ds_context_free(ctx, name);
  return st;
}

static ds_status_t sa_run(const sa_text_t *s, size_t n, size_t k,
                          size_t *out_sa, ds_context_t *ctx) {
  size_t *sa;
  size_t bytes;
  ds_status_t st;
  if (n == 0) {
    return DS_OK;
  }
  if (!ds_size_mul(n + 1, sizeof(size_t), &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  sa = (size_t *)ds_context_alloc(ctx, bytes);
  if (sa == NULL) {
    return DS_ERR_ALLOC;
  }
  st = sa_core(s, n, k, sa, ctx);
  if (st == DS_OK) {
    memcpy(out_sa, sa + 1, n * sizeof(size_t));
  }
  ds_context_free(ctx, sa);
  return st;
}

ds_status_t ds_sa_build(const unsigned char *s, size_t n, size_t *out_sa,
                        ds_context_t *context) {
  sa_text_t text;
  if (n > 0 && (s == NULL || out_sa == NULL)) {
    return DS_ERR_NULL;
  }
  text.b = s;
  text.w = NULL;
  return sa_run(&text, n, 256, out_sa, context);
}

ds_status_t ds_sa_build_int(const size_t *s, size_t n, size_t k, size_t *out_sa,
                            ds_context_t *context) {
  sa_text_t text;
  size_t i;
  if (n > 0 && (s == NULL || out_sa == NULL)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n; i++) {
    if (s[i] >= k) {
      return DS_ERR_INVALID;
    }
  }
  text.b = NULL;
  text.w = s;
  return sa_run(&text, n, k, out_sa, context);
}

void ds_sa_inverse(const size_t *sa, size_t n, size_t *out_isa) {
  size_t i;
  for (i = 0; i < n; i++) {
    out_isa[sa[i]] = i;
  }
}

ds_status_t ds_sa_lcp(const unsigned char *s, size_t n, const size_t *sa,
                      size_t *out_lcp, ds_context_t *context) {
  size_t *rank;
  size_t i;
  size_t h = 0;
  if (n == 0) {
    return DS_OK;
  }
  if (s == NULL || sa == NULL || out_lcp == NULL) {
    return DS_ERR_NULL;
  }
  if (n > ((size_t)-1) / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  rank = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  if (rank == NULL) {
    return DS_ERR_ALLOC;
  }
  ds_sa_inverse(sa, n, rank);
  out_lcp[0] = 0;
  for (i = 0; i < n; i++) { /* Kasai: h drops by at most one per step */
    if (rank[i] > 0) {
      size_t j = sa[rank[i] - 1];
      while (i + h < n && j + h < n && s[i + h] == s[j + h]) {
        h++;
      }
      out_lcp[rank[i]] = h;
      if (h > 0) {
        h--;
      }
    } else {
      h = 0;
    }
  }
  ds_context_free(context, rank);
  return DS_OK;
}

void ds_sa_bwt(const unsigned char *s, size_t n, const size_t *sa,
               unsigned char *out_bwt, size_t *out_primary) {
  size_t i;
  *out_primary = 0;
  out_bwt[0] = n > 0 ? s[n - 1] : 0; /* the row of the marker suffix */
  for (i = 0; i < n; i++) {
    if (sa[i] == 0) {
      out_bwt[i + 1] = 0;
      *out_primary = i + 1;
    } else {
      out_bwt[i + 1] = s[sa[i] - 1];
    }
  }
}
