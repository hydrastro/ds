#ifndef DS_SKIP_LIST_H
#define DS_SKIP_LIST_H

#include "common.h"
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

ds_skip_list_t *FUNC(ds_skip_list_create)(
    int (*compare)(ds_skip_list_node_t *, ds_skip_list_node_t *));
ds_skip_list_t *FUNC(ds_skip_list_create_alloc)(
    int (*compare)(ds_skip_list_node_t *, ds_skip_list_node_t *),
    size_t max_level, unsigned long seed, void *(*allocator)(size_t),
    void (*deallocator)(void *));
void FUNC(ds_skip_list_node_init)(ds_skip_list_node_t *node);
void FUNC(ds_skip_list_insert)(ds_skip_list_t *list, ds_skip_list_node_t *node);
ds_skip_list_node_t *FUNC(ds_skip_list_search)(ds_skip_list_t *list,
                                               ds_skip_list_node_t *node);
ds_skip_list_node_t *FUNC(ds_skip_list_remove)(ds_skip_list_t *list,
                                               ds_skip_list_node_t *node);
ds_skip_list_node_t *FUNC(ds_skip_list_first)(ds_skip_list_t *list);
ds_skip_list_node_t *FUNC(ds_skip_list_next)(ds_skip_list_t *list,
                                             ds_skip_list_node_t *node);
bool FUNC(ds_skip_list_is_empty)(ds_skip_list_t *list);
void FUNC(ds_skip_list_delete_node)(ds_skip_list_t *list,
                                    ds_skip_list_node_t *node);
void FUNC(ds_skip_list_destroy_node)(ds_skip_list_t *list,
                                     ds_skip_list_node_t *node,
                                     void (*destroy)(ds_skip_list_node_t *));
void FUNC(ds_skip_list_walk_forward)(ds_skip_list_t *list,
                                     ds_skip_list_node_t *node,
                                     void (*callback)(ds_skip_list_node_t *));
void FUNC(ds_skip_list_destroy)(ds_skip_list_t *list,
                                void (*destroy)(ds_skip_list_node_t *));
ds_skip_list_t *FUNC(ds_skip_list_clone)(
    ds_skip_list_t *list,
    ds_skip_list_node_t *(*clone_node)(ds_skip_list_node_t *));

#ifdef __cplusplus
}
#endif

#endif /* DS_SKIP_LIST_H */
