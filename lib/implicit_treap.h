#ifndef DS_IMPLICIT_TREAP_H
#define DS_IMPLICIT_TREAP_H

/*
 * Implicit treap: a sequence of fixed-size elements (stored by value) with
 * O(log n) expected insert/erase at any index, split, concatenation, range
 * reversal, range aggregates and lazy range updates. Positions are implicit
 * (subtree sizes), so there are no keys.
 *
 * Everything beyond the sequence itself is pluggable through the config:
 *
 *   Aggregates (summary_size > 0): a monoid over a user summary type.
 *     summarize(out, elem)      summary of a single element
 *     combine(out, left, right) associative; it need not be commutative
 *   query() returns the summary of any range [first, last). Because a range
 *   reversal changes non-commutative aggregates (hashes, compositions,
 *   "first/last" values), each node also keeps the summary of its reversed
 *   subtree, so queries stay exact after reversals.
 *
 *   Lazy updates (tag_size > 0): a user tag type applied to whole ranges.
 *     tag_apply_elem(elem, tag)            update one element
 *     tag_apply_summary(summary, tag, n)   update the summary of n elements
 *     tag_compose(tag, newer)              tag := tag followed by newer
 *   apply() tags a range in O(log n); tags are pushed down lazily.
 *
 * With both, the treap is a lazy segment tree that also supports insertion,
 * deletion, cut and paste. Set summary_size and tag_size to 0 for a plain
 * rope-like sequence. *
 * Laws the callbacks must satisfy (n is a count of elements, e a sequence,
 * S its summary, rev(e) the reversed sequence):
 *   combine       associative; no identity is needed (empty ranges are
 *                 never combined). out never aliases left or right, and
 *                 the summaries passed in are never modified.
 *   reversal      the reversed summary is built with the same combine in
 *                 the mirrored order, so any associative combine works.
 *   tags          uniform per-element actions: applying a tag to a range
 *                 applies the same tag to every element, independent of
 *                 its position. tag_apply_summary(S(e), t, n) must equal
 *                 the summary of e with t applied to each element, for
 *                 forward and reversed summaries alike (both are updated
 *                 with the same call), and tag_compose must make applying
 *                 (t followed by u) equal applying t then u. Actions that
 *                 depend on an element's position (add i to the i-th
 *                 element) need tag splitting and are not supported.
 *   ordering      pending reversals and tags are pushed to children
 *                 before a node is restructured; a tag is applied to a
 *                 subtree after the reversals already pending in it.
 *
 * Element pointers returned by at() are valid until the next modifying call.
 * Nodes come from the ds_context_t. Concurrency: one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_itreap_node ds_itreap_node_t;

typedef struct ds_itreap_config {
  size_t elem_size;
  size_t summary_size;
  void (*summarize)(void *out, const void *elem, void *user);
  void (*combine)(void *out, const void *left, const void *right, void *user);
  size_t tag_size;
  void (*tag_apply_elem)(void *elem, const void *tag, void *user);
  void (*tag_apply_summary)(void *summary, const void *tag, size_t count,
                            void *user);
  void (*tag_compose)(void *tag, const void *newer, void *user);
  void *user;
  ds_context_t *context;
} ds_itreap_config_t;

typedef struct ds_itreap {
  ds_itreap_node_t *root;
  ds_itreap_config_t config;
  size_t elem_offset;
  size_t sum_offset;
  size_t rev_offset;
  size_t tag_offset;
  size_t node_bytes;
  unsigned long seed;
  unsigned char *scratch; /* two summaries for combining */
} ds_itreap_t;

typedef bool (*ds_itreap_visit_func_t)(const void *elem, size_t index,
                                       void *user);

void ds_itreap_config_init(ds_itreap_config_t *config);
ds_status_t ds_itreap_init(ds_itreap_t *treap,
                           const ds_itreap_config_t *config);
void ds_itreap_destroy(ds_itreap_t *treap);

size_t ds_itreap_size(const ds_itreap_t *treap);
ds_status_t ds_itreap_insert(ds_itreap_t *treap, size_t index,
                             const void *elem);
ds_status_t ds_itreap_push_back(ds_itreap_t *treap, const void *elem);
ds_status_t ds_itreap_erase(ds_itreap_t *treap, size_t index, void *out);
void *ds_itreap_at(ds_itreap_t *treap, size_t index);
ds_status_t ds_itreap_get(ds_itreap_t *treap, size_t index, void *out);
ds_status_t ds_itreap_set(ds_itreap_t *treap, size_t index, const void *elem);

ds_status_t ds_itreap_query(ds_itreap_t *treap, size_t first, size_t last,
                            void *out_summary);
ds_status_t ds_itreap_reverse(ds_itreap_t *treap, size_t first, size_t last);
ds_status_t ds_itreap_apply(ds_itreap_t *treap, size_t first, size_t last,
                            const void *tag);

/* After changing an element in place through ds_itreap_at, recompute the
 * summaries on its path (ds_itreap_set does this itself). */
ds_status_t ds_itreap_refresh(ds_itreap_t *treap, size_t index);

/* Composition rules. Two treaps can exchange nodes only when they share
 * the same layout, the same algebra (summarize/combine/tag callbacks and
 * user state) and the same context (nodes are freed through it):
 * ds_itreap_compatible. split initializes out_tail (which must not be
 * initialized, nor be treap itself) with treap's configuration and moves
 * the elements from index on into it. append moves every node of a
 * compatible other treap (not treap itself) to the end of treap, leaving
 * other empty but initialized; DS_ERR_INVALID otherwise. */
bool ds_itreap_compatible(const ds_itreap_t *a, const ds_itreap_t *b);
ds_status_t ds_itreap_split(ds_itreap_t *treap, size_t index,
                            ds_itreap_t *out_tail);
ds_status_t ds_itreap_append(ds_itreap_t *treap, ds_itreap_t *other);
ds_status_t ds_itreap_visit(ds_itreap_t *treap, ds_itreap_visit_func_t visit,
                            void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_IMPLICIT_TREAP_H */
