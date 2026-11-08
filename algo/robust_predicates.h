#ifndef DS_ROBUST_PREDICATES_H
#define DS_ROBUST_PREDICATES_H

/*
 * Robust geometric predicates via floating-point expansion arithmetic
 * (Priest; Shewchuk, "Adaptive Precision Floating-Point Arithmetic and
 * Fast Robust Geometric Predicates", 1997).
 *
 * An expansion is an array of doubles, ordered by increasing magnitude
 * and nonoverlapping, whose exact (unrounded) sum is the value it
 * represents. The primitives below build such expansions exactly from
 * double inputs, so determinant signs can be decided without error.
 *
 * Each predicate first evaluates the determinant in plain floating point
 * and accepts it when it exceeds a forward error bound proportional to
 * the permanent (the same expression with absolute values); otherwise it
 * recomputes the determinant exactly. The returned double always has the
 * exact sign of the determinant (zero exactly when degenerate); its
 * magnitude is approximate.
 *
 * Requirements: IEEE 754 binary64 with round-to-nearest-even, no x87
 * extended precision (FLT_EVAL_METHOD 0) and no contraction into fused
 * multiply-adds; inputs must be small enough that no intermediate result
 * overflows (|x| below about 2^500 for the 3D/circle tests) and large
 * enough, where nonzero, that none underflows.
 *
 *   orient2d(a,b,c)    > 0 when a,b,c turn counterclockwise
 *   orient3d(a,b,c,d)  > 0 when d lies below the plane of a,b,c, those
 *                      appearing counterclockwise from above
 *   incircle(a,b,c,d)  > 0 when d is inside the circle through a,b,c
 *                      (a,b,c counterclockwise)
 *   insphere(a,...,e)  > 0 when e is inside the sphere through a,b,c,d
 *                      (orient3d(a,b,c,d) > 0)
 */

#include "context.h"
#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* exact error-free transformations: x + y == a + b (resp. a - b, a * b) */
void ds_two_sum(double a, double b, double *x, double *y);
void ds_two_diff(double a, double b, double *x, double *y);
void ds_two_product(double a, double b, double *x, double *y);

/* h = e + f; h holds elen + flen; returns the length (>= 1, zeros
 * eliminated). e, f nonoverlapping, increasing; h may not alias them. */
size_t ds_expansion_sum(const double *e, size_t elen, const double *f,
                        size_t flen, double *h);
/* h = e * b; h holds 2 * elen */
size_t ds_expansion_scale(const double *e, size_t elen, double b, double *h);
/* h = e * f; h holds 2 * elen * flen and scratch 2 * elen * flen +
 * 2 * elen doubles */
size_t ds_expansion_product(const double *e, size_t elen, const double *f,
                            size_t flen, double *h, double *scratch);
/* h = -e (in place allowed) */
void ds_expansion_negate(const double *e, size_t elen, double *h);
/* h = e with as few components as possible; h holds elen (h may be e) */
size_t ds_expansion_compress(const double *e, size_t elen, double *h);
/* nearest double to the exact value, with the exact sign */
double ds_expansion_estimate(const double *e, size_t elen);
/* -1, 0 or 1 */
int ds_expansion_sign(const double *e, size_t elen);

double ds_orient2d(const double *a, const double *b, const double *c);
double ds_orient3d(const double *a, const double *b, const double *c,
                   const double *d);
double ds_incircle(const double *a, const double *b, const double *c,
                   const double *d);
/* the exact fallback needs up to a few hundred KiB, allocated from the
 * context only when the filter cannot decide */
ds_status_t ds_insphere(const double *a, const double *b, const double *c,
                        const double *d, const double *e, double *out,
                        ds_context_t *context);

/* exact-only evaluations (no filter), mostly for checking */
double ds_orient2d_exact(const double *a, const double *b, const double *c);
double ds_orient3d_exact(const double *a, const double *b, const double *c,
                         const double *d);
double ds_incircle_exact(const double *a, const double *b, const double *c,
                         const double *d);

#ifdef __cplusplus
}
#endif

#endif /* DS_ROBUST_PREDICATES_H */
