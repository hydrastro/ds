#ifndef DS_ALGO_SEQUENCE_H
#define DS_ALGO_SEQUENCE_H

/*
 * Sequence algorithms over arrays of fixed-size elements (see elem.h).
 *
 * Rearranging (in place, no allocation):
 *   ds_rotate, ds_next_permutation, ds_prev_permutation, ds_shuffle
 *   (Fisher-Yates with a caller RNG), ds_unique (collapse runs of equal
 *   neighbours, returns the new count), ds_partition (unstable, O(n)),
 *   ds_stable_partition (O(n log n) by rotations), ds_perfect_shuffle and
 *   ds_perfect_unshuffle (the riffle of two halves and its inverse, in
 *   place in O(n) time and O(1) space by Jain's cycle-leader algorithm:
 *   the prefix of length 3^k - 1 is a single family of cycles led by
 *   1, 3, 9, ...; an in-shuffle puts the second half's first element
 *   first, an out-shuffle keeps the first element on top).
 *
 * Sorted-sequence operations write into `out` (which must not overlap the
 * inputs and must have room for the worst case) and return the number of
 * elements written. Equal elements are matched with multiset semantics, as
 * in the C++ standard library:
 *   ds_merge, ds_set_union, ds_set_intersection, ds_set_difference,
 *   ds_set_symmetric_difference, ds_includes.
 *
 * Analysis:
 *   ds_lexicographic_compare, ds_count_inversions (merge-based, needs a
 *   scratch buffer of count elements; destroys the input order),
 *   ds_lis_length (longest strictly increasing subsequence, O(n log n),
 *   scratch of count elements), ds_majority_element (Boyer-Moore voting),
 *   ds_max_subarray_long (Kadane).
 */

#include "../lib/status.h"
#include "elem.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void ds_rotate(void *base, size_t count, size_t elem_size, size_t middle);
bool ds_next_permutation(void *base, size_t count, size_t elem_size,
                         ds_compare_func_t compare, void *user);
bool ds_prev_permutation(void *base, size_t count, size_t elem_size,
                         ds_compare_func_t compare, void *user);
void ds_shuffle(void *base, size_t count, size_t elem_size,
                ds_random_func_t random, void *random_state);
/* count must be even. */
ds_status_t ds_perfect_shuffle(void *base, size_t count, size_t elem_size,
                               bool in_shuffle);
ds_status_t ds_perfect_unshuffle(void *base, size_t count, size_t elem_size,
                                 bool in_shuffle);
size_t ds_unique(void *base, size_t count, size_t elem_size,
                 ds_compare_func_t compare, void *user);
size_t ds_partition(void *base, size_t count, size_t elem_size,
                    ds_predicate_func_t predicate, void *user);
size_t ds_stable_partition(void *base, size_t count, size_t elem_size,
                           ds_predicate_func_t predicate, void *user);

size_t ds_merge(const void *a, size_t a_count, const void *b, size_t b_count,
                size_t elem_size, ds_compare_func_t compare, void *user,
                void *out);
size_t ds_set_union(const void *a, size_t a_count, const void *b,
                    size_t b_count, size_t elem_size, ds_compare_func_t compare,
                    void *user, void *out);
size_t ds_set_intersection(const void *a, size_t a_count, const void *b,
                           size_t b_count, size_t elem_size,
                           ds_compare_func_t compare, void *user, void *out);
size_t ds_set_difference(const void *a, size_t a_count, const void *b,
                         size_t b_count, size_t elem_size,
                         ds_compare_func_t compare, void *user, void *out);
size_t ds_set_symmetric_difference(const void *a, size_t a_count, const void *b,
                                   size_t b_count, size_t elem_size,
                                   ds_compare_func_t compare, void *user,
                                   void *out);
bool ds_includes(const void *a, size_t a_count, const void *b, size_t b_count,
                 size_t elem_size, ds_compare_func_t compare, void *user);

int ds_lexicographic_compare(const void *a, size_t a_count, const void *b,
                             size_t b_count, size_t elem_size,
                             ds_compare_func_t compare, void *user);
unsigned long ds_count_inversions(void *base, size_t count, size_t elem_size,
                                  ds_compare_func_t compare, void *user,
                                  void *scratch);
size_t ds_lis_length(const void *base, size_t count, size_t elem_size,
                     ds_compare_func_t compare, void *user, void *scratch);
ds_status_t ds_majority_element(const void *base, size_t count,
                                size_t elem_size, ds_compare_func_t compare,
                                void *user, size_t *out_index);
long ds_max_subarray_long(const long *values, size_t count, size_t *out_first,
                          size_t *out_last);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_SEQUENCE_H */
