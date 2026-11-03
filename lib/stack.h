#ifndef DS_STACK_H
#define DS_STACK_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stack_node {
  struct stack_node *next;
} ds_stack_node_t;

typedef struct stack {
  ds_stack_node_t *top;
  ds_stack_node_t *nil;
  size_t size;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_stack_t;

ds_stack_t *FUNC(ds_stack_create)(void);
ds_stack_t *FUNC(ds_stack_create_alloc)(void *(*allocator)(size_t),
                                        void (*deallocator)(void *));
void FUNC(ds_stack_push)(ds_stack_t *stack, ds_stack_node_t *node);
ds_stack_node_t *FUNC(ds_stack_pop)(ds_stack_t *stack);
ds_stack_node_t *FUNC(ds_stack_peek)(ds_stack_t *stack);
bool FUNC(ds_stack_is_empty)(ds_stack_t *stack);
void FUNC(ds_stack_destroy)(ds_stack_t *stack,
                            void (*destroy)(ds_stack_node_t *));
void FUNC(ds_stack_delete)(ds_stack_t *stack);
void FUNC(ds_stack_delete_node)(ds_stack_t *stack, ds_stack_node_t *node);
void FUNC(ds_stack_destroy_node)(ds_stack_t *stack, ds_stack_node_t *node,
                                 void (*destroy)(ds_stack_node_t *));
void FUNC(ds_stack_pop_destroy)(ds_stack_t *stack,
                                void (*destroy_node)(ds_stack_node_t *));
ds_stack_node_t *FUNC(ds_stack_search)(ds_stack_t *stack, ds_stack_node_t *node,
                                       int (*compare)(ds_stack_node_t *,
                                                      ds_stack_node_t *));
void FUNC(ds_stack_walk_forward)(ds_stack_t *stack, ds_stack_node_t *node,
                                 void (*callback)(ds_stack_node_t *));
void FUNC(ds_stack_walk_backwards)(ds_stack_t *stack, ds_stack_node_t *node,
                                   void (*callback)(ds_stack_node_t *));
ds_stack_t *
    FUNC(ds_stack_clone)(ds_stack_t *stack,
                         ds_stack_node_t *(*clone_node)(ds_stack_node_t *));

#ifdef __cplusplus
}
#endif

#endif /* DS_STACK_H */
