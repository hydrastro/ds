#ifndef DS_MIN_MAX_HEAP_H
#define DS_MIN_MAX_HEAP_H

/*
 * Min-max heap (Atkinson et al., 1986): a double-ended priority queue of
 * fixed-size elements stored by value in one array.
 *
 * Even levels are ordered as a min-heap and odd levels as a max-heap, so
 * both the smallest and the largest element are found in O(1) and either
 * can be removed in O(log n). Typical uses: bounded "keep the best k"
 * buffers, sliding medians, schedulers that need both ends.
 *
 * Storage comes from a ds_vec_t: dynamic through a ds_context_t, or a fixed
 * caller buffer with no allocation (DS_ERR_OVERFLOW when full).
 *
 * Complexity: push, pop_min, pop_max O(log n); peek_min, peek_max O(1).
 * Concurrency: a heap may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include "vec.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ds_min_max_heap_compare_func_t)(const void *a, const void *b,
                                              void *user);

typedef struct ds_min_max_heap {
  ds_vec_t items;
  ds_min_max_heap_compare_func_t compare;
  void *user;
} ds_min_max_heap_t;

ds_status_t FUNC(ds_min_max_heap_init)(ds_min_max_heap_t *heap,
                                       size_t elem_size,
                                       ds_min_max_heap_compare_func_t compare,
                                       void *user, ds_context_t *context);
ds_status_t
    FUNC(ds_min_max_heap_init_buffer)(ds_min_max_heap_t *heap, size_t elem_size,
                                      ds_min_max_heap_compare_func_t compare,
                                      void *user, void *buffer,
                                      size_t capacity);
void FUNC(ds_min_max_heap_destroy)(ds_min_max_heap_t *heap);

ds_status_t FUNC(ds_min_max_heap_push)(ds_min_max_heap_t *heap,
                                       const void *elem);
const void *FUNC(ds_min_max_heap_peek_min)(const ds_min_max_heap_t *heap);
const void *FUNC(ds_min_max_heap_peek_max)(const ds_min_max_heap_t *heap);
ds_status_t FUNC(ds_min_max_heap_pop_min)(ds_min_max_heap_t *heap, void *out);
ds_status_t FUNC(ds_min_max_heap_pop_max)(ds_min_max_heap_t *heap, void *out);
size_t FUNC(ds_min_max_heap_size)(const ds_min_max_heap_t *heap);
bool FUNC(ds_min_max_heap_is_empty)(const ds_min_max_heap_t *heap);
void FUNC(ds_min_max_heap_clear)(ds_min_max_heap_t *heap);

#ifdef __cplusplus
}
#endif

#endif /* DS_MIN_MAX_HEAP_H */
