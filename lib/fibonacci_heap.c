#include "fibonacci_heap.h"
#include <stdlib.h>

ds_fibonacci_heap_t *FUNC(ds_fibonacci_heap_create)(
    int (*compare)(ds_fibonacci_heap_node_t *, ds_fibonacci_heap_node_t *)) {
  return FUNC(ds_fibonacci_heap_create_alloc)(compare, malloc, free);
}

ds_fibonacci_heap_t *FUNC(ds_fibonacci_heap_create_alloc)(
    int (*compare)(ds_fibonacci_heap_node_t *, ds_fibonacci_heap_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *)) {
  ds_fibonacci_heap_t *heap =
      (ds_fibonacci_heap_t *)allocator(sizeof(ds_fibonacci_heap_t));
  heap->compare = compare;
  heap->allocator = allocator;
  heap->deallocator = deallocator;
  heap->size = 0;
  heap->nil =
      (ds_fibonacci_heap_node_t *)allocator(sizeof(ds_fibonacci_heap_node_t));
  heap->nil->parent = heap->nil;
  heap->nil->child = heap->nil;
  heap->nil->left = heap->nil;
  heap->nil->right = heap->nil;
  heap->nil->degree = 0;
  heap->nil->mark = false;
  heap->min = heap->nil;
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(heap);
#endif
  return heap;
}

void FUNC(ds_fibonacci_heap_node_init)(ds_fibonacci_heap_node_t *node) {
  node->parent = NULL;
  node->child = NULL;
  node->left = node;
  node->right = node;
  node->degree = 0;
  node->mark = false;
}

/* splice node into the circular list to the right of `list` */
static void FUNC(fib_list_insert)(ds_fibonacci_heap_node_t *list,
                                  ds_fibonacci_heap_node_t *node) {
  node->left = list;
  node->right = list->right;
  list->right->left = node;
  list->right = node;
}

/* remove node from its circular sibling list */
static void FUNC(fib_list_remove)(ds_fibonacci_heap_node_t *node) {
  node->left->right = node->right;
  node->right->left = node->left;
  node->left = node;
  node->right = node;
}

void FUNC(ds_fibonacci_heap_insert)(ds_fibonacci_heap_t *heap,
                                    ds_fibonacci_heap_node_t *node) {
#ifdef DS_THREAD_SAFE
  LOCK(heap);
#endif
  node->parent = NULL;
  node->child = NULL;
  node->left = node;
  node->right = node;
  node->degree = 0;
  node->mark = false;
  if (heap->min == heap->nil) {
    heap->min = node;
  } else {
    FUNC(fib_list_insert)(heap->min, node);
    if (heap->compare(node, heap->min) < 0) {
      heap->min = node;
    }
  }
  heap->size += 1;
#ifdef DS_THREAD_SAFE
  UNLOCK(heap);
#endif
}

ds_fibonacci_heap_node_t *
FUNC(ds_fibonacci_heap_peek)(ds_fibonacci_heap_t *heap) {
  ds_fibonacci_heap_node_t *result;
#ifdef DS_THREAD_SAFE
  LOCK(heap);
#endif
  result = heap->min;
#ifdef DS_THREAD_SAFE
  UNLOCK(heap);
#endif
  return result;
}

/* make `child` a child of `parent`; both are roots beforehand */
static void FUNC(fib_link)(ds_fibonacci_heap_node_t *child,
                           ds_fibonacci_heap_node_t *parent) {
  child->parent = parent;
  if (parent->child == NULL) {
    parent->child = child;
    child->left = child;
    child->right = child;
  } else {
    FUNC(fib_list_insert)(parent->child, child);
  }
  parent->degree += 1;
  child->mark = false;
}

static void FUNC(fib_consolidate)(ds_fibonacci_heap_t *heap) {
  size_t max_degree;
  ds_fibonacci_heap_node_t **table;
  ds_fibonacci_heap_node_t *start;
  ds_fibonacci_heap_node_t *w;
  size_t i;
  size_t count;
  size_t idx;
  ds_fibonacci_heap_node_t **roots;
  /* an upper bound on degree is ~1.44 log2(n); 2*log2 + 8 is safe */
  max_degree = 8;
  {
    size_t s = heap->size;
    while (s > 0) {
      max_degree += 1;
      s >>= 1;
    }
    max_degree *= 2;
  }
  table = (ds_fibonacci_heap_node_t **)heap->allocator(
      max_degree * sizeof(ds_fibonacci_heap_node_t *));
  for (i = 0; i < max_degree; i++) {
    table[i] = NULL;
  }
  /* collect the root list into an array first (consolidation mutates links) */
  count = 0;
  start = heap->min;
  if (start != heap->nil) {
    w = start;
    do {
      count += 1;
      w = w->right;
    } while (w != start);
  }
  roots = (ds_fibonacci_heap_node_t **)heap->allocator(
      (count == 0 ? 1 : count) * sizeof(ds_fibonacci_heap_node_t *));
  w = start;
  for (i = 0; i < count; i++) {
    roots[i] = w;
    w = w->right;
  }
  for (idx = 0; idx < count; idx++) {
    ds_fibonacci_heap_node_t *x = roots[idx];
    int d;
    x->left = x;
    x->right = x;
    d = x->degree;
    while (table[d] != NULL) {
      ds_fibonacci_heap_node_t *y = table[d];
      if (heap->compare(x, y) > 0) {
        ds_fibonacci_heap_node_t *tmp = x;
        x = y;
        y = tmp;
      }
      FUNC(fib_link)(y, x);
      table[d] = NULL;
      d += 1;
    }
    table[d] = x;
  }
  /* rebuild the root list and find the new min */
  heap->min = heap->nil;
  for (i = 0; i < max_degree; i++) {
    if (table[i] != NULL) {
      ds_fibonacci_heap_node_t *node = table[i];
      node->parent = NULL;
      node->left = node;
      node->right = node;
      if (heap->min == heap->nil) {
        heap->min = node;
      } else {
        FUNC(fib_list_insert)(heap->min, node);
        if (heap->compare(node, heap->min) < 0) {
          heap->min = node;
        }
      }
    }
  }
  heap->deallocator(table);
  heap->deallocator(roots);
}

ds_fibonacci_heap_node_t *
FUNC(ds_fibonacci_heap_extract)(ds_fibonacci_heap_t *heap) {
  ds_fibonacci_heap_node_t *z;
#ifdef DS_THREAD_SAFE
  LOCK(heap);
#endif
  z = heap->min;
  if (z == heap->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(heap);
#endif
    return heap->nil;
  }
  /* move z's children to the root list */
  if (z->child != NULL) {
    ds_fibonacci_heap_node_t *c = z->child;
    ds_fibonacci_heap_node_t *first = c;
    do {
      ds_fibonacci_heap_node_t *nxt = c->right;
      c->parent = NULL;
      c->left = c;
      c->right = c;
      FUNC(fib_list_insert)(heap->min, c);
      c = nxt;
    } while (c != first);
    z->child = NULL;
  }
  /* remove z from the root list */
  if (z->right == z) {
    heap->min = heap->nil;
  } else {
    heap->min = z->right;
    FUNC(fib_list_remove)(z);
    FUNC(fib_consolidate)(heap);
  }
  heap->size -= 1;
  z->left = z;
  z->right = z;
  z->parent = NULL;
  z->child = NULL;
#ifdef DS_THREAD_SAFE
  UNLOCK(heap);
#endif
  return z;
}

static void FUNC(fib_cut)(ds_fibonacci_heap_t *heap,
                          ds_fibonacci_heap_node_t *x,
                          ds_fibonacci_heap_node_t *y) {
  /* remove x from y's child list, add x to the root list */
  if (x->right == x) {
    y->child = NULL;
  } else {
    if (y->child == x) {
      y->child = x->right;
    }
    FUNC(fib_list_remove)(x);
  }
  y->degree -= 1;
  x->parent = NULL;
  x->left = x;
  x->right = x;
  x->mark = false;
  FUNC(fib_list_insert)(heap->min, x);
}

static void FUNC(fib_cascading_cut)(ds_fibonacci_heap_t *heap,
                                    ds_fibonacci_heap_node_t *y) {
  ds_fibonacci_heap_node_t *z = y->parent;
  if (z != NULL) {
    if (!y->mark) {
      y->mark = true;
    } else {
      FUNC(fib_cut)(heap, y, z);
      FUNC(fib_cascading_cut)(heap, z);
    }
  }
}

void FUNC(ds_fibonacci_heap_decrease_key)(ds_fibonacci_heap_t *heap,
                                          ds_fibonacci_heap_node_t *node) {
  ds_fibonacci_heap_node_t *parent;
#ifdef DS_THREAD_SAFE
  LOCK(heap);
#endif
  parent = node->parent;
  if (parent != NULL && heap->compare(node, parent) < 0) {
    FUNC(fib_cut)(heap, node, parent);
    FUNC(fib_cascading_cut)(heap, parent);
  }
  if (heap->compare(node, heap->min) < 0) {
    heap->min = node;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(heap);
#endif
}

void FUNC(ds_fibonacci_heap_merge)(ds_fibonacci_heap_t *heap,
                                   ds_fibonacci_heap_t *other) {
#ifdef DS_THREAD_SAFE
  LOCK(heap);
#endif
  if (other->min == other->nil) {
#ifdef DS_THREAD_SAFE
    UNLOCK(heap);
#endif
    return;
  }
  if (heap->min == heap->nil) {
    heap->min = other->min;
  } else {
    /* concatenate the two circular root lists */
    ds_fibonacci_heap_node_t *a_right = heap->min->right;
    ds_fibonacci_heap_node_t *b_left = other->min->left;
    heap->min->right = other->min;
    other->min->left = heap->min;
    a_right->left = b_left;
    b_left->right = a_right;
    if (heap->compare(other->min, heap->min) < 0) {
      heap->min = other->min;
    }
  }
  heap->size += other->size;
  other->min = other->nil;
  other->size = 0;
#ifdef DS_THREAD_SAFE
  UNLOCK(heap);
#endif
}

size_t FUNC(ds_fibonacci_heap_size)(ds_fibonacci_heap_t *heap) {
  size_t result;
#ifdef DS_THREAD_SAFE
  LOCK(heap);
#endif
  result = heap->size;
#ifdef DS_THREAD_SAFE
  UNLOCK(heap);
#endif
  return result;
}

bool FUNC(ds_fibonacci_heap_is_empty)(ds_fibonacci_heap_t *heap) {
  bool result;
#ifdef DS_THREAD_SAFE
  LOCK(heap);
#endif
  result = heap->size == 0;
#ifdef DS_THREAD_SAFE
  UNLOCK(heap);
#endif
  return result;
}

static void
FUNC(fib_destroy_tree)(ds_fibonacci_heap_t *heap,
                       ds_fibonacci_heap_node_t *node,
                       void (*destroy)(ds_fibonacci_heap_node_t *)) {
  ds_fibonacci_heap_node_t *start = node;
  ds_fibonacci_heap_node_t *current = node;
  if (node == NULL) {
    return;
  }
  do {
    ds_fibonacci_heap_node_t *next = current->right;
    if (current->child != NULL) {
      FUNC(fib_destroy_tree)(heap, current->child, destroy);
    }
    if (destroy != NULL) {
      destroy(current);
    }
    current = next;
  } while (current != start);
}

void FUNC(ds_fibonacci_heap_destroy)(
    ds_fibonacci_heap_t *heap, void (*destroy)(ds_fibonacci_heap_node_t *)) {
  if (heap->min != heap->nil) {
    FUNC(fib_destroy_tree)(heap, heap->min, destroy);
  }
  heap->deallocator(heap->nil);
#ifdef DS_THREAD_SAFE
  LOCK_DESTROY(heap);
#endif
  heap->deallocator(heap);
}
