#ifndef DS_ALGO_NUMERICAL_H
#define DS_ALGO_NUMERICAL_H

/*
 * Numerical methods and small statistics over doubles.
 *
 *   ds_horner()            polynomial value (and derivative) by Horner's
 *                          rule, n multiplications
 *   ds_root_bisection(),   one-dimensional root finding: bisection
 *   ds_root_secant(),      (guaranteed on a sign change), the secant method
 *   ds_root_newton()       ("secant search", superlinear) and Newton's
 *                          method with a derivative
 *   ds_sqrt_newton(),      square and cube roots by Newton / Halley
 *   ds_cbrt_newton()       iteration (independent of libm's sqrt/cbrt)
 *   ds_cordic_*()          CORDIC: sine/cosine (rotation mode) and
 *                          atan2/hypot (vectoring mode) with shifts and
 *                          adds only, n iterations give about n bits
 *   ds_gamma(), ds_lgamma() Gamma and log-Gamma (Lanczos, reflection for
 *                          x < 1/2), ~15 significant digits
 *   ds_stirling()          Stirling's approximation of n! with correction
 *                          terms, and ds_log_factorial()
 *   ds_bbp_pi_hex()        hexadecimal digits of pi from any position
 *                          without the preceding ones (Bailey, Borwein and
 *                          Plouffe), accurate to ~10^7
 *   ds_mode(), ds_midrange() most frequent value and (min + max) / 2
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef double (*ds_real_func_t)(double x, void *user);

/* coef[0] + coef[1] x + ... + coef[degree] x^degree; *out_derivative may
 * be NULL. */
double ds_horner(const double *coef, size_t degree, double x,
                 double *out_derivative);

/* Root of f between a and b (f(a), f(b) of opposite signs). */
ds_status_t ds_root_bisection(ds_real_func_t f, void *user, double a, double b,
                              double tolerance, unsigned int max_iter,
                              double *out_root);
/* Secant iteration from x0, x1; DS_NOT_FOUND if it fails to converge. */
ds_status_t ds_root_secant(ds_real_func_t f, void *user, double x0, double x1,
                           double tolerance, unsigned int max_iter,
                           double *out_root);
ds_status_t ds_root_newton(ds_real_func_t f, ds_real_func_t df, void *user,
                           double x0, double tolerance, unsigned int max_iter,
                           double *out_root);

double ds_sqrt_newton(double x);
double ds_cbrt_newton(double x);

/* angle in radians (any value; reduced first). iterations <= 60. */
void ds_cordic_sincos(double angle, unsigned int iterations, double *out_sin,
                      double *out_cos);
/* atan2(y, x) and sqrt(x^2 + y^2). */
void ds_cordic_atan2(double y, double x, unsigned int iterations,
                     double *out_angle, double *out_radius);

double ds_gamma(double x);
double ds_lgamma(double x);
double ds_stirling(double n);
double ds_log_factorial(unsigned long n);

/* `count` hexadecimal digits of pi starting after the point at 0-based
 * position `position` (0 gives "243f6a88..."), as characters in out. */
ds_status_t ds_bbp_pi_hex(unsigned long position, size_t count, char *out);

/* Most frequent value (smallest on ties); values are copied and sorted
 * using the context. */
ds_status_t ds_mode(const double *values, size_t n, double *out_mode,
                    size_t *out_frequency, ds_context_t *context);
double ds_midrange(const double *values, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_NUMERICAL_H */
