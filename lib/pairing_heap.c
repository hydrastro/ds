#include "pairing_heap.h"
#include <stdlib.h>

ds_pairing_heap_t *ds_pairing_heap_create(
    int (*compare)(ds_pairing_heap_node_t *, ds_pairing_heap_node_t *)) {
  return ds_pairing_heap_create_alloc(compare, malloc, free);
}

ds_pairing_heap_t *ds_pairing_heap_create_alloc(
    int (*compare)(ds_pairing_heap_node_t *, ds_pairing_heap_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *)) {
  ds_pairing_heap_t *heap =
      (ds_pairing_heap_t *)allocator(sizeof(ds_pairing_heap_t));
  heap->allocator = allocator;
  heap->deallocator = deallocator;
  heap->compare = compare;
  heap->nil =
      (ds_pairing_heap_node_t *)allocator(sizeof(ds_pairing_heap_node_t));
  heap->nil->child = NULL;
  heap->nil->sibling = NULL;
  heap->nil->prev = NULL;
  heap->root = NULL;
  heap->size = 0;
  return heap;
}

void ds_pairing_heap_node_init(ds_pairing_heap_node_t *node) {
  node->child = NULL;
  node->sibling = NULL;
  node->prev = NULL;
}

/*
 * Internal child/sibling/prev links use NULL as the terminator so that nodes
 * are independent of any particular heap's sentinel. heap->nil is only used as
 * the public "empty"/"not found" return value.
 */
static ds_pairing_heap_node_t *pairing_heap_link(ds_pairing_heap_t *heap,
                                                 ds_pairing_heap_node_t *a,
                                                 ds_pairing_heap_node_t *b) {
  if (a == NULL) {
    return b;
  }
  if (b == NULL) {
    return a;
  }
  if (heap->compare(b, a) < 0) {
    ds_pairing_heap_node_t *temp = a;
    a = b;
    b = temp;
  }
  b->prev = a;
  b->sibling = a->child;
  if (a->child != NULL) {
    a->child->prev = b;
  }
  a->child = b;
  a->sibling = NULL;
  a->prev = NULL;
  return a;
}

void ds_pairing_heap_insert(ds_pairing_heap_t *heap,
                            ds_pairing_heap_node_t *node) {
  if (node == heap->nil || node == NULL) {
    return;
  }
  node->child = NULL;
  node->sibling = NULL;
  node->prev = NULL;
  heap->root = pairing_heap_link(heap, heap->root, node);
  heap->size += 1;
}

ds_pairing_heap_node_t *ds_pairing_heap_peek(ds_pairing_heap_t *heap) {
  ds_pairing_heap_node_t *result;
  result = heap->root != NULL ? heap->root : heap->nil;
  return result;
}

static ds_pairing_heap_node_t *
pairing_heap_combine_siblings(ds_pairing_heap_t *heap,
                              ds_pairing_heap_node_t *first) {
  ds_pairing_heap_node_t *pairs;
  ds_pairing_heap_node_t *result;
  ds_pairing_heap_node_t *current;
  if (first == NULL || first->sibling == NULL) {
    return first;
  }
  pairs = NULL;
  current = first;
  /* first pass: link consecutive pairs, stacking results into pairs list */
  while (current != NULL) {
    ds_pairing_heap_node_t *a = current;
    ds_pairing_heap_node_t *b = current->sibling;
    if (b != NULL) {
      ds_pairing_heap_node_t *next = b->sibling;
      ds_pairing_heap_node_t *linked;
      a->sibling = NULL;
      a->prev = NULL;
      b->sibling = NULL;
      b->prev = NULL;
      linked = pairing_heap_link(heap, a, b);
      linked->sibling = pairs;
      pairs = linked;
      current = next;
    } else {
      a->sibling = pairs;
      pairs = a;
      current = NULL;
    }
  }
  /* second pass: merge the pairs list into a single tree */
  result = NULL;
  while (pairs != NULL) {
    ds_pairing_heap_node_t *next = pairs->sibling;
    pairs->sibling = NULL;
    pairs->prev = NULL;
    result = pairing_heap_link(heap, result, pairs);
    pairs = next;
  }
  return result;
}

ds_pairing_heap_node_t *ds_pairing_heap_extract(ds_pairing_heap_t *heap) {
  ds_pairing_heap_node_t *root;
  ds_pairing_heap_node_t *child;
  if (heap->root == NULL) {
    return heap->nil;
  }
  root = heap->root;
  child = root->child;
  if (child != NULL) {
    child->prev = NULL;
  }
  heap->root = pairing_heap_combine_siblings(heap, child);
  if (heap->root != NULL) {
    heap->root->prev = NULL;
    heap->root->sibling = NULL;
  }
  root->child = NULL;
  root->sibling = NULL;
  root->prev = NULL;
  heap->size -= 1;
  return root;
}

void ds_pairing_heap_decrease_key(ds_pairing_heap_t *heap,
                                  ds_pairing_heap_node_t *node) {
  if (node == heap->nil || node == NULL || node == heap->root) {
    return;
  }
  /* detach node (with its subtree) from its current position */
  if (node->prev != NULL) {
    if (node->prev->child == node) {
      node->prev->child = node->sibling;
    } else {
      node->prev->sibling = node->sibling;
    }
    if (node->sibling != NULL) {
      node->sibling->prev = node->prev;
    }
  }
  node->sibling = NULL;
  node->prev = NULL;
  heap->root = pairing_heap_link(heap, heap->root, node);
}

bool ds_pairing_heap_is_empty(ds_pairing_heap_t *heap) {
  bool result;
  result = heap->root == NULL;
  return result;
}

size_t ds_pairing_heap_size(ds_pairing_heap_t *heap) {
  size_t result;
  result = heap->size;
  return result;
}

void ds_pairing_heap_merge(ds_pairing_heap_t *heap, ds_pairing_heap_t *other) {
  if (other == NULL || other->root == NULL) {
    return;
  }
  heap->root = pairing_heap_link(heap, heap->root, other->root);
  heap->size += other->size;
  other->root = NULL;
  other->size = 0;
}

static void
pairing_heap_walk_recursive(ds_pairing_heap_node_t *node,
                            void (*callback)(ds_pairing_heap_node_t *)) {
  while (node != NULL) {
    ds_pairing_heap_node_t *sibling = node->sibling;
    callback(node);
    pairing_heap_walk_recursive(node->child, callback);
    node = sibling;
  }
}

void ds_pairing_heap_walk(ds_pairing_heap_t *heap,
                          void (*callback)(ds_pairing_heap_node_t *)) {
  pairing_heap_walk_recursive(heap->root, callback);
}

static void
pairing_heap_destroy_recursive(ds_pairing_heap_node_t *node,
                               void (*destroy)(ds_pairing_heap_node_t *)) {
  while (node != NULL) {
    ds_pairing_heap_node_t *sibling = node->sibling;
    pairing_heap_destroy_recursive(node->child, destroy);
    if (destroy != NULL) {
      destroy(node);
    }
    node = sibling;
  }
}

void ds_pairing_heap_destroy(ds_pairing_heap_t *heap,
                             void (*destroy)(ds_pairing_heap_node_t *)) {
  pairing_heap_destroy_recursive(heap->root, destroy);
  heap->deallocator(heap->nil);
  heap->deallocator(heap);
}
