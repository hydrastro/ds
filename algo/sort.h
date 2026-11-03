#ifndef DS_ALGO_SORT_H
#define DS_ALGO_SORT_H

/*
 * Sorting algorithms over arrays of fixed-size elements (see elem.h).
 *
 *   function               stable  time (worst)   extra space
 *   ds_insertion_sort      yes     O(n^2)         O(1)
 *   ds_shell_sort          no      ~O(n^1.3)      O(1), no recursion
 *   ds_heap_sort           no      O(n log n)     O(1), no recursion
 *   ds_intro_sort          no      O(n log n)     O(log n) stack
 *   ds_merge_sort          yes     O(n log n)     n elements of scratch
 *   ds_inplace_stable_sort yes     O(n log^2 n)   O(log n) stack, no scratch
 *   ds_radix_sort_keyed    yes     O(n * w)       n elements of scratch
 *   ds_radix_sort_ulong    yes     O(n * w)       n unsigned longs of scratch
 *
 * ds_intro_sort is the general-purpose default (quicksort with
 * median-of-three, a heapsort fallback that bounds the worst case, and
 * insertion sort for small ranges). For a stable sort use ds_merge_sort with
 * a scratch buffer, or ds_inplace_stable_sort where no memory is available.
 * The radix sorts order by an unsigned long key in w = sizeof(unsigned long)
 * byte passes, skipping passes where every key shares the same byte.
 *
 * None of these functions allocate.
 */

#include "elem.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long (*ds_key_func_t)(const void *elem, void *user);

void ds_insertion_sort(void *base, size_t count, size_t elem_size,
                       ds_compare_func_t compare, void *user);
void ds_shell_sort(void *base, size_t count, size_t elem_size,
                   ds_compare_func_t compare, void *user);
void ds_heap_sort(void *base, size_t count, size_t elem_size,
                  ds_compare_func_t compare, void *user);
void ds_intro_sort(void *base, size_t count, size_t elem_size,
                   ds_compare_func_t compare, void *user);
void ds_merge_sort(void *base, size_t count, size_t elem_size,
                   ds_compare_func_t compare, void *user, void *scratch);
void ds_inplace_stable_sort(void *base, size_t count, size_t elem_size,
                            ds_compare_func_t compare, void *user);
void ds_radix_sort_keyed(void *base, size_t count, size_t elem_size,
                         ds_key_func_t key, void *user, void *scratch);
void ds_radix_sort_ulong(unsigned long *values, size_t count,
                         unsigned long *scratch);

bool ds_is_sorted(const void *base, size_t count, size_t elem_size,
                  ds_compare_func_t compare, void *user);
size_t ds_is_sorted_until(const void *base, size_t count, size_t elem_size,
                          ds_compare_func_t compare, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_SORT_H */
