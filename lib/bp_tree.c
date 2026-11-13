#include "bp_tree.h"
#include <limits.h>
#include <string.h>

/* opens minus closes in the byte */
static const signed char bp_delta[256] = {
    -8, -6, -6, -4, -6, -4, -4, -2, -6, -4, -4, -2, -4, -2, -2, 0,  -6, -4, -4,
    -2, -4, -2, -2, 0,  -4, -2, -2, 0,  -2, 0,  0,  2,  -6, -4, -4, -2, -4, -2,
    -2, 0,  -4, -2, -2, 0,  -2, 0,  0,  2,  -4, -2, -2, 0,  -2, 0,  0,  2,  -2,
    0,  0,  2,  0,  2,  2,  4,  -6, -4, -4, -2, -4, -2, -2, 0,  -4, -2, -2, 0,
    -2, 0,  0,  2,  -4, -2, -2, 0,  -2, 0,  0,  2,  -2, 0,  0,  2,  0,  2,  2,
    4,  -4, -2, -2, 0,  -2, 0,  0,  2,  -2, 0,  0,  2,  0,  2,  2,  4,  -2, 0,
    0,  2,  0,  2,  2,  4,  0,  2,  2,  4,  2,  4,  4,  6,  -6, -4, -4, -2, -4,
    -2, -2, 0,  -4, -2, -2, 0,  -2, 0,  0,  2,  -4, -2, -2, 0,  -2, 0,  0,  2,
    -2, 0,  0,  2,  0,  2,  2,  4,  -4, -2, -2, 0,  -2, 0,  0,  2,  -2, 0,  0,
    2,  0,  2,  2,  4,  -2, 0,  0,  2,  0,  2,  2,  4,  0,  2,  2,  4,  2,  4,
    4,  6,  -4, -2, -2, 0,  -2, 0,  0,  2,  -2, 0,  0,  2,  0,  2,  2,  4,  -2,
    0,  0,  2,  0,  2,  2,  4,  0,  2,  2,  4,  2,  4,  4,  6,  -2, 0,  0,  2,
    0,  2,  2,  4,  0,  2,  2,  4,  2,  4,  4,  6,  0,  2,  2,  4,  2,  4,  4,
    6,  2,  4,  4,  6,  4,  6,  6,  8};

/* the least prefix excess after 1 to 8 bits */
static const signed char bp_fmin[256] = {
    -8, -6, -6, -4, -6, -4, -4, -2, -6, -4, -4, -2, -4, -2, -2, 0,  -6, -4, -4,
    -2, -4, -2, -2, 0,  -4, -2, -2, 0,  -2, 0,  -1, 1,  -6, -4, -4, -2, -4, -2,
    -2, 0,  -4, -2, -2, 0,  -2, 0,  -1, 1,  -4, -2, -2, 0,  -2, 0,  -1, 1,  -3,
    -1, -1, 1,  -2, 0,  -1, 1,  -6, -4, -4, -2, -4, -2, -2, 0,  -4, -2, -2, 0,
    -2, 0,  -1, 1,  -4, -2, -2, 0,  -2, 0,  -1, 1,  -3, -1, -1, 1,  -2, 0,  -1,
    1,  -5, -3, -3, -1, -3, -1, -1, 1,  -3, -1, -1, 1,  -2, 0,  -1, 1,  -4, -2,
    -2, 0,  -2, 0,  -1, 1,  -3, -1, -1, 1,  -2, 0,  -1, 1,  -7, -5, -5, -3, -5,
    -3, -3, -1, -5, -3, -3, -1, -3, -1, -1, 1,  -5, -3, -3, -1, -3, -1, -1, 1,
    -3, -1, -1, 1,  -2, 0,  -1, 1,  -5, -3, -3, -1, -3, -1, -1, 1,  -3, -1, -1,
    1,  -2, 0,  -1, 1,  -4, -2, -2, 0,  -2, 0,  -1, 1,  -3, -1, -1, 1,  -2, 0,
    -1, 1,  -6, -4, -4, -2, -4, -2, -2, 0,  -4, -2, -2, 0,  -2, 0,  -1, 1,  -4,
    -2, -2, 0,  -2, 0,  -1, 1,  -3, -1, -1, 1,  -2, 0,  -1, 1,  -5, -3, -3, -1,
    -3, -1, -1, 1,  -3, -1, -1, 1,  -2, 0,  -1, 1,  -4, -2, -2, 0,  -2, 0,  -1,
    1,  -3, -1, -1, 1,  -2, 0,  -1, 1};

/* the greatest prefix excess after 1 to 8 bits */
static const signed char bp_fmax[256] = {
    -1, 1, 0,  2, -1, 1, 1,  3, -1, 1, 0,  2, 0,  2, 2,  4, -1, 1, 0,  2, -1, 1,
    1,  3, -1, 1, 1,  3, 1,  3, 3,  5, -1, 1, 0,  2, -1, 1, 1,  3, -1, 1, 0,  2,
    0,  2, 2,  4, -1, 1, 0,  2, 0,  2, 2,  4, 0,  2, 2,  4, 2,  4, 4,  6, -1, 1,
    0,  2, -1, 1, 1,  3, -1, 1, 0,  2, 0,  2, 2,  4, -1, 1, 0,  2, -1, 1, 1,  3,
    -1, 1, 1,  3, 1,  3, 3,  5, -1, 1, 0,  2, -1, 1, 1,  3, -1, 1, 1,  3, 1,  3,
    3,  5, -1, 1, 1,  3, 1,  3, 3,  5, 1,  3, 3,  5, 3,  5, 5,  7, -1, 1, 0,  2,
    -1, 1, 1,  3, -1, 1, 0,  2, 0,  2, 2,  4, -1, 1, 0,  2, -1, 1, 1,  3, -1, 1,
    1,  3, 1,  3, 3,  5, -1, 1, 0,  2, -1, 1, 1,  3, -1, 1, 0,  2, 0,  2, 2,  4,
    -1, 1, 0,  2, 0,  2, 2,  4, 0,  2, 2,  4, 2,  4, 4,  6, -1, 1, 0,  2, -1, 1,
    1,  3, -1, 1, 0,  2, 0,  2, 2,  4, -1, 1, 0,  2, 0,  2, 2,  4, 0,  2, 2,  4,
    2,  4, 4,  6, -1, 1, 0,  2, 0,  2, 2,  4, 0,  2, 2,  4, 2,  4, 4,  6, 0,  2,
    2,  4, 2,  4, 4,  6, 2,  4, 4,  6, 4,  6, 6,  8};

/* the least prefix excess after 0 to 7 bits */
static const signed char bp_bmin[256] = {
    -7, -5, -5, -3, -5, -3, -3, -1, -5, -3, -3, -1, -3, -1, -1, 0,  -5, -3, -3,
    -1, -3, -1, -1, 0,  -3, -1, -1, 0,  -2, 0,  -1, 0,  -5, -3, -3, -1, -3, -1,
    -1, 0,  -3, -1, -1, 0,  -2, 0,  -1, 0,  -4, -2, -2, 0,  -2, 0,  -1, 0,  -3,
    -1, -1, 0,  -2, 0,  -1, 0,  -6, -4, -4, -2, -4, -2, -2, 0,  -4, -2, -2, 0,
    -2, 0,  -1, 0,  -4, -2, -2, 0,  -2, 0,  -1, 0,  -3, -1, -1, 0,  -2, 0,  -1,
    0,  -5, -3, -3, -1, -3, -1, -1, 0,  -3, -1, -1, 0,  -2, 0,  -1, 0,  -4, -2,
    -2, 0,  -2, 0,  -1, 0,  -3, -1, -1, 0,  -2, 0,  -1, 0,  -7, -5, -5, -3, -5,
    -3, -3, -1, -5, -3, -3, -1, -3, -1, -1, 0,  -5, -3, -3, -1, -3, -1, -1, 0,
    -3, -1, -1, 0,  -2, 0,  -1, 0,  -5, -3, -3, -1, -3, -1, -1, 0,  -3, -1, -1,
    0,  -2, 0,  -1, 0,  -4, -2, -2, 0,  -2, 0,  -1, 0,  -3, -1, -1, 0,  -2, 0,
    -1, 0,  -6, -4, -4, -2, -4, -2, -2, 0,  -4, -2, -2, 0,  -2, 0,  -1, 0,  -4,
    -2, -2, 0,  -2, 0,  -1, 0,  -3, -1, -1, 0,  -2, 0,  -1, 0,  -5, -3, -3, -1,
    -3, -1, -1, 0,  -3, -1, -1, 0,  -2, 0,  -1, 0,  -4, -2, -2, 0,  -2, 0,  -1,
    0,  -3, -1, -1, 0,  -2, 0,  -1, 0};

/* the greatest prefix excess after 0 to 7 bits */
static const signed char bp_bmax[256] = {
    0, 1, 0, 2, 0, 1, 1, 3, 0, 1, 0, 2, 0, 2, 2, 4, 0, 1, 0, 2, 0, 1, 1, 3,
    0, 1, 1, 3, 1, 3, 3, 5, 0, 1, 0, 2, 0, 1, 1, 3, 0, 1, 0, 2, 0, 2, 2, 4,
    0, 1, 0, 2, 0, 2, 2, 4, 0, 2, 2, 4, 2, 4, 4, 6, 0, 1, 0, 2, 0, 1, 1, 3,
    0, 1, 0, 2, 0, 2, 2, 4, 0, 1, 0, 2, 0, 1, 1, 3, 0, 1, 1, 3, 1, 3, 3, 5,
    0, 1, 0, 2, 0, 1, 1, 3, 0, 1, 1, 3, 1, 3, 3, 5, 0, 1, 1, 3, 1, 3, 3, 5,
    1, 3, 3, 5, 3, 5, 5, 7, 0, 1, 0, 2, 0, 1, 1, 3, 0, 1, 0, 2, 0, 2, 2, 4,
    0, 1, 0, 2, 0, 1, 1, 3, 0, 1, 1, 3, 1, 3, 3, 5, 0, 1, 0, 2, 0, 1, 1, 3,
    0, 1, 0, 2, 0, 2, 2, 4, 0, 1, 0, 2, 0, 2, 2, 4, 0, 2, 2, 4, 2, 4, 4, 6,
    0, 1, 0, 2, 0, 1, 1, 3, 0, 1, 0, 2, 0, 2, 2, 4, 0, 1, 0, 2, 0, 1, 1, 3,
    0, 1, 1, 3, 1, 3, 3, 5, 0, 1, 0, 2, 0, 1, 1, 3, 0, 1, 1, 3, 1, 3, 3, 5,
    0, 1, 1, 3, 1, 3, 3, 5, 1, 3, 3, 5, 3, 5, 5, 7};

/* how often the least prefix excess after 1 to 8 bits occurs */
static const unsigned char bp_fcnt[256] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 1, 1,
    2, 2, 2, 2, 3, 3, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 3, 3, 1, 1, 3, 3, 3, 3, 4, 4, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 1, 1,
    1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
    1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3,
    3, 3, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 2, 2, 2, 2, 3, 3, 1, 1,
    1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 1, 1, 1, 1,
    1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1};

#define W (8 * sizeof(unsigned long))
#define B ((size_t)DS_PARENS_BLOCK)
#define NONE DS_BP_NONE

static bool bp_bit(const ds_parens_t *p, size_t i) {
  return ((p->words[i / W] >> (i % W)) & 1UL) != 0;
}

/* the byte of bits i .. i + 7, i a multiple of 8 */
static unsigned bp_byte(const ds_parens_t *p, size_t i) {
  return (unsigned)((p->words[i / W] >> (i % W)) & 0xFFUL);
}

/* excess of the prefix of length k (k bits), 0 <= k <= n */
static long bp_e(const ds_parens_t *p, size_t k) {
  return 2 * (long)ds_rs_rank1(&p->rs, k) - (long)k;
}

static size_t bp_min_size(size_t a, size_t b) { return a < b ? a : b; }

/* the excess before block b */
static long bp_start(const ds_parens_t *p, size_t b) {
  return b == 0 ? 0 : p->end[p->leaves + b - 1];
}

static size_t bp_block_end(const ds_parens_t *p, size_t b) {
  return bp_min_size((b + 1) * B, p->n);
}

/* scan bits from .. to - 1 with *cur the excess before bit from: the
 * first prefix length k in (from, to] whose excess is target */
static size_t bp_scan_fwd(const ds_parens_t *p, size_t from, size_t to,
                          long *cur, long target) {
  size_t i = from;
  while (i < to && i % 8 != 0) {
    *cur += bp_bit(p, i) ? 1 : -1;
    i++;
    if (*cur == target) {
      return i;
    }
  }
  while (i + 8 <= to) {
    unsigned byte = bp_byte(p, i);
    long want = target - *cur;
    if (want >= bp_fmin[byte] && want <= bp_fmax[byte]) {
      size_t stop = i + 8;
      while (i < stop) {
        *cur += bp_bit(p, i) ? 1 : -1;
        i++;
        if (*cur == target) {
          return i;
        }
      }
    }
    *cur += bp_delta[byte];
    i += 8;
  }
  while (i < to) {
    *cur += bp_bit(p, i) ? 1 : -1;
    i++;
    if (*cur == target) {
      return i;
    }
  }
  return NONE;
}

/* scan down with *cur the excess of the prefix of length hi: the last
 * prefix length k in [lo, hi) whose excess is target */
static size_t bp_scan_bwd(const ds_parens_t *p, size_t hi, size_t lo, long *cur,
                          long target) {
  size_t x = hi;
  while (x > lo && x % 8 != 0) {
    x--;
    *cur -= bp_bit(p, x) ? 1 : -1;
    if (*cur == target) {
      return x;
    }
  }
  while (x >= lo + 8) {
    unsigned byte = bp_byte(p, x - 8);
    long base = *cur - bp_delta[byte]; /* the excess at x - 8 */
    long want = target - base;
    if (want >= bp_bmin[byte] && want <= bp_bmax[byte]) {
      size_t stop = x - 8;
      while (x > stop) {
        x--;
        *cur -= bp_bit(p, x) ? 1 : -1;
        if (*cur == target) {
          return x;
        }
      }
    }
    *cur = base;
    x -= 8;
  }
  while (x > lo) {
    x--;
    *cur -= bp_bit(p, x) ? 1 : -1;
    if (*cur == target) {
      return x;
    }
  }
  return NONE;
}

static bool bp_has(const ds_parens_t *p, size_t v, long target) {
  return p->mn[v] <= target && target <= p->mx[v];
}

/* the first prefix length k > k0 with excess target */
static size_t bp_fwd(const ds_parens_t *p, size_t k0, long target) {
  long cur;
  size_t b;
  size_t v;
  size_t k;
  if (k0 >= p->n) {
    return NONE;
  }
  cur = bp_e(p, k0);
  b = k0 / B;
  k = bp_scan_fwd(p, k0, bp_block_end(p, b), &cur, target);
  if (k != NONE) {
    return k;
  }
  v = p->leaves + b;
  while (v > 1) {
    if ((v & 1U) == 0 && bp_has(p, v + 1, target)) {
      v++;
      while (v < p->leaves) {
        v = bp_has(p, 2 * v, target) ? 2 * v : 2 * v + 1;
      }
      b = v - p->leaves;
      cur = bp_start(p, b);
      return bp_scan_fwd(p, b * B, bp_block_end(p, b), &cur, target);
    }
    v >>= 1;
  }
  return NONE;
}

/* the last prefix length k < k0 with excess target */
static size_t bp_bwd(const ds_parens_t *p, size_t k0, long target) {
  long cur;
  size_t b;
  size_t v;
  size_t k;
  if (k0 == 0 || k0 > p->n) {
    return NONE;
  }
  cur = bp_e(p, k0);
  b = (k0 - 1) / B;
  k = bp_scan_bwd(p, k0, b * B, &cur, target);
  if (k != NONE) {
    return k;
  }
  v = p->leaves + b;
  while (v > 1) {
    if ((v & 1U) != 0 && bp_has(p, v - 1, target)) {
      size_t hi;
      v--;
      while (v < p->leaves) {
        v = bp_has(p, 2 * v + 1, target) ? 2 * v + 1 : 2 * v;
      }
      b = v - p->leaves;
      hi = bp_block_end(p, b);
      if (p->end[v] == target) {
        return hi;
      }
      cur = p->end[v];
      return bp_scan_bwd(p, hi, b * B, &cur, target);
    }
    v >>= 1;
  }
  return target == 0 ? 0 : NONE; /* the empty prefix */
}

/* least excess (and its count) or greatest excess over prefix lengths
 * k in (from, to], scanning bits from .. to - 1 */
static void bp_scan_min(const ds_parens_t *p, size_t from, size_t to, long cur,
                        long *m, size_t *count) {
  size_t i = from;
  while (i < to && (i % 8 != 0 || i + 8 > to)) {
    cur += bp_bit(p, i) ? 1 : -1;
    i++;
    if (cur < *m) {
      *m = cur;
      *count = 1;
    } else if (cur == *m) {
      (*count)++;
    }
  }
  while (i + 8 <= to) {
    unsigned byte = bp_byte(p, i);
    long low = cur + bp_fmin[byte];
    if (low < *m) {
      *m = low;
      *count = bp_fcnt[byte];
    } else if (low == *m) {
      *count += bp_fcnt[byte];
    }
    cur += bp_delta[byte];
    i += 8;
  }
  while (i < to) {
    cur += bp_bit(p, i) ? 1 : -1;
    i++;
    if (cur < *m) {
      *m = cur;
      *count = 1;
    } else if (cur == *m) {
      (*count)++;
    }
  }
}

static void bp_scan_max(const ds_parens_t *p, size_t from, size_t to, long cur,
                        long *m) {
  size_t i = from;
  while (i < to && (i % 8 != 0 || i + 8 > to)) {
    cur += bp_bit(p, i) ? 1 : -1;
    i++;
    if (cur > *m) {
      *m = cur;
    }
  }
  while (i + 8 <= to) {
    unsigned byte = bp_byte(p, i);
    if (cur + bp_fmax[byte] > *m) {
      *m = cur + bp_fmax[byte];
    }
    cur += bp_delta[byte];
    i += 8;
  }
  while (i < to) {
    cur += bp_bit(p, i) ? 1 : -1;
    i++;
    if (cur > *m) {
      *m = cur;
    }
  }
}

/* the canonical tree nodes covering leaves [l, r], left to right */
static size_t bp_cover(const ds_parens_t *p, size_t l, size_t r, size_t *out) {
  size_t left[2 * 8 * sizeof(size_t)];
  size_t right[2 * 8 * sizeof(size_t)];
  size_t nl = 0;
  size_t nr = 0;
  size_t i;
  size_t a = l + p->leaves;
  size_t b = r + p->leaves + 1;
  while (a < b) {
    if ((a & 1U) != 0) {
      left[nl++] = a++;
    }
    if ((b & 1U) != 0) {
      right[nr++] = --b;
    }
    a >>= 1;
    b >>= 1;
  }
  for (i = 0; i < nl; i++) {
    out[i] = left[i];
  }
  for (i = 0; i < nr; i++) {
    out[nl + i] = right[nr - 1 - i];
  }
  return nl + nr;
}

/* least excess and its count over prefix lengths [a, c], 1 <= a <= c */
static void bp_min_range(const ds_parens_t *p, size_t a, size_t c, long *m,
                         size_t *count) {
  size_t ba = (a - 1) / B;
  size_t bc = (c - 1) / B;
  *m = LONG_MAX;
  *count = 0;
  if (ba == bc) {
    bp_scan_min(p, a - 1, c, bp_e(p, a - 1), m, count);
    return;
  }
  bp_scan_min(p, a - 1, bp_block_end(p, ba), bp_e(p, a - 1), m, count);
  if (ba + 1 < bc) {
    size_t nodes[4 * 8 * sizeof(size_t)];
    size_t k = bp_cover(p, ba + 1, bc - 1, nodes);
    size_t i;
    for (i = 0; i < k; i++) {
      size_t v = nodes[i];
      if (p->mn[v] < *m) {
        *m = p->mn[v];
        *count = p->cnt[v];
      } else if (p->mn[v] == *m) {
        *count += p->cnt[v];
      }
    }
  }
  bp_scan_min(p, bc * B, c, bp_start(p, bc), m, count);
}

static long bp_max_range(const ds_parens_t *p, size_t a, size_t c) {
  size_t ba = (a - 1) / B;
  size_t bc = (c - 1) / B;
  long m = LONG_MIN;
  if (ba == bc) {
    bp_scan_max(p, a - 1, c, bp_e(p, a - 1), &m);
    return m;
  }
  bp_scan_max(p, a - 1, bp_block_end(p, ba), bp_e(p, a - 1), &m);
  if (ba + 1 < bc) {
    size_t nodes[4 * 8 * sizeof(size_t)];
    size_t k = bp_cover(p, ba + 1, bc - 1, nodes);
    size_t i;
    for (i = 0; i < k; i++) {
      if (p->mx[nodes[i]] > m) {
        m = p->mx[nodes[i]];
      }
    }
  }
  bp_scan_max(p, bc * B, c, bp_start(p, bc), &m);
  return m;
}

/* the rem-th (0-based) prefix length in (from, to] with excess m, or NONE
 * after taking the occurrences there off *rem */
static size_t bp_scan_select(const ds_parens_t *p, size_t from, size_t to,
                             long cur, long m, size_t *rem) {
  size_t i;
  for (i = from; i < to; i++) {
    cur += bp_bit(p, i) ? 1 : -1;
    if (cur == m) {
      if (*rem == 0) {
        return i + 1;
      }
      (*rem)--;
    }
  }
  return NONE;
}

/* the k-th prefix length in [a, c] whose excess is the least, m */
static size_t bp_select_min(const ds_parens_t *p, size_t a, size_t c, long m,
                            size_t k) {
  size_t ba = (a - 1) / B;
  size_t bc = (c - 1) / B;
  size_t rem = k;
  size_t r;
  if (ba == bc) {
    return bp_scan_select(p, a - 1, c, bp_e(p, a - 1), m, &rem);
  }
  r = bp_scan_select(p, a - 1, bp_block_end(p, ba), bp_e(p, a - 1), m, &rem);
  if (r != NONE) {
    return r;
  }
  if (ba + 1 < bc) {
    size_t nodes[4 * 8 * sizeof(size_t)];
    size_t n = bp_cover(p, ba + 1, bc - 1, nodes);
    size_t i;
    for (i = 0; i < n; i++) {
      size_t v = nodes[i];
      if (p->mn[v] != m) {
        continue;
      }
      if (p->cnt[v] <= rem) {
        rem -= p->cnt[v];
        continue;
      }
      while (v < p->leaves) {
        size_t l = 2 * v;
        if (p->mn[l] == m) {
          if (p->cnt[l] > rem) {
            v = l;
            continue;
          }
          rem -= p->cnt[l];
        }
        v = l + 1;
      }
      v -= p->leaves;
      return bp_scan_select(p, v * B, bp_block_end(p, v), bp_start(p, v), m,
                            &rem);
    }
  }
  return bp_scan_select(p, bc * B, c, bp_start(p, bc), m, &rem);
}

/* ---- the parentheses core ---- */

ds_status_t ds_parens_build(ds_parens_t *p, const unsigned long *words,
                            size_t n, const ds_rs_ops_t *rs_ops,
                            ds_context_t *context) {
  size_t nw;
  size_t nodes;
  size_t b;
  size_t v;
  long e = 0;
  ds_status_t st;
  if (p == NULL || (n > 0 && words == NULL)) {
    return DS_ERR_NULL;
  }
  memset(p, 0, sizeof(*p));
  if (n > (size_t)LONG_MAX / 2) {
    return DS_ERR_RANGE; /* excess values fit a long */
  }
  p->context = context;
  p->n = n;
  nw = n / W + 1;
  p->blocks = (n + B - 1) / B;
  p->leaves = 1;
  while (p->leaves < p->blocks) {
    p->leaves *= 2;
  }
  nodes = 2 * p->leaves;
  p->words =
      (unsigned long *)ds_context_calloc(context, nw, sizeof(unsigned long));
  p->mn = (long *)ds_context_alloc(context, nodes * sizeof(long));
  p->mx = (long *)ds_context_alloc(context, nodes * sizeof(long));
  p->end = (long *)ds_context_alloc(context, nodes * sizeof(long));
  p->cnt = (size_t *)ds_context_alloc(context, nodes * sizeof(size_t));
  if (p->words == NULL || p->mn == NULL || p->mx == NULL || p->end == NULL ||
      p->cnt == NULL) {
    ds_parens_destroy(p);
    return DS_ERR_ALLOC;
  }
  if (n > 0) {
    memcpy(p->words, words, ((n + W - 1) / W) * sizeof(unsigned long));
    if (n % W != 0) {
      p->words[n / W] &= (1UL << (n % W)) - 1UL; /* no stray bits */
    }
  }
  st = ds_rs_build(&p->rs, rs_ops != NULL ? rs_ops : ds_rs_plain(), p->words, n,
                   context);
  if (st != DS_OK) {
    ds_context_free(context, p->words);
    ds_context_free(context, p->mn);
    ds_context_free(context, p->mx);
    ds_context_free(context, p->end);
    ds_context_free(context, p->cnt);
    memset(p, 0, sizeof(*p));
    return st;
  }
  for (b = 0; b < p->leaves; b++) {
    size_t at = p->leaves + b;
    p->mn[at] = LONG_MAX;
    p->mx[at] = LONG_MIN;
    p->cnt[at] = 0;
    if (b < p->blocks) {
      size_t i;
      for (i = b * B; i < bp_block_end(p, b); i++) {
        e += bp_bit(p, i) ? 1 : -1;
        if (e < p->mn[at]) {
          p->mn[at] = e;
          p->cnt[at] = 1;
        } else if (e == p->mn[at]) {
          p->cnt[at]++;
        }
        if (e > p->mx[at]) {
          p->mx[at] = e;
        }
      }
    }
    p->end[at] = e;
  }
  for (v = p->leaves; v-- > 1;) {
    size_t l = 2 * v;
    size_t r = l + 1;
    p->mn[v] = p->mn[l] < p->mn[r] ? p->mn[l] : p->mn[r];
    p->mx[v] = p->mx[l] > p->mx[r] ? p->mx[l] : p->mx[r];
    p->end[v] = p->end[r];
    p->cnt[v] = (p->mn[l] == p->mn[v] ? p->cnt[l] : 0) +
                (p->mn[r] == p->mn[v] ? p->cnt[r] : 0);
  }
  return DS_OK;
}

void ds_parens_destroy(ds_parens_t *p) {
  if (p == NULL) {
    return;
  }
  if (p->words != NULL && p->mn != NULL && p->mx != NULL && p->end != NULL &&
      p->cnt != NULL) {
    ds_rs_destroy(&p->rs);
  }
  ds_context_free(p->context, p->words);
  ds_context_free(p->context, p->mn);
  ds_context_free(p->context, p->mx);
  ds_context_free(p->context, p->end);
  ds_context_free(p->context, p->cnt);
  memset(p, 0, sizeof(*p));
}

bool ds_parens_bit(const ds_parens_t *p, size_t i) {
  return p != NULL && i < p->n && bp_bit(p, i);
}

long ds_parens_excess(const ds_parens_t *p, size_t i) {
  return p == NULL || i >= p->n ? 0 : bp_e(p, i + 1);
}

size_t ds_parens_fwd_search(const ds_parens_t *p, size_t i, long d) {
  size_t k;
  if (p == NULL || i >= p->n) {
    return NONE;
  }
  k = bp_fwd(p, i + 1, bp_e(p, i + 1) + d);
  return k == NONE ? NONE : k - 1;
}

size_t ds_parens_bwd_search(const ds_parens_t *p, size_t i, long d,
                            bool *before) {
  size_t k;
  if (before != NULL) {
    *before = false;
  }
  if (p == NULL || i >= p->n) {
    return NONE;
  }
  k = bp_bwd(p, i + 1, bp_e(p, i + 1) + d);
  if (k == 0) {
    if (before != NULL) {
      *before = true;
    }
    return NONE;
  }
  return k == NONE ? NONE : k - 1;
}

size_t ds_parens_find_close(const ds_parens_t *p, size_t i) {
  size_t k;
  if (p == NULL || i >= p->n || !bp_bit(p, i)) {
    return NONE;
  }
  k = bp_fwd(p, i + 1, bp_e(p, i));
  return k == NONE ? NONE : k - 1;
}

size_t ds_parens_find_open(const ds_parens_t *p, size_t i) {
  if (p == NULL || i >= p->n || bp_bit(p, i)) {
    return NONE;
  }
  return bp_bwd(p, i + 1, bp_e(p, i + 1));
}

size_t ds_parens_enclose(const ds_parens_t *p, size_t i) {
  if (p == NULL || i >= p->n || !bp_bit(p, i)) {
    return NONE;
  }
  return bp_bwd(p, i, bp_e(p, i) - 1);
}

long ds_parens_range_min(const ds_parens_t *p, size_t i, size_t j,
                         size_t *count) {
  long m = 0;
  size_t c = 0;
  if (p != NULL && i <= j && j < p->n) {
    bp_min_range(p, i + 1, j + 1, &m, &c);
  }
  if (count != NULL) {
    *count = c;
  }
  return m;
}

long ds_parens_range_max(const ds_parens_t *p, size_t i, size_t j) {
  if (p == NULL || i > j || j >= p->n) {
    return 0;
  }
  return bp_max_range(p, i + 1, j + 1);
}

size_t ds_parens_rmq(const ds_parens_t *p, size_t i, size_t j) {
  long m;
  size_t c;
  size_t k;
  if (p == NULL || i > j || j >= p->n) {
    return NONE;
  }
  bp_min_range(p, i + 1, j + 1, &m, &c);
  k = bp_fwd(p, i, m);
  return k == NONE ? NONE : k - 1;
}

size_t ds_parens_min_select(const ds_parens_t *p, size_t i, size_t j,
                            size_t k) {
  long m;
  size_t c;
  size_t r;
  if (p == NULL || i > j || j >= p->n) {
    return NONE;
  }
  bp_min_range(p, i + 1, j + 1, &m, &c);
  if (k >= c) {
    return NONE;
  }
  r = bp_select_min(p, i + 1, j + 1, m, k);
  return r == NONE ? NONE : r - 1;
}

size_t ds_parens_bytes(const ds_parens_t *p) {
  if (p == NULL || p->words == NULL) {
    return 0;
  }
  return (p->n / W + 1) * sizeof(unsigned long) +
         2 * p->leaves * (3 * sizeof(long) + sizeof(size_t)) +
         ds_rs_bytes(&p->rs);
}

/* ---- trees ---- */

static void bp_set(unsigned long *w, size_t i) { w[i / W] |= 1UL << (i % W); }

ds_status_t ds_bp_from_parens(ds_bp_tree_t *t, const unsigned long *words,
                              size_t bits, const ds_rs_ops_t *rs_ops,
                              ds_context_t *context) {
  ds_status_t st;
  if (t == NULL || (bits > 0 && words == NULL)) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (bits % 2 != 0) {
    return DS_ERR_INVALID;
  }
  st = ds_parens_build(&t->p, words, bits, rs_ops, context);
  if (st != DS_OK) {
    return st;
  }
  if (bits > 0) {
    /* one tree: the excess stays positive until the last bit closes the
     * root */
    long m = 1;
    size_t c;
    if (bits > 2) {
      bp_min_range(&t->p, 1, bits - 1, &m, &c);
    }
    if (m < 1 || bp_e(&t->p, bits) != 0 || !bp_bit(&t->p, 0)) {
      ds_parens_destroy(&t->p);
      return DS_ERR_INVALID;
    }
  }
  t->nodes = bits / 2;
  return DS_OK;
}

ds_status_t ds_bp_from_parents(ds_bp_tree_t *t, const size_t *parent, size_t n,
                               size_t *out_number, const ds_rs_ops_t *rs_ops,
                               ds_context_t *context) {
  size_t *first = NULL; /* children of each node, in input order */
  size_t *list = NULL;
  size_t *stack = NULL;
  size_t *next = NULL;
  unsigned long *w = NULL;
  size_t root = NONE;
  size_t i;
  size_t pos = 0;
  size_t seen = 0;
  ds_status_t st = DS_OK;
  if (t == NULL || (n > 0 && parent == NULL)) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (n > ((size_t)-1) / 4 - 1) {
    return DS_ERR_OVERFLOW;
  }
  for (i = 0; i < n; i++) {
    if (parent[i] == NONE) {
      if (root != NONE) {
        return DS_ERR_INVALID;
      }
      root = i;
    } else if (parent[i] >= n) {
      return DS_ERR_INVALID;
    }
  }
  if (n > 0 && root == NONE) {
    return DS_ERR_INVALID;
  }
  first = (size_t *)ds_context_calloc(context, n + 1, sizeof(size_t));
  list = (size_t *)ds_context_alloc(context, (n + 1) * sizeof(size_t));
  stack = (size_t *)ds_context_alloc(context, (n + 1) * sizeof(size_t));
  next = (size_t *)ds_context_alloc(context, (n + 1) * sizeof(size_t));
  w = (unsigned long *)ds_context_calloc(context, (2 * n) / W + 1,
                                         sizeof(unsigned long));
  if (first == NULL || list == NULL || stack == NULL || next == NULL ||
      w == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  /* counting sort of the children by parent, keeping input order */
  for (i = 0; i < n; i++) {
    if (parent[i] != NONE) {
      first[parent[i] + 1]++;
    }
  }
  for (i = 0; i < n; i++) {
    first[i + 1] += first[i];
  }
  for (i = 0; i <= n; i++) {
    next[i] = first[i];
  }
  for (i = 0; i < n; i++) {
    if (parent[i] != NONE) {
      list[next[parent[i]]++] = i;
    }
  }
  for (i = 0; i <= n; i++) {
    next[i] = first[i]; /* reused as the next child to visit */
  }
  if (n > 0) {
    size_t sp = 0;
    stack[sp++] = root;
    bp_set(w, pos++);
    if (out_number != NULL) {
      out_number[root] = seen;
    }
    seen++;
    while (sp > 0) {
      size_t v = stack[sp - 1];
      if (next[v] < first[v + 1]) {
        size_t c = list[next[v]++];
        if (seen >= n || sp >= n) {
          st = DS_ERR_INVALID; /* a cycle */
          goto done;
        }
        bp_set(w, pos++);
        if (out_number != NULL) {
          out_number[c] = seen;
        }
        seen++;
        stack[sp++] = c;
      } else {
        pos++; /* the close: a zero bit */
        sp--;
      }
    }
    if (seen != n) {
      st = DS_ERR_INVALID; /* nodes on cycles never reached */
      goto done;
    }
  }
  st = ds_parens_build(&t->p, w, 2 * n, rs_ops, context);
  if (st == DS_OK) {
    t->nodes = n;
  }
done:
  ds_context_free(context, first);
  ds_context_free(context, list);
  ds_context_free(context, stack);
  ds_context_free(context, next);
  ds_context_free(context, w);
  return st;
}

void ds_bp_destroy(ds_bp_tree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_parens_destroy(&t->p);
  t->nodes = 0;
}

size_t ds_bp_nodes(const ds_bp_tree_t *t) { return t == NULL ? 0 : t->nodes; }

size_t ds_bp_position(const ds_bp_tree_t *t, size_t v) {
  if (t == NULL || v >= t->nodes) {
    return NONE;
  }
  return ds_rs_select1(&t->p.rs, v);
}

size_t ds_bp_node_at(const ds_bp_tree_t *t, size_t position) {
  if (t == NULL || position >= t->p.n || !bp_bit(&t->p, position)) {
    return NONE;
  }
  return ds_rs_rank1(&t->p.rs, position);
}

static size_t bp_node(const ds_bp_tree_t *t, size_t position) {
  return position == NONE ? NONE : ds_rs_rank1(&t->p.rs, position);
}

size_t ds_bp_parent(const ds_bp_tree_t *t, size_t v) {
  size_t p = ds_bp_position(t, v);
  if (p == NONE || p == 0) {
    return NONE;
  }
  return bp_node(t, ds_parens_enclose(&t->p, p));
}

size_t ds_bp_first_child(const ds_bp_tree_t *t, size_t v) {
  size_t p = ds_bp_position(t, v);
  if (p == NONE || !bp_bit(&t->p, p + 1)) {
    return NONE;
  }
  return v + 1;
}

size_t ds_bp_last_child(const ds_bp_tree_t *t, size_t v) {
  size_t p = ds_bp_position(t, v);
  size_t q;
  if (p == NONE || !bp_bit(&t->p, p + 1)) {
    return NONE;
  }
  q = ds_parens_find_close(&t->p, p);
  return bp_node(t, ds_parens_find_open(&t->p, q - 1));
}

size_t ds_bp_next_sibling(const ds_bp_tree_t *t, size_t v) {
  size_t p = ds_bp_position(t, v);
  size_t q;
  if (p == NONE) {
    return NONE;
  }
  q = ds_parens_find_close(&t->p, p);
  if (q == NONE || q + 1 >= t->p.n || !bp_bit(&t->p, q + 1)) {
    return NONE;
  }
  return v + (q - p + 1) / 2;
}

size_t ds_bp_prev_sibling(const ds_bp_tree_t *t, size_t v) {
  size_t p = ds_bp_position(t, v);
  if (p == NONE || p == 0 || bp_bit(&t->p, p - 1)) {
    return NONE;
  }
  return bp_node(t, ds_parens_find_open(&t->p, p - 1));
}

bool ds_bp_is_leaf(const ds_bp_tree_t *t, size_t v) {
  size_t p = ds_bp_position(t, v);
  return p != NONE && !bp_bit(&t->p, p + 1);
}

size_t ds_bp_degree(const ds_bp_tree_t *t, size_t v) {
  size_t p = ds_bp_position(t, v);
  size_t q;
  size_t c = 0;
  if (p == NONE || !bp_bit(&t->p, p + 1)) {
    return 0;
  }
  q = ds_parens_find_close(&t->p, p);
  /* every child's close brings the excess back to v's */
  (void)ds_parens_range_min(&t->p, p + 1, q - 1, &c);
  return c;
}

size_t ds_bp_child(const ds_bp_tree_t *t, size_t v, size_t k) {
  size_t p = ds_bp_position(t, v);
  size_t q;
  size_t c;
  if (p == NONE || !bp_bit(&t->p, p + 1)) {
    return NONE;
  }
  if (k == 0) {
    return v + 1;
  }
  q = ds_parens_find_close(&t->p, p);
  c = ds_parens_min_select(&t->p, p + 1, q - 1, k - 1);
  if (c == NONE || c + 1 >= q) {
    return NONE; /* fewer than k + 1 children */
  }
  return bp_node(t, c + 1);
}

size_t ds_bp_child_rank(const ds_bp_tree_t *t, size_t v) {
  size_t p = ds_bp_position(t, v);
  size_t pp;
  size_t c = 0;
  if (p == NONE || p == 0) {
    return 0;
  }
  pp = ds_parens_enclose(&t->p, p);
  if (pp + 1 == p) {
    return 0;
  }
  (void)ds_parens_range_min(&t->p, pp + 1, p - 1, &c);
  return c;
}

size_t ds_bp_depth(const ds_bp_tree_t *t, size_t v) {
  size_t p = ds_bp_position(t, v);
  return p == NONE ? 0 : (size_t)bp_e(&t->p, p);
}

size_t ds_bp_height(const ds_bp_tree_t *t, size_t v) {
  size_t p = ds_bp_position(t, v);
  size_t q;
  if (p == NONE) {
    return 0;
  }
  q = ds_parens_find_close(&t->p, p);
  return (size_t)(ds_parens_range_max(&t->p, p, q) - bp_e(&t->p, p + 1));
}

size_t ds_bp_subtree_size(const ds_bp_tree_t *t, size_t v) {
  size_t p = ds_bp_position(t, v);
  if (p == NONE) {
    return 0;
  }
  return (ds_parens_find_close(&t->p, p) - p + 1) / 2;
}

bool ds_bp_is_ancestor(const ds_bp_tree_t *t, size_t u, size_t v) {
  if (t == NULL || u >= t->nodes || v >= t->nodes) {
    return false;
  }
  return u <= v && v < u + ds_bp_subtree_size(t, u);
}

size_t ds_bp_lca(const ds_bp_tree_t *t, size_t u, size_t v) {
  size_t pu;
  size_t pv;
  size_t m;
  if (t == NULL || u >= t->nodes || v >= t->nodes) {
    return NONE;
  }
  if (u > v) {
    size_t x = u;
    u = v;
    v = x;
  }
  if (ds_bp_is_ancestor(t, u, v)) {
    return u;
  }
  pu = ds_bp_position(t, u);
  pv = ds_bp_position(t, v);
  m = ds_parens_rmq(&t->p, pu, pv);
  return bp_node(t, ds_parens_enclose(&t->p, m + 1));
}

size_t ds_bp_level_ancestor(const ds_bp_tree_t *t, size_t v, size_t d) {
  size_t p = ds_bp_position(t, v);
  long e;
  if (p == NONE) {
    return NONE;
  }
  if (d == 0) {
    return v;
  }
  e = bp_e(&t->p, p); /* v's depth */
  if (d > (size_t)e) {
    return NONE;
  }
  return bp_node(t, bp_bwd(&t->p, p, e - (long)d));
}

size_t ds_bp_postorder(const ds_bp_tree_t *t, size_t v) {
  size_t p = ds_bp_position(t, v);
  if (p == NONE) {
    return NONE;
  }
  return ds_rs_rank0(&t->p.rs, ds_parens_find_close(&t->p, p));
}

size_t ds_bp_bytes(const ds_bp_tree_t *t) {
  return t == NULL ? 0 : ds_parens_bytes(&t->p);
}
