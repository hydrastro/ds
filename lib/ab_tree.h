#ifndef DS_AB_TREE_H
#define DS_AB_TREE_H

/*
 * (a,b)-trees (Huddleston and Mehlhorn, 1982), for every admissible pair:
 * 2 <= a and 2a - 1 <= b. Items live in the leaves, which all lie at the
 * same depth and hold between a and b items each; internal nodes have
 * between a and b children and a separator between every two of them;
 * the root may hold fewer (from 1 item, or from 2 children). A (t, 2t)
 * pair gives the B-trees of lib/btree, a (2, 3) pair the 2-3 trees, (2, 4)
 * the 2-3-4 trees; b >= 2a is the case with O(1) amortized restructuring
 * per update (the counters below show it), b = 2a - 1 the tightest.
 *
 * Every level is linked (each node knows its neighbours on its level,
 * across parents), and every node keeps the separator that divides it
 * from its left neighbour. A node's key range is therefore known in O(1),
 * which gives finger searches: from a cursor, a search climbs only until
 * the target lies in the range of the node reached or of its neighbour,
 * O(log d) for a target d items away (Huddleston and Mehlhorn's
 * level-linked trees). Cursors step through the leaves in O(1).
 *
 * Nodes count the items below them, so rank and select take O(log n), as
 * do insertions and removals, which descend from the root anyway. Join
 * concatenates two trees in O(log n) and split cuts one at a key in
 * O(log n), both all or nothing.
 *
 * Items are copied in (item_size bytes) and ordered by the comparison;
 * equal items are one item. Pointers to items stay valid until the next
 * insert, remove, split or join. Memory comes from the context; an update
 * takes every node it may need before changing anything, and keeps up to
 * a few freed nodes for the next one.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ds_ab_compare_fn)(const void *a, const void *b, void *user);

typedef struct ds_ab_node {
  struct ds_ab_node *parent;
  struct ds_ab_node *prev; /* level links */
  struct ds_ab_node *next;
  struct ds_ab_node **child; /* internal nodes: up to b + 1 */
  unsigned char *items; /* a leaf's items, or an internal node's separators */
  unsigned char *lo;    /* the separator before this node (if prev) */
  size_t count;         /* items, or children */
  size_t size;          /* items below */
  unsigned char leaf;
} ds_ab_node_t;

typedef struct ds_ab_tree {
  ds_ab_node_t *root;
  ds_ab_node_t *first; /* leftmost and rightmost leaves */
  ds_ab_node_t *last;
  size_t a;
  size_t b;
  size_t item_size;
  size_t size;
  size_t height; /* levels: 0 when empty, 1 for a single leaf */
  ds_ab_compare_fn compare;
  void *user;
  ds_context_t *context;
  ds_ab_node_t *spare; /* freed nodes kept for the next update */
  size_t spares;
  unsigned char *scratch;
  size_t node_bytes;
  /* restructuring done so far, for inspection */
  unsigned long splits;
  unsigned long fusions;
  unsigned long shares;
} ds_ab_tree_t;

/* a position: an item of a leaf, or the end (leaf NULL) */
typedef struct ds_ab_cursor {
  ds_ab_node_t *leaf;
  size_t index;
  size_t item_size;
} ds_ab_cursor_t;

/* DS_ERR_INVALID unless 2 <= a, 2a - 1 <= b and item_size > 0 */
ds_status_t ds_ab_init(ds_ab_tree_t *t, size_t a, size_t b, size_t item_size,
                       ds_ab_compare_fn compare, void *user,
                       ds_context_t *context);
void ds_ab_destroy(ds_ab_tree_t *t);
void ds_ab_clear(ds_ab_tree_t *t);
/* the n sorted, distinct items of `items` into an empty tree, O(n);
 * DS_ERR_INVALID when not strictly increasing */
ds_status_t ds_ab_build(ds_ab_tree_t *t, const void *items, size_t n);

/* DS_EXISTS (and no change) when an equal item is present */
ds_status_t ds_ab_insert(ds_ab_tree_t *t, const void *item);
/* inserts or replaces the equal item; DS_EXISTS when it replaced one */
ds_status_t ds_ab_put(ds_ab_tree_t *t, const void *item);
/* DS_NOT_FOUND when absent; the removed item to out (may be NULL) */
ds_status_t ds_ab_remove(ds_ab_tree_t *t, const void *key, void *out);

void *ds_ab_find(const ds_ab_tree_t *t, const void *key);
/* the greatest item <= key / the least item >= key; NULL when none */
void *ds_ab_floor(const ds_ab_tree_t *t, const void *key);
void *ds_ab_ceiling(const ds_ab_tree_t *t, const void *key);
void *ds_ab_min(const ds_ab_tree_t *t);
void *ds_ab_max(const ds_ab_tree_t *t);
size_t ds_ab_size(const ds_ab_tree_t *t);
size_t ds_ab_height(const ds_ab_tree_t *t);
/* the number of items < key; the k-th item from 0 (NULL when k >= size) */
ds_status_t ds_ab_rank(const ds_ab_tree_t *t, const void *key,
                       size_t *out_rank);
void *ds_ab_select(const ds_ab_tree_t *t, size_t k);

/* cursors: the first item, the least item >= key, stepping both ways;
 * ds_ab_cursor_item is NULL at the end */
void ds_ab_cursor_first(const ds_ab_tree_t *t, ds_ab_cursor_t *c);
void ds_ab_cursor_last(const ds_ab_tree_t *t, ds_ab_cursor_t *c);
void ds_ab_cursor_seek(const ds_ab_tree_t *t, const void *key,
                       ds_ab_cursor_t *c);
bool ds_ab_cursor_next(ds_ab_cursor_t *c);
bool ds_ab_cursor_prev(ds_ab_cursor_t *c);
void *ds_ab_cursor_item(const ds_ab_cursor_t *c);
/* ds_ab_cursor_seek starting from the cursor c (a finger): O(log d) for a
 * target d items away; *out_levels (may be NULL) the levels climbed */
void ds_ab_finger_seek(const ds_ab_tree_t *t, const void *key,
                       ds_ab_cursor_t *c, size_t *out_levels);

/* every item of `other` (all greater than this tree's) appended; other
 * is left empty. DS_ERR_INVALID if the orders overlap or the trees differ
 * in a, b, item size or comparison. O(log n) */
ds_status_t ds_ab_join(ds_ab_tree_t *t, ds_ab_tree_t *other);
/* the items >= key move to the empty tree `upper` (initialized like t);
 * O(log n) */
ds_status_t ds_ab_split(ds_ab_tree_t *t, const void *key, ds_ab_tree_t *upper);

/* in increasing order; a non-zero return stops the walk and is returned */
int ds_ab_foreach(const ds_ab_tree_t *t, int (*visit)(void *item, void *user),
                  void *user);
/* every invariant: degrees, depths, order, separators, level links, the
 * separators kept per node, the counts */
ds_status_t ds_ab_check(const ds_ab_tree_t *t, bool *out_valid);

#ifdef __cplusplus
}
#endif

#endif /* DS_AB_TREE_H */
