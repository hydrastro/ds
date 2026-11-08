#include "string_search.h"
#include "../lib/suffix_automaton.h"
#include <limits.h>
#include <string.h>

typedef ptrdiff_t ss_i;

typedef struct ss {
  const unsigned char *y; /* text */
  ss_i n;
  const unsigned char *x; /* pattern */
  ss_i m;
  ds_match_func_t on_match;
  void *user;
  size_t count;
  bool stop;
  bool failed; /* scratch allocation failed: the result is incomplete */
  ds_context_t *ctx;
} ss_t;

static void ss_report(ss_t *s, ss_i pos) {
  if (s->stop) {
    return;
  }
  s->count++;
  if (s->on_match != NULL && !s->on_match((size_t)pos, s->user)) {
    s->stop = true;
  }
}

static ss_i ss_max(ss_i a, ss_i b) { return a > b ? a : b; }
static ss_i ss_min(ss_i a, ss_i b) { return a < b ? a : b; }

static void *ss_alloc(ss_t *s, size_t count, size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > ((size_t)-1) / size) {
    s->failed = true;
    return NULL;
  }
  {
    void *p = ds_context_alloc(s->ctx, count * size);
    if (p == NULL) {
      s->failed = true;
    }
    return p;
  }
}

/* ---- shared tables ------------------------------------------------------- */

/* Boyer-Moore bad character: distance from the last occurrence to the end. */
static void ss_bm_bc(const ss_t *s, ss_i *bc) {
  ss_i i;
  for (i = 0; i < 256; i++) {
    bc[i] = s->m;
  }
  for (i = 0; i < s->m - 1; i++) {
    bc[s->x[i]] = s->m - 1 - i;
  }
}

/* suff[i]: length of the longest suffix of x[0..i] that is a suffix of x */
static void ss_suffixes(const ss_t *s, ss_i *suff) {
  ss_i m = s->m;
  ss_i f = 0;
  ss_i g = m - 1;
  ss_i i;
  const unsigned char *x = s->x;
  suff[m - 1] = m;
  for (i = m - 2; i >= 0; i--) {
    if (i > g && suff[i + m - 1 - f] < i - g) {
      suff[i] = suff[i + m - 1 - f];
    } else {
      if (i < g) {
        g = i;
      }
      f = i;
      while (g >= 0 && x[g] == x[g + m - 1 - f]) {
        g--;
      }
      suff[i] = f - g;
    }
  }
}

static void ss_bm_gs(const ss_t *s, ss_i *gs, ss_i *suff) {
  ss_i m = s->m;
  ss_i i;
  ss_i j = 0;
  ss_suffixes(s, suff);
  for (i = 0; i < m; i++) {
    gs[i] = m;
  }
  for (i = m - 1; i >= -1; i--) {
    if (i == -1 || suff[i] == i + 1) {
      for (; j < m - 1 - i; j++) {
        if (gs[j] == m) {
          gs[j] = m - 1 - i;
        }
      }
    }
  }
  for (i = 0; i <= m - 2; i++) {
    gs[m - 1 - suff[i]] = m - 1 - i;
  }
}

/* Morris-Pratt borders (mp) or KMP strong borders (kmp), m + 1 entries. */
static void ss_borders(const ss_t *s, ss_i *next, bool strong) {
  ss_i i = 0;
  ss_i j = -1;
  const unsigned char *x = s->x;
  next[0] = -1;
  while (i < s->m) {
    while (j > -1 && x[i] != x[j]) {
      j = next[j];
    }
    i++;
    j++;
    if (strong && i < s->m && x[i] == x[j]) {
      next[i] = next[j];
    } else {
      next[i] = j;
    }
  }
}

/* ---- algorithms ---------------------------------------------------------- */

static void ss_brute(ss_t *s) {
  ss_i j;
  for (j = 0; j <= s->n - s->m && !s->stop; j++) {
    if (memcmp(s->x, s->y + j, (size_t)s->m) == 0) {
      ss_report(s, j);
    }
  }
}

static void ss_morris_pratt(ss_t *s) {
  ss_i *next = (ss_i *)ss_alloc(s, (size_t)s->m + 1, sizeof(ss_i));
  ss_i i = 0;
  ss_i j = 0;
  if (next == NULL) {
    return;
  }
  ss_borders(s, next, false);
  while (j < s->n && !s->stop) {
    while (i > -1 && s->x[i] != s->y[j]) {
      i = next[i];
    }
    i++;
    j++;
    if (i >= s->m) {
      ss_report(s, j - i);
      i = next[i];
    }
  }
  ds_context_free(s->ctx, next);
}

static void ss_boyer_moore(ss_t *s) {
  ss_i bc[256];
  ss_i *gs = (ss_i *)ss_alloc(s, (size_t)s->m, sizeof(ss_i));
  ss_i *suff = (ss_i *)ss_alloc(s, (size_t)s->m, sizeof(ss_i));
  ss_i i;
  ss_i j = 0;
  if (gs == NULL || suff == NULL) {
    ds_context_free(s->ctx, gs);
    ds_context_free(s->ctx, suff);
    return;
  }
  ss_bm_bc(s, bc);
  ss_bm_gs(s, gs, suff);
  while (j <= s->n - s->m && !s->stop) {
    for (i = s->m - 1; i >= 0 && s->x[i] == s->y[i + j]; i--) {
    }
    if (i < 0) {
      ss_report(s, j);
      j += gs[0];
    } else {
      j += ss_max(gs[i], bc[s->y[i + j]] - s->m + 1 + i);
    }
  }
  ds_context_free(s->ctx, gs);
  ds_context_free(s->ctx, suff);
}

static void ss_horspool(ss_t *s) {
  ss_i bc[256];
  ss_i j = 0;
  unsigned char last = s->x[s->m - 1];
  ss_bm_bc(s, bc);
  while (j <= s->n - s->m && !s->stop) {
    unsigned char c = s->y[j + s->m - 1];
    if (c == last && memcmp(s->x, s->y + j, (size_t)(s->m - 1)) == 0) {
      ss_report(s, j);
    }
    j += bc[c];
  }
}

static void ss_raita(ss_t *s) {
  ss_i bc[256];
  ss_i j = 0;
  ss_i m = s->m;
  unsigned char first = s->x[0];
  unsigned char middle = s->x[m / 2];
  unsigned char last = s->x[m - 1];
  ss_bm_bc(s, bc);
  while (j <= s->n - m && !s->stop) {
    unsigned char c = s->y[j + m - 1];
    if (last == c && middle == s->y[j + m / 2] && first == s->y[j] &&
        memcmp(s->x + 1, s->y + j + 1, (size_t)(m > 2 ? m - 2 : 0)) == 0) {
      ss_report(s, j);
    }
    j += bc[c];
  }
}

/* Quick Search shift: distance from the last occurrence to just past the
 * end. */
static void ss_qs_bc(const ss_t *s, ss_i *qs) {
  ss_i i;
  for (i = 0; i < 256; i++) {
    qs[i] = s->m + 1;
  }
  for (i = 0; i < s->m; i++) {
    qs[s->x[i]] = s->m - i;
  }
}

static void ss_quick_search(ss_t *s) {
  ss_i qs[256];
  ss_i j = 0;
  ss_qs_bc(s, qs);
  while (j <= s->n - s->m && !s->stop) {
    if (memcmp(s->x, s->y + j, (size_t)s->m) == 0) {
      ss_report(s, j);
    }
    if (j + s->m >= s->n) {
      break;
    }
    j += qs[s->y[j + s->m]];
  }
}

static void ss_smith(ss_t *s) {
  ss_i bc[256];
  ss_i qs[256];
  ss_i j = 0;
  ss_bm_bc(s, bc);
  ss_qs_bc(s, qs);
  while (j <= s->n - s->m && !s->stop) {
    if (memcmp(s->x, s->y + j, (size_t)s->m) == 0) {
      ss_report(s, j);
    }
    if (j + s->m >= s->n) {
      break;
    }
    j += ss_max(bc[s->y[j + s->m - 1]], qs[s->y[j + s->m]]);
  }
}

static void ss_zhu_takaoka(ss_t *s) {
  ss_i *zt;
  ss_i *gs;
  ss_i *suff;
  ss_i a;
  ss_i b;
  ss_i i;
  ss_i j = 0;
  ss_i m = s->m;
  if (m < 2) {
    ss_brute(s);
    return;
  }
  zt = (ss_i *)ss_alloc(s, 65536, sizeof(ss_i));
  gs = (ss_i *)ss_alloc(s, (size_t)m, sizeof(ss_i));
  suff = (ss_i *)ss_alloc(s, (size_t)m, sizeof(ss_i));
  if (zt == NULL || gs == NULL || suff == NULL) {
    ds_context_free(s->ctx, zt);
    ds_context_free(s->ctx, gs);
    ds_context_free(s->ctx, suff);
    return;
  }
  for (a = 0; a < 256; a++) {
    for (b = 0; b < 256; b++) {
      zt[a * 256 + b] = m;
    }
  }
  for (a = 0; a < 256; a++) {
    zt[a * 256 + s->x[0]] = m - 1;
  }
  for (i = 1; i < m - 1; i++) {
    zt[s->x[i - 1] * 256 + s->x[i]] = m - 1 - i;
  }
  ss_bm_gs(s, gs, suff);
  while (j <= s->n - m && !s->stop) {
    i = m - 1;
    while (i >= 0 && s->x[i] == s->y[i + j]) {
      i--;
    }
    if (i < 0) {
      ss_report(s, j);
      j += gs[0];
    } else {
      j += ss_max(gs[i], zt[s->y[j + m - 2] * 256 + s->y[j + m - 1]]);
    }
  }
  ds_context_free(s->ctx, zt);
  ds_context_free(s->ctx, gs);
  ds_context_free(s->ctx, suff);
}

static void ss_turbo_bm(ss_t *s) {
  ss_i bc[256];
  ss_i *gs = (ss_i *)ss_alloc(s, (size_t)s->m, sizeof(ss_i));
  ss_i *suff = (ss_i *)ss_alloc(s, (size_t)s->m, sizeof(ss_i));
  ss_i i;
  ss_i j = 0;
  ss_i u = 0;
  ss_i v;
  ss_i shift = s->m;
  ss_i turbo;
  ss_i bcs;
  ss_i m = s->m;
  if (gs == NULL || suff == NULL) {
    ds_context_free(s->ctx, gs);
    ds_context_free(s->ctx, suff);
    return;
  }
  ss_bm_bc(s, bc);
  ss_bm_gs(s, gs, suff);
  while (j <= s->n - m && !s->stop) {
    i = m - 1;
    while (i >= 0 && s->x[i] == s->y[i + j]) {
      i--;
      if (u != 0 && i == m - 1 - shift) {
        i -= u;
      }
    }
    if (i < 0) {
      ss_report(s, j);
      shift = gs[0];
      u = m - shift;
    } else {
      v = m - 1 - i;
      turbo = u - v;
      bcs = bc[s->y[i + j]] - m + 1 + i;
      shift = ss_max(turbo, bcs);
      shift = ss_max(shift, gs[i]);
      if (shift == gs[i]) {
        u = ss_min(m - shift, v);
      } else {
        if (turbo < bcs) {
          shift = ss_max(shift, u + 1);
        }
        u = 0;
      }
    }
    j += shift;
  }
  ds_context_free(s->ctx, gs);
  ds_context_free(s->ctx, suff);
}

static void ss_apostolico_giancarlo(ss_t *s) {
  ss_i bc[256];
  ss_i m = s->m;
  ss_i *gs = (ss_i *)ss_alloc(s, (size_t)m, sizeof(ss_i));
  ss_i *suff = (ss_i *)ss_alloc(s, (size_t)m, sizeof(ss_i));
  ss_i *skip = (ss_i *)ss_alloc(s, (size_t)m, sizeof(ss_i));
  ss_i i;
  ss_i j = 0;
  ss_i k;
  ss_i sh;
  ss_i shift;
  if (gs == NULL || suff == NULL || skip == NULL) {
    ds_context_free(s->ctx, gs);
    ds_context_free(s->ctx, suff);
    ds_context_free(s->ctx, skip);
    return;
  }
  ss_bm_bc(s, bc);
  ss_bm_gs(s, gs, suff); /* also fills suff */
  memset(skip, 0, (size_t)m * sizeof(ss_i));
  while (j <= s->n - m && !s->stop) {
    i = m - 1;
    while (i >= 0) {
      k = skip[i];
      sh = suff[i];
      if (k > 0) {
        if (k > sh) {
          if (i + 1 == sh) {
            i = -1;
          } else {
            i -= sh;
          }
          break;
        }
        i -= k;
        if (k < sh) {
          break;
        }
      } else if (s->x[i] == s->y[i + j]) {
        i--;
      } else {
        break;
      }
    }
    if (i < 0) {
      ss_report(s, j);
      skip[m - 1] = m;
      shift = gs[0];
    } else {
      skip[m - 1] = m - 1 - i;
      shift = ss_max(gs[i], bc[s->y[i + j]] - m + 1 + i);
    }
    j += shift;
    if (shift < m) { /* slide the skip table along with the window */
      memmove(skip, skip + shift, (size_t)(m - shift) * sizeof(ss_i));
      memset(skip + m - shift, 0, (size_t)shift * sizeof(ss_i));
    } else {
      memset(skip, 0, (size_t)m * sizeof(ss_i));
    }
  }
  ds_context_free(s->ctx, gs);
  ds_context_free(s->ctx, suff);
  ds_context_free(s->ctx, skip);
}

static void ss_apostolico_crochemore(ss_t *s) {
  ss_i m = s->m;
  ss_i *kmp = (ss_i *)ss_alloc(s, (size_t)m + 1, sizeof(ss_i));
  ss_i ell;
  ss_i i;
  ss_i j = 0;
  ss_i k = 0;
  if (kmp == NULL) {
    return;
  }
  ss_borders(s, kmp, true);
  for (ell = 1; ell < m && s->x[ell - 1] == s->x[ell]; ell++) {
  }
  if (ell == m) {
    ell = 0;
  }
  i = ell;
  while (j <= s->n - m && !s->stop) {
    while (i < m && s->x[i] == s->y[i + j]) {
      i++;
    }
    if (i >= m) {
      while (k < ell && s->x[k] == s->y[j + k]) {
        k++;
      }
      if (k >= ell) {
        ss_report(s, j);
      }
    }
    j += i - kmp[i];
    if (i == ell) {
      k = ss_max(0, k - 1);
    } else if (kmp[i] <= ell) {
      k = ss_max(0, kmp[i]);
      i = ell;
    } else {
      k = ell;
      i = kmp[i];
    }
  }
  ds_context_free(s->ctx, kmp);
}

static void ss_not_so_naive(ss_t *s) {
  ss_i j = 0;
  ss_i k;
  ss_i ell;
  ss_i m = s->m;
  if (m < 2) {
    ss_brute(s);
    return;
  }
  if (s->x[0] == s->x[1]) {
    k = 2;
    ell = 1;
  } else {
    k = 1;
    ell = 2;
  }
  while (j <= s->n - m && !s->stop) {
    if (s->x[1] != s->y[j + 1]) {
      j += k;
    } else {
      if (memcmp(s->x + 2, s->y + j + 2, (size_t)(m - 2)) == 0 &&
          s->x[0] == s->y[j]) {
        ss_report(s, j);
      }
      j += ell;
    }
  }
}

/* maximal suffix of x for the byte order (reverse: the opposite order) */
static ss_i ss_max_suffix(const unsigned char *x, ss_i m, bool reverse,
                          ss_i *out_period) {
  ss_i ms = -1;
  ss_i j = 0;
  ss_i k = 1;
  ss_i p = 1;
  unsigned char a;
  unsigned char b;
  while (j + k < m) {
    a = x[j + k];
    b = x[ms + k];
    if (reverse ? a > b : a < b) {
      j += k;
      k = 1;
      p = j - ms;
    } else if (a == b) {
      if (k != p) {
        k++;
      } else {
        j += p;
        k = 1;
      }
    } else {
      ms = j;
      j = ms + 1;
      k = p = 1;
    }
  }
  *out_period = p;
  return ms;
}

static void ss_two_way(ss_t *s) {
  ss_i m = s->m;
  ss_i p;
  ss_i q;
  ss_i i;
  ss_i j;
  ss_i ell;
  ss_i per;
  ss_i memory;
  ss_i a = ss_max_suffix(s->x, m, false, &p);
  ss_i b = ss_max_suffix(s->x, m, true, &q);
  if (a > b) {
    ell = a;
    per = p;
  } else {
    ell = b;
    per = q;
  }
  if (memcmp(s->x, s->x + per, (size_t)(ell + 1)) == 0) {
    j = 0;
    memory = -1;
    while (j <= s->n - m && !s->stop) {
      i = ss_max(ell, memory) + 1;
      while (i < m && s->x[i] == s->y[i + j]) {
        i++;
      }
      if (i >= m) {
        i = ell;
        while (i > memory && s->x[i] == s->y[i + j]) {
          i--;
        }
        if (i <= memory) {
          ss_report(s, j);
        }
        j += per;
        memory = m - per - 1;
      } else {
        j += i - ell;
        memory = -1;
      }
    }
  } else {
    per = ss_max(ell + 1, m - ell - 1) + 1;
    j = 0;
    while (j <= s->n - m && !s->stop) {
      i = ell + 1;
      while (i < m && s->x[i] == s->y[i + j]) {
        i++;
      }
      if (i >= m) {
        i = ell;
        while (i >= 0 && s->x[i] == s->y[i + j]) {
          i--;
        }
        if (i < 0) {
          ss_report(s, j);
        }
        j += per;
      } else {
        j += i - ell;
      }
    }
  }
}

static void ss_shift_or(ss_t *s) {
  unsigned long mask[256];
  unsigned long state = ~0UL;
  unsigned long lim;
  ss_i i;
  ss_i j;
  if ((size_t)s->m > CHAR_BIT * sizeof(unsigned long)) {
    ss_morris_pratt(s);
    return;
  }
  for (i = 0; i < 256; i++) {
    mask[i] = ~0UL;
  }
  for (i = 0; i < s->m; i++) {
    mask[s->x[i]] &= ~(1UL << i);
  }
  lim = 1UL << (s->m - 1);
  for (j = 0; j < s->n && !s->stop; j++) {
    state = (state << 1) | mask[s->y[j]];
    if ((state & lim) == 0) {
      ss_report(s, j - s->m + 1);
    }
  }
}

static void ss_dfa(ss_t *s) {
  ss_i m = s->m;
  size_t *delta;
  size_t q = 0;
  size_t r = 0; /* state reached by x[1..i) (the failure state) */
  ss_i i;
  ss_i j;
  unsigned int c;
  if ((size_t)m + 1 > ((size_t)-1) / 256 / sizeof(size_t)) {
    return;
  }
  delta = (size_t *)ss_alloc(s, ((size_t)m + 1) * 256, sizeof(size_t));
  if (delta == NULL) {
    return;
  }
  for (c = 0; c < 256; c++) {
    delta[c] = 0;
  }
  delta[s->x[0]] = 1;
  for (i = 1; i <= m; i++) {
    for (c = 0; c < 256; c++) {
      delta[(size_t)i * 256 + c] = delta[r * 256 + c];
    }
    if (i < m) {
      delta[(size_t)i * 256 + s->x[i]] = (size_t)i + 1;
      r = delta[r * 256 + s->x[i]];
    }
  }
  for (j = 0; j < s->n && !s->stop; j++) {
    q = delta[q * 256 + s->y[j]];
    if (q == (size_t)m) {
      ss_report(s, j - m + 1);
    }
  }
  ds_context_free(s->ctx, delta);
}

/* Skip Search on factors of length q: windows sampled every m - q + 1
 * positions, candidates from the pattern positions of the sampled factor
 * (sorted (factor, position) pairs, binary searched). */
typedef struct ss_gram {
  unsigned long key;
  ss_i pos;
} ss_gram_t;

static unsigned long ss_key(const unsigned char *p, ss_i q) {
  unsigned long k = 0;
  ss_i i;
  for (i = 0; i < q; i++) {
    k = (k << 8) | p[i];
  }
  return k;
}

static void ss_gram_sort(ss_gram_t *g, ss_gram_t *tmp, size_t n) {
  size_t width;
  size_t lo;
  size_t i;
  for (width = 1; width < n; width *= 2) {
    for (lo = 0; lo < n; lo += 2 * width) {
      size_t mid = lo + width < n ? lo + width : n;
      size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
      size_t a = lo;
      size_t b = mid;
      size_t k = lo;
      while (a < mid && b < hi) {
        tmp[k++] = g[b].key < g[a].key ? g[b++] : g[a++];
      }
      while (a < mid) {
        tmp[k++] = g[a++];
      }
      while (b < hi) {
        tmp[k++] = g[b++];
      }
    }
    for (i = 0; i < n; i++) {
      g[i] = tmp[i];
    }
  }
}

static void ss_skip_q(ss_t *s, ss_i q) {
  ss_i m = s->m;
  size_t count = (size_t)(m - q + 1);
  ss_gram_t *g = (ss_gram_t *)ss_alloc(s, count, sizeof(ss_gram_t));
  ss_gram_t *tmp = (ss_gram_t *)ss_alloc(s, count, sizeof(ss_gram_t));
  ss_i i;
  ss_i j;
  size_t lo;
  size_t hi;
  size_t mid;
  unsigned long key;
  if (g == NULL || tmp == NULL) {
    ds_context_free(s->ctx, g);
    ds_context_free(s->ctx, tmp);
    return;
  }
  for (i = 0; i <= m - q; i++) {
    g[i].key = ss_key(s->x + i, q);
    g[i].pos = i;
  }
  ss_gram_sort(g, tmp, count); /* stable: positions ascend within a key */
  for (j = m - q; j <= s->n - q && !s->stop; j += m - q + 1) {
    key = ss_key(s->y + j, q);
    lo = 0;
    hi = count;
    while (lo < hi) { /* one past the last entry with this key */
      mid = lo + (hi - lo) / 2;
      if (g[mid].key <= key) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    while (lo > 0 && g[lo - 1].key == key && !s->stop) {
      ss_i p = j - g[lo - 1].pos; /* positions descending: starts ascending */
      lo--;
      if (p >= 0 && p <= s->n - m && memcmp(s->x, s->y + p, (size_t)m) == 0) {
        ss_report(s, p);
      }
    }
  }
  ds_context_free(s->ctx, g);
  ds_context_free(s->ctx, tmp);
}

static void ss_alpha_skip(ss_t *s) {
  ss_i q = 1;
  ss_i span = 256;
  while (span < s->m && q < 3) {
    span *= 256;
    q++;
  }
  if (q > s->m) {
    q = s->m;
  }
  ss_skip_q(s, q);
}

/* ---- more algorithms ----------------------------------------------------- */

/* KmpSkip Search: skip search over the occurrences of the text character
 * in the pattern, with KMP to avoid rescanning. */
static ss_i ss_kmpskip_attempt(const ss_t *s, ss_i start, ss_i wall) {
  ss_i k = wall - start;
  while (k < s->m && s->x[k] == s->y[k + start]) {
    k++;
  }
  return k;
}

static void ss_kmp_skip(ss_t *s) {
  ss_i *kmp = (ss_i *)ss_alloc(s, (size_t)(4 * s->m + 4), sizeof(ss_i));
  ss_i *mp;
  ss_i *list;
  ss_i z[256];
  ss_i i;
  ss_i j;
  ss_i k;
  ss_i kmp_start;
  ss_i per;
  ss_i start;
  ss_i wall;
  const unsigned char *x = s->x;
  const unsigned char *y = s->y;
  ss_i m = s->m;
  ss_i n = s->n;
  if (kmp == NULL) {
    return;
  }
  mp = kmp + m + 1;
  list = mp + m + 1;
  ss_borders(s, mp, false);
  ss_borders(s, kmp, true);
  for (i = 0; i < 256; i++) {
    z[i] = -1;
  }
  for (i = 0; i < m; i++) {
    list[i] = -1;
  }
  z[x[0]] = 0;
  for (i = 1; i < m; i++) {
    list[i] = z[x[i]];
    z[x[i]] = i;
  }
  wall = 0;
  per = m - kmp[m];
  j = -1;
  do {
    j += m;
  } while (j < n && z[y[j]] < 0);
  if (j >= n) {
    goto done;
  }
  i = z[y[j]];
  start = j - i;
  while (start <= n - m && !s->stop) {
    if (start > wall) {
      wall = start;
    }
    k = ss_kmpskip_attempt(s, start, wall);
    wall = start + k;
    if (k == m) {
      ss_report(s, start);
      i -= per;
    } else {
      i = list[i];
    }
    if (i < 0) {
      do {
        j += m;
      } while (j < n && z[y[j]] < 0);
      if (j >= n) {
        goto done;
      }
      i = z[y[j]];
    }
    kmp_start = start + k - kmp[k];
    k = kmp[k];
    start = j - i;
    while (start < kmp_start || (kmp_start < start && start < wall)) {
      if (start < kmp_start) {
        i = list[i];
        if (i < 0) {
          do {
            j += m;
          } while (j < n && z[y[j]] < 0);
          if (j >= n) {
            goto done;
          }
          i = z[y[j]];
        }
        start = j - i;
      } else {
        kmp_start += k - mp[k];
        k = mp[k];
      }
    }
  }
done:
  ds_context_free(s->ctx, kmp);
}

/* Colussi preprocessing (also used by Galil-Giancarlo); returns nd. Arrays
 * h, next, shift have m + 1 entries. */
static ss_i ss_colussi_pre(ss_t *s, ss_i *h, ss_i *next, ss_i *shift,
                           ss_i *work) {
  const unsigned char *x = s->x;
  ss_i m = s->m;
  ss_i *hmax = work;
  ss_i *kmin = hmax + m + 2;
  ss_i *nhd0 = kmin + m + 2;
  ss_i *rmin = nhd0 + m + 2;
  ss_i i;
  ss_i k;
  ss_i q;
  ss_i r;
  ss_i nd;
  ss_i t;
  /* hmax[k]: end of the longest prefix-period run at shift k */
  i = k = 1;
  do {
    while (i < m && x[i] == x[i - k]) {
      i++;
    }
    hmax[k] = i;
    q = k + 1;
    while (q <= m && hmax[q - k] + k < i) {
      hmax[q] = hmax[q - k] + k;
      q++;
    }
    k = q;
    if (k == i + 1) {
      i = k;
    }
  } while (k <= m);
  for (i = 0; i < m; i++) {
    kmin[i] = 0;
  }
  for (i = m; i >= 1; i--) {
    if (hmax[i] < m) {
      kmin[hmax[i]] = i;
    }
  }
  r = m;
  for (i = m - 1; i >= 0; i--) {
    if (hmax[i + 1] == m) {
      r = i + 1;
    }
    rmin[i] = kmin[i] == 0 ? r : 0;
  }
  t = -1;
  r = m;
  for (i = 0; i < m; i++) {
    if (kmin[i] == 0) {
      h[--r] = i;
    } else {
      h[++t] = i;
    }
  }
  nd = t;
  for (i = 0; i <= nd; i++) {
    shift[i] = kmin[h[i]];
  }
  for (i = nd + 1; i < m; i++) {
    shift[i] = rmin[h[i]];
  }
  shift[m] = rmin[0];
  t = 0;
  for (i = 0; i < m; i++) {
    nhd0[i] = t;
    if (kmin[i] > 0) {
      t++;
    }
  }
  nhd0[m] = t;
  for (i = 0; i <= nd; i++) {
    next[i] = nhd0[h[i] - kmin[h[i]]];
  }
  for (i = nd + 1; i < m; i++) {
    next[i] = nhd0[m - rmin[h[i]]];
  }
  next[m] = nhd0[m - rmin[h[m - 1]]];
  return nd;
}

static void ss_colussi_or_gg(ss_t *s, bool galil_giancarlo) {
  ss_i *h = (ss_i *)ss_alloc(s, (size_t)(7 * s->m + 16), sizeof(ss_i));
  ss_i *next;
  ss_i *shift;
  ss_i i;
  ss_i j;
  ss_i k;
  ss_i nd;
  ss_i last;
  ss_i ell;
  bool heavy;
  const unsigned char *x = s->x;
  const unsigned char *y = s->y;
  ss_i m = s->m;
  ss_i n = s->n;
  if (h == NULL) {
    return;
  }
  next = h + m + 1;
  shift = next + m + 1;
  if (galil_giancarlo) {
    for (ell = 0; ell + 1 < m && x[ell] == x[ell + 1]; ell++) {
    }
    if (ell == m - 1) {
      /* a power of one character */
      ell = 0;
      for (j = 0; j < n && !s->stop; j++) {
        if (x[0] == y[j]) {
          if (++ell >= m) {
            ss_report(s, j - m + 1);
          }
        } else {
          ell = 0;
        }
      }
      ds_context_free(s->ctx, h);
      return;
    }
  } else {
    ell = 0;
  }
  nd = ss_colussi_pre(s, h, next, shift, shift + m + 1);
  i = j = 0;
  last = -1;
  heavy = false;
  while (j <= n - m && !s->stop) {
    if (galil_giancarlo && heavy && i == 0) {
      k = last - j + 1;
      while (j + k < n && x[0] == y[j + k]) {
        k++;
      }
      if (k <= ell || j + k >= n || x[ell + 1] != y[j + k]) {
        i = 0;
        j += k + 1;
        last = j - 1;
      } else {
        i = 1;
        last = j + k;
        j = last - (ell + 1);
      }
      heavy = false;
    } else {
      while (i < m && last < j + h[i] && x[h[i]] == y[j + h[i]]) {
        i++;
      }
      if (i >= m || last >= j + h[i]) {
        ss_report(s, j);
        i = m;
      }
      if (i > nd) {
        last = j + m - 1;
      }
      j += shift[i];
      i = next[i];
    }
    if (galil_giancarlo) {
      heavy = j <= last;
    }
  }
  ds_context_free(s->ctx, h);
}

/* Reverse Colussi. */
static void ss_reverse_colussi(ss_t *s) {
  const unsigned char *x = s->x;
  const unsigned char *y = s->y;
  ss_i m = s->m;
  ss_i n = s->n;
  ss_i *bc;
  ss_i *gs;
  ss_i *h;
  ss_i *hmin;
  ss_i *kmin;
  ss_i *link;
  ss_i *rmin;
  ss_i locc[256];
  ss_i a;
  ss_i i;
  ss_i jj;
  ss_i k;
  ss_i q;
  ss_i r;
  ss_i t;
  ss_i j;
  ss_i sh;
  bc = (ss_i *)ss_alloc(s, (size_t)(256 * (m + 1) + 8 * (m + 2)), sizeof(ss_i));
  if (bc == NULL) {
    return;
  }
  gs = bc + 256 * (m + 1);
  h = gs + m + 2;
  hmin = h + m + 2;
  kmin = hmin + 2 * (m + 2);
  link = kmin + m + 2;
  rmin = link + m + 2;
  for (a = 0; a < 256; a++) {
    locc[a] = -1;
  }
  link[0] = -1;
  for (i = 0; i < m - 1; i++) {
    link[i + 1] = locc[x[i]];
    locc[x[i]] = i;
  }
  for (a = 0; a < 256; a++) {
    for (t = 1; t <= m; t++) {
      i = locc[a];
      jj = link[m - t];
      while (i - jj != t && jj >= 0) {
        if (i - jj > t) {
          i = link[i + 1];
        } else {
          jj = link[jj + 1];
        }
      }
      while (i - jj > t) {
        i = link[i + 1];
      }
      bc[a * (m + 1) + t] = m - i - 1;
    }
  }
  k = 1;
  i = m - 1;
  while (k <= m) {
    while (i - k >= 0 && x[i - k] == x[i]) {
      i--;
    }
    hmin[k] = i;
    q = k + 1;
    while (q <= m && hmin[q - k] - (q - k) > i) {
      hmin[q] = hmin[q - k];
      q++;
    }
    i += q - k;
    k = q;
    if (i == m) {
      i = m - 1;
    }
  }
  for (i = 0; i < m; i++) {
    kmin[i] = 0;
  }
  for (k = m; k > 0; k--) {
    kmin[hmin[k]] = k;
  }
  r = m;
  for (i = m - 1; i >= 0; i--) {
    if (hmin[i + 1] == i) {
      r = i + 1;
    }
    rmin[i] = r;
  }
  i = 1;
  for (k = 1; k <= m; k++) {
    if (hmin[k] != m - 1 && kmin[hmin[k]] == k) {
      h[i] = hmin[k];
      gs[i++] = k;
    }
  }
  i = m - 1;
  for (jj = m - 2; jj >= 0; jj--) {
    if (kmin[jj] == 0) {
      h[i] = jj;
      gs[i--] = rmin[jj];
    }
  }
  gs[m] = rmin[0];
  j = 0;
  sh = m;
  while (j <= n - m && !s->stop) {
    while (j <= n - m && x[m - 1] != y[j + m - 1]) {
      sh = bc[y[j + m - 1] * (m + 1) + sh];
      j += sh;
    }
    if (j > n - m) {
      break;
    }
    for (i = 1; i < m && x[h[i]] == y[j + h[i]]; i++) {
    }
    if (i >= m) {
      ss_report(s, j);
    }
    sh = gs[i];
    j += sh;
  }
  ds_context_free(s->ctx, bc);
}

/* Galil-Seiferas: constant extra space; the recursive procedures of the
 * original are a loop over a small state machine here. */
static void ss_galil_seiferas(ss_t *s) {
  const unsigned char *x = s->x;
  const unsigned char *y = s->y;
  ss_i m = s->m;
  ss_i n = s->n;
  ss_i k = 4;
  ss_i p = 0;
  ss_i q = 0;
  ss_i t = 0; /* the "s" of the original */
  ss_i p1 = 1;
  ss_i q1 = 0;
  ss_i p2 = 0;
  ss_i q2 = 0;
  int state = 1; /* 1 newP1, 2 newP2, 3 parse, 4 search */
  while (!s->stop) {
    if (state == 1) {
      while (t + p1 + q1 < m && x[t + q1] == x[t + p1 + q1]) {
        q1++;
      }
      if (p1 + q1 >= k * p1) {
        p2 = q1;
        q2 = 0;
        state = 2;
      } else if (t + p1 + q1 == m) {
        state = 4;
      } else {
        p1 += q1 / k + 1;
        q1 = 0;
      }
    } else if (state == 2) {
      while (t + p2 + q2 < m && x[t + q2] == x[t + p2 + q2] &&
             p2 + q2 < k * p2) {
        q2++;
      }
      if (p2 + q2 == k * p2) {
        state = 3;
      } else if (t + p2 + q2 == m) {
        state = 4;
      } else {
        if (q2 == p1 + q1) {
          p2 += p1;
          q2 -= p1;
        } else {
          p2 += q2 / k + 1;
          q2 = 0;
        }
      }
    } else if (state == 3) {
      for (;;) {
        while (t + p1 + q1 < m && x[t + q1] == x[t + p1 + q1]) {
          q1++;
        }
        while (p1 + q1 >= k * p1) {
          t += p1;
          q1 -= p1;
        }
        p1 += q1 / k + 1;
        q1 = 0;
        if (p1 >= p2) {
          break;
        }
      }
      state = 1;
    } else {
      while (p <= n - m && !s->stop) {
        while (p + t + q < n && t + q < m && x[t + q] == y[p + t + q]) {
          q++;
        }
        if (q == m - t && memcmp(x, y + p, (size_t)(t + 1)) == 0) {
          ss_report(s, p);
        }
        if (q == p1 + q1) {
          p += p1;
          q -= p1;
        } else {
          p += q / k + 1;
          q = 0;
        }
      }
      return;
    }
  }
}

/* Maximal Shift and Optimal Mismatch: the pattern is compared in a chosen
 * order of positions; shifts from an adapted good-suffix table and the
 * Quick Search bad character. */
typedef struct ss_pat {
  ss_i loc;
  unsigned char c;
} ss_pat_t;

static void ss_order_pattern(ss_pat_t *pat, ss_i m, const ss_i *key) {
  /* insertion sort: larger key first, ties by larger location */
  ss_i i;
  ss_i j;
  ss_pat_t v;
  for (i = 1; i < m; i++) {
    v = pat[i];
    for (j = i; j > 0 &&
                (key[pat[j - 1].loc] < key[v.loc] ||
                 (key[pat[j - 1].loc] == key[v.loc] && pat[j - 1].loc < v.loc));
         j--) {
      pat[j] = pat[j - 1];
    }
    pat[j] = v;
  }
}

static ss_i ss_match_shift(const unsigned char *x, ss_i m, ss_i ploc,
                           ss_i lshift, const ss_pat_t *pat) {
  ss_i i;
  ss_i j;
  for (; lshift < m; lshift++) {
    i = ploc;
    while (--i >= 0) {
      j = pat[i].loc - lshift;
      if (j < 0) {
        continue;
      }
      if (pat[i].c != x[j]) {
        break;
      }
    }
    if (i < 0) {
      break;
    }
  }
  return lshift;
}

static void ss_ordered_search(ss_t *s, const ss_i *key) {
  const unsigned char *x = s->x;
  const unsigned char *y = s->y;
  ss_i m = s->m;
  ss_i n = s->n;
  ss_pat_t *pat;
  ss_i *gs;
  ss_i qs[256];
  ss_i i;
  ss_i j;
  ss_i ploc;
  ss_i lshift;
  ss_i sh;
  pat = (ss_pat_t *)ss_alloc(s, (size_t)(m + 1), sizeof(ss_pat_t));
  gs = (ss_i *)ss_alloc(s, (size_t)(m + 1), sizeof(ss_i));
  if (pat == NULL || gs == NULL) {
    ds_context_free(s->ctx, pat);
    ds_context_free(s->ctx, gs);
    return;
  }
  for (i = 0; i < m; i++) {
    pat[i].loc = i;
    pat[i].c = x[i];
  }
  ss_order_pattern(pat, m, key);
  ss_qs_bc(s, qs);
  gs[0] = lshift = 1;
  for (ploc = 1; ploc <= m; ploc++) {
    lshift = ss_match_shift(x, m, ploc, lshift, pat);
    gs[ploc] = lshift;
  }
  for (ploc = 0; ploc < m; ploc++) {
    lshift = gs[ploc];
    while (lshift < m) {
      i = pat[ploc].loc - lshift;
      if (i < 0 || pat[ploc].c != x[i]) {
        break;
      }
      lshift++;
      lshift = ss_match_shift(x, m, ploc, lshift, pat);
    }
    gs[ploc] = lshift;
  }
  j = 0;
  while (j <= n - m && !s->stop) {
    i = 0;
    while (i < m && pat[i].c == y[j + pat[i].loc]) {
      i++;
    }
    if (i >= m) {
      ss_report(s, j);
    }
    sh = gs[i];
    if (j + m < n && qs[y[j + m]] > sh) {
      sh = qs[y[j + m]];
    }
    j += sh;
  }
  ds_context_free(s->ctx, pat);
  ds_context_free(s->ctx, gs);
}

static void ss_maximal_shift(ss_t *s) {
  ss_i *minshift = (ss_i *)ss_alloc(s, (size_t)s->m, sizeof(ss_i));
  ss_i i;
  ss_i j;
  if (minshift == NULL) {
    return;
  }
  for (i = 0; i < s->m; i++) {
    for (j = i - 1; j >= 0 && s->x[i] != s->x[j]; j--) {
    }
    minshift[i] = i - j;
  }
  ss_ordered_search(s, minshift);
  ds_context_free(s->ctx, minshift);
}

static void ss_optimal_mismatch(ss_t *s) {
  /* rarest text characters first: key = -frequency */
  ss_i freq[256];
  ss_i *key = (ss_i *)ss_alloc(s, (size_t)s->m, sizeof(ss_i));
  ss_i i;
  if (key == NULL) {
    return;
  }
  for (i = 0; i < 256; i++) {
    freq[i] = 0;
  }
  for (i = 0; i < s->n; i++) {
    freq[s->y[i]]++;
  }
  for (i = 0; i < s->m; i++) {
    key[i] = -freq[s->x[i]];
  }
  ss_ordered_search(s, key);
  ds_context_free(s->ctx, key);
}

/* Reverse Factor and Turbo Reverse Factor: a suffix automaton of the
 * reversed pattern recognises the window's suffixes read right to left. */
typedef struct ss_rf {
  ds_suffix_automaton_t sa;
  unsigned char *terminal;
} ss_rf_t;

static int ss_rf_init(ss_t *s, ss_rf_t *rf) {
  ss_i i;
  size_t u;
  if (ds_suffix_automaton_init(&rf->sa, s->ctx) != DS_OK) {
    s->failed = true;
    return 0;
  }
  for (i = s->m; i-- > 0;) {
    if (ds_suffix_automaton_extend(&rf->sa, s->x[i]) != DS_OK) {
      ds_suffix_automaton_destroy(&rf->sa);
      s->failed = true;
      return 0;
    }
  }
  rf->terminal = (unsigned char *)ds_context_calloc(s->ctx, rf->sa.states, 1);
  if (rf->terminal == NULL) {
    ds_suffix_automaton_destroy(&rf->sa);
    s->failed = true;
    return 0;
  }
  for (u = rf->sa.last; u != (size_t)-1; u = rf->sa.link[u]) {
    rf->terminal[u] = 1;
  }
  return 1;
}

static void ss_rf_free(ss_t *s, ss_rf_t *rf) {
  ds_context_free(s->ctx, rf->terminal);
  ds_suffix_automaton_destroy(&rf->sa);
}

static void ss_reverse_factor(ss_t *s) {
  ss_rf_t rf;
  ss_i i;
  ss_i j = 0;
  ss_i shift;
  ss_i period = s->m;
  size_t state;
  size_t nx;
  if (!ss_rf_init(s, &rf)) {
    return;
  }
  while (j <= s->n - s->m && !s->stop) {
    i = s->m - 1;
    state = 0;
    shift = s->m;
    while (i + j >= 0 && (nx = ds_suffix_automaton_next(
                              &rf.sa, state, s->y[i + j])) != (size_t)-1) {
      state = nx;
      if (rf.terminal[state]) {
        period = shift;
        shift = i;
      }
      i--;
    }
    if (i < 0) {
      ss_report(s, j);
      shift = period;
    }
    j += shift;
  }
  ss_rf_free(s, &rf);
}

static void ss_turbo_reverse_factor(ss_t *s) {
  ss_rf_t rf;
  ss_i *mp;
  ss_i i;
  ss_i j;
  ss_i u;
  ss_i shift;
  ss_i period;
  ss_i period_u;
  ss_i disp;
  ss_i mu;
  ss_i m = s->m;
  size_t state;
  size_t nx;
  mp = (ss_i *)ss_alloc(s, (size_t)(m + 1), sizeof(ss_i));
  if (mp == NULL) {
    return;
  }
  if (!ss_rf_init(s, &rf)) {
    ds_context_free(s->ctx, mp);
    return;
  }
  ss_borders(s, mp, false);
  period = m - mp[m];
  shift = m;
  if (memcmp(s->x, s->y, (size_t)m) == 0) {
    ss_report(s, 0);
  }
  j = 1;
#define SS_TRF_SCAN(limit)                                                     \
  while (i > (limit) && (nx = ds_suffix_automaton_next(                        \
                             &rf.sa, state, s->y[i + j])) != (size_t) - 1) {   \
    state = nx;                                                                \
    if (rf.terminal[state]) {                                                  \
      shift = i;                                                               \
    }                                                                          \
    i--;                                                                       \
  }
  while (j <= s->n - m && !s->stop) {
    i = m - 1;
    state = 0;
    u = m - 1 - shift;
    period_u = shift != m ? m - shift - mp[m - shift] : 0;
    shift = m;
    SS_TRF_SCAN(u)
    disp = state == 0 ? 0 : (ss_i)rf.sa.firstpos[state] + 1 - (m - 1 - i);
    if (i <= u) {
      if (disp == 0) {
        ss_report(s, j);
        shift = period;
      } else {
        mu = (u + 1) / 2;
        if (period_u <= mu) {
          u -= period_u;
          SS_TRF_SCAN(u)
          if (i <= u) {
            shift = (ss_i)rf.sa.firstpos[state] + 1 - (m - 1 - i);
          }
        } else {
          u = u - mu - 1;
          SS_TRF_SCAN(u)
        }
      }
    }
    j += shift;
  }
#undef SS_TRF_SCAN
  ss_rf_free(s, &rf);
  ds_context_free(s->ctx, mp);
}

/* String matching on ordered alphabets (Crochemore): constant extra space,
 * maximal suffixes of the matched prefix found on the fly. */
static void ss_next_max_suffix(const unsigned char *x, ss_i m, ss_i *i, ss_i *j,
                               ss_i *k, ss_i *p) {
  unsigned char a;
  unsigned char b;
  while (*j + *k < m) {
    a = x[*i + *k];
    b = x[*j + *k];
    if (a == b) {
      if (*k == *p) {
        *j += *p;
        *k = 1;
      } else {
        (*k)++;
      }
    } else if (a > b) {
      *j += *k;
      *k = 1;
      *p = *j - *i;
    } else {
      *i = *j;
      (*j)++;
      *k = *p = 1;
    }
  }
}

static void ss_ordered_alphabet(ss_t *s) {
  const unsigned char *x = s->x;
  const unsigned char *y = s->y;
  ss_i m = s->m;
  ss_i n = s->n;
  ss_i i = 0;
  ss_i ip = -1;
  ss_i j = 0;
  ss_i jp = 0;
  ss_i k = 1;
  ss_i p = 1;
  ss_i len;
  ss_i t;
  while (j <= n - m && !s->stop) {
    while (i + j < n && i < m && x[i] == y[i + j]) {
      i++;
    }
    if (i == 0) {
      j++;
      ip = -1;
      jp = 0;
      k = p = 1;
      continue;
    }
    if (i >= m) {
      ss_report(s, j);
    }
    len = i + 1 < n - j ? i + 1 : n - j;
    ss_next_max_suffix(y + j, len, &ip, &jp, &k, &p);
    if (ip < 0 || (ip < p && memcmp(y + j, y + j + p, (size_t)(ip + 1)) == 0)) {
      j += p;
      i -= p;
      if (i < 0) {
        i = 0;
      }
      if (jp - ip > p) {
        jp -= p;
      } else {
        ip = -1;
        jp = 0;
        k = p = 1;
      }
    } else {
      t = i - ip - 1 < jp + 1 ? i - ip - 1 : jp + 1;
      j += (ip + 1 > t ? ip + 1 : t) + 1;
      i = jp = 0;
      ip = -1;
      k = p = 1;
    }
  }
}

/* ---- dispatch ------------------------------------------------------------ */

static const char *const ss_names[DS_SEARCH_ALGORITHM_COUNT] = {
    "brute force",
    "Morris-Pratt",
    "Boyer-Moore",
    "Horspool",
    "Raita",
    "Quick Search",
    "Smith",
    "Zhu-Takaoka",
    "Turbo-BM",
    "Apostolico-Giancarlo",
    "Apostolico-Crochemore",
    "Not So Naive",
    "Two Way",
    "Shift-Or",
    "DFA",
    "Skip Search",
    "Alpha Skip Search",
    "KmpSkip Search",
    "Colussi",
    "Reverse Colussi",
    "Galil-Giancarlo",
    "Galil-Seiferas",
    "Maximal Shift",
    "Optimal Mismatch",
    "Reverse Factor",
    "Turbo Reverse Factor",
    "ordered alphabets"};

const char *ds_string_search_name(ds_string_search_algorithm_t algorithm) {
  if ((int)algorithm < 0 || algorithm >= DS_SEARCH_ALGORITHM_COUNT) {
    return "unknown";
  }
  return ss_names[algorithm];
}

ds_status_t ds_string_search_status(ds_string_search_algorithm_t algorithm,
                                    const unsigned char *text, size_t n,
                                    const unsigned char *pattern, size_t m,
                                    ds_match_func_t on_match, void *user,
                                    size_t *out_count, ds_context_t *context) {
  ss_t s;
  if (out_count != NULL) {
    *out_count = 0;
  }
  if ((n > 0 && text == NULL) || (m > 0 && pattern == NULL)) {
    return DS_ERR_NULL;
  }
  if ((int)algorithm < 0 || algorithm >= DS_SEARCH_ALGORITHM_COUNT) {
    return DS_ERR_INVALID;
  }
  if (n > (size_t)(((size_t)-1) >> 2)) {
    return DS_ERR_OVERFLOW;
  }
  if (m == 0 || m > n) {
    return DS_OK; /* nothing can match */
  }
  s.y = text;
  s.n = (ss_i)n;
  s.x = pattern;
  s.m = (ss_i)m;
  s.on_match = on_match;
  s.user = user;
  s.count = 0;
  s.stop = false;
  s.failed = false;
  s.ctx = context;
  switch (algorithm) {
  case DS_SEARCH_BRUTE_FORCE:
    ss_brute(&s);
    break;
  case DS_SEARCH_MORRIS_PRATT:
    ss_morris_pratt(&s);
    break;
  case DS_SEARCH_BOYER_MOORE:
    ss_boyer_moore(&s);
    break;
  case DS_SEARCH_HORSPOOL:
    ss_horspool(&s);
    break;
  case DS_SEARCH_RAITA:
    ss_raita(&s);
    break;
  case DS_SEARCH_QUICK_SEARCH:
    ss_quick_search(&s);
    break;
  case DS_SEARCH_SMITH:
    ss_smith(&s);
    break;
  case DS_SEARCH_ZHU_TAKAOKA:
    ss_zhu_takaoka(&s);
    break;
  case DS_SEARCH_TURBO_BM:
    ss_turbo_bm(&s);
    break;
  case DS_SEARCH_APOSTOLICO_GIANCARLO:
    ss_apostolico_giancarlo(&s);
    break;
  case DS_SEARCH_APOSTOLICO_CROCHEMORE:
    ss_apostolico_crochemore(&s);
    break;
  case DS_SEARCH_NOT_SO_NAIVE:
    ss_not_so_naive(&s);
    break;
  case DS_SEARCH_TWO_WAY:
    ss_two_way(&s);
    break;
  case DS_SEARCH_SHIFT_OR:
    ss_shift_or(&s);
    break;
  case DS_SEARCH_DFA:
    ss_dfa(&s);
    break;
  case DS_SEARCH_SKIP:
    ss_skip_q(&s, 1);
    break;
  case DS_SEARCH_ALPHA_SKIP:
    ss_alpha_skip(&s);
    break;
  case DS_SEARCH_KMP_SKIP:
    ss_kmp_skip(&s);
    break;
  case DS_SEARCH_COLUSSI:
    ss_colussi_or_gg(&s, false);
    break;
  case DS_SEARCH_REVERSE_COLUSSI:
    ss_reverse_colussi(&s);
    break;
  case DS_SEARCH_GALIL_GIANCARLO:
    ss_colussi_or_gg(&s, true);
    break;
  case DS_SEARCH_GALIL_SEIFERAS:
    ss_galil_seiferas(&s);
    break;
  case DS_SEARCH_MAXIMAL_SHIFT:
    ss_maximal_shift(&s);
    break;
  case DS_SEARCH_OPTIMAL_MISMATCH:
    ss_optimal_mismatch(&s);
    break;
  case DS_SEARCH_REVERSE_FACTOR:
    ss_reverse_factor(&s);
    break;
  case DS_SEARCH_TURBO_REVERSE_FACTOR:
    ss_turbo_reverse_factor(&s);
    break;
  case DS_SEARCH_ORDERED_ALPHABET:
    ss_ordered_alphabet(&s);
    break;
  case DS_SEARCH_ALGORITHM_COUNT:
  default:
    return DS_ERR_INVALID;
  }
  if (out_count != NULL) {
    *out_count = s.count;
  }
  if (s.failed) {
    return DS_CONTEXT_ERROR(context, DS_ERR_ALLOC,
                            "ds.string_search/scratch_alloc", "string_search",
                            "scratch allocation failed: matches incomplete");
  }
  return DS_OK;
}

size_t ds_string_search(ds_string_search_algorithm_t algorithm,
                        const unsigned char *text, size_t n,
                        const unsigned char *pattern, size_t m,
                        ds_match_func_t on_match, void *user,
                        ds_context_t *context) {
  size_t count = 0;
  (void)ds_string_search_status(algorithm, text, n, pattern, m, on_match, user,
                                &count, context);
  return count;
}

size_t ds_kmismatch_search(const unsigned char *text, size_t n,
                           const unsigned char *pattern, size_t m, size_t k,
                           ds_match_func_t on_match, void *user) {
  size_t j;
  size_t i;
  size_t miss;
  size_t count = 0;
  if (text == NULL || pattern == NULL || m == 0 || m > n) {
    return 0;
  }
  for (j = 0; j + m <= n; j++) {
    miss = 0;
    for (i = 0; i < m && miss <= k; i++) {
      miss += pattern[i] != text[j + i];
    }
    if (miss <= k) {
      count++;
      if (on_match != NULL && !on_match(j, user)) {
        break;
      }
    }
  }
  return count;
}

/* ---- Commentz-Walter ----------------------------------------------------- */

typedef struct cw {
  size_t nodes;
  size_t *depth;
  size_t *fail;
  size_t *out; /* first pattern ending here, or CW_NONE */
  size_t *shift1;
  size_t *shift2;
  size_t *queue;
  size_t *first_child;
  size_t *sibling;
  size_t *label;
  size_t *hkey; /* node * 256 + byte + 1, 0 empty */
  size_t *hval;
  size_t hmask;
  size_t *pat_next;
} cw_t;

#define CW_NONE (~(size_t)0)

static size_t cw_slot(const cw_t *t, size_t key) {
  size_t h = (key * (size_t)2654435761u) & t->hmask;
  while (t->hkey[h] != 0 && t->hkey[h] != key) {
    h = (h + 1) & t->hmask;
  }
  return h;
}

static size_t cw_child(const cw_t *t, size_t v, unsigned char c) {
  size_t h = cw_slot(t, v * 256 + c + 1);
  return t->hkey[h] == 0 ? CW_NONE : t->hval[h];
}

size_t ds_commentz_walter_search(const unsigned char *text, size_t n,
                                 const unsigned char *const *patterns,
                                 const size_t *lengths, size_t count,
                                 ds_multi_match_func_t on_match, void *user,
                                 ds_context_t *context) {
  cw_t t;
  size_t total = 1;
  size_t wmin = CW_NONE;
  size_t chr[256];
  size_t hsize = 2;
  size_t words;
  size_t *block;
  size_t p;
  size_t k;
  size_t v;
  size_t w;
  size_t f;
  size_t head;
  size_t tail;
  size_t i;
  size_t j;
  size_t sh;
  size_t found = 0;
  size_t c;
  if (text == NULL || patterns == NULL || lengths == NULL) {
    return 0;
  }
  for (p = 0; p < count; p++) {
    if (lengths[p] == 0 || patterns[p] == NULL) {
      continue;
    }
    if (lengths[p] > ((size_t)-1) / 1024 - total) {
      return 0;
    }
    total += lengths[p];
    if (lengths[p] < wmin) {
      wmin = lengths[p];
    }
  }
  if (wmin == CW_NONE || wmin > n) {
    return 0;
  }
  while (hsize < 2 * total) {
    hsize *= 2;
  }
  words = 9 * total + 2 * hsize + count;
  block = (size_t *)ds_context_alloc(context, words * sizeof(size_t));
  if (block == NULL) {
    return 0;
  }
  t.depth = block;
  t.fail = t.depth + total;
  t.out = t.fail + total;
  t.shift1 = t.out + total;
  t.shift2 = t.shift1 + total;
  t.queue = t.shift2 + total;
  t.first_child = t.queue + total;
  t.sibling = t.first_child + total;
  t.label = t.sibling + total;
  t.hkey = t.label + total;
  t.hval = t.hkey + hsize;
  t.pat_next = t.hval + hsize;
  t.hmask = hsize - 1;
  for (k = 0; k < hsize; k++) {
    t.hkey[k] = 0;
  }
  for (c = 0; c < 256; c++) {
    chr[c] = wmin + 1;
  }
  /* the trie of the reversed patterns */
  t.nodes = 1;
  t.depth[0] = 0;
  t.out[0] = CW_NONE;
  t.first_child[0] = CW_NONE;
  for (p = 0; p < count; p++) {
    if (lengths[p] == 0 || patterns[p] == NULL) {
      continue;
    }
    v = 0;
    for (k = lengths[p]; k-- > 0;) {
      c = patterns[p][k];
      w = cw_child(&t, v, (unsigned char)c);
      if (w == CW_NONE) {
        w = t.nodes++;
        t.depth[w] = t.depth[v] + 1;
        t.out[w] = CW_NONE;
        t.first_child[w] = CW_NONE;
        t.label[w] = c;
        t.sibling[w] = t.first_child[v];
        t.first_child[v] = w;
        f = cw_slot(&t, v * 256 + c + 1);
        t.hkey[f] = v * 256 + c + 1;
        t.hval[f] = w;
        if (t.depth[w] < chr[c]) {
          chr[c] = t.depth[w];
        }
      }
      v = w;
    }
    t.pat_next[p] = t.out[v];
    t.out[v] = p;
  }
  /* failure links in breadth-first order */
  head = tail = 0;
  t.fail[0] = 0;
  for (w = t.first_child[0]; w != CW_NONE; w = t.sibling[w]) {
    t.fail[w] = 0;
    t.queue[tail++] = w;
  }
  while (head < tail) {
    v = t.queue[head++];
    for (w = t.first_child[v]; w != CW_NONE; w = t.sibling[w]) {
      f = t.fail[v];
      while (f != 0 && cw_child(&t, f, (unsigned char)t.label[w]) == CW_NONE) {
        f = t.fail[f];
      }
      f = cw_child(&t, f, (unsigned char)t.label[w]);
      t.fail[w] = f == CW_NONE || f == w ? 0 : f;
      t.queue[tail++] = w;
    }
  }
  /* shift1: nearest node whose word has this one as a proper suffix;
   * shift2: the same over complete words (via the failure tree, deepest
   * nodes first), then the minimum along the path from the root */
  for (v = 0; v < t.nodes; v++) {
    t.shift1[v] = wmin;
    t.shift2[v] = CW_NONE; /* best complete-word distance below */
  }
  for (k = tail; k-- > 0;) {
    w = t.queue[k];
    f = t.fail[w];
    sh = t.depth[w] - t.depth[f];
    if (sh < t.shift1[f]) {
      t.shift1[f] = sh;
    }
    if (t.out[w] != CW_NONE && sh < t.shift2[f]) {
      t.shift2[f] = sh;
    }
    if (t.shift2[w] != CW_NONE && t.shift2[w] + sh < t.shift2[f]) {
      t.shift2[f] = t.shift2[w] + sh;
    }
  }
  if (t.shift2[0] > wmin) {
    t.shift2[0] = wmin;
  }
  /* top-down: parents come before their children in the queue */
  for (k = 0; k < tail + 1; k++) {
    v = k == 0 ? 0 : t.queue[k - 1];
    for (w = t.first_child[v]; w != CW_NONE; w = t.sibling[w]) {
      if (t.shift2[w] > t.shift2[v]) {
        t.shift2[w] = t.shift2[v];
      }
    }
  }
  /* scanning: right ends of the window move left to right, each window
   * read right to left through the trie */
  i = wmin - 1;
  while (i < n) {
    v = 0;
    j = 0;
    while (j <= i) {
      w = cw_child(&t, v, text[i - j]);
      if (w == CW_NONE) {
        break;
      }
      v = w;
      j++;
      for (p = t.out[v]; p != CW_NONE; p = t.pat_next[p]) {
        found++;
        if (on_match != NULL && !on_match(p, i - j + 1, user)) {
          goto done;
        }
      }
    }
    if (j <= i) {
      c = chr[text[i - j]];
      sh = c > j + 1 ? c - j - 1 : 0;
      if (sh < t.shift1[v]) {
        sh = t.shift1[v];
      }
    } else {
      sh = t.shift1[v];
    }
    if (sh > t.shift2[v]) {
      sh = t.shift2[v];
    }
    i += sh;
  }
done:
  ds_context_free(context, block);
  return found;
}
