#ifndef DS_MONOTONIC_QUEUE_H
#define DS_MONOTONIC_QUEUE_H

/*
 * Monotonic queue: the sliding-window minimum (or maximum, by reversing the
 * comparator) in amortized O(1) per element.
 *
 * Each pushed value gets an increasing sequence number. The queue keeps
 * only the values that can still become the minimum of some future window,
 * in non-decreasing order: a new value discards every larger value behind
 * it. expire_before(seq) drops values older than the window start, and
 * front() is then the window's minimum. Ties keep the newest value.
 *
 * Built on two ds_array_deque_t (sequence numbers and values), so it is
 * growable or lives in two caller buffers of `capacity` entries each. For a
 * window of w values, capacity w is enough when expire_before() is called
 * before each push; pushing first briefly needs w + 1.
 *
 * Complexity: push amortized O(1); front, expire O(1) amortized.
 */

#include "array_deque.h"
#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ds_monotonic_queue_compare_func_t)(const void *a, const void *b,
                                                 void *user);

typedef struct ds_monotonic_queue {
  ds_array_deque_t seqs;
  ds_array_deque_t values;
  unsigned long next_seq;
  ds_monotonic_queue_compare_func_t compare;
  void *user;
} ds_monotonic_queue_t;

ds_status_t ds_monotonic_queue_init(ds_monotonic_queue_t *queue,
                                    size_t elem_size,
                                    ds_monotonic_queue_compare_func_t compare,
                                    void *user, ds_context_t *context);
ds_status_t
ds_monotonic_queue_init_buffer(ds_monotonic_queue_t *queue, size_t elem_size,
                               ds_monotonic_queue_compare_func_t compare,
                               void *user, unsigned long *seq_buffer,
                               void *value_buffer, size_t capacity);
void ds_monotonic_queue_destroy(ds_monotonic_queue_t *queue);

ds_status_t ds_monotonic_queue_push(ds_monotonic_queue_t *queue,
                                    const void *value, unsigned long *out_seq);
void ds_monotonic_queue_expire_before(ds_monotonic_queue_t *queue,
                                      unsigned long seq);
const void *ds_monotonic_queue_front(ds_monotonic_queue_t *queue,
                                     unsigned long *out_seq);
bool ds_monotonic_queue_is_empty(const ds_monotonic_queue_t *queue);
unsigned long ds_monotonic_queue_next_seq(const ds_monotonic_queue_t *queue);
void ds_monotonic_queue_clear(ds_monotonic_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* DS_MONOTONIC_QUEUE_H */
