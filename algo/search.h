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
 * Linear searches (find, find_if, min/max) work on unsorted data.
 */

#include "../lib/status.h"
#include "elem.h"
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
