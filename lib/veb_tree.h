#ifndef DS_VEB_TREE_H
#define DS_VEB_TREE_H

/*
 * van Emde Boas tree (van Emde Boas, 1975): an ordered set of integer keys
 * drawn from a universe [0, 2^w), w <= the width of unsigned long, with
 * O(log w) = O(log log U) insert, remove, member, successor and predecessor.
 * Used as a priority queue it gives O(log log U) push and pop-min, which is
 * the "van Emde Boas priority queue" of the literature.
 *
 * Each level splits a key into high and low halves: a summary structure
 * records which clusters are non-empty and each cluster holds the low
 * halves; the minimum of a level is kept out of its clusters, which is what
 * makes every operation recurse only once. Levels of 32 or fewer keys are
 * single words.
 *
 * Clusters are allocated on demand from the ds_context_t and released when
 * they empty, so memory is O(n * w) words in the worst case rather than
 * O(U). A level with at most 2^10 clusters indexes them with a dense array;
 * wider levels (universes above 20 bits) index them with a Robin Hood hash
 * map (rh_map.h), so even a 64-bit universe stays small and the bounds
 * become expected rather than worst case.
 * Insertion either succeeds or leaves the set unchanged (DS_ERR_ALLOC).
 *
 * Complexity: insert, remove, contains, successor, predecessor, pop_min
 * O(log w); min, max, size O(1).
 * Concurrency: a tree may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_veb_node {
  unsigned long min;
  unsigned long max;
  unsigned long mask; /* leaf levels: the set as a bit mask */
  unsigned int bits;
  bool has; /* non-leaf levels: non-empty */
  struct ds_veb_node *summary;
  void *clusters; /* internal: a dense array, or a hash map when sparse */
} ds_veb_node_t;

typedef struct ds_veb_tree {
  ds_veb_node_t root;
  size_t size;
  ds_context_t *context;
} ds_veb_tree_t;

/* universe_bits in 1 .. CHAR_BIT * sizeof(unsigned long). */
ds_status_t ds_veb_tree_init(ds_veb_tree_t *tree, unsigned int universe_bits,
                             ds_context_t *context);
void ds_veb_tree_destroy(ds_veb_tree_t *tree);
void ds_veb_tree_clear(ds_veb_tree_t *tree);

/* DS_OK, DS_EXISTS, DS_ERR_RANGE (key outside the universe), DS_ERR_ALLOC. */
ds_status_t ds_veb_tree_insert(ds_veb_tree_t *tree, unsigned long key);
/* DS_OK or DS_NOT_FOUND. */
ds_status_t ds_veb_tree_remove(ds_veb_tree_t *tree, unsigned long key);
bool ds_veb_tree_contains(const ds_veb_tree_t *tree, unsigned long key);

/* DS_OK or DS_EMPTY. */
ds_status_t ds_veb_tree_min(const ds_veb_tree_t *tree, unsigned long *out);
ds_status_t ds_veb_tree_max(const ds_veb_tree_t *tree, unsigned long *out);
ds_status_t ds_veb_tree_pop_min(ds_veb_tree_t *tree, unsigned long *out);
ds_status_t ds_veb_tree_pop_max(ds_veb_tree_t *tree, unsigned long *out);
/* Smallest key > key / largest key < key: DS_OK or DS_NOT_FOUND. key may be
 * any value; keys beyond the universe behave as expected. */
ds_status_t ds_veb_tree_successor(const ds_veb_tree_t *tree, unsigned long key,
                                  unsigned long *out);
ds_status_t ds_veb_tree_predecessor(const ds_veb_tree_t *tree,
                                    unsigned long key, unsigned long *out);

size_t ds_veb_tree_size(const ds_veb_tree_t *tree);
bool ds_veb_tree_is_empty(const ds_veb_tree_t *tree);
unsigned int ds_veb_tree_universe_bits(const ds_veb_tree_t *tree);

#ifdef __cplusplus
}
#endif

#endif /* DS_VEB_TREE_H */
