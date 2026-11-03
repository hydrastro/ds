#include "array_deque.h"
#include <string.h>

#define ADQ_SIZE_MAX (~(size_t)0)
#define ADQ_MIN_CAPACITY 4

static size_t adq_phys(const ds_array_deque_t *deque, size_t index) {
  size_t pos = deque->head + index;
  if (pos >= deque->capacity) {
    pos -= deque->capacity;
  }
  return pos;
}

static unsigned char *adq_slot(const ds_array_deque_t *deque, size_t index) {
  return deque->data + adq_phys(deque, index) * deque->elem_size;
}

static void adq_move(ds_array_deque_t *deque, size_t dst, size_t src) {
  memcpy(adq_slot(deque, dst), adq_slot(deque, src), deque->elem_size);
}

static void adq_swap_bytes(unsigned char *a, unsigned char *b, size_t n) {
  unsigned char tmp;
  size_t i;
  for (i = 0; i < n; i++) {
    tmp = a[i];
    a[i] = b[i];
    b[i] = tmp;
  }
}

/* Reverse physical slots [first, last). */
static void adq_reverse_slots(ds_array_deque_t *deque, size_t first,
                              size_t last) {
  size_t es = deque->elem_size;
  while (first + 1 < last) {
    last--;
    adq_swap_bytes(deque->data + first * es, deque->data + last * es, es);
    first++;
  }
}

static ds_status_t adq_set_capacity(ds_array_deque_t *deque, size_t capacity) {
  unsigned char *data;
  size_t first;
  size_t es = deque->elem_size;
  if (deque->fixed) {
    return capacity <= deque->capacity ? DS_OK : DS_ERR_OVERFLOW;
  }
  if (capacity > ADQ_SIZE_MAX / es) {
    return DS_ERR_OVERFLOW;
  }
  if (capacity == 0) {
    ds_context_free(deque->context, deque->data);
    deque->data = NULL;
    deque->capacity = 0;
    deque->head = 0;
    return DS_OK;
  }
  data = (unsigned char *)ds_context_alloc(deque->context, capacity * es);
  if (data == NULL) {
    return DS_ERR_ALLOC;
  }
  if (deque->size > 0) {
    first = deque->capacity - deque->head;
    if (first > deque->size) {
      first = deque->size;
    }
    memcpy(data, deque->data + deque->head * es, first * es);
    if (deque->size > first) {
      memcpy(data + first * es, deque->data, (deque->size - first) * es);
    }
  }
  ds_context_free(deque->context, deque->data);
  deque->data = data;
  deque->capacity = capacity;
  deque->head = 0;
  return DS_OK;
}

static ds_status_t adq_grow_for_one(ds_array_deque_t *deque) {
  size_t capacity;
  if (deque->size < deque->capacity) {
    return DS_OK;
  }
  if (deque->fixed) {
    return DS_ERR_OVERFLOW;
  }
  if (deque->capacity < ADQ_MIN_CAPACITY) {
    capacity = ADQ_MIN_CAPACITY;
  } else if (deque->capacity > ADQ_SIZE_MAX / 2) {
    return DS_ERR_OVERFLOW;
  } else {
    capacity = deque->capacity * 2;
  }
  return adq_set_capacity(deque, capacity);
}

ds_status_t ds_array_deque_init(ds_array_deque_t *deque, size_t elem_size,
                                ds_context_t *context) {
  if (deque == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  deque->data = NULL;
  deque->head = 0;
  deque->size = 0;
  deque->capacity = 0;
  deque->elem_size = elem_size;
  deque->context = context != NULL ? context : ds_default_context();
  deque->fixed = false;
  return DS_OK;
}

ds_status_t ds_array_deque_init_buffer(ds_array_deque_t *deque,
                                       size_t elem_size, void *buffer,
                                       size_t capacity) {
  if (deque == NULL || (buffer == NULL && capacity > 0)) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  deque->data = (unsigned char *)buffer;
  deque->head = 0;
  deque->size = 0;
  deque->capacity = capacity;
  deque->elem_size = elem_size;
  deque->context = NULL;
  deque->fixed = true;
  return DS_OK;
}

void ds_array_deque_destroy(ds_array_deque_t *deque) {
  if (deque == NULL) {
    return;
  }
  if (!deque->fixed) {
    ds_context_free(deque->context, deque->data);
    deque->data = NULL;
    deque->capacity = 0;
  }
  deque->head = 0;
  deque->size = 0;
}

size_t ds_array_deque_size(const ds_array_deque_t *deque) {
  return deque->size;
}

size_t ds_array_deque_capacity(const ds_array_deque_t *deque) {
  return deque->capacity;
}

bool ds_array_deque_is_empty(const ds_array_deque_t *deque) {
  return deque->size == 0;
}

bool ds_array_deque_is_full(const ds_array_deque_t *deque) {
  return deque->size == deque->capacity;
}

void *ds_array_deque_at(ds_array_deque_t *deque, size_t index) {
  if (index >= deque->size) {
    return NULL;
  }
  return adq_slot(deque, index);
}

void *ds_array_deque_front(ds_array_deque_t *deque) {
  return ds_array_deque_at(deque, 0);
}

void *ds_array_deque_back(ds_array_deque_t *deque) {
  if (deque->size == 0) {
    return NULL;
  }
  return adq_slot(deque, deque->size - 1);
}

ds_status_t ds_array_deque_get(const ds_array_deque_t *deque, size_t index,
                               void *out) {
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= deque->size) {
    return DS_ERR_RANGE;
  }
  memcpy(out, adq_slot(deque, index), deque->elem_size);
  return DS_OK;
}

ds_status_t ds_array_deque_set(ds_array_deque_t *deque, size_t index,
                               const void *elem) {
  if (elem == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= deque->size) {
    return DS_ERR_RANGE;
  }
  memmove(adq_slot(deque, index), elem, deque->elem_size);
  return DS_OK;
}

ds_status_t ds_array_deque_push_back(ds_array_deque_t *deque,
                                     const void *elem) {
  ds_status_t status;
  if (elem == NULL) {
    return DS_ERR_NULL;
  }
  status = adq_grow_for_one(deque);
  if (status != DS_OK) {
    return status;
  }
  memcpy(adq_slot(deque, deque->size), elem, deque->elem_size);
  deque->size++;
  return DS_OK;
}

ds_status_t ds_array_deque_push_front(ds_array_deque_t *deque,
                                      const void *elem) {
  ds_status_t status;
  if (elem == NULL) {
    return DS_ERR_NULL;
  }
  status = adq_grow_for_one(deque);
  if (status != DS_OK) {
    return status;
  }
  deque->head = deque->head == 0 ? deque->capacity - 1 : deque->head - 1;
  memcpy(deque->data + deque->head * deque->elem_size, elem, deque->elem_size);
  deque->size++;
  return DS_OK;
}

ds_status_t ds_array_deque_push_back_overwrite(ds_array_deque_t *deque,
                                               const void *elem, void *evicted,
                                               bool *out_evicted) {
  if (out_evicted != NULL) {
    *out_evicted = false;
  }
  if (elem == NULL) {
    return DS_ERR_NULL;
  }
  if (deque->capacity == 0) {
    return DS_ERR_OVERFLOW;
  }
  if (deque->size < deque->capacity) {
    memcpy(adq_slot(deque, deque->size), elem, deque->elem_size);
    deque->size++;
    return DS_OK;
  }
  /* Full: the oldest element (front) is replaced and the ring rotates. */
  if (evicted != NULL) {
    memcpy(evicted, adq_slot(deque, 0), deque->elem_size);
  }
  if (out_evicted != NULL) {
    *out_evicted = true;
  }
  memmove(adq_slot(deque, 0), elem, deque->elem_size);
  deque->head = adq_phys(deque, 1);
  return DS_OK;
}

ds_status_t ds_array_deque_pop_back(ds_array_deque_t *deque, void *out) {
  if (deque->size == 0) {
    return DS_EMPTY;
  }
  deque->size--;
  if (out != NULL) {
    memcpy(out, adq_slot(deque, deque->size), deque->elem_size);
  }
  return DS_OK;
}

ds_status_t ds_array_deque_pop_front(ds_array_deque_t *deque, void *out) {
  if (deque->size == 0) {
    return DS_EMPTY;
  }
  if (out != NULL) {
    memcpy(out, adq_slot(deque, 0), deque->elem_size);
  }
  deque->head = adq_phys(deque, 1);
  deque->size--;
  return DS_OK;
}

ds_status_t ds_array_deque_insert(ds_array_deque_t *deque, size_t index,
                                  const void *elem) {
  ds_status_t status;
  size_t j;
  if (elem == NULL) {
    return DS_ERR_NULL;
  }
  if (index > deque->size) {
    return DS_ERR_RANGE;
  }
  status = adq_grow_for_one(deque);
  if (status != DS_OK) {
    return status;
  }
  if (index < deque->size / 2) {
    /* Open a slot at the front, then shift the first `index` left. */
    deque->head = deque->head == 0 ? deque->capacity - 1 : deque->head - 1;
    for (j = 0; j < index; j++) {
      adq_move(deque, j, j + 1);
    }
  } else {
    for (j = deque->size; j > index; j--) {
      adq_move(deque, j, j - 1);
    }
  }
  memcpy(adq_slot(deque, index), elem, deque->elem_size);
  deque->size++;
  return DS_OK;
}

ds_status_t ds_array_deque_erase(ds_array_deque_t *deque, size_t index,
                                 void *out) {
  size_t j;
  if (index >= deque->size) {
    return DS_ERR_RANGE;
  }
  if (out != NULL) {
    memcpy(out, adq_slot(deque, index), deque->elem_size);
  }
  if (index < deque->size / 2) {
    for (j = index; j > 0; j--) {
      adq_move(deque, j, j - 1);
    }
    deque->head = adq_phys(deque, 1);
  } else {
    for (j = index; j + 1 < deque->size; j++) {
      adq_move(deque, j, j + 1);
    }
  }
  deque->size--;
  return DS_OK;
}

ds_status_t ds_array_deque_reserve(ds_array_deque_t *deque, size_t capacity) {
  if (capacity <= deque->capacity) {
    return DS_OK;
  }
  return adq_set_capacity(deque, capacity);
}

ds_status_t ds_array_deque_shrink_to_fit(ds_array_deque_t *deque) {
  if (deque->fixed || deque->size == deque->capacity) {
    return DS_OK;
  }
  return adq_set_capacity(deque, deque->size);
}

void ds_array_deque_clear(ds_array_deque_t *deque) {
  deque->head = 0;
  deque->size = 0;
}

void *ds_array_deque_linearize(ds_array_deque_t *deque) {
  size_t es = deque->elem_size;
  if (deque->size == 0) {
    deque->head = 0;
    return deque->data;
  }
  if (deque->head + deque->size <= deque->capacity) {
    if (deque->head != 0) {
      memmove(deque->data, deque->data + deque->head * es, deque->size * es);
    }
  } else {
    /* Rotate the whole block left by `head`, in place. */
    adq_reverse_slots(deque, 0, deque->head);
    adq_reverse_slots(deque, deque->head, deque->capacity);
    adq_reverse_slots(deque, 0, deque->capacity);
  }
  deque->head = 0;
  return deque->data;
}
