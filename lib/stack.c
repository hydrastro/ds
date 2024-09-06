#include <stdlib.h>
#include "stack.h"

stack_t *stack_create() {
  stack_t *stack = (stack_t *)malloc(sizeof(stack_t));
  stack->top = NULL;
  return stack;
}

void stack_push(stack_t *stack, stack_node_t *node) {
  if (node == NULL) return;
  node->next = stack->top;
  stack->top = node;
}

stack_node_t *stack_pop(stack_t *stack) {
  if (stack_is_empty(stack)) {
    return NULL;
  }
  stack_node_t *node = stack->top;
  stack->top = node->next;
  return node;
}

stack_node_t *stack_peek(stack_t *stack) {
  return stack->top;
}

int stack_is_empty(stack_t *stack) {
  return stack->top == NULL;
}

void stack_destroy(stack_t *stack, void (*destroy_node)(stack_node_t *)) {
  stack_node_t *node = stack->top;
  while (node != NULL) {
    stack_node_t *next = node->next;
    destroy_node(node);
    node = next;
  }
  free(stack);
}
