#ifndef DS_STACK_H
#define DS_STACK_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef STACK_THREAD_SAFE
#include <pthread.h>
#endif

typedef struct stack_node {
  struct stack_node *next;
} stack_node_t;

typedef struct stack {
  stack_node_t *top;
  stack_node_t *nil;
  size_t size;
#ifdef STACK_THREAD_SAFE
  pthread_mutex_t lock;
  bool is_thread_safe;
#endif
} stack_t;

stack_t *stack_create(void);

void stack_push(stack_t *stack, stack_node_t *node);

stack_node_t *stack_pop(stack_t *stack);

stack_node_t *stack_peek(stack_t *stack);

bool stack_is_empty(stack_t *stack);

void stack_destroy(stack_t *stack, void (*destroy)(stack_node_t *));

void stack_delete(stack_t *stack);
void stack_delete_node(stack_t *stack, stack_node_t *node);
void stack_destroy_node(stack_t *stack, stack_node_t *node,
                        void (*destroy)(stack_node_t *));
void stack_pop_destroy(stack_t *stack, void (*destroy_node)(stack_node_t *));
stack_node_t *stack_search(stack_t *stack, stack_node_t *node,
                           int (*compare)(stack_node_t *, stack_node_t *));
void stack_walk_forward(stack_t *stack, stack_node_t *node,
                        void (*callback)(stack_node_t *));
void stack_walk_backwards(stack_t *stack, stack_node_t *node,
                          void (*callback)(stack_node_t *));

#endif // DS_STACK_H
