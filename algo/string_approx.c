#include "string_approx.h"
#include <limits.h>
#include <string.h>

#define SA_NONE (~(size_t)0)
#define SA_NEG_INF (-1e300)

static void *sa_alloc(ds_context_t *ctx, size_t count, size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > ((size_t)-1) / size) {
    return NULL;
  }
  return ds_context_alloc(ctx, count * size);
}

/* (n + 1) (m + 1) cells, or 0 on overflow. */
static size_t sa_cells(size_t n, size_t m) {
  if (n == (size_t)-1 || m == (size_t)-1 || n + 1 > ((size_t)-1) / (m + 1)) {
    return 0;
  }
  return (n + 1) * (m + 1);
}

static int sa_args(const unsigned char *a, size_t n, const unsigned char *b,
                   size_t m) {
  return (a != NULL || n == 0) && (b != NULL || m == 0);
}

/* ---- edit script ---------------------------------------------------- */

static double sa_sub(const ds_edit_costs_t *c, unsigned char x,
                     unsigned char y) {
  if (x == y) {
    return 0.0;
  }
  return c != NULL && c->substitute != NULL ? c->substitute(x, y, c->user)
                                            : 1.0;
}

static double sa_ins(const ds_edit_costs_t *c, unsigned char y) {
  return c != NULL && c->insert != NULL ? c->insert(y, c->user) : 1.0;
}

static double sa_del(const ds_edit_costs_t *c, unsigned char x) {
  return c != NULL && c->remove != NULL ? c->remove(x, c->user) : 1.0;
}

ds_status_t ds_edit_script(const unsigned char *a, size_t n,
                           const unsigned char *b, size_t m,
                           const ds_edit_costs_t *costs, unsigned char *out_ops,
                           size_t *out_op_count, double *out_cost,
                           ds_context_t *context) {
  double *prev;
  double *cur;
  double *t;
  unsigned char *dir = NULL;
  size_t cells;
  size_t i;
  size_t j;
  size_t k;
  double d;
  double best;
  unsigned char how;
  if (!sa_args(a, n, b, m) || out_cost == NULL) {
    return DS_ERR_NULL;
  }
  cells = sa_cells(n, m);
  if (cells == 0) {
    return DS_ERR_OVERFLOW;
  }
  prev = (double *)sa_alloc(context, 2 * (m + 1), sizeof(double));
  if (out_ops != NULL) {
    dir = (unsigned char *)sa_alloc(context, cells, 1);
  }
  if (prev == NULL || (out_ops != NULL && dir == NULL)) {
    ds_context_free(context, prev);
    ds_context_free(context, dir);
    return DS_ERR_ALLOC;
  }
  cur = prev + (m + 1);
  prev[0] = 0.0;
  for (j = 1; j <= m; j++) {
    prev[j] = prev[j - 1] + sa_ins(costs, b[j - 1]);
    if (dir != NULL) {
      dir[j] = DS_EDIT_INSERT;
    }
  }
  for (i = 1; i <= n; i++) {
    cur[0] = prev[0] + sa_del(costs, a[i - 1]);
    if (dir != NULL) {
      dir[i * (m + 1)] = DS_EDIT_DELETE;
    }
    for (j = 1; j <= m; j++) {
      best = prev[j - 1] + sa_sub(costs, a[i - 1], b[j - 1]);
      how = a[i - 1] == b[j - 1] ? DS_EDIT_MATCH : DS_EDIT_SUBSTITUTE;
      d = prev[j] + sa_del(costs, a[i - 1]);
      if (d < best) {
        best = d;
        how = DS_EDIT_DELETE;
      }
      d = cur[j - 1] + sa_ins(costs, b[j - 1]);
      if (d < best) {
        best = d;
        how = DS_EDIT_INSERT;
      }
      cur[j] = best;
      if (dir != NULL) {
        dir[i * (m + 1) + j] = how;
      }
    }
    t = prev;
    prev = cur;
    cur = t;
  }
  *out_cost = prev[m];
  if (dir != NULL) {
    k = 0;
    i = n;
    j = m;
    while (i > 0 || j > 0) {
      how = dir[i * (m + 1) + j];
      out_ops[k++] = how;
      if (how == DS_EDIT_DELETE) {
        i--;
      } else if (how == DS_EDIT_INSERT) {
        j--;
      } else {
        i--;
        j--;
      }
    }
    for (i = 0; i < k / 2; i++) {
      how = out_ops[i];
      out_ops[i] = out_ops[k - 1 - i];
      out_ops[k - 1 - i] = how;
    }
    if (out_op_count != NULL) {
      *out_op_count = k;
    }
  } else if (out_op_count != NULL) {
    *out_op_count = 0;
  }
  ds_context_free(context, prev < cur ? prev : cur);
  ds_context_free(context, dir);
  return DS_OK;
}

/* ---- alignment (Gotoh, global or local) ----------------------------- */

#define AL_H_STOP 0u
#define AL_H_DIAG 1u
#define AL_H_E 2u
#define AL_H_F 3u
#define AL_E_EXT 4u
#define AL_F_EXT 8u

static double al_score(const ds_align_scoring_t *s, unsigned char x,
                       unsigned char y) {
  if (s->score != NULL) {
    return s->score(x, y, s->user);
  }
  return x == y ? s->match : s->mismatch;
}

ds_status_t ds_align(const unsigned char *a, size_t n, const unsigned char *b,
                     size_t m, const ds_align_scoring_t *scoring,
                     ds_align_mode_t mode, unsigned char *out_ops,
                     ds_alignment_t *out, ds_context_t *context) {
  double *block;
  double *hp;
  double *hc;
  double *fp; /* F of the previous row */
  double *fc;
  double *t;
  double e;
  double hv;
  double cand;
  double best = 0.0;
  double open;
  double ext;
  unsigned char *tb = NULL;
  unsigned char flags;
  size_t cells;
  size_t i;
  size_t j;
  size_t bi = 0;
  size_t bj = 0;
  size_t k;
  int state;
  int local = mode == DS_ALIGN_LOCAL;
  if (!sa_args(a, n, b, m) || scoring == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (mode != DS_ALIGN_GLOBAL && mode != DS_ALIGN_LOCAL) {
    return DS_ERR_INVALID;
  }
  cells = sa_cells(n, m);
  if (cells == 0) {
    return DS_ERR_OVERFLOW;
  }
  open = scoring->gap_open;
  ext = scoring->gap_extend;
  block = hp = (double *)sa_alloc(context, 4 * (m + 1), sizeof(double));
  if (out_ops != NULL) {
    tb = (unsigned char *)sa_alloc(context, cells, 1);
  }
  if (hp == NULL || (out_ops != NULL && tb == NULL)) {
    ds_context_free(context, block);
    ds_context_free(context, tb);
    return DS_ERR_ALLOC;
  }
  hc = hp + (m + 1);
  fp = hc + (m + 1);
  fc = fp + (m + 1);
  hp[0] = 0.0;
  fp[0] = SA_NEG_INF;
  if (tb != NULL) {
    tb[0] = AL_H_STOP;
  }
  for (j = 1; j <= m; j++) {
    hp[j] = local ? 0.0 : -(open + (double)(j - 1) * ext);
    fp[j] = SA_NEG_INF;
    if (tb != NULL) {
      tb[j] =
          local ? AL_H_STOP : (unsigned char)(AL_H_E | (j > 1 ? AL_E_EXT : 0));
    }
  }
  if (local) {
    bi = 0;
    bj = 0;
    best = 0.0;
  }
  for (i = 1; i <= n; i++) {
    hc[0] = local ? 0.0 : -(open + (double)(i - 1) * ext);
    fc[0] = hc[0];
    e = SA_NEG_INF;
    if (tb != NULL) {
      tb[i * (m + 1)] =
          local ? AL_H_STOP : (unsigned char)(AL_H_F | (i > 1 ? AL_F_EXT : 0));
    }
    for (j = 1; j <= m; j++) {
      flags = 0;
      /* E: gap in a (insertion of b[j - 1]) */
      cand = hc[j - 1] - open;
      if (e - ext > cand) {
        cand = e - ext;
        flags |= AL_E_EXT;
      }
      e = cand;
      /* F: gap in b (deletion of a[i - 1]) */
      cand = hp[j] - open;
      if (fp[j] - ext > cand) {
        cand = fp[j] - ext;
        flags |= AL_F_EXT;
      }
      fc[j] = cand;
      hv = hp[j - 1] + al_score(scoring, a[i - 1], b[j - 1]);
      flags |= AL_H_DIAG;
      if (e > hv) {
        hv = e;
        flags = (unsigned char)((flags & ~3u) | AL_H_E);
      }
      if (fc[j] > hv) {
        hv = fc[j];
        flags = (unsigned char)((flags & ~3u) | AL_H_F);
      }
      if (local && !(hv > 0.0)) {
        hv = 0.0;
        flags = (unsigned char)(flags & ~3u);
      }
      hc[j] = hv;
      if (tb != NULL) {
        tb[i * (m + 1) + j] = flags;
      }
      if (local && hv > best) {
        best = hv;
        bi = i;
        bj = j;
      }
    }
    t = hp;
    hp = hc;
    hc = t;
    t = fp;
    fp = fc;
    fc = t;
  }
  if (!local) {
    best = hp[m];
    bi = n;
    bj = m;
  }
  out->score = best;
  out->a_end = bi;
  out->b_end = bj;
  out->a_begin = bi;
  out->b_begin = bj;
  out->op_count = 0;
  if (tb != NULL) {
    k = 0;
    i = bi;
    j = bj;
    state = 0; /* 0: H, 1: E, 2: F */
    for (;;) {
      flags = tb[i * (m + 1) + j];
      if (state == 0) {
        if ((flags & 3u) == AL_H_STOP || (i == 0 && j == 0)) {
          break;
        }
        if ((flags & 3u) == AL_H_DIAG) {
          out_ops[k++] =
              a[i - 1] == b[j - 1] ? DS_EDIT_MATCH : DS_EDIT_SUBSTITUTE;
          i--;
          j--;
          continue;
        }
        state = (flags & 3u) == AL_H_E ? 1 : 2;
        continue;
      }
      if (state == 1) {
        out_ops[k++] = DS_EDIT_INSERT;
        state = (flags & AL_E_EXT) ? 1 : 0;
        j--;
      } else {
        out_ops[k++] = DS_EDIT_DELETE;
        state = (flags & AL_F_EXT) ? 2 : 0;
        i--;
      }
    }
    out->a_begin = i;
    out->b_begin = j;
    for (i = 0; i < k / 2; i++) {
      flags = out_ops[i];
      out_ops[i] = out_ops[k - 1 - i];
      out_ops[k - 1 - i] = flags;
    }
    out->op_count = k;
  }
  ds_context_free(context, tb);
  ds_context_free(context, block);
  return DS_OK;
}

/* ---- approximate search --------------------------------------------- */

ds_status_t ds_approx_search(const unsigned char *text, size_t n,
                             const unsigned char *pattern, size_t m, size_t k,
                             ds_approx_algorithm_t algorithm,
                             ds_approx_match_func_t on_match, void *user,
                             size_t *out_count, ds_context_t *context) {
  size_t bits = sizeof(unsigned long) * CHAR_BIT;
  size_t count = 0;
  size_t j;
  size_t i;
  if (!sa_args(text, n, pattern, m) || on_match == NULL) {
    return DS_ERR_NULL;
  }
  if (out_count != NULL) {
    *out_count = 0;
  }
  if (algorithm == DS_APPROX_AUTO) {
    algorithm = m >= 1 && m <= bits ? DS_APPROX_MYERS : DS_APPROX_SELLERS;
  }
  if (algorithm == DS_APPROX_MYERS) {
    unsigned long peq[256];
    unsigned long pv;
    unsigned long mv = 0;
    unsigned long eq;
    unsigned long xv;
    unsigned long xh;
    unsigned long ph;
    unsigned long mh;
    unsigned long high;
    unsigned long mask;
    size_t score = m;
    if (m == 0 || m > bits) {
      return DS_ERR_INVALID;
    }
    mask = m == bits ? ~0UL : ((1UL << m) - 1UL);
    high = 1UL << (m - 1);
    memset(peq, 0, sizeof(peq));
    for (i = 0; i < m; i++) {
      peq[pattern[i]] |= 1UL << i;
    }
    pv = mask;
    for (j = 0; j < n; j++) {
      eq = peq[text[j]];
      xv = eq | mv;
      xh = ((((eq & pv) + pv) & mask) ^ pv) | eq;
      ph = mv | (~(xh | pv) & mask);
      mh = pv & xh;
      if (ph & high) {
        score++;
      } else if (mh & high) {
        score--;
      }
      ph = (ph << 1) & mask;
      mh = (mh << 1) & mask;
      pv = mh | (~(xv | ph) & mask);
      mv = ph & xv;
      if (score <= k) {
        count++;
        if (!on_match(j + 1, score, user)) {
          break;
        }
      }
    }
  } else if (algorithm == DS_APPROX_SELLERS) {
    /* Column-wise dynamic programming with Ukkonen's cut-off: only rows
     * up to lact, the last one that can still be <= k, are computed;
     * rows below hold k + 1 (any value > k behaves the same). */
    size_t *c;
    size_t lact;
    size_t pc;
    size_t nc;
    size_t v;
    if (m == 0) {
      for (j = 0; j <= n; j++) {
        count++;
        if (!on_match(j, 0, user)) {
          break;
        }
      }
      if (out_count != NULL) {
        *out_count = count;
      }
      return DS_OK;
    }
    c = (size_t *)sa_alloc(context, m + 1, sizeof(size_t));
    if (c == NULL) {
      return DS_ERR_ALLOC;
    }
    lact = k + 1 < m ? k + 1 : m;
    for (i = 0; i <= m; i++) {
      c[i] = i <= lact ? i : k + 1;
    }
    for (j = 0; j < n; j++) {
      pc = 0;
      nc = 0;
      for (i = 1; i <= lact; i++) {
        if (pattern[i - 1] == text[j]) {
          v = pc;
        } else {
          v = pc < nc ? pc : nc;
          if (c[i] < v) {
            v = c[i];
          }
          v++;
        }
        pc = c[i];
        c[i] = v;
        nc = v;
      }
      while (lact > 0 && c[lact] > k) {
        lact--;
      }
      if (lact == m) {
        count++;
        if (!on_match(j + 1, c[m], user)) {
          break;
        }
      } else {
        lact++;
        if (c[lact] <= k) {
          c[lact] = k + 1;
        }
      }
    }
    ds_context_free(context, c);
  } else {
    return DS_ERR_INVALID;
  }
  if (out_count != NULL) {
    *out_count = count;
  }
  return DS_OK;
}

/* ---- substrings, supersequences, superstrings ----------------------- */

ds_status_t ds_longest_common_substring(const unsigned char *a, size_t n,
                                        const unsigned char *b, size_t m,
                                        size_t *out_a_pos, size_t *out_b_pos,
                                        size_t *out_length,
                                        ds_context_t *context) {
  size_t *prev;
  size_t *cur;
  size_t *t;
  size_t i;
  size_t j;
  size_t best = 0;
  size_t ba = 0;
  size_t bb = 0;
  if (!sa_args(a, n, b, m) || out_length == NULL) {
    return DS_ERR_NULL;
  }
  prev = (size_t *)sa_alloc(context, 2 * (m + 1), sizeof(size_t));
  if (prev == NULL) {
    return DS_ERR_ALLOC;
  }
  cur = prev + (m + 1);
  memset(prev, 0, (m + 1) * sizeof(size_t));
  cur[0] = 0;
  for (i = 1; i <= n; i++) {
    for (j = 1; j <= m; j++) {
      cur[j] = a[i - 1] == b[j - 1] ? prev[j - 1] + 1 : 0;
      if (cur[j] > best) {
        best = cur[j];
        ba = i - best;
        bb = j - best;
      }
    }
    t = prev;
    prev = cur;
    cur = t;
  }
  *out_length = best;
  if (out_a_pos != NULL) {
    *out_a_pos = ba;
  }
  if (out_b_pos != NULL) {
    *out_b_pos = bb;
  }
  ds_context_free(context, prev < cur ? prev : cur);
  return DS_OK;
}

ds_status_t ds_shortest_common_supersequence(const unsigned char *a, size_t n,
                                             const unsigned char *b, size_t m,
                                             unsigned char *out,
                                             size_t *out_length,
                                             ds_context_t *context) {
  size_t *prev;
  size_t *cur;
  size_t *t;
  unsigned char *dir = NULL; /* 0 diagonal match, 1 up (a), 2 left (b) */
  size_t cells;
  size_t i;
  size_t j;
  size_t k;
  unsigned char c;
  if (!sa_args(a, n, b, m) || out_length == NULL) {
    return DS_ERR_NULL;
  }
  cells = sa_cells(n, m);
  if (cells == 0) {
    return DS_ERR_OVERFLOW;
  }
  prev = (size_t *)sa_alloc(context, 2 * (m + 1), sizeof(size_t));
  if (out != NULL) {
    dir = (unsigned char *)sa_alloc(context, cells, 1);
  }
  if (prev == NULL || (out != NULL && dir == NULL)) {
    ds_context_free(context, prev);
    ds_context_free(context, dir);
    return DS_ERR_ALLOC;
  }
  cur = prev + (m + 1);
  memset(prev, 0, (m + 1) * sizeof(size_t));
  for (i = 1; i <= n; i++) {
    cur[0] = 0;
    for (j = 1; j <= m; j++) {
      if (a[i - 1] == b[j - 1]) {
        cur[j] = prev[j - 1] + 1;
        c = 0;
      } else if (prev[j] >= cur[j - 1]) {
        cur[j] = prev[j];
        c = 1;
      } else {
        cur[j] = cur[j - 1];
        c = 2;
      }
      if (dir != NULL) {
        dir[i * (m + 1) + j] = c;
      }
    }
    t = prev;
    prev = cur;
    cur = t;
  }
  *out_length = n + m - prev[m];
  if (out != NULL) {
    k = *out_length;
    i = n;
    j = m;
    while (i > 0 || j > 0) {
      if (i == 0) {
        out[--k] = b[--j];
      } else if (j == 0) {
        out[--k] = a[--i];
      } else {
        c = dir[i * (m + 1) + j];
        if (c == 0) {
          out[--k] = a[i - 1];
          i--;
          j--;
        } else if (c == 1) {
          out[--k] = a[--i];
        } else {
          out[--k] = b[--j];
        }
      }
    }
  }
  ds_context_free(context, prev < cur ? prev : cur);
  ds_context_free(context, dir);
  return DS_OK;
}

/* Longest suffix of x that is a proper-or-full prefix of y, by the
 * prefix function of y. pi has ly entries. */
static size_t sa_overlap(const unsigned char *x, size_t lx,
                         const unsigned char *y, size_t ly, size_t *pi) {
  size_t i;
  size_t q = 0;
  if (ly == 0) {
    return 0;
  }
  pi[0] = 0;
  for (i = 1; i < ly; i++) {
    while (q > 0 && y[i] != y[q]) {
      q = pi[q - 1];
    }
    if (y[i] == y[q]) {
      q++;
    }
    pi[i] = q;
  }
  q = 0;
  for (i = 0; i < lx; i++) {
    while (q > 0 && (q == ly || x[i] != y[q])) {
      q = pi[q - 1];
    }
    if (q < ly && x[i] == y[q]) {
      q++;
    }
  }
  return q;
}

static int sa_contains(const unsigned char *hay, size_t lh,
                       const unsigned char *needle, size_t ln) {
  size_t i;
  if (ln == 0) {
    return 1;
  }
  for (i = 0; i + ln <= lh; i++) {
    if (memcmp(hay + i, needle, ln) == 0) {
      return 1;
    }
  }
  return 0;
}

ds_status_t ds_shortest_common_superstring(const unsigned char *const *strs,
                                           const size_t *lens, size_t count,
                                           size_t exact_limit,
                                           unsigned char *out, size_t capacity,
                                           size_t *out_length,
                                           ds_context_t *context) {
  size_t *keep = NULL;
  size_t *ov = NULL;
  size_t *pi = NULL;
  size_t *order = NULL;
  size_t *dp = NULL;
  size_t *par = NULL;
  size_t r = 0;
  size_t i;
  size_t j;
  size_t maxlen = 0;
  size_t total = 0;
  size_t len;
  size_t o;
  ds_status_t st = DS_OK;
  if (out_length == NULL || (count > 0 && (strs == NULL || lens == NULL))) {
    return DS_ERR_NULL;
  }
  if (exact_limit > 16) {
    exact_limit = 16;
  }
  *out_length = 0;
  for (i = 0; i < count; i++) {
    if (strs[i] == NULL && lens[i] > 0) {
      return DS_ERR_NULL;
    }
    if (lens[i] > maxlen) {
      maxlen = lens[i];
    }
  }
  keep = (size_t *)sa_alloc(context, count, sizeof(size_t));
  pi = (size_t *)sa_alloc(context, maxlen, sizeof(size_t));
  if (keep == NULL || pi == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  /* drop empty strings, duplicates (keep the first) and substrings */
  for (i = 0; i < count; i++) {
    int drop = lens[i] == 0;
    for (j = 0; j < count && !drop; j++) {
      if (j == i || lens[j] < lens[i]) {
        continue;
      }
      if (lens[j] == lens[i] && memcmp(strs[j], strs[i], lens[i]) == 0) {
        drop = j < i;
      } else if (lens[j] > lens[i] &&
                 sa_contains(strs[j], lens[j], strs[i], lens[i])) {
        drop = 1;
      }
    }
    if (!drop) {
      keep[r++] = i;
    }
  }
  if (r == 0) {
    goto done;
  }
  ov = (size_t *)sa_alloc(context, r * r, sizeof(size_t));
  order = (size_t *)sa_alloc(context, r, sizeof(size_t));
  if (ov == NULL || order == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (i = 0; i < r; i++) {
    for (j = 0; j < r; j++) {
      ov[i * r + j] = i == j ? 0
                             : sa_overlap(strs[keep[i]], lens[keep[i]],
                                          strs[keep[j]], lens[keep[j]], pi);
    }
  }
  if (r <= exact_limit) {
    /* dp[mask * r + last] = best total overlap of a path over mask */
    size_t full = ((size_t)1 << r) - 1;
    size_t mask;
    size_t v;
    size_t best;
    size_t bl = 0;
    dp = (size_t *)sa_alloc(context, (full + 1) * r, sizeof(size_t));
    par = (size_t *)sa_alloc(context, (full + 1) * r, sizeof(size_t));
    if (dp == NULL || par == NULL) {
      st = DS_ERR_ALLOC;
      goto done;
    }
    for (mask = 0; mask <= full; mask++) {
      for (i = 0; i < r; i++) {
        dp[mask * r + i] = SA_NONE;
      }
    }
    for (i = 0; i < r; i++) {
      dp[((size_t)1 << i) * r + i] = 0;
      par[((size_t)1 << i) * r + i] = SA_NONE;
    }
    for (mask = 1; mask <= full; mask++) {
      for (i = 0; i < r; i++) {
        if (dp[mask * r + i] == SA_NONE) {
          continue;
        }
        for (j = 0; j < r; j++) {
          if (mask & ((size_t)1 << j)) {
            continue;
          }
          v = dp[mask * r + i] + ov[i * r + j];
          if (dp[(mask | ((size_t)1 << j)) * r + j] == SA_NONE ||
              v > dp[(mask | ((size_t)1 << j)) * r + j]) {
            dp[(mask | ((size_t)1 << j)) * r + j] = v;
            par[(mask | ((size_t)1 << j)) * r + j] = i;
          }
        }
      }
    }
    best = 0;
    for (i = 0; i < r; i++) {
      if (dp[full * r + i] != SA_NONE && dp[full * r + i] >= best) {
        best = dp[full * r + i];
        bl = i;
      }
    }
    /* walk back */
    mask = full;
    j = r;
    i = bl;
    while (i != SA_NONE) {
      order[--j] = i;
      v = par[mask * r + i];
      mask &= ~((size_t)1 << i);
      i = v;
    }
  } else {
    /* greedy: take overlaps in decreasing order, joining path ends */
    size_t *next = dp = (size_t *)sa_alloc(context, 3 * r, sizeof(size_t));
    size_t *prevp;
    size_t *head;
    size_t joins = 0;
    if (dp == NULL) {
      st = DS_ERR_ALLOC;
      goto done;
    }
    prevp = next + r;
    head = prevp + r; /* chain start of each string */
    for (i = 0; i < r; i++) {
      next[i] = SA_NONE;
      prevp[i] = SA_NONE;
      head[i] = i;
    }
    while (joins + 1 < r) {
      size_t bi = SA_NONE;
      size_t bj = SA_NONE;
      size_t bo = 0;
      for (i = 0; i < r; i++) {
        if (next[i] != SA_NONE) {
          continue;
        }
        for (j = 0; j < r; j++) {
          if (j == i || prevp[j] != SA_NONE || head[i] == j) {
            continue;
          }
          if (bi == SA_NONE || ov[i * r + j] > bo) {
            bo = ov[i * r + j];
            bi = i;
            bj = j;
          }
        }
      }
      next[bi] = bj;
      prevp[bj] = bi;
      /* every string of bj's chain now starts at head[bi] */
      for (o = bj; o != SA_NONE; o = next[o]) {
        head[o] = head[bi];
      }
      joins++;
    }
    for (i = 0; i < r && prevp[i] != SA_NONE; i++) {
    }
    for (j = 0; i != SA_NONE; i = next[i]) {
      order[j++] = i;
    }
  }
  /* assemble */
  total = lens[keep[order[0]]];
  for (i = 1; i < r; i++) {
    total += lens[keep[order[i]]] - ov[order[i - 1] * r + order[i]];
  }
  *out_length = total;
  if (out != NULL) {
    if (total > capacity) {
      st = DS_ERR_OVERFLOW;
      goto done;
    }
    len = lens[keep[order[0]]];
    memcpy(out, strs[keep[order[0]]], len);
    o = len;
    for (i = 1; i < r; i++) {
      j = ov[order[i - 1] * r + order[i]];
      memcpy(out + o, strs[keep[order[i]]] + j, lens[keep[order[i]]] - j);
      o += lens[keep[order[i]]] - j;
    }
  }
done:
  ds_context_free(context, keep);
  ds_context_free(context, pi);
  ds_context_free(context, ov);
  ds_context_free(context, order);
  ds_context_free(context, dp);
  ds_context_free(context, par);
  return st;
}

ds_status_t ds_heaviest_common_subsequence(
    const unsigned char *a, size_t n, const unsigned char *b, size_t m,
    ds_match_weight_func_t weight, void *user, size_t *out_a_index,
    size_t *out_b_index, size_t *out_count, double *out_weight,
    ds_context_t *context) {
  double *prev;
  double *cur;
  double *t;
  double w;
  unsigned char *dir = NULL;
  int trace = out_a_index != NULL || out_b_index != NULL || out_count != NULL;
  size_t cells;
  size_t i;
  size_t j;
  size_t k;
  unsigned char c;
  if (!sa_args(a, n, b, m) || out_weight == NULL) {
    return DS_ERR_NULL;
  }
  cells = sa_cells(n, m);
  if (cells == 0) {
    return DS_ERR_OVERFLOW;
  }
  prev = (double *)sa_alloc(context, 2 * (m + 1), sizeof(double));
  if (trace) {
    dir = (unsigned char *)sa_alloc(context, cells, 1);
  }
  if (prev == NULL || (trace && dir == NULL)) {
    ds_context_free(context, prev);
    ds_context_free(context, dir);
    return DS_ERR_ALLOC;
  }
  cur = prev + (m + 1);
  for (j = 0; j <= m; j++) {
    prev[j] = 0.0;
  }
  for (i = 1; i <= n; i++) {
    cur[0] = 0.0;
    for (j = 1; j <= m; j++) {
      cur[j] = prev[j];
      c = 1;
      if (cur[j - 1] > cur[j]) {
        cur[j] = cur[j - 1];
        c = 2;
      }
      if (a[i - 1] == b[j - 1]) {
        w = prev[j - 1] + (weight != NULL ? weight(i - 1, j - 1, user) : 1.0);
        if (w > cur[j]) {
          cur[j] = w;
          c = 0;
        }
      }
      if (dir != NULL) {
        dir[i * (m + 1) + j] = c;
      }
    }
    t = prev;
    prev = cur;
    cur = t;
  }
  *out_weight = prev[m];
  if (dir != NULL) {
    k = 0;
    i = n;
    j = m;
    while (i > 0 && j > 0) {
      c = dir[i * (m + 1) + j];
      if (c == 0) {
        if (out_a_index != NULL) {
          out_a_index[k] = i - 1;
        }
        if (out_b_index != NULL) {
          out_b_index[k] = j - 1;
        }
        k++;
        i--;
        j--;
      } else if (c == 1) {
        i--;
      } else {
        j--;
      }
    }
    for (i = 0; i < k / 2; i++) {
      if (out_a_index != NULL) {
        size_t s = out_a_index[i];
        out_a_index[i] = out_a_index[k - 1 - i];
        out_a_index[k - 1 - i] = s;
      }
      if (out_b_index != NULL) {
        size_t s = out_b_index[i];
        out_b_index[i] = out_b_index[k - 1 - i];
        out_b_index[k - 1 - i] = s;
      }
    }
    if (out_count != NULL) {
      *out_count = k;
    }
  }
  ds_context_free(context, prev < cur ? prev : cur);
  ds_context_free(context, dir);
  return DS_OK;
}

ds_status_t ds_lcs_dominant_matches(const unsigned char *a, size_t n,
                                    const unsigned char *b, size_t m,
                                    ds_dominant_match_func_t visit, void *user,
                                    size_t *out_count, ds_context_t *context) {
  size_t *prev;
  size_t *cur;
  size_t *t;
  size_t i;
  size_t j;
  size_t count = 0;
  int go = 1;
  if (!sa_args(a, n, b, m)) {
    return DS_ERR_NULL;
  }
  prev = (size_t *)sa_alloc(context, 2 * (m + 1), sizeof(size_t));
  if (prev == NULL) {
    return DS_ERR_ALLOC;
  }
  cur = prev + (m + 1);
  memset(prev, 0, (m + 1) * sizeof(size_t));
  for (i = 1; i <= n && go; i++) {
    cur[0] = 0;
    for (j = 1; j <= m; j++) {
      if (a[i - 1] == b[j - 1]) {
        cur[j] = prev[j - 1] + 1;
        /* dominant: the value k appears here first in both row and
         * column */
        if (prev[j] < cur[j] && cur[j - 1] < cur[j]) {
          count++;
          if (visit != NULL && !visit(i - 1, j - 1, cur[j], user)) {
            go = 0;
            break;
          }
        }
      } else {
        cur[j] = prev[j] > cur[j - 1] ? prev[j] : cur[j - 1];
      }
    }
    t = prev;
    prev = cur;
    cur = t;
  }
  if (out_count != NULL) {
    *out_count = count;
  }
  ds_context_free(context, prev < cur ? prev : cur);
  return DS_OK;
}

/* ---- similarity ----------------------------------------------------- */

ds_status_t ds_jaro(const unsigned char *a, size_t n, const unsigned char *b,
                    size_t m, double *out_similarity, ds_context_t *context) {
  unsigned char *fa;
  unsigned char *fb;
  size_t window;
  size_t i;
  size_t j;
  size_t lo;
  size_t hi;
  size_t matches = 0;
  size_t half = 0;
  double mm;
  if (!sa_args(a, n, b, m) || out_similarity == NULL) {
    return DS_ERR_NULL;
  }
  if (n == 0 && m == 0) {
    *out_similarity = 1.0;
    return DS_OK;
  }
  if (n == 0 || m == 0) {
    *out_similarity = 0.0;
    return DS_OK;
  }
  fa = (unsigned char *)ds_context_calloc(context, n + m, 1);
  if (fa == NULL) {
    return DS_ERR_ALLOC;
  }
  fb = fa + n;
  window = (n > m ? n : m) / 2;
  window = window > 0 ? window - 1 : 0;
  for (i = 0; i < n; i++) {
    lo = i > window ? i - window : 0;
    hi = i + window + 1 < m ? i + window + 1 : m;
    for (j = lo; j < hi; j++) {
      if (!fb[j] && a[i] == b[j]) {
        fa[i] = 1;
        fb[j] = 1;
        matches++;
        break;
      }
    }
  }
  if (matches == 0) {
    ds_context_free(context, fa);
    *out_similarity = 0.0;
    return DS_OK;
  }
  j = 0;
  for (i = 0; i < n; i++) {
    if (!fa[i]) {
      continue;
    }
    while (!fb[j]) {
      j++;
    }
    if (a[i] != b[j]) {
      half++;
    }
    j++;
  }
  mm = (double)matches;
  *out_similarity =
      (mm / (double)n + mm / (double)m + (mm - (double)(half / 2)) / mm) / 3.0;
  ds_context_free(context, fa);
  return DS_OK;
}

ds_status_t ds_jaro_winkler(const unsigned char *a, size_t n,
                            const unsigned char *b, size_t m,
                            double prefix_scale, double boost_threshold,
                            double *out_similarity, ds_context_t *context) {
  double j;
  size_t l = 0;
  ds_status_t st = ds_jaro(a, n, b, m, &j, context);
  if (st != DS_OK) {
    return st;
  }
  if (prefix_scale < 0.0) {
    prefix_scale = 0.1;
  }
  if (boost_threshold < 0.0) {
    boost_threshold = 0.7;
  }
  if (j > boost_threshold) {
    while (l < 4 && l < n && l < m && a[l] == b[l]) {
      l++;
    }
    j += (double)l * prefix_scale * (1.0 - j);
  }
  *out_similarity = j;
  return DS_OK;
}

ds_status_t ds_ratcliff_obershelp(const unsigned char *a, size_t n,
                                  const unsigned char *b, size_t m,
                                  double *out_similarity, size_t *out_matches,
                                  ds_context_t *context) {
  size_t *stack;
  size_t top = 0;
  size_t k = 0;
  size_t al;
  size_t ah;
  size_t bl;
  size_t bh;
  size_t pa;
  size_t pb;
  size_t len;
  ds_status_t st = DS_OK;
  if (!sa_args(a, n, b, m) || out_similarity == NULL) {
    return DS_ERR_NULL;
  }
  if (n + m == 0) {
    *out_similarity = 1.0;
    if (out_matches != NULL) {
      *out_matches = 0;
    }
    return DS_OK;
  }
  /* each pushed pair of ranges is non-empty and disjoint from the others,
   * so at most n + 1 ranges are pending */
  stack = (size_t *)sa_alloc(context, 4 * (n + 2), sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  stack[0] = 0;
  stack[1] = n;
  stack[2] = 0;
  stack[3] = m;
  top = 1;
  while (top > 0) {
    top--;
    al = stack[4 * top];
    ah = stack[4 * top + 1];
    bl = stack[4 * top + 2];
    bh = stack[4 * top + 3];
    if (ah <= al || bh <= bl) {
      continue;
    }
    st = ds_longest_common_substring(a + al, ah - al, b + bl, bh - bl, &pa, &pb,
                                     &len, context);
    if (st != DS_OK) {
      break;
    }
    if (len == 0) {
      continue;
    }
    k += len;
    stack[4 * top] = al;
    stack[4 * top + 1] = al + pa;
    stack[4 * top + 2] = bl;
    stack[4 * top + 3] = bl + pb;
    top++;
    stack[4 * top] = al + pa + len;
    stack[4 * top + 1] = ah;
    stack[4 * top + 2] = bl + pb + len;
    stack[4 * top + 3] = bh;
    top++;
  }
  ds_context_free(context, stack);
  if (st != DS_OK) {
    return st;
  }
  *out_similarity = 2.0 * (double)k / (double)(n + m);
  if (out_matches != NULL) {
    *out_matches = k;
  }
  return DS_OK;
}
