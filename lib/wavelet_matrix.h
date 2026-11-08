#ifndef DS_WAVELET_MATRIX_H
#define DS_WAVELET_MATRIX_H

/*
 * Wavelet matrix ("The wavelet matrix", SPIRE 2012) over a static sequence
 * of n integers below 2^bits: one bit vector per bit level, each level
 * stably partitioning the sequence by that bit (zeros first). Every query
 * walks the levels with rank (or select) on the bit vectors, so all take
 * O(bits) rank/select operations:
 *
 *   access(i), rank(c, i), select(c, k)
 *   quantile(l, r, k)          k-th smallest of s[l, r)
 *   count_less(l, r, x)        values < x in s[l, r); range_count follows
 *   next_value / prev_value    smallest >= x / largest < x in s[l, r)
 *
 * The level bit vectors use the rank/select capability (lib/rank_select)
 * chosen at build time: plain for speed, RRR for compressible levels,
 * Elias-Fano for sparse ones. Memory comes from the context.
 */

#include "context.h"
#include "rank_select.h"
#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_wavelet_matrix {
  size_t n;
  unsigned bits;
  ds_rs_t *levels; /* bits entries */
  size_t *zeros;   /* zeros per level */
  ds_context_t *context;
} ds_wavelet_matrix_t;

/* bits 0: just enough for the largest value; rs_ops NULL: plain */
ds_status_t ds_wavelet_matrix_build(ds_wavelet_matrix_t *wm,
                                    const unsigned long *values, size_t n,
                                    unsigned bits, const ds_rs_ops_t *rs_ops,
                                    ds_context_t *context);
void ds_wavelet_matrix_destroy(ds_wavelet_matrix_t *wm);
size_t ds_wavelet_matrix_size(const ds_wavelet_matrix_t *wm);
unsigned long ds_wavelet_matrix_access(const ds_wavelet_matrix_t *wm, size_t i);
/* occurrences of c in s[0, i) */
size_t ds_wavelet_matrix_rank(const ds_wavelet_matrix_t *wm, unsigned long c,
                              size_t i);
/* position of the k-th occurrence (0-based) of c, n if none */
size_t ds_wavelet_matrix_select(const ds_wavelet_matrix_t *wm, unsigned long c,
                                size_t k);
/* DS_ERR_RANGE unless l < r <= n and k < r - l */
ds_status_t ds_wavelet_matrix_quantile(const ds_wavelet_matrix_t *wm, size_t l,
                                       size_t r, size_t k, unsigned long *out);
size_t ds_wavelet_matrix_count_less(const ds_wavelet_matrix_t *wm, size_t l,
                                    size_t r, unsigned long x);
/* values v with lo <= v < hi in s[l, r) */
size_t ds_wavelet_matrix_range_count(const ds_wavelet_matrix_t *wm, size_t l,
                                     size_t r, unsigned long lo,
                                     unsigned long hi);
ds_status_t ds_wavelet_matrix_next_value(const ds_wavelet_matrix_t *wm,
                                         size_t l, size_t r, unsigned long x,
                                         unsigned long *out);
ds_status_t ds_wavelet_matrix_prev_value(const ds_wavelet_matrix_t *wm,
                                         size_t l, size_t r, unsigned long x,
                                         unsigned long *out);
size_t ds_wavelet_matrix_bytes(const ds_wavelet_matrix_t *wm);

#ifdef __cplusplus
}
#endif

#endif /* DS_WAVELET_MATRIX_H */
