#ifndef DS_ALGO_MATRIX_H
#define DS_ALGO_MATRIX_H

/*
 * Dense matrix multiplication and triangular matrices (doubles, row
 * major).
 *
 * ds_matrix_multiply computes C = A B (A: n x m, B: m x p, C: n x p) with a
 * chosen algorithm:
 *   DS_MATMUL_NAIVE     the i-k-j triple loop (streams rows of B)
 *   DS_MATMUL_BLOCKED   tiled (block size pluggable, 0: 64) for cache reuse
 *   DS_MATMUL_STRASSEN  Strassen's seven-product recursion, O(n^2.807), on
 *                       the operands padded to a power of two, falling back
 *                       to the naive product below a cutoff (0: 64)
 *
 * Triangular matrices are stored packed, row by row, holding only the
 * triangle: lower, upper, strictly lower (no diagonal) and strictly upper.
 * Index mapping, packing and unpacking, matrix-vector products, products
 * of two triangular matrices of the same kind (which stay in the kind),
 * and forward / back substitution for the non-strict kinds.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_matmul_algorithm {
  DS_MATMUL_NAIVE = 0,
  DS_MATMUL_BLOCKED,
  DS_MATMUL_STRASSEN
} ds_matmul_algorithm_t;

/* param: block size (blocked) or cutoff (Strassen); 0 for the default. C
 * must not alias A or B. */
ds_status_t ds_matrix_multiply(const double *a, const double *b, double *c,
                               size_t n, size_t m, size_t p,
                               ds_matmul_algorithm_t algorithm, size_t param,
                               ds_context_t *context);

typedef enum ds_tri_kind {
  DS_TRI_LOWER = 0,
  DS_TRI_UPPER,
  DS_TRI_STRICT_LOWER,
  DS_TRI_STRICT_UPPER
} ds_tri_kind_t;

/* Entries stored for an n x n matrix of this kind. */
size_t ds_tri_size(size_t n, ds_tri_kind_t kind);
/* Packed index of (i, j); DS_ERR_RANGE outside the triangle. */
ds_status_t ds_tri_index(size_t n, ds_tri_kind_t kind, size_t i, size_t j,
                         size_t *out_index);
ds_status_t ds_tri_pack(const double *full, size_t n, ds_tri_kind_t kind,
                        double *packed);
/* full gets zeros outside the triangle. */
ds_status_t ds_tri_unpack(const double *packed, size_t n, ds_tri_kind_t kind,
                          double *full);
/* y = T x (y must not alias x). */
ds_status_t ds_tri_mul_vec(const double *packed, size_t n, ds_tri_kind_t kind,
                           const double *x, double *y);
/* C = A B for packed A, B, C of the same kind (C not aliasing A or B). */
ds_status_t ds_tri_multiply(const double *a, const double *b, double *c,
                            size_t n, ds_tri_kind_t kind);
/* Solves T x = b (lower: forward, upper: back substitution); x may alias
 * b. DS_ERR_INVALID for a zero diagonal entry or a strict kind. */
ds_status_t ds_tri_solve(const double *packed, size_t n, ds_tri_kind_t kind,
                         const double *b, double *x);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_MATRIX_H */
