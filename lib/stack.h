#ifndef DS_STACK_H
#define DS_STACK_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct stack_node {
  struct stack_node *next;
} stack_node_t;

typedef struct stack {
  stack_node_t *top;
  stack_node_t *nil;
  size_t size;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} stack_t;

stack_t *FUNC(stack_create)(void);
stack_t *FUNC(stack_create_alloc)(void *(*allocator)(size_t),
                                  void (*deallocator)(void *));
void FUNC(stack_push)(stack_t *stack, stack_node_t *node);
stack_node_t *FUNC(stack_pop)(stack_t *stack);
stack_node_t *FUNC(stack_peek)(stack_t *stack);
bool FUNC(stack_is_empty)(stack_t *stack);
void FUNC(stack_destroy)(stack_t *stack, void (*destroy)(stack_node_t *));
void FUNC(stack_delete)(stack_t *stack);
void FUNC(stack_delete_node)(stack_t *stack, stack_node_t *node);
void FUNC(stack_destroy_node)(stack_t *stack, stack_node_t *node,
                              void (*destroy)(stack_node_t *));
void FUNC(stack_pop_destroy)(stack_t *stack,
                             void (*destroy_node)(stack_node_t *));
stack_node_t *FUNC(stack_search)(stack_t *stack, stack_node_t *node,
                                 int (*compare)(stack_node_t *,
                                                stack_node_t *));
void FUNC(stack_walk_forward)(stack_t *stack, stack_node_t *node,
                              void (*callback)(stack_node_t *));
void FUNC(stack_walk_backwards)(stack_t *stack, stack_node_t *node,
                                void (*callback)(stack_node_t *));
stack_t *FUNC(stack_clone)(stack_t *stack,
                           stack_node_t *(*clone_node)(stack_node_t *));

#endif /* DS_STACK_H */
