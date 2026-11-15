#ifndef DS_SKIP_LIST_H
#define DS_SKIP_LIST_H

#include "common.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DS_SKIP_LIST_MAX_LEVEL
#define DS_SKIP_LIST_MAX_LEVEL 32
#endif

typedef struct skip_list_node {
  struct skip_list_node **forward;
  size_t level;
} ds_skip_list_node_t;

typedef struct skip_list {
  ds_skip_list_node_t *header;
  ds_skip_list_node_t *nil;
  size_t level;
  size_t max_level;
  size_t size;
  unsigned long seed;
  int (*compare)(ds_skip_list_node_t *, ds_skip_list_node_t *);
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_skip_list_t;

/* NULL when memory runs out, or without an allocator and a deallocator.
 * max_level is at most DS_SKIP_LIST_MAX_LEVEL, also the default (0). */
ds_skip_list_t *ds_skip_list_create(int (*compare)(ds_skip_list_node_t *,
                                                   ds_skip_list_node_t *));
ds_skip_list_t *ds_skip_list_create_alloc(int (*compare)(ds_skip_list_node_t *,
                                                         ds_skip_list_node_t *),
                                          size_t max_level, unsigned long seed,
                                          void *(*allocator)(size_t),
                                          void (*deallocator)(void *));
void ds_skip_list_node_init(ds_skip_list_node_t *node);
/* Legacy insert: when the node's links cannot be allocated, it is not
 * inserted (the size is unchanged); insert_ex reports it */
void ds_skip_list_insert(ds_skip_list_t *list, ds_skip_list_node_t *node);
/* DS_ERR_ALLOC when the node's links cannot be allocated, with the list
 * unchanged; DS_ERR_INVALID for the list's nil */
ds_status_t ds_skip_list_insert_ex(ds_skip_list_t *list,
                                   ds_skip_list_node_t *node);
ds_skip_list_node_t *ds_skip_list_search(ds_skip_list_t *list,
                                         ds_skip_list_node_t *node);
ds_skip_list_node_t *ds_skip_list_remove(ds_skip_list_t *list,
                                         ds_skip_list_node_t *node);
ds_skip_list_node_t *ds_skip_list_first(ds_skip_list_t *list);
ds_skip_list_node_t *ds_skip_list_next(ds_skip_list_t *list,
                                       ds_skip_list_node_t *node);
bool ds_skip_list_is_empty(ds_skip_list_t *list);
void ds_skip_list_delete_node(ds_skip_list_t *list, ds_skip_list_node_t *node);
void ds_skip_list_destroy_node(ds_skip_list_t *list, ds_skip_list_node_t *node,
                               void (*destroy)(ds_skip_list_node_t *));
void ds_skip_list_walk_forward(ds_skip_list_t *list, ds_skip_list_node_t *node,
                               void (*callback)(ds_skip_list_node_t *));
void ds_skip_list_destroy(ds_skip_list_t *list,
                          void (*destroy)(ds_skip_list_node_t *));
/* Legacy clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_skip_list_t *
ds_skip_list_clone(ds_skip_list_t *list,
                   ds_skip_list_node_t *(*clone_node)(ds_skip_list_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when memory runs out (the clones are rolled back too). All
 * callbacks are required. On any error *out is NULL and the source is
 * unchanged. */
ds_status_t
ds_skip_list_clone_ex(const ds_skip_list_t *list,
                      ds_skip_list_node_t *(*clone_node)(ds_skip_list_node_t *),
                      void (*destroy_clone)(ds_skip_list_node_t *),
                      ds_skip_list_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_SKIP_LIST_H */
