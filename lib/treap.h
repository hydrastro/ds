#ifndef DS_TREAP_H
#define DS_TREAP_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct treap_node {
  struct treap_node *left;
  struct treap_node *right;
  struct treap_node *parent;
  unsigned long priority;
} ds_treap_node_t;

typedef struct treap {
  ds_treap_node_t *root;
  ds_treap_node_t *nil;
  size_t size;
  unsigned long seed;
  int (*compare)(ds_treap_node_t *, ds_treap_node_t *);
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_treap_t;

ds_treap_t *ds_treap_create(int (*compare)(ds_treap_node_t *,
                                           ds_treap_node_t *));
ds_treap_t *
ds_treap_create_alloc(int (*compare)(ds_treap_node_t *, ds_treap_node_t *),
                      unsigned long seed, void *(*allocator)(size_t),
                      void (*deallocator)(void *));
void ds_treap_node_init(ds_treap_node_t *node);
void ds_treap_insert(ds_treap_t *treap, ds_treap_node_t *node);
ds_treap_node_t *ds_treap_search(ds_treap_t *treap, ds_treap_node_t *node);
ds_treap_node_t *ds_treap_remove(ds_treap_t *treap, ds_treap_node_t *node);
ds_treap_node_t *ds_treap_minimum(ds_treap_t *treap, ds_treap_node_t *node);
ds_treap_node_t *ds_treap_maximum(ds_treap_t *treap, ds_treap_node_t *node);
bool ds_treap_is_empty(ds_treap_t *treap);
void ds_treap_destroy_node(ds_treap_t *treap, ds_treap_node_t *node,
                           void (*destroy)(ds_treap_node_t *));
void ds_treap_inorder_walk(ds_treap_t *treap, ds_treap_node_t *node,
                           void (*callback)(ds_treap_node_t *));
void ds_treap_inorder_walk_tree(ds_treap_t *treap,
                                void (*callback)(ds_treap_node_t *));
void ds_treap_destroy(ds_treap_t *treap, void (*destroy)(ds_treap_node_t *));
ds_treap_t *ds_treap_clone(ds_treap_t *treap,
                           ds_treap_node_t *(*clone_node)(ds_treap_node_t *));

#ifdef __cplusplus
}
#endif

#endif /* DS_TREAP_H */
