#include "stack.h"
#include <stdlib.h>

stack_t *stack_create() {
  stack_t *stack = (stack_t *)malloc(sizeof(stack_t));
  stack->top = NULL;
#ifdef STACK_THREAD_SAFE
  stack->is_thread_safe = true;
  pthread_mutex_init(&stack->lock, NULL);
#endif
  return stack;
}

void stack_push(stack_t *stack, stack_node_t *node) {
#ifdef STACK_THREAD_SAFE
  if (stack->is_thread_safe) {
    pthread_mutex_lock(&stack->lock);
  }
#endif
  if (node == NULL) {
    return;
  }
  node->next = stack->top;
  stack->top = node;
#ifdef STACK_THREAD_SAFE
  if (stack->is_thread_safe) {
    pthread_mutex_unlock(&stack->lock);
  }
#endif
}

stack_node_t *stack_pop(stack_t *stack) {
#ifdef STACK_THREAD_SAFE
  if (stack->is_thread_safe) {
    pthread_mutex_lock(&stack->lock);
  }
#endif
  if (stack_is_empty(stack)) {
    return NULL;
  }
  stack_node_t *node = stack->top;
  stack->top = node->next;
#ifdef STACK_THREAD_SAFE
  if (stack->is_thread_safe) {
    pthread_mutex_unlock(&stack->lock);
  }
#endif
  return node;
}

stack_node_t *stack_peek(stack_t *stack) { return stack->top; }

int stack_is_empty(stack_t *stack) { return stack->top == NULL; }

void stack_destroy(stack_t *stack, void (*destroy_node)(stack_node_t *)) {
  stack_node_t *node = stack->top;
  while (node != NULL) {
    stack_node_t *next = node->next;
    destroy_node(node);
    node = next;
  }
#ifdef STACK_THREAD_SAFE
  pthread_mutex_destroy(&stack->lock);
#endif
  free(stack);
}
