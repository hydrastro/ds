#ifndef DS_ROLLBACK_UNION_FIND_H
#define DS_ROLLBACK_UNION_FIND_H

/*
 * Union-find with rollback: a disjoint-set forest over the integers
 * [0, count) whose unions can be undone in LIFO order.
 *
 * snapshot() returns a marker; rollback(marker) undoes every union made
 * since. That is what offline dynamic connectivity (segment tree over time),
 * backtracking searches and "try a merge, maybe revert it" heuristics need.
 *
 * Rollback rules out path compression, so find() uses union by size alone:
 * every tree has height O(log n). Each successful union records one entry,
 * and at most count - 1 unions can succeed, so the history never needs to
 * grow: all memory is sized at init.
 *
 * Storage: dynamic (ds_rollback_uf_init, through a ds_context_t) or three
 * caller arrays of `count` size_t each (ds_rollback_uf_init_buffer, no
 * allocation).
 *
 * Complexity: find, union, same O(log n); undo one union O(1); rollback
 * O(unions undone); reset O(n).
 * Concurrency: a structure may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_rollback_uf {
  size_t *parent;
  size_t *size;
  size_t *history;
  size_t count;
  size_t history_len;
  size_t components;
  ds_context_t *context;
  bool fixed;
} ds_rollback_uf_t;

ds_status_t FUNC(ds_rollback_uf_init)(ds_rollback_uf_t *uf, size_t count,
                                      ds_context_t *context);
ds_status_t FUNC(ds_rollback_uf_init_buffer)(ds_rollback_uf_t *uf,
                                             size_t *parent, size_t *size,
                                             size_t *history, size_t count);
void FUNC(ds_rollback_uf_destroy)(ds_rollback_uf_t *uf);

ds_status_t FUNC(ds_rollback_uf_find)(const ds_rollback_uf_t *uf, size_t x,
                                      size_t *out_root);
ds_status_t FUNC(ds_rollback_uf_union)(ds_rollback_uf_t *uf, size_t a, size_t b,
                                       bool *out_merged);
bool FUNC(ds_rollback_uf_same)(const ds_rollback_uf_t *uf, size_t a, size_t b);
ds_status_t FUNC(ds_rollback_uf_component_size)(const ds_rollback_uf_t *uf,
                                                size_t x, size_t *out_size);
size_t FUNC(ds_rollback_uf_components)(const ds_rollback_uf_t *uf);
size_t FUNC(ds_rollback_uf_count)(const ds_rollback_uf_t *uf);

size_t FUNC(ds_rollback_uf_snapshot)(const ds_rollback_uf_t *uf);
ds_status_t FUNC(ds_rollback_uf_rollback)(ds_rollback_uf_t *uf,
                                          size_t snapshot);
ds_status_t FUNC(ds_rollback_uf_undo)(ds_rollback_uf_t *uf);
void FUNC(ds_rollback_uf_reset)(ds_rollback_uf_t *uf);

#ifdef __cplusplus
}
#endif

#endif /* DS_ROLLBACK_UNION_FIND_H */
