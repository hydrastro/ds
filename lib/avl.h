#ifndef DS_AVL_H
#define DS_AVL_H

#include "common.h"

#ifdef AVL_THREAD_SAFE
#include <pthread.h>
#include <stdbool.h>
#endif

typedef struct avl_node {
    void *data;
    struct avl_node *left;
    struct avl_node *right;
    struct avl_node *parent;
    int height;
} avl_node_t;

typedef struct avl {
    avl_node_t *root;
#ifdef AVL_THREAD_SAFE
    pthread_mutex_t lock;
    bool is_thread_safe;
#endif
} avl_t;

avl_t *avl_create();
avl_node_t *avl_search(avl_t *tree, void *data, int (*compare)(void *, void *));
void avl_insert(avl_t *tree, void *data, int (*compare)(void *, void *));
void avl_delete(avl_t *tree, void *data, int (*compare)(void *, void *));
void avl_inorder_walk(avl_t *tree, avl_node_t *node, void (*callback)(void *));
void avl_inorder_walk_tree(avl_t *tree, void (*callback)(void *));
void avl_preorder_walk_tree(avl_t *tree, void (*callback)(void *));
void avl_postorder_walk_tree(avl_t *tree, void (*callback)(void *));
void avl_destroy_tree(avl_t *tree, void (*destroy)(void *));

#endif // DS_AVL_H
