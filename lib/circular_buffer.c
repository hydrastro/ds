#include "circular_buffer.h"
#include <stdlib.h>

ds_circular_buffer_t *ds_circular_buffer_create(size_t capacity) {
  return ds_circular_buffer_create_alloc(capacity, false, malloc, free);
}

ds_circular_buffer_t *
ds_circular_buffer_create_alloc(size_t capacity, bool overwrite,
                                void *(*allocator)(size_t),
                                void (*deallocator)(void *)) {
  ds_circular_buffer_t *buffer;
  size_t slots = capacity > 0 ? capacity : 1;
  if (allocator == NULL || deallocator == NULL ||
      slots > (size_t)-1 / sizeof(ds_circular_buffer_node_t *)) {
    return NULL;
  }
  buffer = (ds_circular_buffer_t *)allocator(sizeof(ds_circular_buffer_t));
  if (buffer == NULL) {
    return NULL;
  }
  buffer->allocator = allocator;
  buffer->deallocator = deallocator;
  buffer->capacity = slots;
  buffer->overwrite = overwrite;
  buffer->head = 0;
  buffer->tail = 0;
  buffer->size = 0;
  buffer->nil =
      (ds_circular_buffer_node_t *)allocator(sizeof(ds_circular_buffer_node_t));
  if (buffer->nil == NULL) {
    deallocator(buffer);
    return NULL;
  }
  buffer->nil->placeholder = buffer->nil;
  buffer->data = (ds_circular_buffer_node_t **)allocator(
      slots * sizeof(ds_circular_buffer_node_t *));
  if (buffer->data == NULL) {
    deallocator(buffer->nil);
    deallocator(buffer);
    return NULL;
  }
  return buffer;
}

bool ds_circular_buffer_is_empty(ds_circular_buffer_t *buffer) {
  bool result;
  result = buffer->size == 0;
  return result;
}

bool ds_circular_buffer_is_full(ds_circular_buffer_t *buffer) {
  bool result;
  result = buffer->size == buffer->capacity;
  return result;
}

bool ds_circular_buffer_push(ds_circular_buffer_t *buffer,
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
ds_circular_buffer_pop(ds_circular_buffer_t *buffer) {
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
ds_circular_buffer_peek(ds_circular_buffer_t *buffer) {
  ds_circular_buffer_node_t *node;
  if (buffer->size == 0) {
    return buffer->nil;
  }
  node = buffer->data[buffer->head];
  return node;
}

ds_circular_buffer_node_t *
ds_circular_buffer_peek_tail(ds_circular_buffer_t *buffer) {
  ds_circular_buffer_node_t *node;
  size_t index;
  if (buffer->size == 0) {
    return buffer->nil;
  }
  index = (buffer->tail + buffer->capacity - 1) % buffer->capacity;
  node = buffer->data[index];
  return node;
}

ds_circular_buffer_node_t *ds_circular_buffer_at(ds_circular_buffer_t *buffer,
                                                 size_t index) {
  ds_circular_buffer_node_t *node;
  if (index >= buffer->size) {
    return buffer->nil;
  }
  node = buffer->data[(buffer->head + index) % buffer->capacity];
  return node;
}

void ds_circular_buffer_walk_forward(
    ds_circular_buffer_t *buffer,
    void (*callback)(ds_circular_buffer_node_t *)) {
  size_t i;
  for (i = 0; i < buffer->size; i++) {
    callback(buffer->data[(buffer->head + i) % buffer->capacity]);
  }
}

void ds_circular_buffer_clear(ds_circular_buffer_t *buffer) {
  buffer->head = 0;
  buffer->tail = 0;
  buffer->size = 0;
}

void ds_circular_buffer_destroy(ds_circular_buffer_t *buffer,
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

/* clones of the source's items, oldest first, pushed into copy (as
 * large); false when clone_node returns NULL (the clones made so far are
 * in copy) */
static bool circular_buffer_clone_nodes(
    const ds_circular_buffer_t *buffer, ds_circular_buffer_t *copy,
    ds_circular_buffer_node_t *(*clone_node)(ds_circular_buffer_node_t *)) {
  size_t i;
  for (i = 0; i < buffer->size; i++) {
    ds_circular_buffer_node_t *node =
        clone_node(buffer->data[(buffer->head + i) % buffer->capacity]);
    if (node == NULL) {
      return false;
    }
    (void)ds_circular_buffer_push(copy, node);
  }
  return true;
}

ds_circular_buffer_t *ds_circular_buffer_clone(
    ds_circular_buffer_t *buffer,
    ds_circular_buffer_node_t *(*clone_node)(ds_circular_buffer_node_t *)) {
  ds_circular_buffer_t *copy;
  if (buffer == NULL || clone_node == NULL) {
    return NULL;
  }
  copy =
      ds_circular_buffer_create_alloc(buffer->capacity, buffer->overwrite,
                                      buffer->allocator, buffer->deallocator);
  if (copy != NULL && !circular_buffer_clone_nodes(buffer, copy, clone_node)) {
    /* the clones made so far are not released */
    ds_circular_buffer_destroy(copy, NULL);
    copy = NULL;
  }
  return copy;
}

ds_status_t ds_circular_buffer_clone_ex(
    const ds_circular_buffer_t *buffer,
    ds_circular_buffer_node_t *(*clone_node)(ds_circular_buffer_node_t *),
    void (*destroy_clone)(ds_circular_buffer_node_t *),
    ds_circular_buffer_t **out) {
  ds_circular_buffer_t *copy;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (buffer == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  copy =
      ds_circular_buffer_create_alloc(buffer->capacity, buffer->overwrite,
                                      buffer->allocator, buffer->deallocator);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!circular_buffer_clone_nodes(buffer, copy, clone_node)) {
    ds_circular_buffer_destroy(copy, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  *out = copy;
  return DS_OK;
}
