#ifndef DS_EXACT_SUM_H
#define DS_EXACT_SUM_H

/*
 * Exact sums of doubles: a long accumulator (Kulisch) that holds the sum
 * of any number of finite doubles without rounding, so that terms can be
 * added and removed in any order and the total read at any time,
 * correctly rounded (to nearest, ties to even) to the double nearest the
 * true sum. Running totals of structures that add and remove weights (the
 * dynamic minimum spanning forests, for instance) keep no rounding drift
 * and do not overflow in between: only the final rounding can, when the
 * true total lies beyond the double range.
 *
 * The accumulator is a two's-complement integer of DS_XSUM_WORDS 32-bit
 * digits counting units of 2^-1074 (the smallest subnormal), wide enough
 * for every finite double and more than 2^70 terms of the largest one
 * without overflow. Adding a term touches the three digits its 53 bits
 * meet and propagates a carry or borrow (amortized O(1), at most
 * DS_XSUM_WORDS); reading the total costs O(DS_XSUM_WORDS). Strict C89:
 * no 64-bit integer type is needed.
 *
 * Nonfinite terms are counted (NaNs, +inf, -inf), so removing one undoes
 * adding it: the total is NaN while a NaN or both infinities are in, an
 * infinity while only one sign is, else the rounded finite sum. A zero
 * sum reads +0.
 *
 * ds_fsum is the one-shot form: the correctly rounded sum of an array, as
 * Python's math.fsum.
 */

#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_XSUM_WORDS 68

typedef struct ds_xsum {
  unsigned long w[DS_XSUM_WORDS]; /* digits, least significant first */
  unsigned long nans;
  unsigned long pinf;
  unsigned long ninf;
} ds_xsum_t;

void ds_xsum_init(ds_xsum_t *s);
void ds_xsum_add(ds_xsum_t *s, double x);
/* undoes ds_xsum_add(s, x); DS_NOT_FOUND for a NaN or an infinity that
 * is not in the sum (nothing changes) */
ds_status_t ds_xsum_remove(ds_xsum_t *s, double x);
/* s += t */
void ds_xsum_merge(ds_xsum_t *s, const ds_xsum_t *t);
/* the total, correctly rounded; HUGE_VAL with its sign past the range */
double ds_xsum_value(const ds_xsum_t *s);
/* the same, DS_ERR_RANGE when a finite total rounds past the double range
 * (*out is then plus or minus HUGE_VAL) */
ds_status_t ds_xsum_round(const ds_xsum_t *s, double *out);
/* the sign of the finite part: -1, 0 or 1 */
int ds_xsum_sign(const ds_xsum_t *s);
/* compares the finite parts exactly: <0, 0, >0 */
int ds_xsum_compare(const ds_xsum_t *a, const ds_xsum_t *b);
/* the finite part is zero and no nonfinite term is in */
bool ds_xsum_is_zero(const ds_xsum_t *s);

/* the correctly rounded sum of x[0..n-1] */
double ds_fsum(const double *x, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* DS_EXACT_SUM_H */
