#include "string_search.h"
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

static ss_i ss_max(ss_i a, ss_i b) {
  return a > b ? a : b;
}
static ss_i ss_min(ss_i a, ss_i b) {
  return a < b ? a : b;
}

static void *ss_alloc(ss_t *s, size_t count, size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > ((size_t)-1) / size) {
    return NULL;
  }
  return ds_context_alloc(s->ctx, count * size);
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
    "Alpha Skip Search"};

const char *ds_string_search_name(ds_string_search_algorithm_t algorithm) {
  if ((int)algorithm < 0 || algorithm >= DS_SEARCH_ALGORITHM_COUNT) {
    return "unknown";
  }
  return ss_names[algorithm];
}

size_t ds_string_search(ds_string_search_algorithm_t algorithm,
                        const unsigned char *text, size_t n,
                        const unsigned char *pattern, size_t m,
                        ds_match_func_t on_match, void *user,
                        ds_context_t *context) {
  ss_t s;
  if (text == NULL || pattern == NULL || m == 0 || m > n ||
      n > (size_t)(((size_t)-1) >> 2)) {
    return 0;
  }
  s.y = text;
  s.n = (ss_i)n;
  s.x = pattern;
  s.m = (ss_i)m;
  s.on_match = on_match;
  s.user = user;
  s.count = 0;
  s.stop = false;
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
  case DS_SEARCH_ALGORITHM_COUNT:
  default:
    return 0;
  }
  return s.count;
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
