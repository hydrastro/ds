#ifndef DS_ALGO_QUANTUM_REFERENCE_H
#define DS_ALGO_QUANTUM_REFERENCE_H

/* Classical amplitude simulations of four quantum algorithms. These are
 * bounded reference implementations, without quantum speedups. Oracle calls
 * enumerate every classical input. Floating-point amplitudes are approximate.
 * See docs/quantum-reference.md for costs, promises and primary references.
 */
#include <dsa/algorithms/fft.h>
#include <dsa/algorithms/random.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_QREF_AUTO ((size_t) - 1)
typedef struct ds_qref_config {
  ds_context_t *context;
  ds_rng_t *rng;         /* borrowed; NULL: deterministic local seed */
  size_t max_states;     /* positive; default 65536 amplitudes */
  size_t max_shots;      /* default 64; zero permits no sampling attempts */
  size_t max_iterations; /* Grover limit, default 65536 */
} ds_qref_config_t;
void ds_qref_config_init(ds_qref_config_t *config);

/* A stable classical oracle. Boolean algorithms require output 0 or 1;
 * Simon requires output < 2^bits. Callback errors propagate unchanged.
 * Outputs publish only on DS_OK; RNG/callback side effects do not roll back.
 */
typedef ds_status_t (*ds_qref_oracle_fn)(size_t input, size_t *output,
                                         void *user);

/* Reusable normalized Walsh-Hadamard transform over 2^n amplitudes, in
 * place, O(count log count). Input amplitudes must be finite and bounded
 * so the intermediate sums fit double. A count of zero/non-power-of-two is
 * invalid. No allocation. */
ds_status_t ds_qref_hadamard(ds_complex_t *state, size_t count);
/* Sample squared amplitudes, normalizing by their total. Does not collapse
 * or mutate state. Rejects nonfinite/overflowing weights or zero total. */
ds_status_t ds_qref_measure(const ds_complex_t *state, size_t count,
                            ds_rng_t *rng, size_t *out_index);

/* Constant-vs-balanced promise checked over the complete oracle table.
 * Implements uniform preparation, phase oracle, then Hadamard. bits may be
 * zero (one input). Optional out_zero_probability is the zero-state weight.
 * O(bits*2^bits) time, O(2^bits) space, plus 2^bits oracle evaluations. */
ds_status_t ds_qref_deutsch_jozsa(unsigned int bits, ds_qref_oracle_fn oracle,
                                  void *user, const ds_qref_config_t *config,
                                  bool *out_constant,
                                  double *out_zero_probability);
/* Phase inversion and diffusion, followed by one measurement. AUTO chooses
 * floor(pi/(4 asin(sqrt(marked/count)))). Explicit iterations are allowed,
 * including zero. DS_NOT_FOUND means the enumerated oracle marks no states.
 * A successful measurement can be unmarked: inspect out_marked. Optional
 * out_success_probability is the aggregate weight of marked states.
 * O((iterations+1)*2^bits) time and O(2^bits) space. */
ds_status_t ds_qref_grover(unsigned int bits, ds_qref_oracle_fn oracle,
                           void *user, size_t iterations,
                           const ds_qref_config_t *config, size_t *out_index,
                           bool *out_marked, double *out_success_probability);
/* Repeated output-register collapse, Hadamard measurement and GF(2)
 * elimination. Under the injective/XOR-two-to-one promise, returns mask 0
 * for injective or the unique nonzero XOR mask. Promise violations detected
 * by multiplicity/candidate verification return DS_ERR_INVALID. DS_STOP
 * means insufficient independent equations within max_shots; it is not a
 * decision about the oracle. O(max_shots*bits*2^bits) time, O(2^bits) space.
 * bits must be positive. */
ds_status_t ds_qref_simon(unsigned int bits, ds_qref_oracle_fn oracle,
                          void *user, const ds_qref_config_t *config,
                          size_t *out_mask);
/* Shor order finding: collapse modular-exponentiation values, Fourier
 * sample, recover continued-fraction denominators, verify/reduce the order.
 * Requires modulus >= 2 and gcd(base,modulus)==1. Q is the smallest power
 * of two >= modulus^2, constrained by max_states and machine-word sizes.
 * O(max_shots*Q*(log Q + modular-multiply cost)) time, O(Q) space.
 * DS_STOP means sampling exhausted, never an unverified order. */
ds_status_t ds_qref_shor_order(unsigned long base, unsigned long modulus,
                               const ds_qref_config_t *config,
                               unsigned long *out_order);
/* A nontrivial factor, using classical even/perfect-power/gcd reductions
 * and at most max_shots Fourier order samples. DS_NOT_FOUND for prime n,
 * DS_STOP on exhausted sampling, DS_ERR_RANGE for a simulation budget.
 * This is the factoring branch; discrete logarithms are not provided here. */
ds_status_t ds_qref_shor_factor(unsigned long n,
                                const ds_qref_config_t *config,
                                unsigned long *out_factor);

#ifdef __cplusplus
}
#endif
#endif /* DS_ALGO_QUANTUM_REFERENCE_H */
