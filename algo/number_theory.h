#ifndef DS_ALGO_NUMBER_THEORY_H
#define DS_ALGO_NUMBER_THEORY_H

/*
 * More integer number theory and calendar arithmetic (see also numeric.h).
 * Every function detects overflow (DS_ERR_OVERFLOW) instead of wrapping.
 *
 *   ds_crt()                 Chinese remainder theorem for any moduli,
 *                            coprime or not (DS_NOT_FOUND if inconsistent)
 *   ds_fibonacci()           F(n) (fits up to n = 93 with 64-bit longs)
 *   ds_fibonacci_mod()       F(n) mod m by fast doubling, O(log n), any n
 *   ds_kbonacci()            k-th order Fibonacci numbers: each term the
 *                            sum of the previous k (k = 2: Fibonacci,
 *                            k = 3: tribonacci, ...)
 *   ds_ackermann()           Ackermann's function A(m, n) where it fits:
 *                            closed forms for m <= 3, a bounded explicit
 *                            stack otherwise
 *   ds_inverse_ackermann()   alpha(n) = min { k : A(k, k) >= n }, at most
 *                            4 for every representable n
 *   ds_day_of_week_*()       Conway's Doomsday rule and Zeller's
 *                            congruence for the proleptic Gregorian
 *                            calendar (0 = Sunday .. 6 = Saturday)
 */

#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* x = residues[i] mod moduli[i] for all i; result in [0, *out_modulus). */
ds_status_t ds_crt(const unsigned long *residues, const unsigned long *moduli,
                   size_t count, unsigned long *out_x,
                   unsigned long *out_modulus);
ds_status_t ds_fibonacci(unsigned long n, unsigned long *out);
unsigned long ds_fibonacci_mod(unsigned long n, unsigned long m);
ds_status_t ds_kbonacci(unsigned int k, unsigned long n, unsigned long *out);
ds_status_t ds_ackermann(unsigned long m, unsigned long n, unsigned long *out);
unsigned int ds_inverse_ackermann(unsigned long n);
int ds_day_of_week_doomsday(long year, int month, int day);
int ds_day_of_week_zeller(long year, int month, int day);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_NUMBER_THEORY_H */
