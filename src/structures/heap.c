#include <dsa/structures/heap.h>
#include <stdio.h>
#include <stdlib.h>

void ds_heap_swap(ds_heap_node_t **a, ds_heap_node_t **b) {
  ds_heap_node_t *temp;
  size_t index;
  index = (size_t)(*a)->index;
  (*a)->index = (size_t)(*b)->index;
  (*b)->index = index;
  temp = *a;
  *a = *b;
  *b = temp;
}

static void ds_heapify_up_with(ds_heap_t *heap, size_t index,
                               ds_heap_compare_fn compare, void *user) {
  size_t parent;
  parent = (index - 1) / 2;
  while (index > 0 &&
         compare(heap->data[index], heap->data[parent], user) < 0) {
    ds_heap_swap(&heap->data[index], &heap->data[parent]);
    index = parent;
    parent = (index - 1) / 2;
  }
}

static void ds_heapify_down_with(ds_heap_t *heap, size_t index,
                                 ds_heap_compare_fn compare, void *user) {
  size_t left, right, smallest;
  while (index < heap->size / 2) {
    left = 2 * index + 1;
    right = 2 * index + 2;
    smallest = index;

    if (left < heap->size &&
        compare(heap->data[left], heap->data[smallest], user) < 0) {
      smallest = left;
    }
    if (right < heap->size &&
        compare(heap->data[right], heap->data[smallest], user) < 0) {
      smallest = right;
    }
    if (smallest != index) {
      ds_heap_swap(&heap->data[index], &heap->data[smallest]);
      index = smallest;
    } else {
      break;
    }
  }
}

static int heap_compare_direct(ds_heap_node_t *a, ds_heap_node_t *b,
                               void *user) {
  int (**compare)(ds_heap_node_t *, ds_heap_node_t *) =
      (int (**)(ds_heap_node_t *, ds_heap_node_t *))user;
  return (*compare)(a, b);
}
void ds_heapify_up(ds_heap_t *heap, size_t index,
                   int (*compare)(ds_heap_node_t *, ds_heap_node_t *)) {
  if (heap != NULL && compare != NULL && index < heap->size)
    ds_heapify_up_with(heap, index, heap_compare_direct, &compare);
}
void ds_heapify_down(ds_heap_t *heap, size_t index,
                     int (*compare)(ds_heap_node_t *, ds_heap_node_t *)) {
  if (heap != NULL && compare != NULL && index < heap->size)
    ds_heapify_down_with(heap, index, heap_compare_direct, &compare);
}

static ds_heap_t *heap_create_internal(size_t capacity,
                                       ds_context_t *context) {
  ds_heap_t *heap;
  if (capacity > (size_t)-1 / sizeof(ds_heap_node_t *)) {
    return NULL;
  }
  heap = (ds_heap_t *)ds_context_alloc(context, sizeof(ds_heap_t));
  if (heap == NULL) {
    return NULL;
  }
  heap->context = context != NULL ? context : ds_default_context();

  heap->data = (capacity == 0U)
                   ? NULL
                   : (ds_heap_node_t **)ds_context_alloc(
                         context, capacity * sizeof(ds_heap_node_t *));
  if (capacity != 0U && heap->data == NULL) {
    ds_context_free(context, heap);
    return NULL;
  }
  heap->size = 0;
  heap->capacity = capacity;
  heap->nil =
      (ds_heap_node_t *)ds_context_alloc(context, sizeof(ds_heap_node_t));
  if (heap->nil == NULL) {
    if (heap->data != NULL) {
      ds_context_free(context, heap->data);
    }
    ds_context_free(context, heap);
    return NULL;
  }
  heap->nil->index = (size_t)-1;
  return heap;
}
ds_heap_t *ds_heap_create(size_t capacity, ds_context_t *context) {
  return heap_create_internal(capacity, context);
}
ds_status_t ds_heap_create_context(size_t capacity, ds_context_t *context,
                                   ds_heap_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (capacity > (size_t)-1 / sizeof(ds_heap_node_t *))
    return DS_ERR_OVERFLOW;
  *out = heap_create_internal(
      capacity, context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

ds_status_t ds_heap_insert_with(ds_heap_t *heap, ds_heap_node_t *node,
                                ds_heap_compare_fn compare, void *user) {
  if (heap == NULL || node == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (node == heap->nil)
    return DS_ERR_INVALID;
  if (heap->size >= heap->capacity) {
    size_t new_capacity;
    ds_heap_node_t **new_data;
    size_t i;
    if (heap->capacity > (size_t)-1 / 2U / sizeof(*new_data)) {
      return DS_ERR_OVERFLOW;
    }
    new_capacity = heap->capacity == 0U ? 1U : heap->capacity * 2U;
    new_data = (ds_heap_node_t **)ds_context_alloc(
        heap->context, new_capacity * sizeof(*new_data));
    if (new_data == NULL) {
      return DS_ERR_ALLOC;
    }
    for (i = 0U; i < heap->size; i++) {
      new_data[i] = heap->data[i];
    }
    if (heap->data != NULL) {
      ds_context_free(heap->context, heap->data);
    }
    heap->data = new_data;
    heap->capacity = new_capacity;
  }
  node->index = heap->size;
  heap->data[heap->size] = node;
  ds_heapify_up_with(heap, heap->size, compare, user);
  heap->size++;
  return DS_OK;
}

ds_status_t ds_heap_insert_ex(ds_heap_t *heap, ds_heap_node_t *node,
                              int (*compare)(ds_heap_node_t *,
                                             ds_heap_node_t *)) {
  if (compare == NULL)
    return DS_ERR_NULL;
  return ds_heap_insert_with(heap, node, heap_compare_direct, &compare);
}
void ds_heap_insert(ds_heap_t *heap, ds_heap_node_t *node,
                    int (*compare)(ds_heap_node_t *, ds_heap_node_t *)) {
  (void)ds_heap_insert_ex(heap, node, compare);
}

ds_status_t ds_heap_extract_with(ds_heap_t *heap, ds_heap_compare_fn compare,
                                 void *user, ds_heap_node_t **out) {
  ds_heap_node_t *root;
  if (heap == NULL || compare == NULL || out == NULL)
    return DS_ERR_NULL;
  if (heap->size == 0)
    return DS_EMPTY;
  root = heap->data[0];
  heap->data[0] = heap->data[heap->size - 1];
  heap->size -= 1;
  if (heap->size != 0) {
    heap->data[0]->index = 0;
    ds_heapify_down_with(heap, 0, compare, user);
  }
  root->index = (size_t)-1;
  *out = root;
  return DS_OK;
}
void *ds_heap_extract_root(ds_heap_t *heap, int (*compare)(ds_heap_node_t *,
                                                           ds_heap_node_t *)) {
  ds_heap_node_t *node;
  if (heap == NULL)
    return NULL;
  node = heap->nil;
  if (compare != NULL)
    (void)ds_heap_extract_with(heap, heap_compare_direct, &compare, &node);
  return node;
}

void *ds_heap_peek_root(ds_heap_t *heap) {
  if (ds_heap_is_empty(heap)) {
    return heap->nil;
  }
  return heap->data[0];
}

bool ds_heap_is_empty(ds_heap_t *heap) { return heap->size == 0; }

static void heap_destroy_internal(ds_heap_t *heap, ds_heap_destroy_fn destroy,
                                  void *user) {
  size_t i;
  if (destroy != NULL) {
    for (i = 0; i < heap->size; ++i) {
      destroy(heap->data[i], user);
    }
  }
  ds_context_free(heap->context, heap->nil);
  if (heap->data != NULL) {
    ds_context_free(heap->context, heap->data);
  }
  ds_context_free(heap->context, heap);
}
void ds_heap_destroy_with(ds_heap_t *container, ds_heap_destroy_fn destroy,
                          void *user) {
  if (container != NULL)
    heap_destroy_internal(container, destroy, user);
}
static void heap_destroy_direct(ds_heap_node_t *node, void *user) {
  void (**destroy)(ds_heap_node_t *) = (void (**)(ds_heap_node_t *))user;
  (*destroy)(node);
}
void ds_heap_destroy(ds_heap_t *container, void (*destroy)(ds_heap_node_t *)) {
  ds_heap_destroy_with(container, destroy != NULL ? heap_destroy_direct : NULL,
                       &destroy);
}

/* clones of the source's nodes at the same positions in copy (as large);
 * false when clone_node returns NULL (the clones made so far are in copy) */
static bool heap_clone_nodes(const ds_heap_t *heap, ds_heap_t *copy,
                             ds_heap_node_t *(*clone_node)(ds_heap_node_t *)) {
  size_t i;
  for (i = 0; i < heap->size; ++i) {
    ds_heap_node_t *node = clone_node(heap->data[i]);
    if (node == NULL) {
      return false;
    }
    node->index = i;
    copy->data[i] = node;
    copy->size = i + 1;
  }
  return true;
}

ds_heap_t *ds_heap_clone(ds_heap_t *heap,
                         ds_heap_node_t *(*clone_node)(ds_heap_node_t *)) {
  ds_heap_t *copy;
  if (heap == NULL || clone_node == NULL) {
    return NULL;
  }
  copy = heap_create_internal(heap->capacity, heap->context);
  if (copy != NULL && !heap_clone_nodes(heap, copy, clone_node)) {
    /* the clones made so far are not released */
    ds_heap_destroy(copy, NULL);
    copy = NULL;
  }
  return copy;
}

ds_status_t ds_heap_clone_ex(const ds_heap_t *heap,
                             ds_heap_node_t *(*clone_node)(ds_heap_node_t *),
                             void (*destroy_clone)(ds_heap_node_t *),
                             ds_heap_t **out) {
  ds_heap_t *copy;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (heap == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  copy = heap_create_internal(heap->capacity, heap->context);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!heap_clone_nodes(heap, copy, clone_node)) {
    ds_heap_destroy(copy, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  *out = copy;
  return DS_OK;
}

ds_status_t ds_heap_walk_with(ds_heap_t *container, ds_heap_visit_fn visit,
                              void *user) {
  size_t i, position;
  ds_heap_node_t *node;
  ds_status_t st;
  if (container == NULL || visit == NULL)
    return DS_ERR_NULL;
  position = 0;
  for (i = 0; i < container->size; ++i) {
    node = container->data[position];
    st = visit(node, user);
    if (st != DS_OK)
      return st;
    position += 1;
  }
  return DS_OK;
}
ds_status_t ds_heap_find_with(ds_heap_t *container, ds_heap_node_t *key,
                              ds_heap_compare_fn compare, void *user,
                              ds_heap_node_t **out) {
  size_t i, position;
  ds_heap_node_t *node;
  if (container == NULL || key == NULL || compare == NULL || out == NULL)
    return DS_ERR_NULL;
  position = 0;
  for (i = 0; i < container->size; ++i) {
    node = container->data[position];
    if (compare(node, key, user) == 0) {
      *out = node;
      return DS_OK;
    }
    position += 1;
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_heap_clone_with(const ds_heap_t *container,
                               ds_heap_clone_fn clone,
                               ds_heap_destroy_fn destroy, void *user,
                               ds_context_t *context, ds_heap_t **out) {
  ds_heap_t *copy;
  size_t i, position;
  ds_heap_node_t *node;
  ds_heap_node_t *new_node;

  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (container == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  copy = heap_create_internal(container->capacity,
                              context != NULL ? context : container->context);
  if (copy == NULL)
    return DS_ERR_ALLOC;

  position = 0;
  for (i = 0; i < container->size; ++i) {
    node = container->data[position];
    new_node = NULL;
    st = clone(node, &new_node, user);
    if (st != DS_OK || new_node == NULL) {
      ds_heap_destroy_with(copy, destroy, user);
      return st < 0 ? st : DS_ERR_CALLBACK;
    }
    new_node->index = i;
    copy->data[i] = new_node;
    copy->size += 1;
    position += 1;
  }
  *out = copy;
  return DS_OK;
}

ds_status_t ds_heap_walk_reverse_with(ds_heap_t *container,
                                      ds_heap_visit_fn visit, void *user) {
  size_t i, position;
  ds_status_t st;
  if (container == NULL || visit == NULL)
    return DS_ERR_NULL;
  position = container->size;
  for (i = 0; i < container->size; ++i) {
    position -= 1;
    st = visit(container->data[position], user);
    if (st != DS_OK)
      return st;
  }
  return DS_OK;
}

ds_status_t ds_heap_update_with(ds_heap_t *heap, ds_heap_node_t *node,
                                ds_heap_compare_fn compare, void *user) {
  if (heap == NULL || node == NULL || compare == NULL)
    return DS_ERR_NULL;
  if (node == heap->nil || node->index >= heap->size ||
      heap->data[node->index] != node)
    return DS_NOT_FOUND;
  ds_heapify_up_with(heap, node->index, compare, user);
  ds_heapify_down_with(heap, node->index, compare, user);
  return DS_OK;
}
ds_status_t ds_heap_remove_with(ds_heap_t *heap, ds_heap_node_t *node,
                                ds_heap_compare_fn compare, void *user) {
  size_t index;
  ds_heap_node_t *replacement;
  if (heap == NULL || node == NULL || compare == NULL)
    return DS_ERR_NULL;
  if (node == heap->nil || node->index >= heap->size ||
      heap->data[node->index] != node)
    return DS_NOT_FOUND;
  index = node->index;
  heap->size -= 1;
  if (index != heap->size) {
    replacement = heap->data[heap->size];
    heap->data[index] = replacement;
    replacement->index = index;
    ds_heapify_up_with(heap, index, compare, user);
    ds_heapify_down_with(heap, replacement->index, compare, user);
  }
  node->index = (size_t)-1;
  return DS_OK;
}
