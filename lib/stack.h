#ifndef DS_STACK_H
#define DS_STACK_H

#ifndef CAST
#define CAST(node, type) ((type *)(node))
#endif

typedef struct stack_node {
  struct stack_node *next;
} stack_node_t;

typedef struct stack {
  stack_node_t *top;
} stack_t;

stack_t *stack_create();

void stack_push(stack_t *stack, stack_node_t *node);

stack_node_t *stack_pop(stack_t *stack);

stack_node_t *stack_peek(stack_t *stack);

int stack_is_empty(stack_t *stack);

void stack_destroy(stack_t *stack,
                                 void (*destroy_node)(stack_node_t *));

#endif // DS_STACK_H
