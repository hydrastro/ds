#ifndef DS_SPARSE_SET_H
#define DS_SPARSE_SET_H

/*
 * Sparse set (Briggs-Torczon): a set of integers in [0, universe) with O(1)
 * insert, remove, contains and clear, and iteration in O(size) over a dense
 * array of the members.
 *
 * Two arrays of `universe` size_t each: `dense` holds the members in
 * insertion order (until a removal swaps the last member into the hole), and
 * `sparse[x]` holds x's position in `dense`. Clearing only resets the count,
 * which is what makes the structure useful for per-iteration scratch sets
 * (compilers, graph searches, ECS component masks).
 *
 * Two storage modes: dynamic (ds_sparse_set_init, through a ds_context_t;
 * the universe can be enlarged later) or caller-supplied arrays
 * (ds_sparse_set_init_buffer, no allocation).
 *
 * Concurrency: a set may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_sparse_set {
  size_t *dense;
  size_t *sparse;
  size_t size;
  size_t universe;
  ds_context_t *context;
  bool fixed;
} ds_sparse_set_t;

ds_status_t ds_sparse_set_init(ds_sparse_set_t *set, size_t universe,
                               ds_context_t *context);
ds_status_t ds_sparse_set_init_buffer(ds_sparse_set_t *set, size_t *dense,
                                      size_t *sparse, size_t universe);
void ds_sparse_set_destroy(ds_sparse_set_t *set);

ds_status_t ds_sparse_set_insert(ds_sparse_set_t *set, size_t value);
ds_status_t ds_sparse_set_remove(ds_sparse_set_t *set, size_t value);
bool ds_sparse_set_contains(const ds_sparse_set_t *set, size_t value);
ds_status_t ds_sparse_set_index_of(const ds_sparse_set_t *set, size_t value,
                                   size_t *out_index);
void ds_sparse_set_clear(ds_sparse_set_t *set);

size_t ds_sparse_set_size(const ds_sparse_set_t *set);
size_t ds_sparse_set_universe(const ds_sparse_set_t *set);
const size_t *ds_sparse_set_data(const ds_sparse_set_t *set);
ds_status_t ds_sparse_set_at(const ds_sparse_set_t *set, size_t index,
                             size_t *out_value);
ds_status_t ds_sparse_set_pop(ds_sparse_set_t *set, size_t *out_value);
ds_status_t ds_sparse_set_set_universe(ds_sparse_set_t *set, size_t universe);

#ifdef __cplusplus
}
#endif

#endif /* DS_SPARSE_SET_H */
