#ifndef DS_ALGO_SORT_MISC_H
#define DS_ALGO_SORT_MISC_H

/*
 * More comparison sorts (see sort.h and sort_classic.h for the others).
 *
 * Quicksort with pluggable pieces (ds_quicksort_config_t):
 *   pivot      first, last, middle, median of three, ninther (median of
 *              three medians of three), random, or the exact median by
 *              linear-time selection (balanced quicksort: O(n log n)
 *              worst case)
 *   partition  Hoare (two scans towards each other), Lomuto (one scan),
 *              or three-way (Dijkstra's Dutch national flag; linear on
 *              inputs with few distinct keys)
 *   cutoff     partitions at or below it are finished by `small_sort`
 *              (insertion sort by default); 0 disables
 *   depth limit when set, a partition deeper than 2 log2 n falls back to
 *              heapsort (introspective)
 * The recursion is an explicit stack (larger part deferred), so stack use
 * is O(log n) whatever the input. Presets:
 *   ds_quicksort_plain  Hoare's original: first element pivot, no cutoff
 *   ds_q_sort           q sort: one member of the "library sort" family
 *                       DADS describes (quicksort finished by insertion
 *                       sort): middle pivot, insertion sort below 16
 *   ds_median3_quicksort  median-of-three pivot, insertion cutoff, no
 *                       introspection
 *   ds_qm_sort          deprecated alias of ds_median3_quicksort. It is
 *                       NOT the "qm sort" of the DADS entry (Pat Morin's
 *                       implementation), which this library does not
 *                       provide; the name is kept for compatibility only
 *   ds_balanced_quicksort  median pivot (guaranteed balanced splits)
 * ds_dual_pivot_quicksort() is Yaroslavskiy's two-pivot, three-way
 * partitioning quicksort.
 *
 * Merge sorts (stable, scratch of count elements):
 *   ds_two_way_merge_sort, ds_three_way_merge_sort, ds_kway_merge_sort
 *              bottom-up merge sort merging `ways` runs at a time through
 *              a loser tree (p-way merge sort)
 *   ds_adaptive_sort  natural merge sort: finds existing ascending and
 *              strictly descending runs, extends short runs by binary
 *              insertion, merges with a run stack and galloping trims
 *              (O(n) on sorted or reversed input, O(n log n) worst)
 *   ds_strand_sort  repeatedly pulls a non-decreasing strand out of the
 *              remaining items and merges it into the result
 *
 * Others: ds_bingo_sort (selection sort moving every copy of the current
 * maximum per pass, O(n m) for m distinct values), ds_dutch_national_flag
 * (three-way partition by a classifier).
 *
 * Joke sorts, with a step budget (0: unbounded) and a pluggable random
 * source; each returns whether the result is sorted: ds_bogosort
 * (shuffle until sorted), ds_bozo_sort (swap two random elements until
 * sorted), ds_permutation_sort (try every permutation, Heap's algorithm),
 * ds_lucky_sort (does nothing; tells you whether you were lucky),
 * ds_taco_sort_ulong (Michael Bernard's taco sort: set a random item to a
 * random k-bit value until the array is a sorted permutation of its
 * original contents; expected n 2^(n k) steps).
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include "elem.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ds_sort_func_t)(void *base, size_t count, size_t elem_size,
                               ds_compare_func_t compare, void *user);

typedef enum ds_quicksort_pivot {
  DS_QS_PIVOT_FIRST = 0,
  DS_QS_PIVOT_LAST = 1,
  DS_QS_PIVOT_MIDDLE = 2,
  DS_QS_PIVOT_MEDIAN3 = 3,
  DS_QS_PIVOT_NINTHER = 4,
  DS_QS_PIVOT_RANDOM = 5,
  DS_QS_PIVOT_MEDIAN = 6
} ds_quicksort_pivot_t;

typedef enum ds_quicksort_partition {
  DS_QS_PARTITION_HOARE = 0,
  DS_QS_PARTITION_LOMUTO = 1,
  DS_QS_PARTITION_THREE_WAY = 2
} ds_quicksort_partition_t;

typedef struct ds_quicksort_config {
  ds_quicksort_pivot_t pivot;
  ds_quicksort_partition_t partition;
  size_t cutoff;             /* 0: partition down to single elements */
  ds_sort_func_t small_sort; /* NULL: insertion sort */
  bool introspective;        /* heapsort below depth 2 log2 n */
  ds_random_func_t random;   /* DS_QS_PIVOT_RANDOM (NULL: internal) */
  void *random_state;
} ds_quicksort_config_t;

/* Fills the defaults: median of three, Hoare, cutoff 16, insertion
 * sort, introspective. */
void ds_quicksort_config_default(ds_quicksort_config_t *config);
/* config NULL means the defaults. */
void ds_quicksort(void *base, size_t count, size_t elem_size,
                  ds_compare_func_t compare, void *user,
                  const ds_quicksort_config_t *config);
void ds_quicksort_plain(void *base, size_t count, size_t elem_size,
                        ds_compare_func_t compare, void *user);
void ds_q_sort(void *base, size_t count, size_t elem_size,
               ds_compare_func_t compare, void *user);
void ds_median3_quicksort(void *base, size_t count, size_t elem_size,
                          ds_compare_func_t compare, void *user);
void ds_qm_sort(void *base, size_t count, size_t elem_size,
                ds_compare_func_t compare, void *user);
void ds_balanced_quicksort(void *base, size_t count, size_t elem_size,
                           ds_compare_func_t compare, void *user);
void ds_dual_pivot_quicksort(void *base, size_t count, size_t elem_size,
                             ds_compare_func_t compare, void *user);

/* Three-way partition: classify(elem) returns 0, 1 or 2 (anything else
 * counts as 2). Afterwards [0, *out_first1) holds class 0,
 * [*out_first1, *out_first2) class 1 and the rest class 2. Not stable;
 * at most count swaps. */
typedef int (*ds_classify_func_t)(const void *elem, void *user);
void ds_dutch_national_flag(void *base, size_t count, size_t elem_size,
                            ds_classify_func_t classify, void *user,
                            size_t *out_first1, size_t *out_first2);

/* ways >= 2; scratch holds count elements. Working memory for the loser
 * tree comes from the context (DS_ERR_ALLOC if it fails). */
ds_status_t ds_kway_merge_sort(void *base, size_t count, size_t elem_size,
                               size_t ways, ds_compare_func_t compare,
                               void *user, void *scratch,
                               ds_context_t *context);
ds_status_t ds_two_way_merge_sort(void *base, size_t count, size_t elem_size,
                                  ds_compare_func_t compare, void *user,
                                  void *scratch);
ds_status_t ds_three_way_merge_sort(void *base, size_t count, size_t elem_size,
                                    ds_compare_func_t compare, void *user,
                                    void *scratch, ds_context_t *context);
/* scratch: count / 2 + 1 elements; NULL falls back to the in-place
 * stable sort. */
void ds_adaptive_sort(void *base, size_t count, size_t elem_size,
                      ds_compare_func_t compare, void *user, void *scratch);
/* Stable; scratch holds count elements (DS_ERR_NULL without it). */
ds_status_t ds_strand_sort(void *base, size_t count, size_t elem_size,
                           ds_compare_func_t compare, void *user,
                           void *scratch);
void ds_bingo_sort(void *base, size_t count, size_t elem_size,
                   ds_compare_func_t compare, void *user);

bool ds_bogosort(void *base, size_t count, size_t elem_size,
                 ds_compare_func_t compare, void *user, ds_random_func_t random,
                 void *random_state, unsigned long max_steps);
bool ds_bozo_sort(void *base, size_t count, size_t elem_size,
                  ds_compare_func_t compare, void *user,
                  ds_random_func_t random, void *random_state,
                  unsigned long max_steps);
/* Needs count counters from the context; false also on allocation
 * failure. */
bool ds_permutation_sort(void *base, size_t count, size_t elem_size,
                         ds_compare_func_t compare, void *user,
                         unsigned long max_steps, ds_context_t *context);
bool ds_lucky_sort(void *base, size_t count, size_t elem_size,
                   ds_compare_func_t compare, void *user);
/* values are k-bit (1 <= bits <= 32); false when the budget runs out (the
 * array then holds the last random state) or on allocation failure. */
bool ds_taco_sort_ulong(unsigned long *values, size_t count, unsigned bits,
                        ds_random_func_t random, void *random_state,
                        unsigned long max_steps, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_SORT_MISC_H */
