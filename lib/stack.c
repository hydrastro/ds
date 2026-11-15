#include "stack.h"
#include <stdlib.h>

ds_stack_t *ds_stack_create(void) {
  return ds_stack_create_alloc(malloc, free);
}

ds_stack_t *ds_stack_create_alloc(void *(*allocator)(size_t),
                                  void (*deallocator)(void *)) {
  ds_stack_t *stack;
  if (allocator == NULL || deallocator == NULL) {
    return NULL;
  }
  stack = (ds_stack_t *)allocator(sizeof(ds_stack_t));
  if (stack == NULL) {
    return NULL;
  }
  stack->allocator = allocator;
  stack->deallocator = deallocator;
  stack->nil = (ds_stack_node_t *)allocator(sizeof(ds_stack_node_t));
  if (stack->nil == NULL) {
    deallocator(stack);
    return NULL;
  }
  stack->nil->next = stack->nil;
  stack->top = stack->nil;
  stack->size = 0;
  return stack;
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

void ds_stack_destroy(ds_stack_t *stack, void (*destroy)(ds_stack_node_t *)) {
  ds_stack_node_t *node = stack->top;
  while (node != stack->nil) {
    ds_stack_node_t *next = node->next;
    if (destroy != NULL) { /* as the queue and the list: NULL owns nothing */
      destroy(node);
    }
    node = next;
  }
  stack->deallocator(stack->nil);
  stack->deallocator(stack);
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
  copy = ds_stack_create_alloc(stack->allocator, stack->deallocator);
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
  copy = ds_stack_create_alloc(stack->allocator, stack->deallocator);
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
