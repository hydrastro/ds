#ifndef DS_TRIE_STORES_H
#define DS_TRIE_STORES_H

/*
 * Additional child stores for the trie (trie.h), built only on the public
 * ds_trie_store_ops_t contract. Pick one with ds_trie_config_t.store_ops:
 *
 *   ds_trie_rh_map_store_ops()      children in a Robin Hood hash map
 *                                   (rh_map.h): O(1) child lookup, compact
 *                                   open addressing, no per-child nodes.
 *   ds_trie_sorted_vec_store_ops()  children in a sorted vector (vec.h) with
 *                                   binary search: the smallest memory per
 *                                   child, and walks visit children in
 *                                   slice order, so traversals are sorted.
 *
 * Both provide next (trie.h), so tries on them walk, copy and destroy
 * themselves without recursion, and their apply and clone are
 * ds_trie_walk and ds_trie_clone_store over it. The Robin Hood store's
 * order is the map's stable order (ds_rh_map_after: by hash, kept through
 * growth); the vector finds a child by address, at the place it answered
 * last, so walking it costs O(1) a step.
 *
 * Memory: a store, and the map or vector inside it, come from where the
 * trie's nodes come from: its context, or its allocator functions. Given
 * functions other than malloc and free, each store carries a context over
 * them (ds_context_use_allocator_functions in allocators.h) in its own
 * block, about 130 bytes more per node on 64-bit systems; a trie given a
 * context instead needs none.
 *
 * Contract notes for anyone writing a store (as these do):
 *   - provide next (trie.h); apply and clone can then be ds_trie_walk and
 *     ds_trie_clone_store over it.
 *   - without next, apply(trie, store, f, args) must visit the whole
 *     subtree in post-order: for each child, first apply() to
 *     child->children, then f(trie, child, args), each call with its own
 *     va_list copy; and clone must call ds_trie_clone_node() for each
 *     child. Both recurse as deep as the longest key.
 *   - remove/destroy_entry only detach a child; the trie frees nodes.
 */

#include "trie.h"

#ifdef __cplusplus
extern "C" {
#endif

const ds_trie_store_ops_t *ds_trie_rh_map_store_ops(void);
const ds_trie_store_ops_t *ds_trie_sorted_vec_store_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* DS_TRIE_STORES_H */
