#ifndef DS_STACK_H
#define DS_STACK_H

#ifdef STACK_THREAD_SAFE
#include <pthread.h>
#include <stdbool.h>
#endif

#ifndef CAST
#define CAST(node, type) ((type *)(node))
#endif

typedef struct stack_node {
  struct stack_node *next;
} stack_node_t;

typedef struct stack {
  stack_node_t *top;
#ifdef STACK_THREAD_SAFE
  pthread_mutex_t lock;
  bool is_thread_safe;
#endif
} stack_t;

stack_t *stack_create();

void stack_push(stack_t *stack, stack_node_t *node);

stack_node_t *stack_pop(stack_t *stack);

stack_node_t *stack_peek(stack_t *stack);

int stack_is_empty(stack_t *stack);

void stack_destroy(stack_t *stack, void (*destroy_node)(stack_node_t *));

#endif // DS_STACK_H
