#ifndef DS_ALGO_SORT_HEAP_H
#define DS_ALGO_SORT_HEAP_H

/*
 * Heap- and tree-based sorts (heapsort itself is in sort.h).
 *
 *   ds_smoothsort           Dijkstra's smoothsort: a forest of Leonardo
 *                           heaps built and dismantled in place; O(n log n)
 *                           worst case, O(n) on sorted input, no extra
 *                           memory
 *   ds_adaptive_heap_sort   Levcopoulos-Petersson: a heap seeded with the
 *                           root of the input's Cartesian tree; extracting
 *                           a node inserts its Cartesian children, so the
 *                           heap stays as small as the disorder;
 *                           O(n log Osc(X) / n), stable
 *   ds_dominance_tree_sort  pairwise matches link each loser below its
 *                           winner (dominance tree); the children of an
 *                           extracted root play again, two-pass pairing;
 *                           stable
 *   ds_tournament_sort      winners of a loser tree over all items, one
 *                           replay per output; stable
 *   ds_tree_sort            insert into a binary search tree and walk it
 *                           in order; unbalanced (classic treesort) or a
 *                           randomized treap; stable
 *   ds_jsort                JSort: build a min-heap, build a reversed
 *                           max-heap, then insertion sort the nearly
 *                           sorted result; in place
 *
 * Sorts that need working memory take it from the ds_context_t and return
 * DS_ERR_ALLOC (input unchanged) when it fails.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include "elem.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void ds_smoothsort(void *base, size_t count, size_t elem_size,
                   ds_compare_func_t compare, void *user);
ds_status_t ds_adaptive_heap_sort(void *base, size_t count, size_t elem_size,
                                  ds_compare_func_t compare, void *user,
                                  ds_context_t *context);
ds_status_t ds_dominance_tree_sort(void *base, size_t count, size_t elem_size,
                                   ds_compare_func_t compare, void *user,
                                   ds_context_t *context);
ds_status_t ds_tournament_sort(void *base, size_t count, size_t elem_size,
                               ds_compare_func_t compare, void *user,
                               ds_context_t *context);
ds_status_t ds_tree_sort(void *base, size_t count, size_t elem_size,
                         ds_compare_func_t compare, void *user, bool balanced,
                         ds_context_t *context);
void ds_jsort(void *base, size_t count, size_t elem_size,
              ds_compare_func_t compare, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_SORT_HEAP_H */
