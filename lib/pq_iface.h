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
 *   DS_PQ_WORST_LOG      push, pop, update and remove each in O(log n)
 *                        worst case; for array backends push only once
 *                        capacity is reserved (ds_pq_reserve), since a
 *                        growing push relocates the O(n) array
 *   DS_PQ_AMORTIZED      the main bounds are amortized
 *   DS_PQ_RANDOMIZED     the bounds hold in expectation
 *   DS_PQ_IMPLICIT       array based: an index per item, no child links
 *   DS_PQ_FAST_DECREASE  decreasing a key costs o(log n) amortized
 *   DS_PQ_MELD_ANY_CONTEXT  native meld also between queues with different
 *                        contexts (nodes remember the context that
 *                        allocated them)
 *   DS_PQ_MOVING_ITEMS   items move inside the queue: pointers from top()
 *                        and item() are valid only until the next push,
 *                        pop, update or remove (handles stay valid); other
 *                        backends keep each item at one address while it
 *                        is queued
 *
 * The traits are a summary; ops->costs states each operation's bound,
 * worst case and amortized (expected for DS_PQ_RANDOMIZED backends), as a
 * DS_PQ_COST_* class. DS_PQ_COST_UNSTATED means no bound is claimed, not
 * that the operation is slow. Allocator calls count as O(1).
 *
 * Backends built on the library's heaps (worst / amortized; "-" = no
 * native meld, the copying meld is used):
 *
 *                          push     pop      decrease update   remove   meld
 *   ds_pq_binary_ops()     n/log    log      log      log      log      -
 *     implicit binary heap; push is log worst case after ds_pq_reserve
 *   ds_pq_quaternary_ops() n/log    log      log      log      log      -
 *     implicit 4-ary heap: shallower, cheaper decreases
 *   ds_pq_packed_ops()     n/log    log      log      log      log      -
 *     packed binary heap: items inline in one array (no allocation per
 *     item), handles are cells in chunks of 256 that never move;
 *     DS_PQ_MOVING_ITEMS. The binary and 4-ary heaps above keep one node
 *     per item for stable item addresses.
 *   ds_pq_binomial_ops()   log/1    log      log^2    log^2    log^2    log
 *     binomial heap (lib/binomial_heap): a structural parent swap repairs
 *     the parent links of two child lists, O(log n) each
 *   ds_pq_leftist_ops()    log      log      log      log      log      log
 *     leftist tree (lib/meldable_heap): rank refresh stops within the
 *     O(log n) rank bound
 *   ds_pq_weight_ops()     log      log      n        n        n        log
 *     weight-biased leftist tree: removal refreshes subtree weights up the
 *     whole ancestor chain, which can be a left chain of linear height
 *   ds_pq_skew_ops()       n/log    n/log    n/?      n/?      n/?      n/log
 *     skew heap: the amortized analysis covers push, pop and meld only
 *   ds_pq_random_ops()     n/log    n/log    n/log    n/log    n/log    n/log
 *     randomized meldable heap (expected bounds)
 *   ds_pq_pairing_ops()    1        n/log    1/o(log) n/log    n/log    1
 *     pairing heap (lib/pairing_heap); native meld across contexts
 *   ds_pq_hollow_ops()     1        n/log    n/1      n/log    n/log    1
 *     hollow heap (lib/hollow_heap): decrease leaves the old node hollow
 *     instead of cutting; the linear worst case of decrease is the rebuild
 *     that keeps hollow nodes from outnumbering items. Items live in the
 *     nodes and an update moves its item: DS_PQ_MOVING_ITEMS
 *   ds_pq_rank_pairing_ops()  1   n/log    n/1      n/log    n/log    1
 *   ds_pq_rank_pairing2_ops()
 *     rank-pairing heap (lib/rank_pairing_heap), type 1 and type 2 rank
 *     rules: half trees linked in one pass at pop, decrease cuts one node
 *     and lowers ranks above it (O(1) amortized, no cascading cuts);
 *     increase and remove go through the minimum's relinking. Nodes never
 *     move, so item pointers stay valid
 *   ds_pq_strict_fibonacci_ops()  1   log      1        log      log      1
 *     strict Fibonacci heap (lib/strict_fibonacci_heap): every bound worst
 *     case (DS_PQ_WORST_LOG with O(1) push, decrease and meld); items live
 *     in cells that never move, so item pointers stay valid
 *
 * Any other heap can be adapted by filling in a ds_pq_ops_t.
 *
 * Melding: the native meld transfers nodes, so it is used only when the
 * two queues have the same order (compare function and user data) and
 * compatible memory (ds_context_compatible: either context may release
 * the other's nodes), or the backend has DS_PQ_MELD_ANY_CONTEXT; the
 * absorbed queue's handles then stay valid and now belong to pq.
 * Otherwise, and for backends without a meld, items are copied one at a
 * time in pq's order (push into pq, then pop from other; pq reserves room
 * first) and the absorbed queue's handles are invalidated. If a push fails
 * midway the copy stops: every item moved so far is in pq, the rest are
 * still in other, both queues are valid, and the error is returned.
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
#define DS_PQ_MELD_ANY_CONTEXT 0x40u
#define DS_PQ_MOVING_ITEMS 0x80u

/* cost classes of ds_pq_cost_t */
#define DS_PQ_COST_UNSTATED 0u /* no bound claimed (or not native) */
#define DS_PQ_COST_CONST 1u    /* O(1) */
#define DS_PQ_COST_SUBLOG 2u   /* o(log n) */
#define DS_PQ_COST_LOG 3u      /* O(log n) */
#define DS_PQ_COST_LOG2 4u     /* O(log^2 n) */
#define DS_PQ_COST_LINEAR 5u   /* O(n) */

typedef struct ds_pq_cost {
  unsigned char worst;     /* one call, worst case */
  unsigned char amortized; /* amortized over any sequence (expected for
                              DS_PQ_RANDOMIZED) */
} ds_pq_cost_t;

/* update is split by direction: decrease moves an item towards the top */
typedef struct ds_pq_costs {
  ds_pq_cost_t push;
  ds_pq_cost_t pop;
  ds_pq_cost_t decrease;
  ds_pq_cost_t increase;
  ds_pq_cost_t remove;
  ds_pq_cost_t meld; /* the native meld; UNSTATED when there is none */
} ds_pq_costs_t;

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
  /* make room for count more pushes without growth; NULL when pushes never
   * relocate storage */
  ds_status_t (*reserve)(void *pq, size_t count);
  ds_pq_costs_t costs;
} ds_pq_ops_t;

typedef struct ds_pq {
  const ds_pq_ops_t *ops;
  void *impl;
  size_t item_size;
  ds_context_t *context;
  ds_pq_compare_fn compare; /* the order, compared before a native meld */
  void *user;
} ds_pq_t;

const ds_pq_ops_t *ds_pq_binary_ops(void);
const ds_pq_ops_t *ds_pq_quaternary_ops(void);
const ds_pq_ops_t *ds_pq_packed_ops(void);
const ds_pq_ops_t *ds_pq_binomial_ops(void);
const ds_pq_ops_t *ds_pq_leftist_ops(void);
const ds_pq_ops_t *ds_pq_weight_ops(void);
const ds_pq_ops_t *ds_pq_skew_ops(void);
const ds_pq_ops_t *ds_pq_random_ops(void);
const ds_pq_ops_t *ds_pq_pairing_ops(void);
const ds_pq_ops_t *ds_pq_hollow_ops(void);
const ds_pq_ops_t *ds_pq_rank_pairing_ops(void);
const ds_pq_ops_t *ds_pq_rank_pairing2_ops(void);
const ds_pq_ops_t *ds_pq_strict_fibonacci_ops(void);

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
/* room for count more pushes without relocation (DS_OK and nothing to do
 * for backends without a reserve op) */
ds_status_t ds_pq_reserve(ds_pq_t *pq, size_t count);
/* other must use the same ops and item size; it is left empty. Different
 * orders or incompatible contexts make it a copy (see Melding). */
ds_status_t ds_pq_meld(ds_pq_t *pq, ds_pq_t *other);

#ifdef __cplusplus
}
#endif

#endif /* DS_PQ_IFACE_H */
