#ifndef DS_DISJOINT_SET_H
#define DS_DISJOINT_SET_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct disjoint_set_node {
  struct disjoint_set_node *parent;
  size_t rank;
} ds_disjoint_set_node_t;

typedef struct disjoint_set {
  size_t count;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_disjoint_set_t;

ds_disjoint_set_t *ds_disjoint_set_create(void);
ds_disjoint_set_t *ds_disjoint_set_create_alloc(void *(*allocator)(size_t),
                                                void (*deallocator)(void *));
void ds_disjoint_set_make(ds_disjoint_set_t *set, ds_disjoint_set_node_t *node);
ds_disjoint_set_node_t *ds_disjoint_set_find(ds_disjoint_set_t *set,
                                             ds_disjoint_set_node_t *node);
bool ds_disjoint_set_union(ds_disjoint_set_t *set, ds_disjoint_set_node_t *a,
                           ds_disjoint_set_node_t *b);
bool ds_disjoint_set_connected(ds_disjoint_set_t *set,
                               ds_disjoint_set_node_t *a,
                               ds_disjoint_set_node_t *b);
size_t ds_disjoint_set_count(ds_disjoint_set_t *set);
void ds_disjoint_set_destroy(ds_disjoint_set_t *set);

#ifdef __cplusplus
}
#endif

#endif /* DS_DISJOINT_SET_H */
