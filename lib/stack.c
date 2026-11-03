#include "stack.h"
#include <stdlib.h>

ds_stack_t *FUNC(ds_stack_create)(void) {
  return FUNC(ds_stack_create_alloc)(malloc, free);
}

ds_stack_t *FUNC(ds_stack_create_alloc)(void *(*allocator)(size_t),
                                        void (*deallocator)(void *)) {
  ds_stack_t *stack = (ds_stack_t *)allocator(sizeof(ds_stack_t));
  stack->allocator = allocator;
  stack->deallocator = deallocator;
  stack->nil = (ds_stack_node_t *)allocator(sizeof(ds_stack_node_t));
  stack->nil->next = stack->nil;
  stack->top = stack->nil;
  stack->size = 0;
  return stack;
}

void FUNC(ds_stack_push)(ds_stack_t *stack, ds_stack_node_t *node) {
  if (node == stack->nil) {
    return;
  }
  node->next = stack->top;
  stack->top = node;
  stack->size += 1;
}

ds_stack_node_t *FUNC(ds_stack_pop)(ds_stack_t *stack) {
  ds_stack_node_t *node;
  if (FUNC(ds_stack_is_empty)(stack)) {
    return stack->nil;
  }
  node = stack->top;
  stack->top = node->next;
  stack->size -= 1;
  return node;
}

ds_stack_node_t *FUNC(ds_stack_peek)(ds_stack_t *stack) {
  ds_stack_node_t *result;
  result = stack->top;
  return result;
}

bool FUNC(ds_stack_is_empty)(ds_stack_t *stack) {
  bool result;
  result = stack->top == stack->nil;
  return result;
}

void FUNC(ds_stack_destroy)(ds_stack_t *stack,
                            void (*destroy)(ds_stack_node_t *)) {
  ds_stack_node_t *node = stack->top;
  while (node != stack->nil) {
    ds_stack_node_t *next = node->next;
    destroy(node);
    node = next;
  }
  stack->deallocator(stack->nil);
  stack->deallocator(stack);
}

void FUNC(ds_stack_delete)(ds_stack_t *stack) {
  stack->top = stack->nil;
  stack->size = 0;
}

void FUNC(ds_stack_delete_node)(ds_stack_t *stack, ds_stack_node_t *node) {
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

void FUNC(ds_stack_destroy_node)(ds_stack_t *stack, ds_stack_node_t *node,
                                 void (*destroy)(ds_stack_node_t *)) {
  FUNC(ds_stack_delete_node)(stack, node);
  if (destroy != NULL) {
    destroy(node);
  }
}

void FUNC(ds_stack_pop_destroy)(ds_stack_t *stack,
                                void (*destroy_node)(ds_stack_node_t *)) {
  if (!FUNC(ds_stack_is_empty)(stack)) {
    ds_stack_node_t *node = FUNC(ds_stack_pop)(stack);
    destroy_node(node);
  }
}

ds_stack_node_t *FUNC(ds_stack_search)(ds_stack_t *stack, ds_stack_node_t *node,
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

void FUNC(ds_stack_walk_forward)(ds_stack_t *stack, ds_stack_node_t *start_node,
                                 void (*callback)(ds_stack_node_t *)) {
  ds_stack_node_t *current;

  current = start_node != stack->nil ? start_node : stack->top;

  while (current != stack->nil) {
    callback(current);
    current = current->next;
  }
}

void FUNC(ds_stack_walk_backwards)(ds_stack_t *stack, ds_stack_node_t *current,
                                   void (*callback)(ds_stack_node_t *)) {
  if (current == stack->nil) {
    return;
  }

  FUNC(ds_stack_walk_backwards)(stack, current->next, callback);
  callback(current);
}

ds_stack_t *
FUNC(ds_stack_clone)(ds_stack_t *stack,
                     ds_stack_node_t *(*clone_node)(ds_stack_node_t *)) {
  ds_stack_t *temp_stack;
  ds_stack_t *new_stack;
  ds_stack_node_t *current;
  temp_stack =
      FUNC(ds_stack_create_alloc)(stack->allocator, stack->deallocator);
  new_stack = FUNC(ds_stack_create_alloc)(stack->allocator, stack->deallocator);
  current = stack->top;
  while (current != stack->nil) {
    ds_stack_node_t *cloned_node = clone_node(current);
    FUNC(ds_stack_push)(temp_stack, cloned_node);
    current = current->next;
  }
  while (!FUNC(ds_stack_is_empty)(temp_stack)) {
    ds_stack_node_t *node = FUNC(ds_stack_pop)(temp_stack);
    FUNC(ds_stack_push)(new_stack, node);
  }
  FUNC(ds_stack_destroy)(temp_stack, NULL);

  return new_stack;
}
