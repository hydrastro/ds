#ifndef DS_MELDABLE_HEAP_H
#define DS_MELDABLE_HEAP_H

/*
 * Meldable heaps: heap-ordered binary trees whose meld walks down the right
 * spines of both trees, with a pluggable shape policy deciding, bottom-up
 * along the merge path, whether to swap each node's children. The policy is
 * what makes the family member:
 *
 *   ds_meldable_heap_leftist_policy()   leftist tree (Crane, 1972): a node's
 *                                       rank is its shortest path to a NULL
 *                                       link and the left child never has the
 *                                       smaller rank, so the right spine has
 *                                       O(log n) nodes.
 *   ds_meldable_heap_weight_policy()    weight-biased leftist tree: the same
 *                                       with subtree sizes instead of ranks.
 *   ds_meldable_heap_skew_policy()      skew heap (Sleator and Tarjan, 1986):
 *                                       swap at every node of the merge path;
 *                                       no shape data, O(log n) amortized.
 *   ds_meldable_heap_random_policy()    randomized meldable heap (Gambin and
 *                                       Malinowski, 1998): swap with
 *                                       probability 1/2; O(log n) expected.
 *
 * A custom policy only has to implement fix(): look at a node's children,
 * update node->aux (free for the policy) and say whether to swap them.
 *
 * The heap is intrusive and never allocates: embed ds_meldable_heap_node_t
 * in your struct and recover it with CAST() or offsetof. The comparator
 * orders nodes (smallest on top) and receives the user pointer. Handles stay
 * valid while the node is in the heap, so any node can be removed and keys
 * can change in place (decrease or update).
 *
 * Complexity, h = merge path length (O(log n) worst case for the leftist and
 * weight-biased policies, amortized for skew, expected for random):
 *   push, pop, meld, decrease   O(h)
 *   remove, update              O(h) plus the length of the path along
 *                               which the policy's shape data changes (up to
 *                               the depth of the node for the weight policy)
 *   peek, size                  O(1)
 * All operations are iterative; skew heaps can have long left paths and
 * nothing recurses on them.
 * Concurrency: a heap may be used by one thread at a time.
 */

#include "common.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_meldable_heap_node {
  struct ds_meldable_heap_node *parent;
  struct ds_meldable_heap_node *left;
  struct ds_meldable_heap_node *right;
  size_t aux; /* owned by the policy: rank, size, ... */
} ds_meldable_heap_node_t;

typedef int (*ds_meldable_heap_compare_func_t)(const ds_meldable_heap_node_t *a,
                                               const ds_meldable_heap_node_t *b,
                                               void *user);
typedef ds_status_t (*ds_meldable_heap_visit_func_t)(
    ds_meldable_heap_node_t *node, void *user);

typedef struct ds_meldable_heap_policy {
  const char *name;
  /* Called on a node whose subtrees just changed (children are already
   * fixed). Update node->aux; return true to swap node->left and
   * node->right. `state` is the heap's policy_state. */
  bool (*fix)(ds_meldable_heap_node_t *node, void *state);
  /* Shape data of a fresh single node (usually 0 or 1). */
  size_t leaf_aux;
} ds_meldable_heap_policy_t;

typedef struct ds_meldable_heap {
  ds_meldable_heap_node_t *root;
  size_t size;
  ds_meldable_heap_compare_func_t compare;
  void *user;
  const ds_meldable_heap_policy_t *policy;
  void *policy_state;
  unsigned long rng; /* default state for the random policy */
} ds_meldable_heap_t;

const ds_meldable_heap_policy_t *ds_meldable_heap_leftist_policy(void);
const ds_meldable_heap_policy_t *ds_meldable_heap_weight_policy(void);
const ds_meldable_heap_policy_t *ds_meldable_heap_skew_policy(void);
/* state: an unsigned long seed updated in place; NULL uses the heap's own. */
const ds_meldable_heap_policy_t *ds_meldable_heap_random_policy(void);

/* policy NULL selects the leftist policy. */
ds_status_t ds_meldable_heap_init(ds_meldable_heap_t *heap,
                                  ds_meldable_heap_compare_func_t compare,
                                  void *user,
                                  const ds_meldable_heap_policy_t *policy,
                                  void *policy_state);

void ds_meldable_heap_push(ds_meldable_heap_t *heap,
                           ds_meldable_heap_node_t *node);
ds_meldable_heap_node_t *ds_meldable_heap_peek(const ds_meldable_heap_t *heap);
ds_meldable_heap_node_t *ds_meldable_heap_pop(ds_meldable_heap_t *heap);
/* Move every node of `other` into `heap`; other is left empty. Both heaps
 * must use the same comparator and policy. */
void ds_meldable_heap_meld(ds_meldable_heap_t *heap, ds_meldable_heap_t *other);
void ds_meldable_heap_decrease(ds_meldable_heap_t *heap,
                               ds_meldable_heap_node_t *node);
void ds_meldable_heap_update(ds_meldable_heap_t *heap,
                             ds_meldable_heap_node_t *node);
void ds_meldable_heap_remove(ds_meldable_heap_t *heap,
                             ds_meldable_heap_node_t *node);

size_t ds_meldable_heap_size(const ds_meldable_heap_t *heap);
bool ds_meldable_heap_is_empty(const ds_meldable_heap_t *heap);

/* Preorder walk; the visitor must not modify the heap. */
ds_status_t ds_meldable_heap_visit(const ds_meldable_heap_t *heap,
                                   ds_meldable_heap_visit_func_t visit,
                                   void *user);
/* Empty the heap, children before parents; `release` may free each node. */
void ds_meldable_heap_clear(ds_meldable_heap_t *heap,
                            void (*release)(ds_meldable_heap_node_t *node,
                                            void *user),
                            void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_MELDABLE_HEAP_H */
