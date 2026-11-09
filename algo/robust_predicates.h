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
 * Exact-sign claims hold under these requirements; the magnitudes are
 * only approximations in every case.
 *
 * Floating point: IEEE 754 binary64 with round-to-nearest-even, no x87
 * extended precision (FLT_EVAL_METHOD 0) and no contraction into fused
 * multiply-adds. The Makefile compiles this file with -ffp-contract=off
 * and the source disables contraction for clang (whose default contracts
 * within expressions); other builds must do the same. GCC already
 * disables contraction in its ISO C modes. ds_robust_predicates_check()
 * tests the compiled code and floating-point environment at run time.
 *
 * Range: a predicate of degree d multiplies d coordinate differences, so
 * the safe range shrinks with d. Sufficient (conservative) conditions on
 * every nonzero input coordinate x, zero always allowed:
 *
 *   orient2d   degree 2   2^-450 <= |x| <= 2^500
 *   orient3d   degree 3   2^-280 <= |x| <= 2^330
 *   incircle   degree 4   2^-195 <= |x| <= 2^250
 *   insphere   degree 5   2^-145 <= |x| <= 2^195
 *
 * The upper bounds keep the largest term (2 |x|)^d times the number of
 * terms, and the 2^27 splitting of each product, below the overflow
 * threshold; the lower bounds keep products of d rounding tails (each at
 * least 2^-53 of the smallest nonzero coordinate) above the underflow
 * threshold. Outside them a result may be wrong and nothing detects it.
 * Scaling all coordinates by the same power of two (ldexp) preserves every
 * sign, so inputs can be brought into range exactly when no coordinate
 * underflows in the process.
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
/* DS_OK when the compiled arithmetic is exact as required (round to
 * nearest, no extended precision, no contraction), DS_ERR_UNSUPPORTED
 * otherwise; cheap enough to call once at start-up */
ds_status_t ds_robust_predicates_check(void);

double ds_orient2d_exact(const double *a, const double *b, const double *c);
double ds_orient3d_exact(const double *a, const double *b, const double *c,
                         const double *d);
double ds_incircle_exact(const double *a, const double *b, const double *c,
                         const double *d);

#ifdef __cplusplus
}
#endif

#endif /* DS_ROBUST_PREDICATES_H */
