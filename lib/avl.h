#ifndef DS_AVL_H
#define DS_AVL_H

#include "common.h"
#include <stdbool.h>

typedef struct avl_node {
  struct avl_node *left;
  struct avl_node *right;
  struct avl_node *parent;
  int height;
} avl_node_t;

typedef struct avl {
  avl_node_t *root;
  avl_node_t *nil;
  size_t size;
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} avl_t;

avl_t *FUNC(avl_create)(void);
avl_node_t *FUNC(avl_search)(avl_t *tree, avl_node_t *data,
                             int (*compare)(avl_node_t *, avl_node_t *));
void FUNC(avl_insert)(avl_t *tree, avl_node_t *data,
                      int (*compare)(avl_node_t *, avl_node_t *));
void FUNC(avl_inorder_walk_helper)(avl_t *tree, avl_node_t *node,
                                   void (*callback)(avl_node_t *));
void FUNC(avl_inorder_walk)(avl_t *tree, avl_node_t *node,
                            void (*callback)(avl_node_t *));
void FUNC(avl_inorder_walk_tree)(avl_t *tree, void (*callback)(avl_node_t *));
void FUNC(avl_preorder_walk_helper)(avl_t *tree, avl_node_t *node,
                                    void (*callback)(avl_node_t *));
void FUNC(avl_preorder_walk)(avl_t *tree, avl_node_t *node,
                             void (*callback)(avl_node_t *));
void FUNC(avl_preorder_walk_tree)(avl_t *tree, void (*callback)(avl_node_t *));
void FUNC(avl_postorder_walk_helper)(avl_t *tree, avl_node_t *node,
                                     void (*callback)(avl_node_t *));
void FUNC(avl_postorder_walk)(avl_t *tree, avl_node_t *node,
                              void (*callback)(avl_node_t *));
void FUNC(avl_postorder_walk_tree)(avl_t *tree, void (*callback)(avl_node_t *));
int FUNC(avl_get_height)(avl_t *tree, avl_node_t *node);
int FUNC(avl_get_balance)(avl_t *tree, avl_node_t *node);
void FUNC(avl_update_height)(avl_t *tree, avl_node_t *node);
avl_node_t *FUNC(avl_left_rotate)(avl_t *tree, avl_node_t *node);
avl_node_t *FUNC(avl_right_rotate)(avl_t *tree, avl_node_t *node);
avl_node_t *FUNC(avl_balance)(avl_t *tree, avl_node_t *node);
avl_node_t *FUNC(avl_min_node)(avl_t *tree, avl_node_t *node);
void FUNC(avl_transplant)(avl_t *tree, avl_node_t *u, avl_node_t *v);

void FUNC(avl_delete_node)(avl_t *tree, avl_node_t *data);
void FUNC(avl_destroy_node)(avl_t *tree, avl_node_t *node,
                            void (*destroy)(avl_node_t *));
void FUNC(avl_destroy_recursive)(avl_t *tree, avl_node_t *node,
                                 void (*destroy)(avl_node_t *));
void FUNC(avl_destroy_tree)(avl_t *tree, void (*destroy)(avl_node_t *));
void FUNC(avl_delete_tree)(avl_t *tree);
bool FUNC(avl_is_empty)(avl_t *tree);
avl_node_t *FUNC(avl_clone_recursive)(avl_t *tree, avl_t *new_tree,
                                      avl_node_t *node,
                                      avl_node_t *(*clone_node)(avl_node_t *));
avl_t *FUNC(avl_clone)(avl_t *tree, avl_node_t *(*clone_node)(avl_node_t *));

#endif /* DS_AVL_H */
