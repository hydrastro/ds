#ifndef DS_ALGO_LSH_H
#define DS_ALGO_LSH_H

/*
 * Locality-sensitive hashing: hashes under which similar inputs collide
 * with high probability, so near-duplicates can be found by bucketing
 * instead of comparing every pair.
 *
 * MinHash (Broder, 1997) for Jaccard similarity of sets. A set is given as
 * the 32-bit hashes of its elements. Signature entry i is the minimum of
 * the i-th random universal hash over the set; two signatures agree in a
 * fraction of entries that estimates |A n B| / |A u B| with standard error
 * about 1 / sqrt(k).
 *
 * Banding (Indyk and Motwani; Leskovec et al.): split a k-entry signature
 * into `bands` bands of r = k / bands rows and hash each band; sets that
 * share any band hash are candidate pairs. The probability of becoming a
 * candidate is 1 - (1 - s^r)^bands for similarity s, an S-curve whose
 * threshold is about (1 / bands)^(1 / r).
 *
 * SimHash (Charikar, 2002) for weighted feature sets / cosine similarity:
 * each feature hash votes +weight or -weight per bit; the fingerprint keeps
 * the sign of each total. The Hamming distance between fingerprints
 * estimates the angle between the feature vectors.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include "elem.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_minhash {
  unsigned long *a; /* k multipliers in [1, p) */
  unsigned long *b; /* k offsets in [0, p) */
  size_t k;
  ds_context_t *context;
} ds_minhash_t;

ds_status_t ds_minhash_init(ds_minhash_t *mh, size_t k, ds_random_func_t random,
                            void *random_state, ds_context_t *context);
void ds_minhash_destroy(ds_minhash_t *mh);
/* out: k entries. An empty set gets all entries equal to p (2^31 - 1). */
void ds_minhash_signature(const ds_minhash_t *mh,
                          const unsigned long *element_hashes, size_t count,
                          unsigned long *out);
/* Fraction of equal entries of two k-entry signatures. */
double ds_minhash_similarity(const unsigned long *a, const unsigned long *b,
                             size_t k);
/* Exact Jaccard similarity of two sets of hashes (each sorted ascending,
 * duplicates allowed), for reference and small sets. */
double ds_jaccard_sorted(const unsigned long *a, size_t na,
                         const unsigned long *b, size_t nb);

/* Hash each of `bands` bands of a k-entry signature (k divisible by bands)
 * into out_bands[bands]. */
ds_status_t ds_lsh_bands(const unsigned long *signature, size_t k, size_t bands,
                         unsigned long *out_bands);
/* Probability that two items of similarity s become candidates. */
double ds_lsh_candidate_probability(double s, size_t rows, size_t bands);

/* SimHash fingerprint of `bits` (1..32) bits; weights may be NULL (all 1).
 */
unsigned long ds_simhash(const unsigned long *feature_hashes,
                         const double *weights, size_t count,
                         unsigned int bits);
unsigned int ds_simhash_distance(unsigned long a, unsigned long b);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_LSH_H */
