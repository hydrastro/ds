#ifndef DS_LIST_H
#define DS_LIST_H

#include "common.h"
#include "status.h"
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
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_list_t;

/* Backward compatibility for code written against earlier ds headers. */
typedef ds_list_node_t list_node_t;
typedef ds_list_t list_t;

/* NULL on allocation failure (or a missing allocator/deallocator). */
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
/* Legacy clone: NULL is returned if allocating the list or sentinel
 * fails, or when clone_node returns NULL; the clones it made before are
 * then not released. Use clone_ex for fallible cloning and caller-defined
 * rollback. */
ds_list_t *ds_list_clone(ds_list_t *list,
                         ds_list_node_t *(*clone_node)(ds_list_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back completed
 * clones through destroy_clone and returns DS_ERR_CALLBACK. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t ds_list_clone_ex(
    const ds_list_t *list, ds_list_node_t *(*clone_node)(ds_list_node_t *),
    void (*destroy_clone)(ds_list_node_t *), ds_list_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_LIST_H */
