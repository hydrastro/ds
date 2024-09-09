#ifndef DS_BST_H
#define DS_BST_H

#include "common.h"

#ifdef BST_THREAD_SAFE
#include <pthread.h>
#include <stdbool.h>
#endif

typedef struct bst_node {
  int key;
  struct bst_node *left;
  struct bst_node *right;
} bst_node_t;

typedef struct bst {
  bst_node_t *root;
  bst_node_t *nil;
#ifdef BST_THREAD_SAFE
  pthread_mutex_t lock;
  bool is_thread_safe;
#endif
} bst_t;

bst_t *bst_create(void);
void bst_insert(bst_t *tree, int key);
bst_node_t *bst_search(bst_t *tree, int key);
void bst_delete(bst_t *tree, int key);
void bst_destroy(bst_t *tree, void (*destroy_node)(bst_node_t *));
bst_node_t *bst_minimum(bst_t *tree);
bst_node_t *bst_maximum(bst_t *tree);

#endif // DS_BST_H
