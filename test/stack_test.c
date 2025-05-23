#include "../lib/stack.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct int_node {
  ds_stack_node_t node;
  int value;
} int_node_t;

int_node_t *create_int_node(int value) {
  int_node_t *new_node = (int_node_t *)malloc(sizeof(int_node_t));
  new_node->value = value;
  return new_node;
}

void destroy_node(ds_stack_node_t *node) { free(node); }

ds_stack_node_t *clone_node(ds_stack_node_t *node) {
  int_node_t *new_node = (int_node_t *)malloc(sizeof(int_node_t));
  new_node->value = (CAST(node, int_node_t))->value;
  return &new_node->node;
}

int main(void) {
  ds_stack_t *stack = stack_create();

  for (int i = 1; i <= 5; ++i) {
    int_node_t *new_node = create_int_node(i);
    printf("push %d\n", new_node->value);
    stack_push(stack, &new_node->node);
  }

  ds_stack_t *new_stack = stack_clone(stack, clone_node);

  int_node_t *top = CAST(stack_peek(stack), int_node_t);
  printf("stack top: %d\n", top->value);

  while (!stack_is_empty(stack)) {
    int_node_t *node = CAST(stack_pop(stack), int_node_t);
    printf("pop %d\n", node->value);
    destroy_node(&node->node);
  }

  printf("empty? %s\n", stack_is_empty(stack) ? "yes" : "no");

  stack_destroy(stack, destroy_node);

  top = CAST(stack_peek(new_stack), int_node_t);
  printf("stack top: %d\n", top->value);

  while (!stack_is_empty(new_stack)) {
    int_node_t *node = CAST(stack_pop(new_stack), int_node_t);
    printf("pop %d\n", node->value);
    destroy_node(&node->node);
  }

  printf("empty? %s\n", stack_is_empty(new_stack) ? "yes" : "no");

  stack_destroy(new_stack, destroy_node);

  return 0;
}
