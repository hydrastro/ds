#ifndef DS_ALGO_CARTESIAN_TREE_H
#define DS_ALGO_CARTESIAN_TREE_H

/*
 * Cartesian trees (Vuillemin): the binary tree of an array that is heap
 * ordered by value and whose in-order traversal is the array. Built in
 * O(n) with a stack of the right spine; ties keep the earlier element as
 * the ancestor, so the tree is unique.
 *
 *   ds_cartesian_tree_build  parent / left / right index arrays and the
 *                            root, for any comparator (min-heap order:
 *                            the root is the minimum)
 *   ds_cartesian_tree_rmq    range minimum through the tree: the lowest
 *                            common ancestor of i and j (O(depth))
 *   ds_cartesian_tree_sort   Levcopoulos-Petersson adaptive sort: the
 *                            tree's root goes into a heap; popping a node
 *                            pushes its children. Stable and adaptive:
 *                            the heap stays small on nearly sorted input
 *                            (O(n) when sorted, O(n log n) worst case).
 *
 * Elements are by value (elem_size bytes); indices are size_t with
 * DS_CARTESIAN_NONE for "no node". Scratch comes from the ds_context_t.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_CARTESIAN_NONE (~(size_t)0)

typedef int (*ds_cartesian_compare_func_t)(const void *a, const void *b,
                                           void *user);

/* left, right (and parent, which may be NULL): n entries each. */
ds_status_t ds_cartesian_tree_build(const void *base, size_t n,
                                    size_t elem_size,
                                    ds_cartesian_compare_func_t compare,
                                    void *user, size_t *parent, size_t *left,
                                    size_t *right, size_t *out_root,
                                    ds_context_t *context);
/* Index of the minimum of base[i..j] (i <= j) given the tree's parent
 * links (built from the same array). */
ds_status_t ds_cartesian_tree_rmq(const size_t *parent, size_t n, size_t i,
                                  size_t j, size_t *out_index);
/* Sorts base in place (stable). */
ds_status_t ds_cartesian_tree_sort(void *base, size_t n, size_t elem_size,
                                   ds_cartesian_compare_func_t compare,
                                   void *user, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_CARTESIAN_TREE_H */
