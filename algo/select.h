#ifndef DS_ALGO_SELECT_H
#define DS_ALGO_SELECT_H

/*
 * Selection: find the k-th smallest element and partition around it.
 *
 * Every algorithm leaves base[k] holding the element that would be there
 * after sorting, with no larger element before it and no smaller one
 * after it (select and partition). None allocates; the pivot is tracked
 * by position, so element size does not matter.
 *
 *   DS_SELECT_FIND           Hoare's FIND (1961): partition around the
 *                            current base[k], keep the side holding k;
 *                            O(n) average, O(n^2) worst
 *   DS_SELECT_MODIFIND       Zabrodsky's MODIFIND: FIND that stops a
 *                            partitioning pass as soon as the scans cross
 *                            k, fewer swaps and comparisons
 *   DS_SELECT_FLOYD_RIVEST   Floyd-Rivest SELECT: recursive sampling picks
 *                            two pivots bracketing k, ~n + min(k, n - k)
 *                            comparisons on average
 *   DS_SELECT_MEDIAN_OF_MEDIANS  Blum-Floyd-Pratt-Rivest-Tarjan, O(n) worst
 *                            case (groups of five)
 *   DS_SELECT_RANDOMIZED     Randomized-Select: uniform random pivot,
 *                            three-way partition; O(n) expected on any
 *                            input (Las Vegas)
 *   DS_SELECT_INTRO          introselect (ds_nth_element, sort.h)
 *
 * ds_select_mode() finds a most frequent element (select mode).
 */

#include "../lib/status.h"
#include "elem.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_select_algorithm {
  DS_SELECT_INTRO = 0,
  DS_SELECT_FIND = 1,
  DS_SELECT_MODIFIND = 2,
  DS_SELECT_FLOYD_RIVEST = 3,
  DS_SELECT_MEDIAN_OF_MEDIANS = 4,
  DS_SELECT_RANDOMIZED = 5
} ds_select_algorithm_t;

typedef struct ds_select_config {
  ds_select_algorithm_t algorithm;
  /* DS_SELECT_RANDOMIZED: random source (NULL: a fixed internal
   * generator, still O(n) expected on non-adversarial input). */
  ds_random_func_t random;
  void *random_state;
} ds_select_config_t;

/* config NULL means DS_SELECT_INTRO. DS_ERR_RANGE if k >= count. */
ds_status_t ds_select(void *base, size_t count, size_t elem_size, size_t k,
                      ds_compare_func_t compare, void *user,
                      const ds_select_config_t *config);
void ds_select_find(void *base, size_t count, size_t elem_size, size_t k,
                    ds_compare_func_t compare, void *user);
void ds_select_modifind(void *base, size_t count, size_t elem_size, size_t k,
                        ds_compare_func_t compare, void *user);
void ds_select_floyd_rivest(void *base, size_t count, size_t elem_size,
                            size_t k, ds_compare_func_t compare, void *user);
void ds_select_median_of_medians(void *base, size_t count, size_t elem_size,
                                 size_t k, ds_compare_func_t compare,
                                 void *user);
void ds_select_randomized(void *base, size_t count, size_t elem_size, size_t k,
                          ds_compare_func_t compare, void *user,
                          ds_random_func_t random, void *random_state);

/* Median (lower median for even counts): selects index (count - 1) / 2
 * and returns it. */
size_t ds_select_median(void *base, size_t count, size_t elem_size,
                        ds_compare_func_t compare, void *user);

/* A most frequent element: sorts base in place, then scans; the index of
 * the first element of the longest run of equal elements goes to
 * *out_index and its length to *out_frequency (ties: the smallest
 * value). ds_select_mode_sorted() does the scan on sorted input without
 * reordering. DS_EMPTY for count 0. */
ds_status_t ds_select_mode(void *base, size_t count, size_t elem_size,
                           ds_compare_func_t compare, void *user,
                           size_t *out_index, size_t *out_frequency);
ds_status_t ds_select_mode_sorted(const void *base, size_t count,
                                  size_t elem_size, ds_compare_func_t compare,
                                  void *user, size_t *out_index,
                                  size_t *out_frequency);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_SELECT_H */
