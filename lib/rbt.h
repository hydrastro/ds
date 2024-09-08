#ifndef DS_RBT_H
#define DS_RBT_H

#include "common.h"

#ifdef RBT_THREAD_SAFE
#include <pthread.h>
#include <stdbool.h>
#endif

typedef struct rbt_node {
  unsigned long parent_color;
  struct rbt_node *right;
  struct rbt_node *left;
} __attribute__((aligned(sizeof(long)))) rbt_node_t;

#define RBT_RED 0
#define RBT_BLACK 1

#define RBT_GET_PARENT_FROM_COLOR(color) ((rbt_node_t *)((color) & (long unsigned int)~3))
#define RBT_GET_COLOR_FROM_COLOR(color) ((color) & 1)
#define RBT_IS_RED_FROM_COLOR(color)                                           \
  (RBT_GET_COLOR_FROM_COLOR(color) == RBT_RED)
#define RBT_IS_BLACK_FROM_COLOR(color)                                         \
  (RBT_GET_COLOR_FROM_COLOR(color) == RBT_BLACK)
#define RBT_SET_RED_FROM_COLOR(color) ((color) &= (long unsigned int)~1)
#define RBT_SET_BLACK_FROM_COLOR(color) ((color) |= (long unsigned int)1)

#define RBT_GET_PARENT_FROM_NODE(node)                                         \
  (RBT_GET_PARENT_FROM_COLOR((node)->parent_color))
#define RBT_GET_COLOR_FROM_NODE(node)                                          \
  (RBT_GET_COLOR_FROM_COLOR((node)->parent_color))
#define RBT_IS_RED_FROM_NODE(node)                                             \
  (RBT_IS_RED_FROM_COLOR(RBT_GET_COLOR_FROM_NODE(node)))
#define RBT_IS_BLACK_FROM_NODE(node)                                           \
  (RBT_IS_BLACK_FROM_COLOR(RBT_GET_COLOR_FROM_NODE(node)))
#define RBT_SET_RED_FROM_NODE(node)                                            \
  (RBT_SET_RED_FROM_COLOR((node)->parent_color))
#define RBT_SET_BLACK_FROM_NODE(node)                                          \
  (RBT_SET_BLACK_FROM_COLOR((node)->parent_color))
#define RBT_SET_COLOR_FROM_NODE(node, color)                                   \
  ((node)->parent_color =                                                      \
       (unsigned long)RBT_GET_PARENT_FROM_NODE(node) + ((long unsigned int)color))

typedef struct rbt {
  rbt_node_t *root;
  rbt_node_t *nil;
#ifdef RBT_THREAD_SAFE
  pthread_mutex_t lock;
  bool is_thread_safe;
#endif
} __attribute__((aligned(sizeof(long)))) rbt_t;

void rbt_set_parent(rbt_node_t *node, rbt_node_t *parent);
void rbt_set_parent_color(rbt_node_t *node, rbt_node_t *parent, int color);
rbt_t *rbt_create(void);
rbt_node_t *rbt_minimum(rbt_t *tree, rbt_node_t *node_x);
rbt_node_t *rbt_maximum(rbt_t *tree, rbt_node_t *node_x);
rbt_node_t *rbt_successor(rbt_t *tree, rbt_node_t *node_x);
rbt_node_t *rbt_predecessor(rbt_t *tree, rbt_node_t *node_x);
rbt_node_t *rbt_search(rbt_t *tree, rbt_node_t *node_x, void *data,
                       int (*compare)(void *, void *));
rbt_node_t *rbt_bigger_than(rbt_t *tree, rbt_node_t *node_x, void *key,
                            int (*compare)(void *, void *));
rbt_node_t *rbt_smaller_than(rbt_t *tree, rbt_node_t *node_x, void *key,
                             int (*compare)(void *, void *));
void rbt_left_rotate(rbt_t *tree, rbt_node_t *node_x);
void rbt_right_rotate(rbt_t *tree, rbt_node_t *node_x);
void rbt_insert_fixup(rbt_t *tree, rbt_node_t *node_z);
void rbt_insert(rbt_t *tree, rbt_node_t *node_z,
                int (*compare)(void *, void *));
void rbt_inorder_walk(rbt_t *tree, rbt_node_t *node, void (*callback)(void *));
void rbt_inorder_walk_tree(rbt_t *tree, void (*callback)(void *));
void rbt_preorder_walk(rbt_t *tree, rbt_node_t *node, void (*callback)(void *));
void rbt_preorder_walk_tree(rbt_t *tree, void (*callback)(void *));
void rbt_postorder_walk(rbt_t *tree, rbt_node_t *node,
                        void (*callback)(void *));
void rbt_postorder_walk_tree(rbt_t *tree, void (*callback)(void *));
void rbt_transplant(rbt_t *tree, rbt_node_t *node_u, rbt_node_t *node_v);
void rbt_delete_fixup(rbt_t *tree, rbt_node_t *node_x);
void rbt_delete_node(rbt_t *tree, rbt_node_t *node_z);
void rbt_delete(rbt_t *tree);
void rbt_destroy_node(rbt_t *tree, rbt_node_t *node, void (*destroy)(void *));
void rbt_destroy(rbt_t *tree, rbt_node_t *node, void (*destroy)(void *));
void rbt_destroy_tree(rbt_t *tree, void (*destroy)(void *));

#endif // DS_RBT_H
