#ifndef DS_BTREE_H
#define DS_BTREE_H

#include "common.h"
#include <stdbool.h>

typedef struct btree_node {
  struct btree_internal_node *internal;
} btree_node_t;

typedef struct btree_internal_node {
  int num_keys;
  bool is_leaf;
  struct btree_internal_node **children;
  struct btree_internal_node *parent;
  btree_node_t **data;
} btree_internal_node_t;

typedef struct btree {
  int degree;
  btree_internal_node_t *root;
  btree_node_t *nil;
  size_t size;
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} btree_t;

btree_internal_node_t *btree_create_node(btree_t *tree, bool is_leaf);
btree_t *btree_create(int degree);
void btree_split_child(btree_t *tree, btree_internal_node_t *parent, int index);
void btree_insert_non_full(btree_t *tree, btree_internal_node_t *node,
                           btree_node_t *data,
                           int (*compare)(btree_node_t *, btree_node_t *));
void btree_insert(btree_t *tree, btree_node_t *data,
                  int (*compare)(btree_node_t *, btree_node_t *));
btree_node_t *btree_search_node(btree_t *tree, btree_internal_node_t *node,
                                btree_node_t *key,
                                int (*compare)(btree_node_t *, btree_node_t *));
btree_node_t *btree_search(btree_t *tree, btree_node_t *key,
                           int (*compare)(btree_node_t *, btree_node_t *));
void *btree_local_minimum(btree_t *tree, btree_internal_node_t *node);
void *btree_local_maximum(btree_t *tree, btree_internal_node_t *node);
void *btree_minimum(btree_t *tree);
void *btree_maximum(btree_t *tree);
void btree_rebalance(btree_t *tree, btree_internal_node_t *node);
void btree_delete_node(btree_t *tree, btree_node_t *key);
void btree_destroy_node(btree_t *tree, btree_node_t *node,
                        void (*destroy)(btree_node_t *));
void btree_destroy_recursive(btree_t *tree, btree_internal_node_t *node,
                             void (*destroy)(btree_node_t *));
void btree_destroy_tree(btree_t *tree, void (*destroy)(btree_node_t *));
void btree_inorder_walk_helper(btree_t *tree, btree_internal_node_t *node,
                               void (*callback)(btree_node_t *));
void btree_inorder_walk(btree_t *tree, btree_internal_node_t *node,
                        void (*callback)(btree_node_t *));
void btree_inorder_walk_tree(btree_t *tree, void (*callback)(btree_node_t *));
void btree_preorder_walk_helper(btree_t *tree, btree_internal_node_t *node,
                                void (*callback)(btree_node_t *));
void btree_preorder_walk(btree_t *tree, btree_internal_node_t *node,
                         void (*callback)(btree_node_t *));
void btree_preorder_walk_tree(btree_t *tree, void (*callback)(btree_node_t *));
void btree_postorder_walk_helper(btree_t *tree, btree_internal_node_t *node,
                                 void (*callback)(btree_node_t *));
void btree_postorder_walk(btree_t *tree, btree_internal_node_t *node,
                          void (*callback)(btree_node_t *));
void btree_postorder_walk_tree(btree_t *tree, void (*callback)(btree_node_t *));
btree_internal_node_t *
btree_clone_recursive(btree_t *tree, btree_t *new_tree,
                      btree_internal_node_t *node,
                      btree_node_t *(*clone_node)(btree_node_t *));
btree_t *btree_clone(btree_t *tree,
                     btree_node_t *(*clone_node)(btree_node_t *));

#endif /* DS_BTREE_H */
