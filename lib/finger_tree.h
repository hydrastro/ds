#ifndef DS_FINGER_TREE_H
#define DS_FINGER_TREE_H

/*
 * Persistent 2-3 finger trees with a monoid measure (Hinze & Paterson,
 * "Finger trees: a simple general-purpose data structure", 2006).
 *
 * A sequence of elements (stored by value) whose every subtree caches the
 * combined measure of its elements. With the measure chosen by the
 * caller, the same structure is a deque, an indexed sequence (measure =
 * count), a priority queue (measure = max), an ordered sequence (measure =
 * last key) or an interval structure:
 *
 *   push/pop at either end    O(1) amortized
 *   concat                    O(log min(n, m))
 *   split / lookup by a monotone predicate on prefix measures  O(log n)
 *
 * Versions are handles over reference-counted, immutable nodes: every
 * operation returns a new handle and leaves its inputs valid (an output
 * may be one of the inputs, which is then released). combine(out, a, b)
 * must be associative with identity `identity` and must accept out == a.
 * Elements are owned by the nodes holding them and destroyed with the
 * last one (destroy_elem, optional). Memory comes from the context.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_ftree_config {
  size_t elem_size;
  size_t measure_size;
  void (*measure)(void *out, const void *elem, void *user);
  void (*combine)(void *out, const void *a, const void *b, void *user);
  const void *identity;
  void (*destroy_elem)(void *elem, void *user);
  void *user;
  ds_context_t *context;
} ds_ftree_config_t;

/* monotone: once true for a prefix measure, true for every longer one */
typedef bool (*ds_ftree_pred_t)(const void *measure, void *user);
typedef bool (*ds_ftree_visit_t)(const void *elem, void *user);

struct ds_ftree_tree;

typedef struct ds_ftree {
  struct ds_ftree_tree *root; /* NULL when empty */
  const ds_ftree_config_t *config;
} ds_ftree_t;

ds_status_t ds_ftree_init(ds_ftree_t *t, const ds_ftree_config_t *config);
void ds_ftree_copy(const ds_ftree_t *t, ds_ftree_t *out);
void ds_ftree_release(ds_ftree_t *t);
bool ds_ftree_is_empty(const ds_ftree_t *t);
/* the whole sequence's measure (identity when empty) */
void ds_ftree_measure(const ds_ftree_t *t, void *out);
const void *ds_ftree_front(const ds_ftree_t *t);
const void *ds_ftree_back(const ds_ftree_t *t);
ds_status_t ds_ftree_push_front(const ds_ftree_t *t, const void *elem,
                                ds_ftree_t *out);
ds_status_t ds_ftree_push_back(const ds_ftree_t *t, const void *elem,
                               ds_ftree_t *out);
/* out_elem (optional) receives a byte copy of the element */
ds_status_t ds_ftree_pop_front(const ds_ftree_t *t, void *out_elem,
                               ds_ftree_t *out);
ds_status_t ds_ftree_pop_back(const ds_ftree_t *t, void *out_elem,
                              ds_ftree_t *out);
ds_status_t ds_ftree_concat(const ds_ftree_t *a, const ds_ftree_t *b,
                            ds_ftree_t *out);
/* left: the longest prefix whose measure fails pred; right: the rest
 * (starting with the element where pred turns true). */
ds_status_t ds_ftree_split(const ds_ftree_t *t, ds_ftree_pred_t pred,
                           void *user, ds_ftree_t *out_left,
                           ds_ftree_t *out_right);
/* the element at which pred turns true, NULL if it never does */
const void *ds_ftree_lookup(const ds_ftree_t *t, ds_ftree_pred_t pred,
                            void *user);
/* in order; DS_STOP when visit returns false */
ds_status_t ds_ftree_visit(const ds_ftree_t *t, ds_ftree_visit_t visit,
                           void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_FINGER_TREE_H */
