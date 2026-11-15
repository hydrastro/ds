#ifndef DS_DLIST_H
#define DS_DLIST_H

#include "common.h"
#include "status.h"
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
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_dlist_t;

/* NULL when memory runs out, or without an allocator and a deallocator */
ds_dlist_t *ds_dlist_create(void);
ds_dlist_t *ds_dlist_create_alloc(void *(*allocator)(size_t),
                                  void (*deallocator)(void *));
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
/* Legacy clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_dlist_t *ds_dlist_clone(ds_dlist_t *list,
                           ds_dlist_node_t *(*clone_node)(ds_dlist_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t ds_dlist_clone_ex(const ds_dlist_t *list,
                              ds_dlist_node_t *(*clone_node)(ds_dlist_node_t *),
                              void (*destroy_clone)(ds_dlist_node_t *),
                              ds_dlist_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_DLIST_H */
