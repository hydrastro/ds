# Quantum algorithm references

`dsa/algorithms/quantum_reference.h` contains classical amplitude simulations. They enumerate classical
oracle inputs and keep exponentially many amplitudes. They do not supply a
quantum device backend, quantum query complexity or a speedup over ordinary C
algorithms. Amplitudes and sampling probabilities use floating-point arithmetic.

| Entry point | Computation | Scope |
|---|---|---|
| `ds_qref_deutsch_jozsa` | Uniform amplitudes, Boolean phase oracle, Walsh-Hadamard transform | Validates constant/balanced promise; reports zero-state probability |
| `ds_qref_grover` | Repeated phase inversion and inversion about the mean, then measurement | Caller chooses iterations or automatic count from the enumerated marked set; measurement may miss |
| `ds_qref_simon` | Sample an output-register value, collapse its preimages, transform and sample input register, solve equations over GF(2) | Injective or XOR-two-to-one oracles with word-sized outputs below the input-domain size; verifies recovered mask |
| `ds_qref_shor_order` | Collapse modular-exponentiation values, Fourier sample, recover continued-fraction denominators | Verifies and reduces candidate orders before returning |
| `ds_qref_shor_factor` | Even/perfect-power/gcd reductions and sampled orders | Returns one nontrivial factor; does not implement the discrete-logarithm branch |

The transform and measurement operations are public for reuse. Fourier steps use
`dsa/algorithms/fft.h`; modular arithmetic and primality use `dsa/algorithms/numeric.h`; sampling uses
`dsa/algorithms/random.h`. There is no shared global random state. Supply a borrowed generator
for a continuing stream; otherwise each call starts with a documented-in-source,
fixed local seed. This makes examples repeatable, not cryptographically random.

Initialize options with `ds_qref_config_init`. Defaults permit 65,536 states,
64 sampling attempts and 65,536 Grover iterations. State and iteration limits
produce `DS_ERR_RANGE`; allocation/size failures are explicit. Simon and Shor
return `DS_STOP` when sampling attempts run out; that is not evidence of no
solution. Shor returns `DS_NOT_FOUND` for a prime input, while Grover uses it when
the fully enumerated oracle marks no inputs. Outputs publish only on `DS_OK`;
oracle and RNG state changes are not rolled back.

For b input bits, Deutsch-Jozsa takes O(b 2^b) work, Simon up to
O(shots b 2^b), and Grover O((iterations+1) 2^b), all with O(2^b) storage.
Shor uses Q, the smallest power of two at least N²: O(Q) storage and Fourier
work O(Q log Q) per attempt, plus O(Q) modular multiplications. Its factoring
preprocessing and order verification add classical work. The default budget
therefore supports only small integers for simulated order finding. Increasing
the limit does not change these costs.

`interference_patterns` demonstrates a balanced oracle, marked-state search,
paired/injective Simon oracles, order 10 for base 5 modulo 33, and a factor of 33.

Primary algorithm references:

- [Cleve, Ekert, Macchiavello and Mosca, Quantum Algorithms Revisited](https://arxiv.org/abs/quant-ph/9708016), the phase-oracle Deutsch-Jozsa form and interference construction.
- [Grover, A fast quantum mechanical algorithm for database search](https://arxiv.org/abs/quant-ph/9605043), phase inversion and diffusion.
- [Simon, On the Power of Quantum Computation](https://doi.org/10.1137/S0097539796298637), XOR constraints and linear-system recovery.
- [Shor, Polynomial-Time Algorithms for Prime Factorization and Discrete Logarithms on a Quantum Computer](https://arxiv.org/abs/quant-ph/9508027), Fourier order finding and continued-fraction postprocessing.
