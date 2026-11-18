#include "core/owner_alloc.h"
#include <dsa/structures/circular_buffer.h>
#include <stdlib.h>

ds_circular_buffer_t *ds_circular_buffer_create(size_t capacity) {
  return ds_circular_buffer_create_alloc(capacity, false, malloc, free);
}

static ds_circular_buffer_t *circular_buffer_create_internal(
    size_t capacity, bool overwrite, void *(*allocator)(size_t),
    void (*deallocator)(void *), ds_context_t *context) {
  ds_circular_buffer_t *buffer;
  size_t slots = capacity > 0 ? capacity : 1;
  if (allocator == NULL || deallocator == NULL ||
      slots > (size_t)-1 / sizeof(ds_circular_buffer_node_t *)) {
    return NULL;
  }
  buffer = (ds_circular_buffer_t *)DS_OWNER_ALLOCATE(
      context, allocator, sizeof(ds_circular_buffer_t));
  if (buffer == NULL) {
    return NULL;
  }
  buffer->context = context;
  buffer->allocator = allocator;
  buffer->deallocator = deallocator;
  buffer->capacity = slots;
  buffer->overwrite = overwrite;
  buffer->head = 0;
  buffer->tail = 0;
  buffer->size = 0;
  buffer->nil = (ds_circular_buffer_node_t *)DS_OWNER_ALLOCATE(
      context, allocator, sizeof(ds_circular_buffer_node_t));
  if (buffer->nil == NULL) {
    DS_OWNER_RELEASE(context, deallocator, buffer);
    return NULL;
  }
  buffer->nil->placeholder = buffer->nil;
  buffer->data = (ds_circular_buffer_node_t **)DS_OWNER_ALLOCATE(
      context, allocator, slots * sizeof(ds_circular_buffer_node_t *));
  if (buffer->data == NULL) {
    DS_OWNER_RELEASE(context, deallocator, buffer->nil);
    DS_OWNER_RELEASE(context, deallocator, buffer);
    return NULL;
  }
  return buffer;
}
ds_circular_buffer_t *
ds_circular_buffer_create_alloc(size_t capacity, bool overwrite,
                                void *(*allocator)(size_t),
                                void (*deallocator)(void *)) {
  return circular_buffer_create_internal(capacity, overwrite, allocator,
                                         deallocator, NULL);
}
ds_status_t ds_circular_buffer_create_context(size_t capacity, bool overwrite,
                                              ds_context_t *context,
                                              ds_circular_buffer_t **out) {

  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (capacity > (size_t)-1 / sizeof(ds_circular_buffer_node_t *))
    return DS_ERR_OVERFLOW;
  *out = circular_buffer_create_internal(
      capacity, overwrite, malloc, free,
      context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
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

ds_status_t
ds_circular_buffer_push_ex(ds_circular_buffer_t *buffer,
                           ds_circular_buffer_node_t *node,
                           ds_circular_buffer_node_t **out_evicted) {
  ds_circular_buffer_node_t *evicted = NULL;
  if (buffer == NULL || node == NULL)
    return DS_ERR_NULL;
  if (node == buffer->nil)
    return DS_ERR_INVALID;
  if (buffer->size == buffer->capacity) {
    if (!buffer->overwrite)
      return DS_ERR_OVERFLOW;
    evicted = buffer->data[buffer->head];
  }
  (void)ds_circular_buffer_push(buffer, node);
  if (out_evicted != NULL)
    *out_evicted = evicted;
  return DS_OK;
}
ds_status_t ds_circular_buffer_pop_ex(ds_circular_buffer_t *buffer,
                                      ds_circular_buffer_node_t **out) {
  if (buffer == NULL || out == NULL)
    return DS_ERR_NULL;
  if (buffer->size == 0)
    return DS_EMPTY;
  *out = ds_circular_buffer_pop(buffer);
  return DS_OK;
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

static void
circular_buffer_destroy_internal(ds_circular_buffer_t *buffer,
                                 ds_circular_buffer_destroy_fn destroy,
                                 void *user) {
  size_t i;
  if (destroy != NULL) {
    for (i = 0; i < buffer->size; i++) {
      destroy(buffer->data[(buffer->head + i) % buffer->capacity], user);
    }
  }
  DS_OWNER_FREE(buffer, buffer->data);
  DS_OWNER_FREE(buffer, buffer->nil);
  DS_OWNER_FREE(buffer, buffer);
}
void ds_circular_buffer_destroy_with(ds_circular_buffer_t *container,
                                     ds_circular_buffer_destroy_fn destroy,
                                     void *user) {
  if (container != NULL)
    circular_buffer_destroy_internal(container, destroy, user);
}
static void circular_buffer_destroy_direct(ds_circular_buffer_node_t *node,
                                           void *user) {
  void (**destroy)(ds_circular_buffer_node_t *) =
      (void (**)(ds_circular_buffer_node_t *))user;
  (*destroy)(node);
}
void ds_circular_buffer_destroy(ds_circular_buffer_t *container,
                                void (*destroy)(ds_circular_buffer_node_t *)) {
  ds_circular_buffer_destroy_with(
      container, destroy != NULL ? circular_buffer_destroy_direct : NULL,
      &destroy);
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
  copy = circular_buffer_create_internal(buffer->capacity, buffer->overwrite,
                                         buffer->allocator, buffer->deallocator,
                                         buffer->context);
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
  copy = circular_buffer_create_internal(buffer->capacity, buffer->overwrite,
                                         buffer->allocator, buffer->deallocator,
                                         buffer->context);
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

ds_status_t ds_circular_buffer_walk_with(ds_circular_buffer_t *container,
                                         ds_circular_buffer_visit_fn visit,
                                         void *user) {
  size_t i, position;
  ds_circular_buffer_node_t *node;
  ds_status_t st;
  if (container == NULL || visit == NULL)
    return DS_ERR_NULL;
  position = container->head;
  for (i = 0; i < container->size; ++i) {
    node = container->data[position];
    st = visit(node, user);
    if (st != DS_OK)
      return st;
    position = position + 1 == container->capacity ? 0 : position + 1;
  }
  return DS_OK;
}
ds_status_t ds_circular_buffer_find_with(ds_circular_buffer_t *container,
                                         ds_circular_buffer_node_t *key,
                                         ds_circular_buffer_compare_fn compare,
                                         void *user,
                                         ds_circular_buffer_node_t **out) {
  size_t i, position;
  ds_circular_buffer_node_t *node;
  if (container == NULL || key == NULL || compare == NULL || out == NULL)
    return DS_ERR_NULL;
  position = container->head;
  for (i = 0; i < container->size; ++i) {
    node = container->data[position];
    if (compare(node, key, user) == 0) {
      *out = node;
      return DS_OK;
    }
    position = position + 1 == container->capacity ? 0 : position + 1;
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_circular_buffer_clone_with(const ds_circular_buffer_t *container,
                                          ds_circular_buffer_clone_fn clone,
                                          ds_circular_buffer_destroy_fn destroy,
                                          void *user, ds_context_t *context,
                                          ds_circular_buffer_t **out) {
  ds_circular_buffer_t *copy;
  size_t i, position;
  ds_circular_buffer_node_t *node;
  ds_circular_buffer_node_t *new_node;

  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (container == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  copy = circular_buffer_create_internal(
      container->capacity, container->overwrite, container->allocator,
      container->deallocator, context != NULL ? context : container->context);
  if (copy == NULL)
    return DS_ERR_ALLOC;

  position = container->head;
  for (i = 0; i < container->size; ++i) {
    node = container->data[position];
    new_node = NULL;
    st = clone(node, &new_node, user);
    if (st != DS_OK || new_node == NULL) {
      ds_circular_buffer_destroy_with(copy, destroy, user);
      return st < 0 ? st : DS_ERR_CALLBACK;
    }
    (void)ds_circular_buffer_push(copy, new_node);
    position = position + 1 == container->capacity ? 0 : position + 1;
  }
  *out = copy;
  return DS_OK;
}

ds_status_t
ds_circular_buffer_walk_reverse_with(ds_circular_buffer_t *container,
                                     ds_circular_buffer_visit_fn visit,
                                     void *user) {
  size_t i, position;
  ds_status_t st;
  if (container == NULL || visit == NULL)
    return DS_ERR_NULL;
  position = container->tail;
  for (i = 0; i < container->size; ++i) {
    position = position == 0 ? container->capacity - 1 : position - 1;
    st = visit(container->data[position], user);
    if (st != DS_OK)
      return st;
  }
  return DS_OK;
}
