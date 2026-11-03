#ifndef DS_INDEXED_HEAP_H
#define DS_INDEXED_HEAP_H

/*
 * Indexed d-ary heap (indexed priority queue).
 *
 * Items are integer ids in [0, max_ids), each with a key of `key_size`
 * bytes stored by value. Because the heap knows where every id sits, a key
 * can be changed by id in O(log_d n): this is the decrease-key / increase-key
 * operation that Dijkstra, Prim and A* need, without the lazy "push a
 * duplicate and skip stale entries" workaround.
 *
 * Ordering: the top is the item whose key compares smallest. For a max-heap,
 * reverse the comparator.
 *
 * The arity d (2 = binary heap, 4 is a good default) trades a shallower tree
 * for more comparisons per level; larger d favours decrease-key-heavy loads.
 *
 * Storage: dynamic (ds_indexed_heap_init, through a ds_context_t; the id
 * range can be enlarged) or three caller arrays (ds_indexed_heap_init_buffer,
 * no allocation): heap[max_ids], pos[max_ids], keys[max_ids * key_size].
 *
 * Complexity (n = size, d = arity):
 *   push, update (either direction)   O(log_d n)
 *   pop, remove                       O(d log_d n)
 *   top, contains, key                O(1)
 *   clear                             O(n)
 *
 * Concurrency: a heap may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_INDEXED_HEAP_NONE (~(size_t)0)

typedef int (*ds_indexed_heap_compare_func_t)(const void *a, const void *b,
                                              void *user);

typedef struct ds_indexed_heap_config {
  size_t key_size;
  size_t arity;
  ds_indexed_heap_compare_func_t compare;
  void *user;
  ds_context_t *context;
} ds_indexed_heap_config_t;

typedef struct ds_indexed_heap {
  size_t *heap;
  size_t *pos;
  unsigned char *keys;
  size_t size;
  size_t max_ids;
  size_t key_size;
  size_t arity;
  ds_indexed_heap_compare_func_t compare;
  void *user;
  ds_context_t *context;
  bool fixed;
} ds_indexed_heap_t;

void FUNC(ds_indexed_heap_config_init)(ds_indexed_heap_config_t *config);
ds_status_t FUNC(ds_indexed_heap_init)(ds_indexed_heap_t *heap,
                                       const ds_indexed_heap_config_t *config,
                                       size_t max_ids);
ds_status_t FUNC(ds_indexed_heap_init_buffer)(
    ds_indexed_heap_t *heap, const ds_indexed_heap_config_t *config,
    size_t *heap_buffer, size_t *pos_buffer, void *key_buffer, size_t max_ids);
void FUNC(ds_indexed_heap_destroy)(ds_indexed_heap_t *heap);

ds_status_t FUNC(ds_indexed_heap_push)(ds_indexed_heap_t *heap, size_t id,
                                       const void *key);
ds_status_t FUNC(ds_indexed_heap_update)(ds_indexed_heap_t *heap, size_t id,
                                         const void *key);
ds_status_t FUNC(ds_indexed_heap_push_or_update)(ds_indexed_heap_t *heap,
                                                 size_t id, const void *key);
ds_status_t FUNC(ds_indexed_heap_top)(const ds_indexed_heap_t *heap,
                                      size_t *out_id, void *out_key);
ds_status_t FUNC(ds_indexed_heap_pop)(ds_indexed_heap_t *heap, size_t *out_id,
                                      void *out_key);
ds_status_t FUNC(ds_indexed_heap_remove)(ds_indexed_heap_t *heap, size_t id,
                                         void *out_key);
bool FUNC(ds_indexed_heap_contains)(const ds_indexed_heap_t *heap, size_t id);
const void *FUNC(ds_indexed_heap_key)(const ds_indexed_heap_t *heap, size_t id);
void FUNC(ds_indexed_heap_clear)(ds_indexed_heap_t *heap);
size_t FUNC(ds_indexed_heap_size)(const ds_indexed_heap_t *heap);
bool FUNC(ds_indexed_heap_is_empty)(const ds_indexed_heap_t *heap);
size_t FUNC(ds_indexed_heap_max_ids)(const ds_indexed_heap_t *heap);
ds_status_t FUNC(ds_indexed_heap_set_max_ids)(ds_indexed_heap_t *heap,
                                              size_t max_ids);

#ifdef __cplusplus
}
#endif

#endif /* DS_INDEXED_HEAP_H */
