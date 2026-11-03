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
 *                                   child, and apply() visits children in
 *                                   slice order, so traversals are sorted.
 *
 * Contract notes for anyone writing a store (as these do):
 *   - apply(trie, store, f, args) must visit the whole subtree in
 *     post-order: for each child, first apply() to child->children, then
 *     f(trie, child, args). Trie destruction relies on this. Give each call
 *     its own va_list copy, and tolerate f removing entries (iterate over a
 *     snapshot).
 *   - remove/destroy_entry only detach a child; the trie frees nodes.
 *   - clone must call ds_trie_clone_node() for each child.
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
