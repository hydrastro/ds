#ifndef DS_RING_LIST_H
#define DS_RING_LIST_H

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ring_list_node {
  struct ring_list_node *next;
} ds_ring_list_node_t;

typedef struct ring_list {
  ds_ring_list_node_t *tail;
  ds_ring_list_node_t *nil;
  size_t size;
  ds_context_t *context; /* NULL for legacy allocator constructors */
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_ring_list_t;

/* NULL when memory runs out, or without an allocator and a deallocator */
/* Context owns all internal storage and must outlive the container. NULL
 * context selects the default. On failure *out is NULL. Existing constructors
 * retain their allocator behavior. Public layouts require recompilation. */
ds_status_t ds_ring_list_create_context(ds_context_t *context,
                                        ds_ring_list_t **out);

ds_ring_list_t *ds_ring_list_create(void);
ds_ring_list_t *ds_ring_list_create_alloc(void *(*allocator)(size_t),
                                          void (*deallocator)(void *));
void ds_ring_list_push_front(ds_ring_list_t *list, ds_ring_list_node_t *node);
void ds_ring_list_push_back(ds_ring_list_t *list, ds_ring_list_node_t *node);
ds_ring_list_node_t *ds_ring_list_pop_front(ds_ring_list_t *list);
ds_ring_list_node_t *ds_ring_list_front(ds_ring_list_t *list);
ds_ring_list_node_t *ds_ring_list_back(ds_ring_list_t *list);
void ds_ring_list_rotate(ds_ring_list_t *list);
bool ds_ring_list_is_empty(ds_ring_list_t *list);
ds_ring_list_node_t *ds_ring_list_search(ds_ring_list_t *list,
                                         ds_ring_list_node_t *node,
                                         int (*compare)(ds_ring_list_node_t *,
                                                        ds_ring_list_node_t *));
void ds_ring_list_delete_node(ds_ring_list_t *list, ds_ring_list_node_t *node);
void ds_ring_list_destroy_node(ds_ring_list_t *list, ds_ring_list_node_t *node,
                               void (*destroy)(ds_ring_list_node_t *));
void ds_ring_list_walk_forward(ds_ring_list_t *list, ds_ring_list_node_t *node,
                               void (*callback)(ds_ring_list_node_t *));
void ds_ring_list_destroy(ds_ring_list_t *list,
                          void (*destroy)(ds_ring_list_node_t *));
/* Legacy clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_ring_list_t *
ds_ring_list_clone(ds_ring_list_t *list,
                   ds_ring_list_node_t *(*clone_node)(ds_ring_list_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t
ds_ring_list_clone_ex(const ds_ring_list_t *list,
                      ds_ring_list_node_t *(*clone_node)(ds_ring_list_node_t *),
                      void (*destroy_clone)(ds_ring_list_node_t *),
                      ds_ring_list_t **out);

/* Callback policies require stable topology during calls. Views are borrowed.
 * Walk stops on the first non-DS_OK result; find leaves out unchanged on
 * absence/error. Intrusive insertions require a detached non-sentinel node.
 * Checked pop returns DS_EMPTY without changing out. */
typedef ds_status_t (*ds_ring_list_visit_fn)(ds_ring_list_node_t *, void *);
typedef int (*ds_ring_list_compare_fn)(ds_ring_list_node_t *,
                                       ds_ring_list_node_t *, void *);
typedef ds_status_t (*ds_ring_list_clone_fn)(ds_ring_list_node_t *,
                                             ds_ring_list_node_t **, void *);
typedef void (*ds_ring_list_destroy_fn)(ds_ring_list_node_t *, void *);
ds_status_t ds_ring_list_walk_with(ds_ring_list_t *container,
                                   ds_ring_list_visit_fn visit, void *user);
ds_status_t ds_ring_list_find_with(ds_ring_list_t *container,
                                   ds_ring_list_node_t *key,
                                   ds_ring_list_compare_fn compare, void *user,
                                   ds_ring_list_node_t **out);
/* NULL context inherits the source owner. Clone must return a fresh equivalent
 * node on DS_OK. A failing callback owns its unfinished node. Completed nodes
 * are rolled back via destroy; positive clone statuses map to DS_ERR_CALLBACK.
 * Output is NULL on failure. Destroy callbacks may release payloads but may
 * not inspect or mutate the container during destruction. */
ds_status_t ds_ring_list_clone_with(const ds_ring_list_t *container,
                                    ds_ring_list_clone_fn clone,
                                    ds_ring_list_destroy_fn destroy, void *user,
                                    ds_context_t *context,
                                    ds_ring_list_t **out);
void ds_ring_list_destroy_with(ds_ring_list_t *container,
                               ds_ring_list_destroy_fn destroy, void *user);
ds_status_t ds_ring_list_push_front_ex(ds_ring_list_t *container,
                                       ds_ring_list_node_t *node);
ds_status_t ds_ring_list_push_back_ex(ds_ring_list_t *container,
                                      ds_ring_list_node_t *node);
ds_status_t ds_ring_list_pop_front_ex(ds_ring_list_t *container,
                                      ds_ring_list_node_t **out);
/* Identity-based O(n) membership check before unlinking. */
ds_status_t ds_ring_list_remove_ex(ds_ring_list_t *container,
                                   ds_ring_list_node_t *target);

/* Reverse walk temporarily reverses links in O(1) extra space. Visitors
 * must not inspect/traverse/change the container or free nodes; all links are
 * restored even on a visitor stop/error. */
ds_status_t ds_ring_list_walk_reverse_with(ds_ring_list_t *container,
                                           ds_ring_list_visit_fn visit,
                                           void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_RING_LIST_H */
