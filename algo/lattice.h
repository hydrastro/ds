#ifndef DS_ALGO_LATTICE_H
#define DS_ALGO_LATTICE_H

/*
 * Lattices: reduced bases, short vectors and integer relations.
 *
 *   ds_lll_reduce         the Lenstra-Lenstra-Lovasz algorithm: turns a
 *                         basis (rows) into a reduced basis, one that is
 *                         nearly orthogonal (size-reduced, |mu_ij| <= 1/2,
 *                         and satisfying the Lovasz condition with a
 *                         chosen delta in (1/4, 1)), in place
 *   ds_lll_is_reduced     checks both conditions
 *   ds_lattice_shortest   an exact shortest non-zero lattice vector by
 *                         Schnorr-Euchner enumeration over an LLL basis
 *   ds_spectral_test      Knuth's spectral test of a linear congruential
 *                         generator in t >= 2 dimensions: nu_t, the length
 *                         of the shortest vector of the dual lattice
 *                         x_1 + a x_2 + ... + a^(t-1) x_t = 0 (mod m)
 *   ds_integer_relation   integers c (not all zero) with c . x ~ 0:
 *                         DS_RELATION_PSLQ is the PSLQ algorithm of
 *                         Ferguson and Bailey, the descendant of the
 *                         Ferguson-Forcade algorithm (a generalised
 *                         Euclidean algorithm on a lower-trapezoidal
 *                         matrix); DS_RELATION_LLL reduces the lattice
 *                         [I | N x] instead
 *
 * Coordinates are doubles holding integers (exact below 2^53), so the
 * relation finders detect relations whose coefficients and residual fit
 * double precision. Memory comes from the ds_context_t.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* basis: n rows of m coordinates (linearly independent); delta in
 * (0.25, 1), 0 for 0.75. */
ds_status_t ds_lll_reduce(double *basis, size_t n, size_t m, double delta,
                          ds_context_t *context);
bool ds_lll_is_reduced(const double *basis, size_t n, size_t m, double delta,
                       ds_context_t *context);

/* out: m coordinates of a shortest non-zero vector; out_norm2 its squared
 * length. n <= 32. The basis is not changed. */
ds_status_t ds_lattice_shortest(const double *basis, size_t n, size_t m,
                                double *out, double *out_norm2,
                                ds_context_t *context);

/* nu_t (and nu_t^2) for multiplier a and modulus m < 2^26, 2 <= t <= 8. */
ds_status_t ds_spectral_test(unsigned long a, unsigned long m, size_t t,
                             double *out_nu, double *out_nu2,
                             ds_context_t *context);

typedef enum ds_relation_method {
  DS_RELATION_PSLQ = 0,
  DS_RELATION_LLL
} ds_relation_method_t;

/* x: n >= 2 reals; out: n integer coefficients (as doubles). tolerance
 * (0: 1e-10) is the residual |c . x| / |x| accepted; max_coeff (0: 1e6)
 * bounds the search. DS_NOT_FOUND when no relation within the bound turns
 * up. */
ds_status_t ds_integer_relation(const double *x, size_t n,
                                ds_relation_method_t method, double tolerance,
                                double max_coeff, double *out,
                                ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_LATTICE_H */
