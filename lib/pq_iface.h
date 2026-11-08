#ifndef DS_PQ_IFACE_H
#define DS_PQ_IFACE_H

/*
 * The addressable priority-queue capability: what an algorithm needs from
 * the heap it runs on (Dijkstra, Prim, A*, event simulation, k-way merging,
 * branch and bound), so the caller picks the heap.
 *
 * A backend is a ds_pq_ops_t. Items are stored by value (item_size bytes)
 * and ordered by a three-way comparator with a user pointer (smallest
 * first; reverse it for a max-heap). push returns a handle that stays
 * valid, and keeps addressing the same item, until that item is popped or
 * removed: update() re-keys it in either direction, remove() deletes it
 * and item() reads it. Traits say what a consumer may rely on:
 *
 *   DS_PQ_MELDABLE       meld in O(log n) or better, keeping the handles
 *                        of the absorbed queue valid
 *   DS_PQ_WORST_LOG      push, pop, update and remove in O(log n) worst case
 *   DS_PQ_AMORTIZED      the bounds are amortized
 *   DS_PQ_RANDOMIZED     the bounds hold in expectation
 *   DS_PQ_IMPLICIT       array based: an index per item, no child links
 *   DS_PQ_FAST_DECREASE  decreasing a key costs o(log n) amortized
 *
 * Backends built on the library's heaps:
 *
 *   ds_pq_binary_ops()    implicit binary heap               worst log
 *   ds_pq_quaternary_ops() implicit 4-ary heap (shallower,   worst log
 *                          cheaper decreases)
 *   ds_pq_binomial_ops()  binomial heap (lib/binomial_heap)  worst log, meld
 *   ds_pq_leftist_ops()   leftist tree (lib/meldable_heap)   worst log, meld
 *   ds_pq_weight_ops()    weight-biased leftist tree         worst log, meld
 *   ds_pq_skew_ops()      skew heap                          amortized, meld
 *   ds_pq_random_ops()    randomized meldable heap           expected, meld
 *   ds_pq_pairing_ops()   pairing heap (lib/pairing_heap)    amortized, meld,
 *                                                            fast decrease
 *
 * Any other heap can be adapted by filling in a ds_pq_ops_t. ds_pq_meld
 * also works without a meld operation by moving items one at a time;
 * then the absorbed queue's handles are invalidated.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ds_pq_compare_fn)(const void *a, const void *b, void *user);
typedef struct ds_pq_handle_s *ds_pq_handle_t;

#define DS_PQ_MELDABLE 0x1u
#define DS_PQ_WORST_LOG 0x2u
#define DS_PQ_AMORTIZED 0x4u
#define DS_PQ_RANDOMIZED 0x8u
#define DS_PQ_IMPLICIT 0x10u
#define DS_PQ_FAST_DECREASE 0x20u

typedef struct ds_pq_ops {
  const char *name;
  unsigned traits;
  size_t instance_size;
  ds_status_t (*init)(void *pq, size_t item_size, ds_pq_compare_fn compare,
                      void *user, ds_context_t *context);
  void (*destroy)(void *pq);
  ds_status_t (*push)(void *pq, const void *item, ds_pq_handle_t *out_handle);
  /* NULL when empty */
  const void *(*top)(const void *pq);
  /* DS_EMPTY when empty; out_item optional */
  ds_status_t (*pop)(void *pq, void *out_item);
  ds_status_t (*update)(void *pq, ds_pq_handle_t handle, const void *item);
  ds_status_t (*remove)(void *pq, ds_pq_handle_t handle, void *out_item);
  const void *(*item)(const void *pq, ds_pq_handle_t handle);
  size_t (*size)(const void *pq);
  /* move every item of other (same ops, item size and order) into pq;
   * NULL when not supported */
  void (*meld)(void *pq, void *other);
} ds_pq_ops_t;

typedef struct ds_pq {
  const ds_pq_ops_t *ops;
  void *impl;
  size_t item_size;
  ds_context_t *context;
} ds_pq_t;

const ds_pq_ops_t *ds_pq_binary_ops(void);
const ds_pq_ops_t *ds_pq_quaternary_ops(void);
const ds_pq_ops_t *ds_pq_binomial_ops(void);
const ds_pq_ops_t *ds_pq_leftist_ops(void);
const ds_pq_ops_t *ds_pq_weight_ops(void);
const ds_pq_ops_t *ds_pq_skew_ops(void);
const ds_pq_ops_t *ds_pq_random_ops(void);
const ds_pq_ops_t *ds_pq_pairing_ops(void);

/* ops NULL selects ds_pq_binary_ops() */
ds_status_t ds_pq_init(ds_pq_t *pq, const ds_pq_ops_t *ops, size_t item_size,
                       ds_pq_compare_fn compare, void *user,
                       ds_context_t *context);
void ds_pq_destroy(ds_pq_t *pq);
ds_status_t ds_pq_push(ds_pq_t *pq, const void *item,
                       ds_pq_handle_t *out_handle);
const void *ds_pq_top(const ds_pq_t *pq);
ds_status_t ds_pq_pop(ds_pq_t *pq, void *out_item);
ds_status_t ds_pq_update(ds_pq_t *pq, ds_pq_handle_t handle, const void *item);
ds_status_t ds_pq_remove(ds_pq_t *pq, ds_pq_handle_t handle, void *out_item);
const void *ds_pq_item(const ds_pq_t *pq, ds_pq_handle_t handle);
size_t ds_pq_size(const ds_pq_t *pq);
bool ds_pq_is_empty(const ds_pq_t *pq);
/* other must use the same ops, item size and order; it is left empty */
ds_status_t ds_pq_meld(ds_pq_t *pq, ds_pq_t *other);

#ifdef __cplusplus
}
#endif

#endif /* DS_PQ_IFACE_H */
