#ifndef DS_ARRAY_DEQUE_H
#define DS_ARRAY_DEQUE_H

/*
 * Array deque: a ring buffer of fixed-size elements stored by value, with
 * O(1) push and pop at both ends.
 *
 * Two storage modes:
 *   - dynamic: ds_array_deque_init() grows through a ds_context_t;
 *   - fixed: ds_array_deque_init_buffer() uses caller-owned memory and never
 *     allocates (DS_ERR_OVERFLOW when full). With
 *     ds_array_deque_push_back_overwrite() a fixed deque becomes a classic
 *     "keep the last N" ring, e.g. for logs or sensor samples.
 *
 * The struct is public so a deque can live on the stack or in static storage.
 * Element pointers stay valid until the next push, pop, insert, erase or
 * capacity change.
 *
 * Complexity:
 *   push/pop front/back       O(1) (amortized when the deque grows)
 *   at / get / set            O(1)
 *   insert / erase at index   O(min(index, size - index))
 *   linearize                 O(capacity), in place, no allocation
 *
 * Concurrency: a deque may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_array_deque {
  unsigned char *data;
  size_t head;
  size_t size;
  size_t capacity;
  size_t elem_size;
  ds_context_t *context;
  bool fixed;
} ds_array_deque_t;

ds_status_t ds_array_deque_init(ds_array_deque_t *deque, size_t elem_size,
                                ds_context_t *context);
ds_status_t ds_array_deque_init_buffer(ds_array_deque_t *deque,
                                       size_t elem_size, void *buffer,
                                       size_t capacity);
void ds_array_deque_destroy(ds_array_deque_t *deque);

size_t ds_array_deque_size(const ds_array_deque_t *deque);
size_t ds_array_deque_capacity(const ds_array_deque_t *deque);
bool ds_array_deque_is_empty(const ds_array_deque_t *deque);
bool ds_array_deque_is_full(const ds_array_deque_t *deque);

void *ds_array_deque_at(ds_array_deque_t *deque, size_t index);
void *ds_array_deque_front(ds_array_deque_t *deque);
void *ds_array_deque_back(ds_array_deque_t *deque);
ds_status_t ds_array_deque_get(const ds_array_deque_t *deque, size_t index,
                               void *out);
ds_status_t ds_array_deque_set(ds_array_deque_t *deque, size_t index,
                               const void *elem);

ds_status_t ds_array_deque_push_back(ds_array_deque_t *deque, const void *elem);
ds_status_t ds_array_deque_push_front(ds_array_deque_t *deque,
                                      const void *elem);
ds_status_t ds_array_deque_push_back_overwrite(ds_array_deque_t *deque,
                                               const void *elem, void *evicted,
                                               bool *out_evicted);
ds_status_t ds_array_deque_pop_back(ds_array_deque_t *deque, void *out);
ds_status_t ds_array_deque_pop_front(ds_array_deque_t *deque, void *out);
ds_status_t ds_array_deque_insert(ds_array_deque_t *deque, size_t index,
                                  const void *elem);
ds_status_t ds_array_deque_erase(ds_array_deque_t *deque, size_t index,
                                 void *out);

ds_status_t ds_array_deque_reserve(ds_array_deque_t *deque, size_t capacity);
ds_status_t ds_array_deque_shrink_to_fit(ds_array_deque_t *deque);
void ds_array_deque_clear(ds_array_deque_t *deque);
void *ds_array_deque_linearize(ds_array_deque_t *deque);

#ifdef __cplusplus
}
#endif

#endif /* DS_ARRAY_DEQUE_H */
