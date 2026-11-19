#ifndef DS_QUEUE_H
#define DS_QUEUE_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct queue_node {
  struct queue_node *next;
} ds_queue_node_t;

typedef struct queue {
  ds_queue_node_t *head;
  ds_queue_node_t *tail;
  ds_queue_node_t *nil;
  size_t size;
  ds_context_t *context; /* borrowed allocation owner */
} ds_queue_t;

/* NULL when memory runs out */
/* Context owns all internal storage and must outlive the container. NULL
 * context selects the default. On failure *out is NULL. The context is
 * borrowed. */
ds_status_t ds_queue_create_context(ds_context_t *context, ds_queue_t **out);

ds_queue_t *ds_queue_create(ds_context_t *context);
void ds_queue_enqueue(ds_queue_t *queue, ds_queue_node_t *node);
ds_queue_node_t *ds_queue_dequeue(ds_queue_t *queue);
ds_queue_node_t *ds_queue_peek(ds_queue_t *queue);
ds_queue_node_t *ds_queue_peek_tail(ds_queue_t *queue);
bool ds_queue_is_empty(ds_queue_t *queue);
void ds_queue_destroy(ds_queue_t *queue, void (*destroy)(ds_queue_node_t *));

void ds_queue_delete(ds_queue_t *queue);
void ds_queue_delete_node(ds_queue_t *queue, ds_queue_node_t *node);
void ds_queue_destroy_node(ds_queue_t *queue, ds_queue_node_t *node,
                           void (*destroy)(ds_queue_node_t *));
void ds_queue_pop_destroy(ds_queue_t *queue,
                          void (*destroy_node)(ds_queue_node_t *));
ds_queue_node_t *ds_queue_search(ds_queue_t *queue, ds_queue_node_t *node,
                                 int (*compare)(ds_queue_node_t *,
                                                ds_queue_node_t *));
void ds_queue_walk_forward(ds_queue_t *queue, ds_queue_node_t *node,
                           void (*callback)(ds_queue_node_t *));
/* from node to the end, last first, in O(1) space: the links are reversed
 * during the walk (the callback must not inspect, traverse or change the
 * queue) and restored by the time it returns */
void ds_queue_walk_backwards(ds_queue_t *queue, ds_queue_node_t *node,
                             void (*callback)(ds_queue_node_t *));
/* Direct clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_queue_t *ds_queue_clone(ds_queue_t *queue,
                           ds_queue_node_t *(*clone_node)(ds_queue_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t
ds_queue_clone_ex(const ds_queue_t *queue,
                  ds_queue_node_t *(*clone_node)(ds_queue_node_t *),
                  void (*destroy_clone)(ds_queue_node_t *), ds_queue_t **out);

/* Callback policies require stable topology during calls. Views are borrowed.
 * Walk stops on the first non-DS_OK result; find leaves out unchanged on
 * absence/error. Intrusive insertions require a detached non-sentinel node.
 * Checked pop returns DS_EMPTY without changing out. */
typedef ds_status_t (*ds_queue_visit_fn)(ds_queue_node_t *, void *);
typedef int (*ds_queue_compare_fn)(ds_queue_node_t *, ds_queue_node_t *,
                                   void *);
typedef ds_status_t (*ds_queue_clone_fn)(ds_queue_node_t *, ds_queue_node_t **,
                                         void *);
typedef void (*ds_queue_destroy_fn)(ds_queue_node_t *, void *);
ds_status_t ds_queue_walk_with(ds_queue_t *container, ds_queue_visit_fn visit,
                               void *user);
ds_status_t ds_queue_find_with(ds_queue_t *container, ds_queue_node_t *key,
                               ds_queue_compare_fn compare, void *user,
                               ds_queue_node_t **out);
/* NULL context inherits the source owner. Clone must return a fresh equivalent
 * node on DS_OK. A failing callback owns its unfinished node. Completed nodes
 * are rolled back via destroy; positive clone statuses map to DS_ERR_CALLBACK.
 * Output is NULL on failure. Destroy callbacks may release payloads but may
 * not inspect or mutate the container during destruction. */
ds_status_t ds_queue_clone_with(const ds_queue_t *container,
                                ds_queue_clone_fn clone,
                                ds_queue_destroy_fn destroy, void *user,
                                ds_context_t *context, ds_queue_t **out);
void ds_queue_destroy_with(ds_queue_t *container, ds_queue_destroy_fn destroy,
                           void *user);
ds_status_t ds_queue_enqueue_ex(ds_queue_t *container, ds_queue_node_t *node);
ds_status_t ds_queue_dequeue_ex(ds_queue_t *container, ds_queue_node_t **out);
/* Identity-based O(n) membership check before unlinking. */
ds_status_t ds_queue_remove_ex(ds_queue_t *container, ds_queue_node_t *target);

/* Reverse walk temporarily reverses links in O(1) extra space. Visitors
 * must not inspect/traverse/change the container or free nodes; all links are
 * restored even on a visitor stop/error. */
ds_status_t ds_queue_walk_reverse_with(ds_queue_t *container,
                                       ds_queue_visit_fn visit, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_QUEUE_H */
