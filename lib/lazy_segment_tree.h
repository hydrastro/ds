#ifndef DS_LAZY_SEGMENT_TREE_H
#define DS_LAZY_SEGMENT_TREE_H

/*
 * Generic lazy segment trees over a monoid with an action, values by
 * value (lib/segment_tree is the long-valued classic).
 *
 * Summaries form a monoid: combine (associative, not necessarily
 * commutative) with an identity element given at init. Tags act on
 * summaries: tag_apply_summary(summary, tag, count) where count is the
 * number of real elements below (padding never sees a tag), and
 * tag_compose(tag, newer) composes a newer tag after an older one. The
 * laws the caller guarantees: apply(combine(a, b), f, ca + cb) ==
 * combine(apply(a, f, ca), apply(b, f, cb)) and
 * apply(apply(s, f), g) == apply(s, compose(f, g)).
 *
 * Point get/set, range query and range apply in O(log n), and binary
 * searches max_right / min_left over a monotone predicate on prefix /
 * suffix aggregates, all iterative (array layout of size 2^k).
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_lazy_segtree_config {
  size_t summary_size;
  const void *identity; /* summary_size bytes */
  void (*combine)(void *out, const void *left, const void *right, void *user);
  size_t tag_size; /* 0: no range apply */
  void (*tag_apply_summary)(void *summary, const void *tag, size_t count,
                            void *user);
  void (*tag_compose)(void *tag, const void *newer, void *user);
  void *user;
  ds_context_t *context;
} ds_lazy_segtree_config_t;

typedef struct ds_lazy_segtree {
  ds_lazy_segtree_config_t config;
  size_t n;
  size_t size; /* power of two >= n */
  unsigned log;
  unsigned char *d;   /* 2 size summaries */
  unsigned char *lz;  /* size tags */
  unsigned char *has; /* size flags */
  size_t *cnt;        /* real leaves below each node */
  unsigned char *tmp; /* three summaries of scratch */
} ds_lazy_segtree_t;

typedef bool (*ds_lazy_segtree_pred_fn)(const void *summary, void *user);

void ds_lazy_segtree_config_init(ds_lazy_segtree_config_t *config);
/* values (optional): n initial summaries; identity otherwise */
ds_status_t ds_lazy_segtree_init(ds_lazy_segtree_t *t, size_t n,
                                 const void *values,
                                 const ds_lazy_segtree_config_t *config);
void ds_lazy_segtree_destroy(ds_lazy_segtree_t *t);
ds_status_t ds_lazy_segtree_set(ds_lazy_segtree_t *t, size_t i,
                                const void *value);
ds_status_t ds_lazy_segtree_get(ds_lazy_segtree_t *t, size_t i, void *out);
/* aggregate of [first, last) (identity when empty) */
ds_status_t ds_lazy_segtree_query(ds_lazy_segtree_t *t, size_t first,
                                  size_t last, void *out);
const void *ds_lazy_segtree_all(const ds_lazy_segtree_t *t);
ds_status_t ds_lazy_segtree_apply(ds_lazy_segtree_t *t, size_t first,
                                  size_t last, const void *tag);
/* largest r with pred(aggregate of [first, r)) true; pred(identity) must
 * hold and pred must be monotone */
size_t ds_lazy_segtree_max_right(ds_lazy_segtree_t *t, size_t first,
                                 ds_lazy_segtree_pred_fn pred, void *user);
/* smallest l with pred(aggregate of [l, last)) true */
size_t ds_lazy_segtree_min_left(ds_lazy_segtree_t *t, size_t last,
                                ds_lazy_segtree_pred_fn pred, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_LAZY_SEGMENT_TREE_H */
