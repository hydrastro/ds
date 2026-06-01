#include "../lib/wavelet_tree.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int cmp_size(const void *a, const void *b) {
  size_t x = *(const size_t *)a;
  size_t y = *(const size_t *)b;
  if (x < y)
    return -1;
  if (x > y)
    return 1;
  return 0;
}

int main(void) {
  size_t n = 2000;
  size_t sigma = 16;
  size_t *seq = (size_t *)malloc(sizeof(size_t) * n);
  ds_wavelet_tree_t *wt;
  size_t i;
  unsigned state = 999U;

  for (i = 0; i < n; ++i) {
    state = state * 1103515245U + 12345U;
    seq[i] = (state >> 8) % sigma;
  }
  wt = ds_wavelet_tree_create(seq, n, sigma);
  assert(ds_wavelet_tree_length(wt) == n);

  /* access matches the original sequence */
  for (i = 0; i < n; ++i) {
    assert(ds_wavelet_tree_access(wt, i) == seq[i]);
  }
  printf("access verified\n");

  /* rank matches a brute-force prefix count */
  for (i = 0; i <= n; i += 31) {
    size_t c;
    for (c = 0; c < sigma; ++c) {
      size_t naive = 0;
      size_t j;
      for (j = 0; j < i; ++j) {
        if (seq[j] == c) {
          naive += 1;
        }
      }
      assert(ds_wavelet_tree_rank(wt, c, i) == naive);
    }
  }
  printf("rank verified\n");

  /* select: the k-th occurrence position, cross-checked by scanning */
  {
    size_t c;
    for (c = 0; c < sigma; ++c) {
      size_t occ = 0;
      size_t j;
      for (j = 0; j < n; ++j) {
        if (seq[j] == c) {
          if (occ % 5 == 0) {
            size_t sel = ds_wavelet_tree_select(wt, c, occ);
            assert(sel == j);
          }
          occ += 1;
        }
      }
      /* out-of-range select returns NPOS */
      assert(ds_wavelet_tree_select(wt, c, occ) == DS_WAVELET_NPOS);
    }
  }
  printf("select verified\n");

  /* quantile: k-th smallest in a range, checked by sorting the slice */
  {
    size_t trials;
    for (trials = 0; trials < 200; ++trials) {
      size_t a, b, tmp, k;
      size_t *slice;
      size_t len;
      size_t got;
      state = state * 1103515245U + 12345U;
      a = (state >> 8) % n;
      state = state * 1103515245U + 12345U;
      b = (state >> 8) % n;
      if (a > b) {
        tmp = a;
        a = b;
        b = tmp;
      }
      b += 1; /* make [a, b) non-empty half-open */
      len = b - a;
      slice = (size_t *)malloc(sizeof(size_t) * len);
      for (i = 0; i < len; ++i) {
        slice[i] = seq[a + i];
      }
      qsort(slice, len, sizeof(size_t), cmp_size);
      state = state * 1103515245U + 12345U;
      k = (state >> 8) % len;
      got = ds_wavelet_tree_quantile(wt, a, b, k);
      assert(got == slice[k]);
      free(slice);
    }
  }
  printf("quantile verified\n");

  ds_wavelet_tree_destroy(wt);
  free(seq);
  printf("ok\n");
  return 0;
}
