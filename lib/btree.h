#ifndef DS_BTREE_H
#define DS_BTREE_H

#include "common.h"
#include <stdbool.h>

typedef struct btree_node {
  struct btree_internal_node *ref;
} btree_node_t;

typedef struct btree_internal_node {
  int num_keys;
  bool is_leaf;
  struct btree_internal_node **children;
  btree_node_t **data;
} btree_internal_node_t;

typedef struct btree {
  int degree;
  btree_internal_node_t *root;
  btree_node_t *nil;
#ifdef BTREE_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} btree_t;

static btree_internal_node_t *btree_create_node(btree_t *tree, bool is_leaf);
btree_t *btree_create(int degree);
static void btree_split_child(btree_t *tree, btree_internal_node_t *parent,
                              int index, btree_internal_node_t *child);
static void btree_insert_non_full(btree_t *tree, btree_internal_node_t *node,
                                  btree_node_t *data,
                                  int (*compare)(btree_node_t *,
                                                 btree_node_t *));
void btree_insert(btree_t *tree, btree_node_t *data,
                  int (*compare)(btree_node_t *, btree_node_t *));
static btree_node_t *
btree_search_node(btree_t *tree, btree_internal_node_t *node, btree_node_t *key,
                  int (*compare)(btree_node_t *, btree_node_t *));
btree_node_t *btree_search(btree_t *tree, btree_node_t *key,
                           int (*compare)(btree_node_t *, btree_node_t *));

static void btree_merge_nodes(btree_t *tree, btree_internal_node_t *parent,
                              int index,
                              int (*compare)(btree_node_t *, btree_node_t *));
static void
btree_delete_non_leaf(btree_t *tree, btree_internal_node_t *node, int index,
                      int (*compare)(btree_node_t *, btree_node_t *));
static void btree_delete_node(btree_t *tree, btree_internal_node_t *node,
                              btree_node_t *key,
                              int (*compare)(btree_node_t *, btree_node_t *));
void btree_delete(btree_t *tree, btree_node_t *key,
                  void (*destroy_data)(btree_node_t *),
                  int (*compare)(btree_node_t *, btree_node_t *));
void btree_destroy_node(btree_t *tree, btree_internal_node_t *node,
                        void (*destroy_data)(btree_node_t *));
void btree_destroy(btree_t *tree, void (*destroy_data)(btree_node_t *));
void *btree_maximum(btree_t *tree);

#endif // DS_BTREE_H
