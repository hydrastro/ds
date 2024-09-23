#include "stack.h"
#include <stdlib.h>

stack_t *stack_create() {
  stack_t *stack = (stack_t *)malloc(sizeof(stack_t));
  stack->nil = (stack_node_t *)malloc(sizeof(stack_t));
  stack->nil->next = stack->nil;
  stack->top = stack->nil;
  stack->size = 0;
#ifdef STACK_THREAD_SAFE
  LOCK_INIT_RECURSIVE(stack)
#endif
  return stack;
}

void stack_push(stack_t *stack, stack_node_t *node) {
#ifdef STACK_THREAD_SAFE
  LOCK(stack)
#endif
  if (node == stack->nil) {
    return;
  }
  node->next = stack->top;
  stack->top = node;
  stack->size += 1;
#ifdef STACK_THREAD_SAFE
  UNLOCK(stack)
#endif
}

stack_node_t *stack_pop(stack_t *stack) {
#ifdef STACK_THREAD_SAFE
  LOCK(stack)
#endif
  if (stack_is_empty(stack)) {
    return stack->nil;
  }
  stack_node_t *node = stack->top;
  stack->top = node->next;
  stack->size -= 1;
#ifdef STACK_THREAD_SAFE
  UNLOCK(stack)
#endif
  return node;
}

stack_node_t *stack_peek(stack_t *stack) {
#ifdef STACK_THREAD_SAFE
  LOCK(stack)
#endif
  stack_node_t *result = stack->top;
#ifdef STACK_THREAD_SAFE
  LOCK(stack)
#endif
  return result;
}

bool stack_is_empty(stack_t *stack) {
#ifdef STACK_THREAD_SAFE
  LOCK(stack)
#endif
  bool result = stack->top == stack->nil;
#ifdef STACK_THREAD_SAFE
  UNLOCK(stack)
  return result;
#endif
}

void stack_destroy(stack_t *stack, void (*destroy)(stack_node_t *)) {
  stack_node_t *node = stack->top;
  while (node != stack->nil) {
    stack_node_t *next = node->next;
    destroy(node);
    node = next;
  }
  stack->size = 0;
#ifdef STACK_THREAD_SAFE
  LOCK_DESTROY(stack)
#endif
  free(stack);
}

void stack_delete(stack_t *stack) {
#ifdef STACK_THREAD_SAFE
  LOCK(stack)
#endif

  stack->top = stack->nil;
  stack->size = 0;

#ifdef STACK_THREAD_SAFE
  UNLOCK(stack)
#endif
}

void stack_delete_node(stack_t *stack, stack_node_t *node) {
#ifdef STACK_THREAD_SAFE
  LOCK(stack)
#endif

  stack_node_t *current = stack->top;
  stack_node_t *previous = stack->nil;

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
  }

#ifdef STACK_THREAD_SAFE
  UNLOCK(stack)
#endif
}

void stack_destroy_node(stack_t *stack, stack_node_t *node,
                        void (*destroy)(stack_node_t *)) {
#ifdef STACK_THREAD_SAFE
  LOCK(stack)
#endif

  stack_delete_node(stack, node);
  if (destroy != NULL) {
    destroy(node);
  }

#ifdef STACK_THREAD_SAFE
  UNLOCK(stack)
#endif
}

void stack_pop_destroy(stack_t *stack, void (*destroy_node)(stack_node_t *)) {
#ifdef STACK_THREAD_SAFE
  LOCK(stack)
#endif

  if (!stack_is_empty(stack)) {
    stack_node_t *node = stack_pop(stack);
    destroy_node(node);
  }

#ifdef STACK_THREAD_SAFE
  UNLOCK(stack)
#endif
}

stack_node_t *stack_search(stack_t *stack, stack_node_t *node,
                           int (*compare)(stack_node_t *, stack_node_t *)) {
#ifdef STACK_THREAD_SAFE
  LOCK(stack)
#endif

  stack_node_t *current = stack->top;

  while (current != stack->nil) {
    if (compare(current, node) == 0) {
#ifdef STACK_THREAD_SAFE
      UNLOCK(stack)
#endif
      return current;
    }
    current = current->next;
  }

#ifdef STACK_THREAD_SAFE
  UNLOCK(stack)
#endif

  return stack->nil;
}

void stack_walk_forward(stack_t *stack, stack_node_t *start_node,
                        void (*callback)(stack_node_t *)) {
#ifdef STACK_THREAD_SAFE
  LOCK(stack)
#endif

  stack_node_t *current = start_node != stack->nil ? start_node : stack->top;

  while (current != stack->nil) {
    callback(current);
    current = current->next;
  }

#ifdef STACK_THREAD_SAFE
  UNLOCK(stack)
#endif
}

void stack_walk_backwards(stack_t *stack, stack_node_t *current,
                          void (*callback)(stack_node_t *)) {
#ifdef STACK_THREAD_SAFE
  LOCK(stack)
#endif

  if (current == stack->nil) {
    return;
  }

  stack_walk_backwards(stack, current->next, callback);
  callback(current);

#ifdef STACK_THREAD_SAFE
  UNLOCK(stack)
#endif
}
