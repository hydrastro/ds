#include "monotonic_queue.h"

ds_status_t ds_monotonic_queue_init(ds_monotonic_queue_t *queue,
                                    size_t elem_size,
                                    ds_monotonic_queue_compare_func_t compare,
                                    void *user, ds_context_t *context) {
  ds_status_t status;
  if (queue == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  status = ds_array_deque_init(&queue->seqs, sizeof(unsigned long), context);
  if (status == DS_OK) {
    status = ds_array_deque_init(&queue->values, elem_size, context);
  }
  queue->next_seq = 0;
  queue->compare = compare;
  queue->user = user;
  return status;
}

ds_status_t
ds_monotonic_queue_init_buffer(ds_monotonic_queue_t *queue, size_t elem_size,
                               ds_monotonic_queue_compare_func_t compare,
                               void *user, unsigned long *seq_buffer,
                               void *value_buffer, size_t capacity) {
  ds_status_t status;
  if (queue == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  status = ds_array_deque_init_buffer(&queue->seqs, sizeof(unsigned long),
                                      seq_buffer, capacity);
  if (status == DS_OK) {
    status = ds_array_deque_init_buffer(&queue->values, elem_size, value_buffer,
                                        capacity);
  }
  queue->next_seq = 0;
  queue->compare = compare;
  queue->user = user;
  return status;
}

void ds_monotonic_queue_destroy(ds_monotonic_queue_t *queue) {
  if (queue == NULL) {
    return;
  }
  ds_array_deque_destroy(&queue->seqs);
  ds_array_deque_destroy(&queue->values);
}

ds_status_t ds_monotonic_queue_push(ds_monotonic_queue_t *queue,
                                    const void *value, unsigned long *out_seq) {
  ds_status_t status;
  unsigned long seq = queue->next_seq;
  if (value == NULL) {
    return DS_ERR_NULL;
  }
  /* Values behind that are larger can never be a window minimum again. */
  while (!ds_array_deque_is_empty(&queue->values) &&
         queue->compare(ds_array_deque_back(&queue->values), value,
                        queue->user) >= 0) {
    ds_array_deque_pop_back(&queue->values, NULL);
    ds_array_deque_pop_back(&queue->seqs, NULL);
  }
  status = ds_array_deque_push_back(&queue->values, value);
  if (status != DS_OK) {
    return status;
  }
  status = ds_array_deque_push_back(&queue->seqs, &seq);
  if (status != DS_OK) {
    ds_array_deque_pop_back(&queue->values, NULL);
    return status;
  }
  queue->next_seq++;
  if (out_seq != NULL) {
    *out_seq = seq;
  }
  return DS_OK;
}

void ds_monotonic_queue_expire_before(ds_monotonic_queue_t *queue,
                                      unsigned long seq) {
  while (!ds_array_deque_is_empty(&queue->seqs) &&
         *(const unsigned long *)ds_array_deque_front(&queue->seqs) < seq) {
    ds_array_deque_pop_front(&queue->seqs, NULL);
    ds_array_deque_pop_front(&queue->values, NULL);
  }
}

const void *ds_monotonic_queue_front(ds_monotonic_queue_t *queue,
                                     unsigned long *out_seq) {
  if (ds_array_deque_is_empty(&queue->values)) {
    return NULL;
  }
  if (out_seq != NULL) {
    *out_seq = *(const unsigned long *)ds_array_deque_front(&queue->seqs);
  }
  return ds_array_deque_front(&queue->values);
}

bool ds_monotonic_queue_is_empty(const ds_monotonic_queue_t *queue) {
  return ds_array_deque_is_empty(&queue->values);
}

unsigned long ds_monotonic_queue_next_seq(const ds_monotonic_queue_t *queue) {
  return queue->next_seq;
}

void ds_monotonic_queue_clear(ds_monotonic_queue_t *queue) {
  ds_array_deque_clear(&queue->seqs);
  ds_array_deque_clear(&queue->values);
}
