#ifndef DS_BST_H
#define DS_BST_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef BST_THREAD_SAFE
#include <pthread.h>
#endif

typedef struct bst_node {
  struct bst_node *left;
  struct bst_node *right;
  struct bst_node *parent;
} bst_node_t;

typedef struct bst {
  bst_node_t *root;
  bst_node_t *nil;
  size_t size;
#ifdef BST_THREAD_SAFE
  pthread_mutex_t lock;
  bool is_thread_safe;
#endif
} bst_t;

bst_t *bst_create(void);
void bst_insert(bst_t *tree, bst_node_t *node,
                int (*compare)(bst_node_t *, bst_node_t *));
bst_node_t *bst_search(bst_t *tree, bst_node_t *data,
                       int (*compare)(bst_node_t *, bst_node_t *));
bst_node_t *bst_minimum(bst_t *tree, bst_node_t *node);
bst_node_t *bst_maximum(bst_t *tree, bst_node_t *node);
void bst_transplant(bst_t *tree, bst_node_t *u, bst_node_t *v);
void bst_delete_node(bst_t *tree, bst_node_t *node);
void bst_destroy_node(bst_t *tree, bst_node_t *node,
                      void (*destroy)(bst_node_t *));
void bst_destroy(bst_t *tree, bst_node_t *root, void (*destroy)(bst_node_t *));
void bst_destroy_tree(bst_t *tree, void (*destroy)(bst_node_t *));
void bst_delete(bst_t *tree);
void bst_inorder_walk(bst_t *tree, bst_node_t *node, void (*callback)(void *));
void bst_preorder_walk(bst_t *tree, bst_node_t *node, void (*callback)(void *));
void bst_postorder_walk(bst_t *tree, bst_node_t *node,
                        void (*callback)(void *));
bst_node_t *bst_successor(bst_t *tree, bst_node_t *node);
bst_node_t *bst_predecessor(bst_t *tree, bst_node_t *node);
#endif // DS_BST_H