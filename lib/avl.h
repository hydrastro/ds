#ifndef DS_AVL_H
#define DS_AVL_H

#include "common.h"

#ifdef AVL_THREAD_SAFE
#include <pthread.h>
#include <stdbool.h>
#endif

typedef struct avl_node {
  struct avl_node *left;
  struct avl_node *right;
  struct avl_node *parent;
  int height;
} avl_node_t;

typedef struct avl {
  avl_node_t *root;
  avl_node_t *nil;
#ifdef AVL_THREAD_SAFE
  pthread_mutex_t lock;
  bool is_thread_safe;
#endif
} avl_t;

avl_t *avl_create();
avl_node_t *avl_search(avl_t *tree, avl_node_t *data,
                       int (*compare)(avl_node_t *, avl_node_t *));
void avl_insert(avl_t *tree, avl_node_t *data,
                int (*compare)(avl_node_t *, avl_node_t *));
void avl_delete_node(avl_t *tree, avl_node_t *root, avl_node_t *data);
void avl_delete_nodex(avl_t *tree, avl_node_t *data,
                      int (*compare)(avl_node_t *, avl_node_t *));
void avl_inorder_walk(avl_t *tree, avl_node_t *node,
                      void (*callback)(avl_node_t *));
void avl_inorder_walk_tree(avl_t *tree, void (*callback)(avl_node_t *));
void avl_preorder_walk_tree(avl_t *tree, void (*callback)(avl_node_t *));
void avl_postorder_walk_tree(avl_t *tree, void (*callback)(avl_node_t *));
void avl_destroy_tree(avl_t *tree, void (*destroy)(avl_node_t *));

#endif // DS_AVL_H
