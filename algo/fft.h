#ifndef DS_ALGO_FFT_H
#define DS_ALGO_FFT_H

/*
 * Discrete Fourier transforms.
 *
 *   ds_dft                 the finite (discrete) Fourier transform by its
 *                          definition, O(n^2): the reference
 *   ds_fft                 fast Fourier transform for any n: iterative
 *                          radix-2 Cooley-Tukey (in place, bit reversal)
 *                          when n is a power of two, Bluestein's chirp-z
 *                          algorithm otherwise; forward or inverse (the
 *                          inverse divides by n)
 *   ds_fft_convolve        linear convolution of real sequences
 *                          (polynomial product) through the FFT
 *   ds_ntt / ds_ntt_convolve  number-theoretic transform modulo the prime
 *                          998244353 (= 119 * 2^23 + 1): exact integer
 *                          convolution without rounding, sizes up to 2^23
 *
 * Complex numbers are pairs of doubles (C89 has no complex type). Scratch
 * memory comes from the ds_context_t.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_complex {
  double re;
  double im;
} ds_complex_t;

/* out[k] = sum_j in[j] e^(-+2 pi i jk/n); inverse also divides by n. in
 * and out must not overlap. */
ds_status_t ds_dft(const ds_complex_t *in, ds_complex_t *out, size_t n,
                   bool inverse);
/* In place. */
ds_status_t ds_fft(ds_complex_t *a, size_t n, bool inverse,
                   ds_context_t *context);
/* out: na + nb - 1 entries (nothing when either is empty). */
ds_status_t ds_fft_convolve(const double *a, size_t na, const double *b,
                            size_t nb, double *out, ds_context_t *context);

#define DS_NTT_MOD 998244353UL

/* In place over residues mod DS_NTT_MOD; n a power of two <= 2^23. */
ds_status_t ds_ntt(unsigned long *a, size_t n, bool inverse);
/* Exact convolution mod DS_NTT_MOD (inputs reduced first); out: na + nb -
 * 1 entries. */
ds_status_t ds_ntt_convolve(const unsigned long *a, size_t na,
                            const unsigned long *b, size_t nb,
                            unsigned long *out, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_FFT_H */
