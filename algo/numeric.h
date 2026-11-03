#ifndef DS_ALGO_NUMERIC_H
#define DS_ALGO_NUMERIC_H

/*
 * Number theory and small numerics.
 *
 * Integer functions use unsigned long and never overflow internally:
 * ds_mulmod falls back to double-and-add when the product could exceed
 * unsigned long, so ds_powmod and ds_is_prime are exact for every modulus.
 * ds_is_prime is a deterministic Miller-Rabin test (bases 2, 7, 61 below
 * 2^32; a 7-base set proven for all 64-bit values above that).
 *
 * ds_sieve marks primes below n in a caller byte array (Eratosthenes,
 * odd-only inner loop) and returns how many there are.
 *
 * ds_kahan_sum is Neumaier's compensated summation. ds_welford_t computes
 * running mean and variance in one pass without catastrophic cancellation.
 */

#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned long ds_gcd(unsigned long a, unsigned long b);
unsigned long ds_binary_gcd(unsigned long a, unsigned long b);
ds_status_t ds_lcm(unsigned long a, unsigned long b, unsigned long *out);
long ds_ext_gcd(long a, long b, long *out_x, long *out_y);
unsigned long ds_mulmod(unsigned long a, unsigned long b, unsigned long m);
unsigned long ds_powmod(unsigned long base, unsigned long exponent,
                        unsigned long m);
ds_status_t ds_modinv(unsigned long a, unsigned long m, unsigned long *out);
unsigned long ds_isqrt(unsigned long n);
bool ds_is_prime(unsigned long n);
size_t ds_sieve(unsigned char *is_prime, size_t n);

double ds_kahan_sum(const double *values, size_t count);

typedef struct ds_welford {
  size_t count;
  double mean;
  double m2;
} ds_welford_t;

void ds_welford_init(ds_welford_t *w);
void ds_welford_add(ds_welford_t *w, double x);
double ds_welford_mean(const ds_welford_t *w);
double ds_welford_variance(const ds_welford_t *w);
double ds_welford_sample_variance(const ds_welford_t *w);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_NUMERIC_H */
