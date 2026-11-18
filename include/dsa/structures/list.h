#ifndef DS_LIST_H
#define DS_LIST_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct list_node {
  struct list_node *next;
} ds_list_node_t;

typedef struct linked_list {
  ds_list_node_t *head;
  ds_list_node_t *tail;
  ds_list_node_t *nil;
  size_t size;
  ds_context_t *context; /* NULL for allocator-pair constructors */
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_list_t;

/* NULL on allocation failure (or a missing allocator/deallocator). */
/* Context owns all internal storage and must outlive the container. NULL
 * context selects the default. On failure *out is NULL. The context is
 * borrowed. */
ds_status_t ds_list_create_context(ds_context_t *context, ds_list_t **out);

ds_list_t *ds_list_create(void);
ds_list_t *ds_list_create_alloc(void *(*allocator)(size_t),
                                void (*deallocator)(void *));
void ds_list_append(ds_list_t *list, ds_list_node_t *node);
void ds_list_prepend(ds_list_t *list, ds_list_node_t *node);
ds_list_node_t *ds_list_search(ds_list_t *list, ds_list_node_t *node,
                               int (*compare)(ds_list_node_t *,
                                              ds_list_node_t *));
void ds_list_insert_before(ds_list_t *list, ds_list_node_t *node,
                           ds_list_node_t *next);
void ds_list_insert_after(ds_list_t *list, ds_list_node_t *node,
                          ds_list_node_t *prev);
void ds_list_delete_node(ds_list_t *list, ds_list_node_t *node);
void ds_list_delete(ds_list_t *list);
void ds_list_destroy_node(ds_list_t *list, ds_list_node_t *node,
                          void (*destroy)(ds_list_node_t *));
void ds_list_destroy(ds_list_t *list, void (*destroy)(ds_list_node_t *));
void ds_list_walk_forward(ds_list_t *list, ds_list_node_t *node,
                          void (*callback)(ds_list_node_t *));
/* Temporarily reverses links: callback must not inspect/traverse/mutate
 * the list or free its nodes. Links are restored when the walk returns. */
void ds_list_walk_backwards(ds_list_t *list, ds_list_node_t *node,
                            void (*callback)(ds_list_node_t *));
bool ds_list_is_empty(ds_list_t *list);
/* Direct clone: NULL is returned if allocating the list or sentinel
 * fails, or when clone_node returns NULL; the clones it made before are
 * then not released. Use clone_ex for fallible cloning and caller-defined
 * rollback. */
ds_list_t *ds_list_clone(ds_list_t *list,
                         ds_list_node_t *(*clone_node)(ds_list_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back completed
 * clones through destroy_clone and returns DS_ERR_CALLBACK. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t ds_list_clone_ex(const ds_list_t *list,
                             ds_list_node_t *(*clone_node)(ds_list_node_t *),
                             void (*destroy_clone)(ds_list_node_t *),
                             ds_list_t **out);

/* Callback policies require stable topology during calls. Views are borrowed.
 * Walk stops on the first non-DS_OK result; find leaves out unchanged on
 * absence/error. Intrusive insertions require a detached non-sentinel node.
 * Checked pop returns DS_EMPTY without changing out. */
typedef ds_status_t (*ds_list_visit_fn)(ds_list_node_t *, void *);
typedef int (*ds_list_compare_fn)(ds_list_node_t *, ds_list_node_t *, void *);
typedef ds_status_t (*ds_list_clone_fn)(ds_list_node_t *, ds_list_node_t **,
                                        void *);
typedef void (*ds_list_destroy_fn)(ds_list_node_t *, void *);
ds_status_t ds_list_walk_with(ds_list_t *container, ds_list_visit_fn visit,
                              void *user);
ds_status_t ds_list_find_with(ds_list_t *container, ds_list_node_t *key,
                              ds_list_compare_fn compare, void *user,
                              ds_list_node_t **out);
/* NULL context inherits the source owner. Clone must return a fresh equivalent
 * node on DS_OK. A failing callback owns its unfinished node. Completed nodes
 * are rolled back via destroy; positive clone statuses map to DS_ERR_CALLBACK.
 * Output is NULL on failure. Destroy callbacks may release payloads but may
 * not inspect or mutate the container during destruction. */
ds_status_t ds_list_clone_with(const ds_list_t *container,
                               ds_list_clone_fn clone,
                               ds_list_destroy_fn destroy, void *user,
                               ds_context_t *context, ds_list_t **out);
void ds_list_destroy_with(ds_list_t *container, ds_list_destroy_fn destroy,
                          void *user);
ds_status_t ds_list_append_ex(ds_list_t *container, ds_list_node_t *node);
ds_status_t ds_list_prepend_ex(ds_list_t *container, ds_list_node_t *node);
/* Identity-based O(n) membership check before unlinking. */
ds_status_t ds_list_remove_ex(ds_list_t *container, ds_list_node_t *target);

/* Reverse walk temporarily reverses links in O(1) extra space. Visitors
 * must not inspect/traverse/change the container or free nodes; all links are
 * restored even on a visitor stop/error. */
ds_status_t ds_list_walk_reverse_with(ds_list_t *container,
                                      ds_list_visit_fn visit, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_LIST_H */
