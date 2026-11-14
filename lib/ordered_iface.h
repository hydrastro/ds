#ifndef DS_ORDERED_IFACE_H
#define DS_ORDERED_IFACE_H

/*
 * The ordered-map capability over unsigned long keys: what a composite
 * structure needs from a small sorted container it splits and joins
 * (Y-fast trie buckets, B-tree-like leaves, run-length segments), so the
 * caller chooses the trade-off.
 *
 * Values are stored by value (value_size bytes, may be 0). Besides the
 * point operations, floor/ceiling, select by rank and in-order cursors,
 * a backend must split (keys >= k move to another instance) and join
 * (append an instance whose keys are all greater). Traits:
 *
 *   DS_ORDERED_LOG_UPDATE  insert/remove in O(log n) (expected)
 *   DS_ORDERED_CONTIGUOUS  keys in one sorted array: O(n) updates by
 *                          memmove, cache-friendly scans
 *   DS_ORDERED_SPLIT_FAST  split and join in O(log n) without allocating
 *
 * Backends: ds_ordered_array_ops() (sorted arrays),
 * ds_ordered_treap_ops() (treap with subtree sizes) and
 * ds_ordered_fusion_ops() (fusion tree). split and join check
 * compatibility themselves (DS_ERR_INVALID otherwise), also when called
 * through the ops table directly; the array backend copies and accepts any
 * contexts, the treap backend moves nodes and requires the same context.
 * Memory comes from the context.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_ORDERED_LOG_UPDATE 0x1u
#define DS_ORDERED_CONTIGUOUS 0x2u
#define DS_ORDERED_SPLIT_FAST 0x4u

/* visitor for ds_ordered_for_each: anything but DS_OK stops the walk and
 * is returned; the container must not change during the walk */
typedef ds_status_t (*ds_ordered_visit_fn)(unsigned long key, void *value,
                                           void *user);

typedef struct ds_ordered_ops {
  const char *name;
  unsigned traits;
  size_t instance_size;
  ds_status_t (*init)(void *s, size_t value_size, ds_context_t *context);
  void (*destroy)(void *s);
  /* DS_OK when added, DS_EXISTS when the value was replaced */
  ds_status_t (*insert)(void *s, unsigned long key, const void *value);
  ds_status_t (*remove)(void *s, unsigned long key, void *out_value);
  /* the value (a pointer to the stored key when value_size is 0) */
  void *(*find)(const void *s, unsigned long key);
  ds_status_t (*floor)(const void *s, unsigned long x, unsigned long *out);
  ds_status_t (*ceiling)(const void *s, unsigned long x, unsigned long *out);
  size_t (*size)(const void *s);
  ds_status_t (*select)(const void *s, size_t k, unsigned long *out);
  /* move every key >= key into upper (initialised by the same ops and
   * empty); nothing moves on failure */
  ds_status_t (*split)(void *s, unsigned long key, void *upper);
  /* append upper, whose keys all exceed s's, and leave it empty; nothing
   * moves on failure */
  ds_status_t (*join)(void *s, void *upper);
  /* in-order cursor starting at 0 (treaps: O(log n) per step, by rank, so
   * a full scan costs O(n log n); for_each is the linear walk) */
  bool (*next)(const void *s, size_t *cursor, unsigned long *key, void **value);
  /* whether split/join may move entries between the two instances: the
   * value layout must agree, and backends that move nodes (rather than
   * copy) also need the same context to free them */
  bool (*compatible)(const void *s, const void *other);
  /* in-order walk in O(n) total; NULL makes ds_ordered_for_each step the
   * cursor instead */
  ds_status_t (*for_each)(const void *s, ds_ordered_visit_fn visit, void *user);
} ds_ordered_ops_t;

typedef struct ds_ordered {
  const ds_ordered_ops_t *ops;
  void *impl;
  ds_context_t *context;
} ds_ordered_t;

const ds_ordered_ops_t *ds_ordered_array_ops(void);
const ds_ordered_ops_t *ds_ordered_treap_ops(void);
/* lib/fusion_tree: O(log n / log K) node searches by word-parallel
 * sketches, B-tree updates; split and join rebuild in O(n) and copy, so
 * any contexts are compatible */
const ds_ordered_ops_t *ds_ordered_fusion_ops(void);

/* ops NULL selects ds_ordered_treap_ops() */
ds_status_t ds_ordered_init(ds_ordered_t *s, const ds_ordered_ops_t *ops,
                            size_t value_size, ds_context_t *context);
void ds_ordered_destroy(ds_ordered_t *s);
ds_status_t ds_ordered_insert(ds_ordered_t *s, unsigned long key,
                              const void *value);
ds_status_t ds_ordered_remove(ds_ordered_t *s, unsigned long key,
                              void *out_value);
void *ds_ordered_find(const ds_ordered_t *s, unsigned long key);
ds_status_t ds_ordered_floor(const ds_ordered_t *s, unsigned long x,
                             unsigned long *out);
ds_status_t ds_ordered_ceiling(const ds_ordered_t *s, unsigned long x,
                               unsigned long *out);
size_t ds_ordered_size(const ds_ordered_t *s);
ds_status_t ds_ordered_select(const ds_ordered_t *s, size_t k,
                              unsigned long *out);
bool ds_ordered_compatible(const ds_ordered_t *a, const ds_ordered_t *b);
/* upper must be initialised with the same ops, compatible and empty */
ds_status_t ds_ordered_split(ds_ordered_t *s, unsigned long key,
                             ds_ordered_t *upper);
ds_status_t ds_ordered_join(ds_ordered_t *s, ds_ordered_t *upper);
bool ds_ordered_next(const ds_ordered_t *s, size_t *cursor, unsigned long *key,
                     void **value);
/* every entry in key order, O(n) for both backends (the treap walks with
 * an explicit stack; if the stack cannot grow it continues by rank, so the
 * walk never fails for lack of memory); returns the visitor's first
 * non-DS_OK status, else DS_OK */
ds_status_t ds_ordered_for_each(const ds_ordered_t *s,
                                ds_ordered_visit_fn visit, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_ORDERED_IFACE_H */
