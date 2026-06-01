#include "circular_buffer.h"
#include <stdlib.h>

ds_circular_buffer_t *FUNC(ds_circular_buffer_create)(size_t capacity) {
  return FUNC(ds_circular_buffer_create_alloc)(capacity, false, malloc, free);
}

ds_circular_buffer_t *
FUNC(ds_circular_buffer_create_alloc)(size_t capacity, bool overwrite,
                                      void *(*allocator)(size_t),
                                      void (*deallocator)(void *)) {
  ds_circular_buffer_t *buffer;
  size_t slots = capacity > 0 ? capacity : 1;
  buffer = (ds_circular_buffer_t *)allocator(sizeof(ds_circular_buffer_t));
  buffer->allocator = allocator;
  buffer->deallocator = deallocator;
  buffer->capacity = slots;
  buffer->overwrite = overwrite;
  buffer->head = 0;
  buffer->tail = 0;
  buffer->size = 0;
  buffer->nil =
      (ds_circular_buffer_node_t *)allocator(sizeof(ds_circular_buffer_node_t));
  buffer->nil->placeholder = buffer->nil;
  buffer->data = (ds_circular_buffer_node_t **)allocator(
      slots * sizeof(ds_circular_buffer_node_t *));
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(buffer);
#endif
  return buffer;
}

bool FUNC(ds_circular_buffer_is_empty)(ds_circular_buffer_t *buffer) {
  bool result;
#ifdef DS_THREAD_SAFE
  LOCK(buffer);
#endif
  result = buffer->size == 0;
#ifdef DS_THREAD_SAFE
  UNLOCK(buffer);
#endif
  return result;
}

bool FUNC(ds_circular_buffer_is_full)(ds_circular_buffer_t *buffer) {
  bool result;
#ifdef DS_THREAD_SAFE
  LOCK(buffer);
#endif
  result = buffer->size == buffer->capacity;
#ifdef DS_THREAD_SAFE
  UNLOCK(buffer);
#endif
  return result;
}

bool FUNC(ds_circular_buffer_push)(ds_circular_buffer_t *buffer,
                                   ds_circular_buffer_node_t *node) {
#ifdef DS_THREAD_SAFE
  LOCK(buffer);
#endif
  if (node == buffer->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(buffer);
#endif
    return false;
  }
  if (buffer->size == buffer->capacity) {
    if (!buffer->overwrite) {
#ifdef DS_THREAD_SAFE
      UNLOCK(buffer);
#endif
      return false;
    }
    buffer->head = (buffer->head + 1) % buffer->capacity;
    buffer->size -= 1;
  }
  buffer->data[buffer->tail] = node;
  buffer->tail = (buffer->tail + 1) % buffer->capacity;
  buffer->size += 1;
#ifdef DS_THREAD_SAFE
  UNLOCK(buffer);
#endif
  return true;
}

ds_circular_buffer_node_t *
FUNC(ds_circular_buffer_pop)(ds_circular_buffer_t *buffer) {
  ds_circular_buffer_node_t *node;
#ifdef DS_THREAD_SAFE
  LOCK(buffer);
#endif
  if (buffer->size == 0) {
#ifdef DS_THREAD_SAFE
    UNLOCK(buffer);
#endif
    return buffer->nil;
  }
  node = buffer->data[buffer->head];
  buffer->head = (buffer->head + 1) % buffer->capacity;
  buffer->size -= 1;
#ifdef DS_THREAD_SAFE
  UNLOCK(buffer);
#endif
  return node;
}

ds_circular_buffer_node_t *
FUNC(ds_circular_buffer_peek)(ds_circular_buffer_t *buffer) {
  ds_circular_buffer_node_t *node;
#ifdef DS_THREAD_SAFE
  LOCK(buffer);
#endif
  if (buffer->size == 0) {
#ifdef DS_THREAD_SAFE
    UNLOCK(buffer);
#endif
    return buffer->nil;
  }
  node = buffer->data[buffer->head];
#ifdef DS_THREAD_SAFE
  UNLOCK(buffer);
#endif
  return node;
}

ds_circular_buffer_node_t *
FUNC(ds_circular_buffer_peek_tail)(ds_circular_buffer_t *buffer) {
  ds_circular_buffer_node_t *node;
  size_t index;
#ifdef DS_THREAD_SAFE
  LOCK(buffer);
#endif
  if (buffer->size == 0) {
#ifdef DS_THREAD_SAFE
    UNLOCK(buffer);
#endif
    return buffer->nil;
  }
  index = (buffer->tail + buffer->capacity - 1) % buffer->capacity;
  node = buffer->data[index];
#ifdef DS_THREAD_SAFE
  UNLOCK(buffer);
#endif
  return node;
}

ds_circular_buffer_node_t *
FUNC(ds_circular_buffer_at)(ds_circular_buffer_t *buffer, size_t index) {
  ds_circular_buffer_node_t *node;
#ifdef DS_THREAD_SAFE
  LOCK(buffer);
#endif
  if (index >= buffer->size) {
#ifdef DS_THREAD_SAFE
    UNLOCK(buffer);
#endif
    return buffer->nil;
  }
  node = buffer->data[(buffer->head + index) % buffer->capacity];
#ifdef DS_THREAD_SAFE
  UNLOCK(buffer);
#endif
  return node;
}

void FUNC(ds_circular_buffer_walk_forward)(
    ds_circular_buffer_t *buffer,
    void (*callback)(ds_circular_buffer_node_t *)) {
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(buffer);
#endif
  for (i = 0; i < buffer->size; i++) {
    callback(buffer->data[(buffer->head + i) % buffer->capacity]);
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(buffer);
#endif
}

void FUNC(ds_circular_buffer_clear)(ds_circular_buffer_t *buffer) {
#ifdef DS_THREAD_SAFE
  LOCK(buffer);
#endif
  buffer->head = 0;
  buffer->tail = 0;
  buffer->size = 0;
#ifdef DS_THREAD_SAFE
  UNLOCK(buffer);
#endif
}

void FUNC(ds_circular_buffer_destroy)(
    ds_circular_buffer_t *buffer,
    void (*destroy)(ds_circular_buffer_node_t *)) {
  size_t i;
  if (destroy != NULL) {
    for (i = 0; i < buffer->size; i++) {
      destroy(buffer->data[(buffer->head + i) % buffer->capacity]);
    }
  }
  buffer->deallocator(buffer->data);
  buffer->deallocator(buffer->nil);
#ifdef DS_THREAD_SAFE
  LOCK_DESTROY(buffer);
#endif
  buffer->deallocator(buffer);
}

ds_circular_buffer_t *FUNC(ds_circular_buffer_clone)(
    ds_circular_buffer_t *buffer,
    ds_circular_buffer_node_t *(*clone_node)(ds_circular_buffer_node_t *)) {
  ds_circular_buffer_t *new_buffer;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(buffer);
#endif
  new_buffer = FUNC(ds_circular_buffer_create_alloc)(
      buffer->capacity, buffer->overwrite, buffer->allocator,
      buffer->deallocator);
  for (i = 0; i < buffer->size; i++) {
    ds_circular_buffer_node_t *cloned =
        clone_node(buffer->data[(buffer->head + i) % buffer->capacity]);
    FUNC(ds_circular_buffer_push)(new_buffer, cloned);
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(buffer);
#endif
  return new_buffer;
}
