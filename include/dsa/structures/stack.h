#ifndef DS_STACK_H
#define DS_STACK_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
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
  ds_context_t *context; /* borrowed allocation owner */
} ds_stack_t;

/* NULL when memory runs out */
/* Context owns all internal storage and must outlive the container. NULL
 * context selects the default. On failure *out is NULL. The context is
 * borrowed. */
ds_status_t ds_stack_create_context(ds_context_t *context, ds_stack_t **out);

ds_stack_t *ds_stack_create(ds_context_t *context);
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
/* Direct clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_stack_t *ds_stack_clone(ds_stack_t *stack,
                           ds_stack_node_t *(*clone_node)(ds_stack_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t
ds_stack_clone_ex(const ds_stack_t *stack,
                  ds_stack_node_t *(*clone_node)(ds_stack_node_t *),
                  void (*destroy_clone)(ds_stack_node_t *), ds_stack_t **out);

/* Callback policies require stable topology during calls. Views are borrowed.
 * Walk stops on the first non-DS_OK result; find leaves out unchanged on
 * absence/error. Intrusive insertions require a detached non-sentinel node.
 * Checked pop returns DS_EMPTY without changing out. */
typedef ds_status_t (*ds_stack_visit_fn)(ds_stack_node_t *, void *);
typedef int (*ds_stack_compare_fn)(ds_stack_node_t *, ds_stack_node_t *,
                                   void *);
typedef ds_status_t (*ds_stack_clone_fn)(ds_stack_node_t *, ds_stack_node_t **,
                                         void *);
typedef void (*ds_stack_destroy_fn)(ds_stack_node_t *, void *);
ds_status_t ds_stack_walk_with(ds_stack_t *container, ds_stack_visit_fn visit,
                               void *user);
ds_status_t ds_stack_find_with(ds_stack_t *container, ds_stack_node_t *key,
                               ds_stack_compare_fn compare, void *user,
                               ds_stack_node_t **out);
/* NULL context inherits the source owner. Clone must return a fresh equivalent
 * node on DS_OK. A failing callback owns its unfinished node. Completed nodes
 * are rolled back via destroy; positive clone statuses map to DS_ERR_CALLBACK.
 * Output is NULL on failure. Destroy callbacks may release payloads but may
 * not inspect or mutate the container during destruction. */
ds_status_t ds_stack_clone_with(const ds_stack_t *container,
                                ds_stack_clone_fn clone,
                                ds_stack_destroy_fn destroy, void *user,
                                ds_context_t *context, ds_stack_t **out);
void ds_stack_destroy_with(ds_stack_t *container, ds_stack_destroy_fn destroy,
                           void *user);
ds_status_t ds_stack_push_ex(ds_stack_t *container, ds_stack_node_t *node);
ds_status_t ds_stack_pop_ex(ds_stack_t *container, ds_stack_node_t **out);
/* Identity-based O(n) membership check before unlinking. */
ds_status_t ds_stack_remove_ex(ds_stack_t *container, ds_stack_node_t *target);

/* Reverse walk temporarily reverses links in O(1) extra space. Visitors
 * must not inspect/traverse/change the container or free nodes; all links are
 * restored even on a visitor stop/error. */
ds_status_t ds_stack_walk_reverse_with(ds_stack_t *container,
                                       ds_stack_visit_fn visit, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_STACK_H */
