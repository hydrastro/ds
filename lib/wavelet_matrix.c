#include "wavelet_matrix.h"
#include "checked.h"
#include <limits.h>
#include <string.h>

#define WM_W (sizeof(unsigned long) * CHAR_BIT)

static int wm_bit(unsigned long v, unsigned level, unsigned bits) {
  return (int)((v >> (bits - 1 - level)) & 1UL);
}

void ds_wavelet_matrix_destroy(ds_wavelet_matrix_t *wm) {
  unsigned l;
  if (wm == NULL) {
    return;
  }
  if (wm->levels != NULL) {
    for (l = 0; l < wm->bits; l++) {
      ds_rs_destroy(&wm->levels[l]);
    }
  }
  ds_context_free(wm->context, wm->levels);
  ds_context_free(wm->context, wm->zeros);
  memset(wm, 0, sizeof(*wm));
}

ds_status_t ds_wavelet_matrix_build(ds_wavelet_matrix_t *wm,
                                    const unsigned long *values, size_t n,
                                    unsigned bits, const ds_rs_ops_t *rs_ops,
                                    ds_context_t *context) {
  unsigned long *cur, *next, *sw, *words;
  size_t i, bytes, nw = n / WM_W + 1;
  unsigned l;
  ds_status_t st = DS_OK;
  if (wm == NULL || (values == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  memset(wm, 0, sizeof(*wm));
  if (bits > WM_W) {
    return DS_ERR_INVALID;
  }
  if (bits == 0) {
    unsigned long mx = 0;
    for (i = 0; i < n; i++) {
      mx = values[i] > mx ? values[i] : mx;
    }
    bits = 1;
    while (bits < WM_W && (mx >> bits) != 0) {
      bits++;
    }
  } else if (bits < WM_W) {
    for (i = 0; i < n; i++) {
      if ((values[i] >> bits) != 0) {
        return DS_ERR_RANGE;
      }
    }
  }
  wm->n = n;
  wm->context = context;
  if (!ds_size_mul(n == 0 ? 1 : n, sizeof(unsigned long), &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  wm->levels = (ds_rs_t *)ds_context_calloc(context, bits, sizeof(ds_rs_t));
  wm->zeros = (size_t *)ds_context_calloc(context, bits, sizeof(size_t));
  cur = (unsigned long *)ds_context_alloc(context, bytes);
  next = (unsigned long *)ds_context_alloc(context, bytes);
  words =
      (unsigned long *)ds_context_alloc(context, nw * sizeof(unsigned long));
  if (wm->levels == NULL || wm->zeros == NULL || cur == NULL || next == NULL ||
      words == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  if (n > 0) {
    memcpy(cur, values, n * sizeof(unsigned long));
  }
  for (l = 0; l < bits; l++) {
    size_t z = 0, o;
    memset(words, 0, nw * sizeof(unsigned long));
    for (i = 0; i < n; i++) {
      if (wm_bit(cur[i], l, bits)) {
        words[i / WM_W] |= 1UL << (i % WM_W);
      } else {
        z++;
      }
    }
    st = ds_rs_build(&wm->levels[l], rs_ops, words, n, context);
    if (st != DS_OK) {
      goto done;
    }
    wm->bits = l + 1;
    wm->zeros[l] = z;
    o = z;
    z = 0;
    for (i = 0; i < n; i++) {
      if (wm_bit(cur[i], l, bits)) {
        next[o++] = cur[i];
      } else {
        next[z++] = cur[i];
      }
    }
    sw = cur;
    cur = next;
    next = sw;
  }
  wm->bits = bits;
done:
  ds_context_free(context, cur);
  ds_context_free(context, next);
  ds_context_free(context, words);
  if (st != DS_OK) {
    ds_wavelet_matrix_destroy(wm);
  }
  return st;
}

size_t ds_wavelet_matrix_size(const ds_wavelet_matrix_t *wm) {
  return wm == NULL ? 0 : wm->n;
}

unsigned long ds_wavelet_matrix_access(const ds_wavelet_matrix_t *wm,
                                       size_t i) {
  unsigned long v = 0;
  unsigned l;
  if (wm == NULL || i >= wm->n) {
    return 0;
  }
  for (l = 0; l < wm->bits; l++) {
    const ds_rs_t *b = &wm->levels[l];
    if (ds_rs_access(b, i)) {
      v = (v << 1) | 1UL;
      i = wm->zeros[l] + ds_rs_rank1(b, i);
    } else {
      v <<= 1;
      i = ds_rs_rank0(b, i);
    }
  }
  return v;
}

static bool wm_fits(const ds_wavelet_matrix_t *wm, unsigned long c) {
  return wm->bits >= WM_W || (c >> wm->bits) == 0;
}

/* narrow [s, e) to the positions of value c at the bottom */
static void wm_descend(const ds_wavelet_matrix_t *wm, unsigned long c,
                       size_t *s, size_t *e) {
  unsigned l;
  for (l = 0; l < wm->bits; l++) {
    const ds_rs_t *b = &wm->levels[l];
    if (wm_bit(c, l, wm->bits)) {
      *s = wm->zeros[l] + ds_rs_rank1(b, *s);
      *e = wm->zeros[l] + ds_rs_rank1(b, *e);
    } else {
      *s = ds_rs_rank0(b, *s);
      *e = ds_rs_rank0(b, *e);
    }
  }
}

size_t ds_wavelet_matrix_rank(const ds_wavelet_matrix_t *wm, unsigned long c,
                              size_t i) {
  size_t s = 0, e;
  if (wm == NULL || !wm_fits(wm, c)) {
    return 0;
  }
  e = i > wm->n ? wm->n : i;
  wm_descend(wm, c, &s, &e);
  return e - s;
}

size_t ds_wavelet_matrix_select(const ds_wavelet_matrix_t *wm, unsigned long c,
                                size_t k) {
  size_t s = 0, e, pos;
  unsigned l;
  if (wm == NULL) {
    return 0;
  }
  if (!wm_fits(wm, c)) {
    return wm->n;
  }
  e = wm->n;
  wm_descend(wm, c, &s, &e);
  if (k >= e - s) {
    return wm->n;
  }
  pos = s + k;
  for (l = wm->bits; l-- > 0;) {
    const ds_rs_t *b = &wm->levels[l];
    if (wm_bit(c, l, wm->bits)) {
      pos = ds_rs_select1(b, pos - wm->zeros[l]);
    } else {
      pos = ds_rs_select0(b, pos);
    }
  }
  return pos;
}

ds_status_t ds_wavelet_matrix_quantile(const ds_wavelet_matrix_t *wm, size_t l,
                                       size_t r, size_t k, unsigned long *out) {
  unsigned long v = 0;
  unsigned lv;
  if (wm == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (l >= r || r > wm->n || k >= r - l) {
    return DS_ERR_RANGE;
  }
  for (lv = 0; lv < wm->bits; lv++) {
    const ds_rs_t *b = &wm->levels[lv];
    size_t zl = ds_rs_rank0(b, l), zr = ds_rs_rank0(b, r);
    if (k < zr - zl) {
      v <<= 1;
      l = zl;
      r = zr;
    } else {
      k -= zr - zl;
      v = (v << 1) | 1UL;
      l = wm->zeros[lv] + (l - zl);
      r = wm->zeros[lv] + (r - zr);
    }
  }
  *out = v;
  return DS_OK;
}

size_t ds_wavelet_matrix_count_less(const ds_wavelet_matrix_t *wm, size_t l,
                                    size_t r, unsigned long x) {
  size_t count = 0;
  unsigned lv;
  if (wm == NULL || l >= r) {
    return 0;
  }
  if (r > wm->n) {
    r = wm->n;
  }
  if (l >= r) {
    return 0;
  }
  if (!wm_fits(wm, x)) {
    return r - l;
  }
  for (lv = 0; lv < wm->bits && l < r; lv++) {
    const ds_rs_t *b = &wm->levels[lv];
    size_t zl = ds_rs_rank0(b, l), zr = ds_rs_rank0(b, r);
    if (wm_bit(x, lv, wm->bits)) {
      count += zr - zl;
      l = wm->zeros[lv] + (l - zl);
      r = wm->zeros[lv] + (r - zr);
    } else {
      l = zl;
      r = zr;
    }
  }
  return count;
}

size_t ds_wavelet_matrix_range_count(const ds_wavelet_matrix_t *wm, size_t l,
                                     size_t r, unsigned long lo,
                                     unsigned long hi) {
  size_t a, b;
  if (lo >= hi) {
    return 0;
  }
  a = ds_wavelet_matrix_count_less(wm, l, r, hi);
  b = ds_wavelet_matrix_count_less(wm, l, r, lo);
  return a - b;
}

ds_status_t ds_wavelet_matrix_next_value(const ds_wavelet_matrix_t *wm,
                                         size_t l, size_t r, unsigned long x,
                                         unsigned long *out) {
  size_t c;
  if (wm == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (r > wm->n) {
    r = wm->n;
  }
  if (l >= r) {
    return DS_NOT_FOUND;
  }
  c = ds_wavelet_matrix_count_less(wm, l, r, x);
  if (c == r - l) {
    return DS_NOT_FOUND;
  }
  return ds_wavelet_matrix_quantile(wm, l, r, c, out);
}

ds_status_t ds_wavelet_matrix_prev_value(const ds_wavelet_matrix_t *wm,
                                         size_t l, size_t r, unsigned long x,
                                         unsigned long *out) {
  size_t c;
  if (wm == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (r > wm->n) {
    r = wm->n;
  }
  if (l >= r) {
    return DS_NOT_FOUND;
  }
  c = ds_wavelet_matrix_count_less(wm, l, r, x);
  if (c == 0) {
    return DS_NOT_FOUND;
  }
  return ds_wavelet_matrix_quantile(wm, l, r, c - 1, out);
}

size_t ds_wavelet_matrix_bytes(const ds_wavelet_matrix_t *wm) {
  size_t b;
  unsigned l;
  if (wm == NULL) {
    return 0;
  }
  b = sizeof(*wm) + wm->bits * (sizeof(ds_rs_t) + sizeof(size_t));
  for (l = 0; l < wm->bits; l++) {
    b += ds_rs_bytes(&wm->levels[l]);
  }
  return b;
}
