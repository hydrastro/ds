#ifndef DS_STACK_H
#define DS_STACK_H

#include "common.h"
#include "status.h"
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

/* NULL when memory runs out, or without an allocator and a deallocator */
ds_stack_t *ds_stack_create(void);
ds_stack_t *ds_stack_create_alloc(void *(*allocator)(size_t),
                                  void (*deallocator)(void *));
void ds_stack_push(ds_stack_t *stack, ds_stack_node_t *node);
ds_stack_node_t *ds_stack_pop(ds_stack_t *stack);
ds_stack_node_t *ds_stack_peek(ds_stack_t *stack);
bool ds_stack_is_empty(ds_stack_t *stack);
void ds_stack_destroy(ds_stack_t *stack, void (*destroy)(ds_stack_node_t *));
void ds_stack_delete(ds_stack_t *stack);
void ds_stack_delete_node(ds_stack_t *stack, ds_stack_node_t *node);
void ds_stack_destroy_node(ds_stack_t *stack, ds_stack_node_t *node,
                           void (*destroy)(ds_stack_node_t *));
void ds_stack_pop_destroy(ds_stack_t *stack,
                          void (*destroy_node)(ds_stack_node_t *));
ds_stack_node_t *ds_stack_search(ds_stack_t *stack, ds_stack_node_t *node,
                                 int (*compare)(ds_stack_node_t *,
                                                ds_stack_node_t *));
void ds_stack_walk_forward(ds_stack_t *stack, ds_stack_node_t *node,
                           void (*callback)(ds_stack_node_t *));
/* from node to the end, last first, in O(1) space: the links are reversed
 * during the walk (the callback must not inspect, traverse or change the
 * stack) and restored by the time it returns */
void ds_stack_walk_backwards(ds_stack_t *stack, ds_stack_node_t *node,
                             void (*callback)(ds_stack_node_t *));
/* Legacy clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_stack_t *ds_stack_clone(ds_stack_t *stack,
                           ds_stack_node_t *(*clone_node)(ds_stack_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t ds_stack_clone_ex(const ds_stack_t *stack,
                              ds_stack_node_t *(*clone_node)(ds_stack_node_t *),
                              void (*destroy_clone)(ds_stack_node_t *),
                              ds_stack_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_STACK_H */
