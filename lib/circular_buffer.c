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
  return buffer;
}

bool FUNC(ds_circular_buffer_is_empty)(ds_circular_buffer_t *buffer) {
  bool result;
  result = buffer->size == 0;
  return result;
}

bool FUNC(ds_circular_buffer_is_full)(ds_circular_buffer_t *buffer) {
  bool result;
  result = buffer->size == buffer->capacity;
  return result;
}

bool FUNC(ds_circular_buffer_push)(ds_circular_buffer_t *buffer,
                                   ds_circular_buffer_node_t *node) {
  if (node == buffer->nil) {
    return false;
  }
  if (buffer->size == buffer->capacity) {
    if (!buffer->overwrite) {
      return false;
    }
    buffer->head = (buffer->head + 1) % buffer->capacity;
    buffer->size -= 1;
  }
  buffer->data[buffer->tail] = node;
  buffer->tail = (buffer->tail + 1) % buffer->capacity;
  buffer->size += 1;
  return true;
}

ds_circular_buffer_node_t *
FUNC(ds_circular_buffer_pop)(ds_circular_buffer_t *buffer) {
  ds_circular_buffer_node_t *node;
  if (buffer->size == 0) {
    return buffer->nil;
  }
  node = buffer->data[buffer->head];
  buffer->head = (buffer->head + 1) % buffer->capacity;
  buffer->size -= 1;
  return node;
}

ds_circular_buffer_node_t *
FUNC(ds_circular_buffer_peek)(ds_circular_buffer_t *buffer) {
  ds_circular_buffer_node_t *node;
  if (buffer->size == 0) {
    return buffer->nil;
  }
  node = buffer->data[buffer->head];
  return node;
}

ds_circular_buffer_node_t *
FUNC(ds_circular_buffer_peek_tail)(ds_circular_buffer_t *buffer) {
  ds_circular_buffer_node_t *node;
  size_t index;
  if (buffer->size == 0) {
    return buffer->nil;
  }
  index = (buffer->tail + buffer->capacity - 1) % buffer->capacity;
  node = buffer->data[index];
  return node;
}

ds_circular_buffer_node_t *
FUNC(ds_circular_buffer_at)(ds_circular_buffer_t *buffer, size_t index) {
  ds_circular_buffer_node_t *node;
  if (index >= buffer->size) {
    return buffer->nil;
  }
  node = buffer->data[(buffer->head + index) % buffer->capacity];
  return node;
}

void FUNC(ds_circular_buffer_walk_forward)(
    ds_circular_buffer_t *buffer,
    void (*callback)(ds_circular_buffer_node_t *)) {
  size_t i;
  for (i = 0; i < buffer->size; i++) {
    callback(buffer->data[(buffer->head + i) % buffer->capacity]);
  }
}

void FUNC(ds_circular_buffer_clear)(ds_circular_buffer_t *buffer) {
  buffer->head = 0;
  buffer->tail = 0;
  buffer->size = 0;
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
  buffer->deallocator(buffer);
}

ds_circular_buffer_t *FUNC(ds_circular_buffer_clone)(
    ds_circular_buffer_t *buffer,
    ds_circular_buffer_node_t *(*clone_node)(ds_circular_buffer_node_t *)) {
  ds_circular_buffer_t *new_buffer;
  size_t i;
  new_buffer = FUNC(ds_circular_buffer_create_alloc)(
      buffer->capacity, buffer->overwrite, buffer->allocator,
      buffer->deallocator);
  for (i = 0; i < buffer->size; i++) {
    ds_circular_buffer_node_t *cloned =
        clone_node(buffer->data[(buffer->head + i) % buffer->capacity]);
    FUNC(ds_circular_buffer_push)(new_buffer, cloned);
  }
  return new_buffer;
}
