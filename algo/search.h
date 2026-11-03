#ifndef DS_ALGO_SEARCH_H
#define DS_ALGO_SEARCH_H

/*
 * Searching over arrays of fixed-size elements (see elem.h).
 *
 * The bound functions require [0, count) to be sorted by `compare` and use
 * the C++ conventions:
 *   lower_bound  first index whose element is not less than key
 *   upper_bound  first index whose element is greater than key
 *   equal_range  [lower_bound, upper_bound)
 * All return `count` when no such element exists. Comparators are called as
 * compare(element, key) or compare(key, element), so key and element types
 * may differ as long as the comparator handles both orders.
 *
 * ds_exponential_search gallops from the front (1, 2, 4, ...) before the
 * binary search: O(log i) for a match at index i, which is what merging runs
 * and searching unbounded or front-loaded data want.
 *
 * More searches over sorted data, all returning the lower bound unless
 * noted:
 *   ds_jump_search        jump (block) search: probe every m-th element
 *                         (m = floor(sqrt(count)) by default), then scan
 *                         one block; O(sqrt n), friendly to media where
 *                         stepping back is expensive
 *   ds_fibonacci_search   Fibonaccian search: splits at Fibonacci offsets,
 *                         only additions and subtractions, O(log n)
 *   ds_ternary_search     two probes per step, thirds; O(log n)
 *   ds_interpolation_sequential_search[_long]  one interpolation probe,
 *                         then a sequential scan towards the key; index of
 *                         the first equal element or count
 * and on other shapes:
 *   ds_ternary_search_peak  index of the maximum of a strictly unimodal
 *                         array (increasing, then decreasing)
 *   ds_ternary_search_min   minimum of a unimodal function on [lo, hi]
 *   ds_bond_search[_ulong]  Bond sequential search: find two keys in one
 *                         pass; the unsigned long version filters with
 *                         the bitwise OR of the keys before comparing
 *
 * Linear searches (find, find_if, min/max) work on unsorted data.
 */

#include "../lib/status.h"
#include "elem.h"
#include "numerical.h" /* ds_real_func_t */
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t ds_lower_bound(const void *base, size_t count, size_t elem_size,
                      const void *key, ds_compare_func_t compare, void *user);
size_t ds_upper_bound(const void *base, size_t count, size_t elem_size,
                      const void *key, ds_compare_func_t compare, void *user);
void ds_equal_range(const void *base, size_t count, size_t elem_size,
                    const void *key, ds_compare_func_t compare, void *user,
                    size_t *out_first, size_t *out_last);
ds_status_t ds_binary_search(const void *base, size_t count, size_t elem_size,
                             const void *key, ds_compare_func_t compare,
                             void *user, size_t *out_index);
size_t ds_exponential_search(const void *base, size_t count, size_t elem_size,
                             const void *key, ds_compare_func_t compare,
                             void *user);
size_t ds_interpolation_search_long(const long *values, size_t count, long key);

/* Maps an element (or the key) to a number for interpolation. */
typedef double (*ds_numeric_func_t)(const void *elem, void *user);

/* step 0 means floor(sqrt(count)). */
size_t ds_jump_search(const void *base, size_t count, size_t elem_size,
                      const void *key, size_t step, ds_compare_func_t compare,
                      void *user);
size_t ds_fibonacci_search(const void *base, size_t count, size_t elem_size,
                           const void *key, ds_compare_func_t compare,
                           void *user);
size_t ds_ternary_search(const void *base, size_t count, size_t elem_size,
                         const void *key, ds_compare_func_t compare,
                         void *user);
size_t ds_interpolation_sequential_search_long(const long *values, size_t count,
                                               long key);
/* compare decides, numeric(elem) and numeric(key) only place the probe. */
size_t ds_interpolation_sequential_search(const void *base, size_t count,
                                          size_t elem_size, const void *key,
                                          ds_compare_func_t compare,
                                          ds_numeric_func_t numeric,
                                          void *user);
/* count when count == 0. */
size_t ds_ternary_search_peak(const void *base, size_t count, size_t elem_size,
                              ds_compare_func_t compare, void *user);
/* Stops when the bracket is narrower than tolerance (or after 300
 * rounds); returns its midpoint. */
double ds_ternary_search_min(ds_real_func_t f, void *user, double lo, double hi,
                             double tolerance);
/* First indexes of key1 and key2 (count when absent) in one pass over
 * unsorted data. */
void ds_bond_search(const void *base, size_t count, size_t elem_size,
                    const void *key1, const void *key2,
                    ds_compare_func_t compare, void *user, size_t *out_index1,
                    size_t *out_index2);
void ds_bond_search_ulong(const unsigned long *values, size_t count,
                          unsigned long key1, unsigned long key2,
                          size_t *out_index1, size_t *out_index2);

size_t ds_find(const void *base, size_t count, size_t elem_size,
               const void *key, ds_compare_func_t compare, void *user);
size_t ds_find_if(const void *base, size_t count, size_t elem_size,
                  ds_predicate_func_t predicate, void *user);
size_t ds_count_if(const void *base, size_t count, size_t elem_size,
                   ds_predicate_func_t predicate, void *user);
size_t ds_min_element(const void *base, size_t count, size_t elem_size,
                      ds_compare_func_t compare, void *user);
size_t ds_max_element(const void *base, size_t count, size_t elem_size,
                      ds_compare_func_t compare, void *user);
void ds_minmax_element(const void *base, size_t count, size_t elem_size,
                       ds_compare_func_t compare, void *user, size_t *out_min,
                       size_t *out_max);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_SEARCH_H */
