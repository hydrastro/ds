#ifndef DS_ALGO_STRINGOLOGY_H
#define DS_ALGO_STRINGOLOGY_H

/*
 * String algorithms over byte arrays (text, DNA, binary data: any bytes,
 * NUL included; lengths are explicit).
 *
 * Matching (report every occurrence through a callback, return the count):
 *   ds_kmp_search          Knuth-Morris-Pratt, O(n + m)
 *   ds_rabin_karp_search   rolling hash with verification, O(n + m) expected
 *   ds_prefix_function     KMP failure function (border lengths)
 *   ds_z_function          Z-array: longest common prefix with every suffix
 *
 * Indexing:
 *   ds_suffix_array        prefix doubling with radix sort, O(n log n)
 *   ds_lcp_array           Kasai's algorithm, O(n)
 *
 * Structure:
 *   ds_manacher            all palindromes in O(n)
 *   ds_minimal_rotation    lexicographically least rotation (Booth), O(n)
 *   ds_lyndon_factorization Duval's algorithm, O(n)
 *
 * Distances and similarity:
 *   ds_levenshtein         insert/delete/substitute, O(nm) time, O(m) space
 *   ds_osa_distance        Levenshtein plus adjacent transposition
 *                          (optimal string alignment Damerau variant)
 *   ds_lcs_length          longest common subsequence length
 *   ds_hamming_distance    positions that differ (equal lengths)
 *
 * Scratch memory comes from a ds_context_t (NULL for the default).
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*ds_match_func_t)(size_t position, void *user);

void ds_prefix_function(const unsigned char *s, size_t n, size_t *out_pi);
void ds_z_function(const unsigned char *s, size_t n, size_t *out_z);
size_t ds_kmp_search(const unsigned char *text, size_t n,
                     const unsigned char *pattern, size_t m,
                     ds_match_func_t on_match, void *user,
                     ds_context_t *context);
size_t ds_rabin_karp_search(const unsigned char *text, size_t n,
                            const unsigned char *pattern, size_t m,
                            ds_match_func_t on_match, void *user);

ds_status_t ds_suffix_array(const unsigned char *s, size_t n, size_t *out_sa,
                            ds_context_t *context);
ds_status_t ds_lcp_array(const unsigned char *s, size_t n, const size_t *sa,
                         size_t *out_lcp, ds_context_t *context);

void ds_manacher(const unsigned char *s, size_t n, size_t *out_odd,
                 size_t *out_even, size_t *out_start, size_t *out_length);
ds_status_t ds_minimal_rotation(const unsigned char *s, size_t n,
                                size_t *out_index, ds_context_t *context);
size_t ds_lyndon_factorization(const unsigned char *s, size_t n,
                               bool (*on_factor)(size_t start, size_t length,
                                                 void *user),
                               void *user);

ds_status_t ds_levenshtein(const unsigned char *a, size_t n,
                           const unsigned char *b, size_t m,
                           size_t *out_distance, ds_context_t *context);
ds_status_t ds_osa_distance(const unsigned char *a, size_t n,
                            const unsigned char *b, size_t m,
                            size_t *out_distance, ds_context_t *context);
ds_status_t ds_lcs_length(const unsigned char *a, size_t n,
                          const unsigned char *b, size_t m, size_t *out_length,
                          ds_context_t *context);
size_t ds_hamming_distance(const unsigned char *a, const unsigned char *b,
                           size_t n);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_STRINGOLOGY_H */
