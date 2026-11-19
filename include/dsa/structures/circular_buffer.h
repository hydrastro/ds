#ifndef DS_CIRCULAR_BUFFER_H
#define DS_CIRCULAR_BUFFER_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct circular_buffer_node {
  struct circular_buffer_node *placeholder;
} ds_circular_buffer_node_t;

typedef struct circular_buffer {
  ds_circular_buffer_node_t **data;
  size_t head;
  size_t tail;
  size_t size;
  size_t capacity;
  bool overwrite;
  ds_circular_buffer_node_t *nil;
  ds_context_t *context; /* borrowed allocation owner */
} ds_circular_buffer_t;

/* NULL when memory runs out, for a capacity whose slot array cannot be
 * sized */

ds_circular_buffer_t *ds_circular_buffer_create(size_t capacity,
                                                bool overwrite,
                                                ds_context_t *context);
bool ds_circular_buffer_is_empty(ds_circular_buffer_t *buffer);
bool ds_circular_buffer_is_full(ds_circular_buffer_t *buffer);
bool ds_circular_buffer_push(ds_circular_buffer_t *buffer,
                             ds_circular_buffer_node_t *node);
/* Checked push reports DS_ERR_OVERFLOW when full and overwrite is disabled.
 * On success out_evicted (optional) receives the displaced node, or NULL.
 * Checked pop returns DS_EMPTY without changing *out. Payloads remain owned
 * by the caller; reporting an evicted node lets it release that payload. */
ds_status_t
ds_circular_buffer_push_ex(ds_circular_buffer_t *buffer,
                           ds_circular_buffer_node_t *node,
                           ds_circular_buffer_node_t **out_evicted);
ds_status_t ds_circular_buffer_pop_ex(ds_circular_buffer_t *buffer,
                                      ds_circular_buffer_node_t **out);
ds_circular_buffer_node_t *
ds_circular_buffer_pop(ds_circular_buffer_t *buffer);
ds_circular_buffer_node_t *
ds_circular_buffer_peek(ds_circular_buffer_t *buffer);
ds_circular_buffer_node_t *
ds_circular_buffer_peek_tail(ds_circular_buffer_t *buffer);
ds_circular_buffer_node_t *ds_circular_buffer_at(ds_circular_buffer_t *buffer,
                                                 size_t index);
void ds_circular_buffer_walk_forward(
    ds_circular_buffer_t *buffer,
    void (*callback)(ds_circular_buffer_node_t *));
void ds_circular_buffer_clear(ds_circular_buffer_t *buffer);
void ds_circular_buffer_destroy(ds_circular_buffer_t *buffer,
                                void (*destroy)(ds_circular_buffer_node_t *));
/* Direct clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_circular_buffer_t *ds_circular_buffer_clone(
    ds_circular_buffer_t *buffer,
    ds_circular_buffer_node_t *(*clone_node)(ds_circular_buffer_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t ds_circular_buffer_clone_ex(
    const ds_circular_buffer_t *buffer,
    ds_circular_buffer_node_t *(*clone_node)(ds_circular_buffer_node_t *),
    void (*destroy_clone)(ds_circular_buffer_node_t *),
    ds_circular_buffer_t **out);

/* Context-owned internal storage; NULL context chooses the default.
 * The context must outlive the container. On failure *out is NULL. */
ds_status_t ds_circular_buffer_create_context(size_t capacity, bool overwrite,
                                              ds_context_t *context,
                                              ds_circular_buffer_t **out);

/* Callback policies require stable topology during calls. Views are borrowed.
 * Walk stops on the first non-DS_OK result; find leaves out unchanged on
 * absence/error. Intrusive insertions require a detached non-sentinel node.
 * Checked pop returns DS_EMPTY without changing out. */
typedef ds_status_t (*ds_circular_buffer_visit_fn)(ds_circular_buffer_node_t *,
                                                   void *);
typedef int (*ds_circular_buffer_compare_fn)(ds_circular_buffer_node_t *,
                                             ds_circular_buffer_node_t *,
                                             void *);
typedef ds_status_t (*ds_circular_buffer_clone_fn)(
    ds_circular_buffer_node_t *, ds_circular_buffer_node_t **, void *);
typedef void (*ds_circular_buffer_destroy_fn)(ds_circular_buffer_node_t *,
                                              void *);
ds_status_t ds_circular_buffer_walk_with(ds_circular_buffer_t *container,
                                         ds_circular_buffer_visit_fn visit,
                                         void *user);
ds_status_t ds_circular_buffer_find_with(ds_circular_buffer_t *container,
                                         ds_circular_buffer_node_t *key,
                                         ds_circular_buffer_compare_fn compare,
                                         void *user,
                                         ds_circular_buffer_node_t **out);
/* NULL context inherits the source owner. Clone must return a fresh equivalent
 * node on DS_OK. A failing callback owns its unfinished node. Completed nodes
 * are rolled back via destroy; positive clone statuses map to DS_ERR_CALLBACK.
 * Output is NULL on failure. Destroy callbacks may release payloads but may
 * not inspect or mutate the container during destruction. */
ds_status_t ds_circular_buffer_clone_with(
    const ds_circular_buffer_t *container, ds_circular_buffer_clone_fn clone,
    ds_circular_buffer_destroy_fn destroy, void *user, ds_context_t *context,
    ds_circular_buffer_t **out);
void ds_circular_buffer_destroy_with(ds_circular_buffer_t *container,
                                     ds_circular_buffer_destroy_fn destroy,
                                     void *user);

/* Reverse traversal visits occupied slots in reverse walk_with order. */
ds_status_t
ds_circular_buffer_walk_reverse_with(ds_circular_buffer_t *container,
                                     ds_circular_buffer_visit_fn visit,
                                     void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_CIRCULAR_BUFFER_H */
