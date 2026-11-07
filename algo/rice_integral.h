#ifndef DS_ALGO_RICE_INTEGRAL_H
#define DS_ALGO_RICE_INTEGRAL_H

/*
 * Rice's method for alternating binomial sums
 *
 *     S = sum_{k=a}^{n} C(n, k) (-1)^k f(k),
 *
 * which arise in the analysis of digital trees, radix sorting and
 * splitting processes. Summed directly they cancel catastrophically
 * (terms of size 2^n for a result of size 1). The Norlund-Rice integral
 * turns the sum into a contour integral,
 *
 *     S = (-1)^n / (2 pi i) * integral_C f(s) n! / (s (s-1) ... (s-n)) ds,
 *
 * over a contour C around a, a+1, ..., n, for f analytic inside it.
 * ds_rice_sum evaluates it with the trapezoidal rule on a circle through
 * a - margin and n + margin, which converges geometrically for analytic
 * integrands and never forms the large terms. The kernel is accumulated
 * in logarithms, so n can be large; the cost is O(n * points), O(n^2)
 * with the default node count.
 *
 * f is given on complex arguments. ds_rice_sum_direct sums the terms
 * plainly (compensated), for comparison or small n.
 */

#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* f(re + i im) = *out_re + i *out_im */
typedef void (*ds_rice_func_t)(double re, double im, double *out_re,
                               double *out_im, void *user);

/* a <= n <= 2^24; margin in (0, 1) (0: 0.5) keeps the poles 0..a-1 and
 * n + 1 outside; points (0: 128 + 40 (n - a + 1)) trapezoid nodes (the
 * error falls like exp(-points * margin / radius)). */
ds_status_t ds_rice_sum(ds_rice_func_t f, void *user, unsigned long a,
                        unsigned long n, double margin, size_t points,
                        double *out);
ds_status_t ds_rice_sum_direct(ds_rice_func_t f, void *user, unsigned long a,
                               unsigned long n, double *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_RICE_INTEGRAL_H */
