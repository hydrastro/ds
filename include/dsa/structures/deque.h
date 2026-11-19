#ifndef DS_DEQUE_H
#define DS_DEQUE_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct deque_node {
  struct deque_node *next;
  struct deque_node *prev;
} ds_deque_node_t;

typedef struct deque {
  ds_deque_node_t *head;
  ds_deque_node_t *tail;
  ds_deque_node_t *nil;
  size_t size;
  ds_context_t *context; /* borrowed allocation owner */
} ds_deque_t;

/* NULL when memory runs out */
/* Context owns all internal storage and must outlive the container. NULL
 * context selects the default. On failure *out is NULL. The context is
 * borrowed. */
ds_status_t ds_deque_create_context(ds_context_t *context, ds_deque_t **out);

ds_deque_t *ds_deque_create(ds_context_t *context);
void ds_deque_push_front(ds_deque_t *deque, ds_deque_node_t *node);
void ds_deque_push_back(ds_deque_t *deque, ds_deque_node_t *node);
ds_deque_node_t *ds_deque_pop_front(ds_deque_t *deque);
ds_deque_node_t *ds_deque_pop_back(ds_deque_t *deque);
ds_deque_node_t *ds_deque_peek_front(ds_deque_t *deque);
ds_deque_node_t *ds_deque_peek_back(ds_deque_t *deque);
bool ds_deque_is_empty(ds_deque_t *deque);
void ds_deque_destroy(ds_deque_t *deque, void (*destroy)(ds_deque_node_t *));

void ds_deque_delete(ds_deque_t *deque);
void ds_deque_delete_node(ds_deque_t *deque, ds_deque_node_t *node);
void ds_deque_destroy_node(ds_deque_t *deque, ds_deque_node_t *node,
                           void (*destroy)(ds_deque_node_t *));
void ds_deque_pop_front_destroy(ds_deque_t *deque,
                                void (*destroy)(ds_deque_node_t *));
void ds_deque_pop_back_destroy(ds_deque_t *deque,
                               void (*destroy)(ds_deque_node_t *));
ds_deque_node_t *ds_deque_search(ds_deque_t *deque, ds_deque_node_t *node,
                                 int (*compare)(ds_deque_node_t *,
                                                ds_deque_node_t *));
void ds_deque_walk_forward(ds_deque_t *deque, ds_deque_node_t *node,
                           void (*callback)(ds_deque_node_t *));
void ds_deque_walk_backwards(ds_deque_t *deque, ds_deque_node_t *node,
                             void (*callback)(ds_deque_node_t *));
/* Direct clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_deque_t *ds_deque_clone(ds_deque_t *deque,
                           ds_deque_node_t *(*clone_node)(ds_deque_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t
ds_deque_clone_ex(const ds_deque_t *deque,
                  ds_deque_node_t *(*clone_node)(ds_deque_node_t *),
                  void (*destroy_clone)(ds_deque_node_t *), ds_deque_t **out);

/* Callback policies require stable topology during calls. Views are borrowed.
 * Walk stops on the first non-DS_OK result; find leaves out unchanged on
 * absence/error. Intrusive insertions require a detached non-sentinel node.
 * Checked pop returns DS_EMPTY without changing out. */
typedef ds_status_t (*ds_deque_visit_fn)(ds_deque_node_t *, void *);
typedef int (*ds_deque_compare_fn)(ds_deque_node_t *, ds_deque_node_t *,
                                   void *);
typedef ds_status_t (*ds_deque_clone_fn)(ds_deque_node_t *, ds_deque_node_t **,
                                         void *);
typedef void (*ds_deque_destroy_fn)(ds_deque_node_t *, void *);
ds_status_t ds_deque_walk_with(ds_deque_t *container, ds_deque_visit_fn visit,
                               void *user);
ds_status_t ds_deque_find_with(ds_deque_t *container, ds_deque_node_t *key,
                               ds_deque_compare_fn compare, void *user,
                               ds_deque_node_t **out);
/* NULL context inherits the source owner. Clone must return a fresh equivalent
 * node on DS_OK. A failing callback owns its unfinished node. Completed nodes
 * are rolled back via destroy; positive clone statuses map to DS_ERR_CALLBACK.
 * Output is NULL on failure. Destroy callbacks may release payloads but may
 * not inspect or mutate the container during destruction. */
ds_status_t ds_deque_clone_with(const ds_deque_t *container,
                                ds_deque_clone_fn clone,
                                ds_deque_destroy_fn destroy, void *user,
                                ds_context_t *context, ds_deque_t **out);
void ds_deque_destroy_with(ds_deque_t *container, ds_deque_destroy_fn destroy,
                           void *user);
ds_status_t ds_deque_push_front_ex(ds_deque_t *container,
                                   ds_deque_node_t *node);
ds_status_t ds_deque_push_back_ex(ds_deque_t *container,
                                  ds_deque_node_t *node);
ds_status_t ds_deque_pop_front_ex(ds_deque_t *container,
                                  ds_deque_node_t **out);
ds_status_t ds_deque_pop_back_ex(ds_deque_t *container, ds_deque_node_t **out);
/* Identity-based O(n) membership check before unlinking. */
ds_status_t ds_deque_remove_ex(ds_deque_t *container, ds_deque_node_t *target);

/* Reverse traversal has the same callback contract as walk_with. */
ds_status_t ds_deque_walk_reverse_with(ds_deque_t *container,
                                       ds_deque_visit_fn visit, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_DEQUE_H */
