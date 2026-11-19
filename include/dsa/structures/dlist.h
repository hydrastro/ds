#ifndef DS_DLIST_H
#define DS_DLIST_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dlist_node {
  struct dlist_node *next;
  struct dlist_node *prev;
} ds_dlist_node_t;

typedef struct dlist {
  size_t size;
  ds_dlist_node_t *head;
  ds_dlist_node_t *tail;
  ds_dlist_node_t *nil;
  ds_context_t *context; /* borrowed allocation owner */
} ds_dlist_t;

/* NULL when memory runs out */
/* Context owns all internal storage and must outlive the container. NULL
 * context selects the default. On failure *out is NULL. The context is
 * borrowed. */
ds_status_t ds_dlist_create_context(ds_context_t *context, ds_dlist_t **out);

ds_dlist_t *ds_dlist_create(ds_context_t *context);
void ds_dlist_append(ds_dlist_t *list, ds_dlist_node_t *node);

void ds_dlist_prepend(ds_dlist_t *list, ds_dlist_node_t *node);
ds_dlist_node_t *ds_dlist_search(ds_dlist_t *list, ds_dlist_node_t *node,
                                 int (*compare)(ds_dlist_node_t *,
                                                ds_dlist_node_t *));
void ds_dlist_insert_before(ds_dlist_t *list, ds_dlist_node_t *node,
                            ds_dlist_node_t *next);
void ds_dlist_insert_after(ds_dlist_t *list, ds_dlist_node_t *node,
                           ds_dlist_node_t *prev);
void ds_dlist_delete_node(ds_dlist_t *list, ds_dlist_node_t *node);
void ds_dlist_delete(ds_dlist_t *list);
void ds_dlist_destroy_node(ds_dlist_t *list, ds_dlist_node_t *node,
                           void (*destroy)(ds_dlist_node_t *));
void ds_dlist_destroy(ds_dlist_t *list, void (*destroy)(ds_dlist_node_t *));
void ds_dlist_walk_forward(ds_dlist_t *list, ds_dlist_node_t *node,
                           void (*callback)(ds_dlist_node_t *));
void ds_dlist_walk_backwards(ds_dlist_t *list, ds_dlist_node_t *node,
                             void (*callback)(ds_dlist_node_t *));
bool ds_dlist_is_empty(ds_dlist_t *);
/* Direct clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_dlist_t *ds_dlist_clone(ds_dlist_t *list,
                           ds_dlist_node_t *(*clone_node)(ds_dlist_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t
ds_dlist_clone_ex(const ds_dlist_t *list,
                  ds_dlist_node_t *(*clone_node)(ds_dlist_node_t *),
                  void (*destroy_clone)(ds_dlist_node_t *), ds_dlist_t **out);

/* Callback policies require stable topology during calls. Views are borrowed.
 * Walk stops on the first non-DS_OK result; find leaves out unchanged on
 * absence/error. Intrusive insertions require a detached non-sentinel node.
 * Checked pop returns DS_EMPTY without changing out. */
typedef ds_status_t (*ds_dlist_visit_fn)(ds_dlist_node_t *, void *);
typedef int (*ds_dlist_compare_fn)(ds_dlist_node_t *, ds_dlist_node_t *,
                                   void *);
typedef ds_status_t (*ds_dlist_clone_fn)(ds_dlist_node_t *, ds_dlist_node_t **,
                                         void *);
typedef void (*ds_dlist_destroy_fn)(ds_dlist_node_t *, void *);
ds_status_t ds_dlist_walk_with(ds_dlist_t *container, ds_dlist_visit_fn visit,
                               void *user);
ds_status_t ds_dlist_find_with(ds_dlist_t *container, ds_dlist_node_t *key,
                               ds_dlist_compare_fn compare, void *user,
                               ds_dlist_node_t **out);
/* NULL context inherits the source owner. Clone must return a fresh equivalent
 * node on DS_OK. A failing callback owns its unfinished node. Completed nodes
 * are rolled back via destroy; positive clone statuses map to DS_ERR_CALLBACK.
 * Output is NULL on failure. Destroy callbacks may release payloads but may
 * not inspect or mutate the container during destruction. */
ds_status_t ds_dlist_clone_with(const ds_dlist_t *container,
                                ds_dlist_clone_fn clone,
                                ds_dlist_destroy_fn destroy, void *user,
                                ds_context_t *context, ds_dlist_t **out);
void ds_dlist_destroy_with(ds_dlist_t *container, ds_dlist_destroy_fn destroy,
                           void *user);
ds_status_t ds_dlist_append_ex(ds_dlist_t *container, ds_dlist_node_t *node);
ds_status_t ds_dlist_prepend_ex(ds_dlist_t *container, ds_dlist_node_t *node);
/* Identity-based O(n) membership check before unlinking. */
ds_status_t ds_dlist_remove_ex(ds_dlist_t *container, ds_dlist_node_t *target);

/* Reverse traversal has the same callback contract as walk_with. */
ds_status_t ds_dlist_walk_reverse_with(ds_dlist_t *container,
                                       ds_dlist_visit_fn visit, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_DLIST_H */
