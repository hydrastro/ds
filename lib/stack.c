#include "stack.h"
#include "legacy_alloc.h"
#include <stdlib.h>

ds_stack_t *ds_stack_create(void) {
  return ds_stack_create_alloc(malloc, free);
}

static ds_stack_t *stack_create_internal(void *(*allocator)(size_t),
                                         void (*deallocator)(void *),
                                         ds_context_t *context) {
  ds_stack_t *stack;
  if (allocator == NULL || deallocator == NULL) {
    return NULL;
  }
  stack =
      (ds_stack_t *)DS_LEGACY_ALLOCATE(context, allocator, sizeof(ds_stack_t));
  if (stack == NULL) {
    return NULL;
  }
  stack->context = context;
  stack->allocator = allocator;
  stack->deallocator = deallocator;
  stack->nil = (ds_stack_node_t *)DS_LEGACY_ALLOCATE(context, allocator,
                                                     sizeof(ds_stack_node_t));
  if (stack->nil == NULL) {
    DS_LEGACY_RELEASE(context, deallocator, stack);
    return NULL;
  }
  stack->nil->next = stack->nil;
  stack->top = stack->nil;
  stack->size = 0;
  return stack;
}

ds_stack_t *ds_stack_create_alloc(void *(*allocator)(size_t),
                                  void (*deallocator)(void *)) {
  return stack_create_internal(allocator, deallocator, NULL);
}
ds_status_t ds_stack_create_context(ds_context_t *context, ds_stack_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = stack_create_internal(
      malloc, free, context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

void ds_stack_push(ds_stack_t *stack, ds_stack_node_t *node) {
  if (node == stack->nil) {
    return;
  }
  node->next = stack->top;
  stack->top = node;
  stack->size += 1;
}

ds_stack_node_t *ds_stack_pop(ds_stack_t *stack) {
  ds_stack_node_t *node;
  if (ds_stack_is_empty(stack)) {
    return stack->nil;
  }
  node = stack->top;
  stack->top = node->next;
  stack->size -= 1;
  return node;
}

ds_stack_node_t *ds_stack_peek(ds_stack_t *stack) {
  ds_stack_node_t *result;
  result = stack->top;
  return result;
}

bool ds_stack_is_empty(ds_stack_t *stack) {
  bool result;
  result = stack->top == stack->nil;
  return result;
}

static void stack_destroy_internal(ds_stack_t *stack,
                                   ds_stack_destroy_fn destroy, void *user) {
  ds_stack_node_t *node = stack->top;
  while (node != stack->nil) {
    ds_stack_node_t *next = node->next;
    if (destroy != NULL) { /* as the queue and the list: NULL owns nothing */
      destroy(node, user);
    }
    node = next;
  }
  DS_LEGACY_FREE(stack, stack->nil);
  DS_LEGACY_FREE(stack, stack);
}
void ds_stack_destroy_with(ds_stack_t *container, ds_stack_destroy_fn destroy,
                           void *user) {
  if (container != NULL)
    stack_destroy_internal(container, destroy, user);
}
static void stack_destroy_legacy(ds_stack_node_t *node, void *user) {
  void (**destroy)(ds_stack_node_t *) = (void (**)(ds_stack_node_t *))user;
  (*destroy)(node);
}
void ds_stack_destroy(ds_stack_t *container,
                      void (*destroy)(ds_stack_node_t *)) {
  ds_stack_destroy_with(
      container, destroy != NULL ? stack_destroy_legacy : NULL, &destroy);
}

void ds_stack_delete(ds_stack_t *stack) {
  stack->top = stack->nil;
  stack->size = 0;
}

void ds_stack_delete_node(ds_stack_t *stack, ds_stack_node_t *node) {
  ds_stack_node_t *current;
  ds_stack_node_t *previous;

  current = stack->top;
  previous = stack->nil;

  while (current != stack->nil && current != node) {
    previous = current;
    current = current->next;
  }

  if (current == node) {
    if (previous == stack->nil) {
      stack->top = current->next;
    } else {
      previous->next = current->next;
    }
    stack->size -= 1;
  }
}

void ds_stack_destroy_node(ds_stack_t *stack, ds_stack_node_t *node,
                           void (*destroy)(ds_stack_node_t *)) {
  ds_stack_delete_node(stack, node);
  if (destroy != NULL) {
    destroy(node);
  }
}

void ds_stack_pop_destroy(ds_stack_t *stack,
                          void (*destroy_node)(ds_stack_node_t *)) {
  if (!ds_stack_is_empty(stack)) {
    ds_stack_node_t *node = ds_stack_pop(stack);
    destroy_node(node);
  }
}

ds_stack_node_t *ds_stack_search(ds_stack_t *stack, ds_stack_node_t *node,
                                 int (*compare)(ds_stack_node_t *,
                                                ds_stack_node_t *)) {
  ds_stack_node_t *current;

  current = stack->top;

  while (current != stack->nil) {
    if (compare(current, node) == 0) {
      return current;
    }
    current = current->next;
  }

  return stack->nil;
}

void ds_stack_walk_forward(ds_stack_t *stack, ds_stack_node_t *start_node,
                           void (*callback)(ds_stack_node_t *)) {
  ds_stack_node_t *current;

  current = start_node != stack->nil ? start_node : stack->top;

  while (current != stack->nil) {
    callback(current);
    current = current->next;
  }
}

/* reverses the links from current to the end, then walks them back,
 * restoring each link before its node is visited: O(1) space, where
 * recursion would go as deep as the stack is long */
void ds_stack_walk_backwards(ds_stack_t *stack, ds_stack_node_t *current,
                             void (*callback)(ds_stack_node_t *)) {
  ds_stack_node_t *rev = stack->nil;
  ds_stack_node_t *next;
  while (current != stack->nil) {
    next = current->next;
    current->next = rev;
    rev = current;
    current = next;
  }
  current = rev;
  rev = stack->nil;
  while (current != stack->nil) {
    next = current->next;
    current->next = rev;
    rev = current;
    callback(current);
    current = next;
  }
}

/* clones of the source's nodes, top first, added below copy's; false
 * when clone_node returns NULL (the clones made so far are in copy) */
static bool
stack_clone_nodes(const ds_stack_t *stack, ds_stack_t *copy,
                  ds_stack_node_t *(*clone_node)(ds_stack_node_t *)) {
  ds_stack_node_t *current;
  ds_stack_node_t *last = copy->nil;
  for (current = stack->top; current != stack->nil; current = current->next) {
    ds_stack_node_t *node = clone_node(current);
    if (node == NULL) {
      return false;
    }
    node->next = copy->nil;
    if (last == copy->nil) {
      copy->top = node;
    } else {
      last->next = node;
    }
    last = node;
    copy->size += 1;
  }
  return true;
}

ds_stack_t *ds_stack_clone(ds_stack_t *stack,
                           ds_stack_node_t *(*clone_node)(ds_stack_node_t *)) {
  ds_stack_t *copy;
  if (stack == NULL || clone_node == NULL) {
    return NULL;
  }
  copy = stack_create_internal(stack->allocator, stack->deallocator,
                               stack->context);
  if (copy != NULL && !stack_clone_nodes(stack, copy, clone_node)) {
    /* the clones made so far are not released */
    ds_stack_destroy(copy, NULL);
    copy = NULL;
  }
  return copy;
}

ds_status_t ds_stack_clone_ex(const ds_stack_t *stack,
                              ds_stack_node_t *(*clone_node)(ds_stack_node_t *),
                              void (*destroy_clone)(ds_stack_node_t *),
                              ds_stack_t **out) {
  ds_stack_t *copy;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (stack == NULL || clone_node == NULL || destroy_clone == NULL) {
    return DS_ERR_NULL;
  }
  copy = stack_create_internal(stack->allocator, stack->deallocator,
                               stack->context);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  if (!stack_clone_nodes(stack, copy, clone_node)) {
    ds_stack_destroy(copy, destroy_clone);
    return DS_ERR_CALLBACK;
  }
  *out = copy;
  return DS_OK;
}

ds_status_t ds_stack_walk_with(ds_stack_t *container, ds_stack_visit_fn visit,
                               void *user) {
  size_t i;
  ds_stack_node_t *node;
  ds_status_t st;
  if (container == NULL || visit == NULL)
    return DS_ERR_NULL;
  node = container->top;
  for (i = 0; i < container->size; ++i) {

    st = visit(node, user);
    if (st != DS_OK)
      return st;
    node = node->next;
  }
  return DS_OK;
}
ds_status_t ds_stack_find_with(ds_stack_t *container, ds_stack_node_t *key,
                               ds_stack_compare_fn compare, void *user,
                               ds_stack_node_t **out) {
  size_t i;
  ds_stack_node_t *node;
  if (container == NULL || key == NULL || compare == NULL || out == NULL)
    return DS_ERR_NULL;
  node = container->top;
  for (i = 0; i < container->size; ++i) {

    if (compare(node, key, user) == 0) {
      *out = node;
      return DS_OK;
    }
    node = node->next;
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_stack_clone_with(const ds_stack_t *container,
                                ds_stack_clone_fn clone,
                                ds_stack_destroy_fn destroy, void *user,
                                ds_context_t *context, ds_stack_t **out) {
  ds_stack_t *copy;
  size_t i;
  ds_stack_node_t *node;
  ds_stack_node_t *new_node;
  ds_stack_node_t *last;
  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (container == NULL || clone == NULL || destroy == NULL)
    return DS_ERR_NULL;
  copy = stack_create_internal(container->allocator, container->deallocator,
                               context != NULL ? context : container->context);
  if (copy == NULL)
    return DS_ERR_ALLOC;
  last = copy->nil;
  node = container->top;
  for (i = 0; i < container->size; ++i) {

    new_node = NULL;
    st = clone(node, &new_node, user);
    if (st != DS_OK || new_node == NULL) {
      ds_stack_destroy_with(copy, destroy, user);
      return st < 0 ? st : DS_ERR_CALLBACK;
    }
    new_node->next = copy->nil;
    if (last == copy->nil)
      copy->top = new_node;
    else
      last->next = new_node;
    last = new_node;
    copy->size += 1;
    node = node->next;
  }
  *out = copy;
  return DS_OK;
}

ds_status_t ds_stack_push_ex(ds_stack_t *container, ds_stack_node_t *node) {
  if (container == NULL || node == NULL)
    return DS_ERR_NULL;
  if (node == container->nil)
    return DS_ERR_INVALID;
  if (container->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  ds_stack_push(container, node);
  return DS_OK;
}

ds_status_t ds_stack_pop_ex(ds_stack_t *container, ds_stack_node_t **out) {
  if (container == NULL || out == NULL)
    return DS_ERR_NULL;
  if (container->size == 0)
    return DS_EMPTY;
  *out = ds_stack_pop(container);
  return DS_OK;
}

ds_status_t ds_stack_remove_ex(ds_stack_t *container, ds_stack_node_t *target) {
  size_t i;
  ds_stack_node_t *node;
  if (container == NULL || target == NULL)
    return DS_ERR_NULL;
  if (target == container->nil)
    return DS_ERR_INVALID;
  node = container->top;
  for (i = 0; i < container->size; ++i) {

    if (node == target) {
      ds_stack_delete_node(container, node);
      return DS_OK;
    }
    node = node->next;
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_stack_walk_reverse_with(ds_stack_t *container,
                                       ds_stack_visit_fn visit, void *user) {
  ds_stack_node_t *node, *next, *rev, *first;
  size_t i;
  ds_status_t st = DS_OK;
  if (container == NULL || visit == NULL)
    return DS_ERR_NULL;
  if (container->size == 0)
    return DS_OK;
  first = container->top;
  node = first;
  rev = container->nil;
  for (i = 0; i < container->size; ++i) {
    next = node->next;
    node->next = rev;
    rev = node;
    node = next;
  }
  node = rev;
  rev = container->nil;
  for (i = 0; i < container->size; ++i) {
    next = node->next;
    node->next = rev;
    rev = node;
    if (st == DS_OK)
      st = visit(node, user);
    node = next;
  }
  return st;
}
