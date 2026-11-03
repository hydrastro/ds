#ifndef DS_ALGO_SORT_CLASSIC_H
#define DS_ALGO_SORT_CLASSIC_H

/*
 * Classic sorting algorithms (see sort.h for the general-purpose ones).
 * Each has a niche: teaching, tiny inputs, minimum writes, or a fixed
 * comparison pattern that maps onto networks and SIMD.
 *
 *   function                 stable  time          notes
 *   ds_bubble_sort           yes     O(n^2)        stops early when sorted
 *   ds_cocktail_sort         yes     O(n^2)        bidirectional bubble
 *   ds_selection_sort        no      O(n^2)        at most n - 1 swaps
 *   ds_gnome_sort            yes     O(n^2)
 *   ds_binary_insertion_sort yes     O(n^2) moves, O(n log n) compares
 *   ds_comb_sort             no      ~O(n log n)   shrink factor 1.3
 *   ds_odd_even_sort         yes     O(n^2)        brick sort; parallel
 *   ds_cycle_sort            no      O(n^2)        minimum number of writes
 *                                                  (flash/EEPROM friendly)
 *   ds_pancake_sort          no      O(n^2)        only prefix reversals
 *   ds_bitonic_sort          no      O(n log^2 n)  data-independent
 *                                                  comparisons, any n
 *   ds_stooge_sort           no      O(n^2.71)     teaching only
 *   ds_counting_sort_keyed   yes     O(n + k)      integer keys in [0, k)
 *
 * All sort in place without allocation, except counting sort, which takes
 * caller buffers.
 */

#include "elem.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*ds_index_key_func_t)(const void *elem, void *user);

void ds_bubble_sort(void *base, size_t count, size_t elem_size,
                    ds_compare_func_t compare, void *user);
void ds_cocktail_sort(void *base, size_t count, size_t elem_size,
                      ds_compare_func_t compare, void *user);
void ds_selection_sort(void *base, size_t count, size_t elem_size,
                       ds_compare_func_t compare, void *user);
void ds_gnome_sort(void *base, size_t count, size_t elem_size,
                   ds_compare_func_t compare, void *user);
void ds_binary_insertion_sort(void *base, size_t count, size_t elem_size,
                              ds_compare_func_t compare, void *user);
void ds_comb_sort(void *base, size_t count, size_t elem_size,
                  ds_compare_func_t compare, void *user);
void ds_odd_even_sort(void *base, size_t count, size_t elem_size,
                      ds_compare_func_t compare, void *user);
size_t ds_cycle_sort(void *base, size_t count, size_t elem_size,
                     ds_compare_func_t compare, void *user);
void ds_pancake_sort(void *base, size_t count, size_t elem_size,
                     ds_compare_func_t compare, void *user);
void ds_bitonic_sort(void *base, size_t count, size_t elem_size,
                     ds_compare_func_t compare, void *user);
void ds_stooge_sort(void *base, size_t count, size_t elem_size,
                    ds_compare_func_t compare, void *user);
void ds_counting_sort_keyed(const void *base, size_t count, size_t elem_size,
                            ds_index_key_func_t key, void *user,
                            size_t key_range, size_t *counts, void *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_SORT_CLASSIC_H */
