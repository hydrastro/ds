#ifndef DS_CHROMATIC_TREE_H
#define DS_CHROMATIC_TREE_H

/*
 * Chromatic trees: red-black trees with relaxed balance (Nurmi and
 * Soisalon-Soininen), where updates and rebalancing are uncoupled.
 *
 * The tree is leaf-oriented: elements sit in the leaves, and every internal
 * node has two children and a router, a pointer to the leftmost leaf of its
 * right subtree (searches go right on keys at least the router's, and no
 * element is ever copied). Every node has a weight: 0 is red, 1 black, more
 * is overweight. Two things hold at all times: leaves weigh at least 1, and
 * every path from the root to a leaf has the same total weight. A red node
 * with a red parent, and an overweight node, are violations; a tree without
 * violations is a red-black tree, of height at most 2 log2(n) + 1.
 *
 * Updates are local and never rotate. An insertion replaces the leaf it
 * reaches by an internal node of weight w - 1 over the old and the new leaf
 * (weight 1 each); a deletion replaces the leaf's parent by the leaf's
 * sibling, which takes on the parent's weight as well as its own. Each can
 * leave one violation on the key's search path, and the tree stays a valid
 * search tree with exact rank and select.
 *
 * Rebalancing works on the topmost violation of a key's search path, with
 * the transformations of Boyar and Larsen's rebalancing scheme, in mirror
 * pairs: for a red node under a red parent, blacking (the parent and its
 * red sibling turn black, the grandparent red) or a single or double
 * rotation; for an overweight node, a push (it and its sibling hand one
 * unit of weight to their parent), a rotation when the sibling is red, and
 * single or double rotations that absorb one unit when the sibling has a
 * red child. A violation in the way (a red sibling with a red parent or
 * child) is resolved first. Each transformation is O(1) after the walk that
 * finds it; blacking and pushes move a violation up, the others remove one.
 * Boyar, Fagerberg and Larsen show O(1) amortized transformations per
 * update; the counters in ds_chromatic_tree_t record what ran.
 *
 * Eager mode (the default) rebalances each update's search path at once,
 * so the tree is a red-black tree between calls. Deferred mode only records
 * a leaf whose root path holds the update's violation (in a stack of
 * pending leaves; no key is copied, and a pending leaf that is deleted
 * hands its place to a leaf of its replacement); ds_chromatic_rebalance()
 * later runs a chosen number of transformations, for instance after a
 * batch of updates or in idle time. Transformations only create violations
 * on the path they clean, so every violation lies on some pending leaf's
 * path, and once nothing is pending the tree is balanced again. Switching
 * back to eager mode finishes the rebalancing.
 *
 * Routers are links to leaves by default, so keys are never copied and may
 * point at memory freed once their element is removed. Inline routers
 * (ds_chromatic_set_inline_routers(), on an empty tree) store a copy of
 * the router's element in each internal node instead, so searches read no
 * leaves on the way down, at elem_size more bytes per internal node; they
 * suit self-contained keys only, since a copy can outlive the element it
 * came from.
 *
 * Elements are stored by value (elem_size bytes) and ordered by the
 * comparison; a stored element's key must not change, the rest may.
 * Nothing recurses: walks use the root-to-leaf path, a scratch buffer that
 * grows with the height (deferred mode can make the tree deep), and
 * destruction flattens the tree. Memory comes from the context.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ds_chromatic_compare_fn)(const void *a, const void *b,
                                       void *user);

typedef struct ds_chromatic_node {
  struct ds_chromatic_node *child[2]; /* both NULL for a leaf */
  struct ds_chromatic_node *router;   /* internal: leftmost right leaf
                                         (NULL with inline routers) */
  size_t size;                        /* leaves below */
  size_t weight;                      /* 0 red, 1 black, more overweight */
  size_t pending; /* leaf: 1 + its slot among the pending leaves, or 0 */
} ds_chromatic_node_t;

/* transformation counters */
enum {
  DS_CHROMATIC_BLACKING = 0, /* red-red: recolour, violation moves up */
  DS_CHROMATIC_ROTATE1 = 1,  /* red-red: single rotation */
  DS_CHROMATIC_ROTATE2 = 2,  /* red-red: double rotation */
  DS_CHROMATIC_PUSH = 3,     /* overweight: push a unit to the parent */
  DS_CHROMATIC_RED_SIB = 4,  /* overweight: rotate a red sibling up */
  DS_CHROMATIC_ABSORB1 = 5,  /* overweight: single rotation absorbs one */
  DS_CHROMATIC_ABSORB2 = 6,  /* overweight: double rotation absorbs one */
  DS_CHROMATIC_RULES = 7
};

typedef struct ds_chromatic_tree {
  ds_chromatic_node_t *root;
  size_t size;
  size_t elem_size;
  size_t leaf_bytes; /* header plus element, aligned */
  ds_chromatic_compare_fn compare;
  void *user;
  bool deferred;
  bool inline_routers; /* routers hold key copies instead of leaf links */
  /* leaves whose root paths may hold violations (NULL: a dropped slot) */
  ds_chromatic_node_t **pending;
  size_t pending_count; /* slots in use */
  size_t pending_live;  /* leaves among them */
  size_t pending_capacity;
  ds_chromatic_node_t **path; /* scratch: a root-to-node path */
  size_t path_capacity;
  unsigned long transformations[DS_CHROMATIC_RULES];
  ds_context_t *context;
} ds_chromatic_tree_t;

ds_status_t ds_chromatic_init(ds_chromatic_tree_t *t, size_t elem_size,
                              ds_chromatic_compare_fn compare, void *user,
                              ds_context_t *context);
void ds_chromatic_destroy(ds_chromatic_tree_t *t);

/* deferred: updates only record their keys. Leaving deferred mode runs
 * the pending rebalancing (DS_ERR_ALLOC if the path cannot grow; the tree
 * is then still valid, the mode deferred). */
ds_status_t ds_chromatic_set_deferred(ds_chromatic_tree_t *t, bool deferred);

/* routers as key copies in internal nodes (see above); only while the
 * tree is empty (DS_ERR_STATE otherwise) */
ds_status_t ds_chromatic_set_inline_routers(ds_chromatic_tree_t *t, bool on);

/* DS_EXISTS when an equal element is present (nothing changes) */
ds_status_t ds_chromatic_insert(ds_chromatic_tree_t *t, const void *elem);
/* DS_NOT_FOUND when absent; out (optional) gets the removed element */
ds_status_t ds_chromatic_remove(ds_chromatic_tree_t *t, const void *key,
                                void *out);
/* the stored element equal to key, or NULL */
void *ds_chromatic_get(const ds_chromatic_tree_t *t, const void *key);
size_t ds_chromatic_size(const ds_chromatic_tree_t *t);
/* the element of rank k (0-based), or NULL; O(height) */
void *ds_chromatic_select(const ds_chromatic_tree_t *t, size_t k);
/* how many elements are smaller than key */
size_t ds_chromatic_rank(const ds_chromatic_tree_t *t, const void *key);
/* the smallest element >= key, the largest <= key; NULL when none */
void *ds_chromatic_ceiling(const ds_chromatic_tree_t *t, const void *key);
void *ds_chromatic_floor(const ds_chromatic_tree_t *t, const void *key);

/* up to max_steps transformations on pending paths; *out_steps (optional)
 * gets how many ran. Nothing pending afterwards means balanced. */
ds_status_t ds_chromatic_rebalance(ds_chromatic_tree_t *t, size_t max_steps,
                                   size_t *out_steps);
/* pending leaves (paths that may hold violations) */
size_t ds_chromatic_pending(const ds_chromatic_tree_t *t);

/* in key order; a visitor returning false stops the walk. DS_ERR_ALLOC
 * only if the walk's stack cannot grow. */
ds_status_t ds_chromatic_foreach(const ds_chromatic_tree_t *t,
                                 bool (*visit)(void *elem, void *user),
                                 void *user);
/* height in edges (0 for one leaf or an empty tree) */
ds_status_t ds_chromatic_height(const ds_chromatic_tree_t *t,
                                size_t *out_height);
/* the invariants (order, routers, sizes, leaf weights, equal path
 * weights); *out_violations (optional) counts red-red and overweight
 * nodes. O(n height). */
ds_status_t ds_chromatic_check(const ds_chromatic_tree_t *t, bool *out_valid,
                               size_t *out_violations);

#ifdef __cplusplus
}
#endif

#endif /* DS_CHROMATIC_TREE_H */
