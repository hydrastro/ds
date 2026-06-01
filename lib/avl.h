#ifndef DS_AVL_H
#define DS_AVL_H

#include "common.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct avl_node {
  struct avl_node *left;
  struct avl_node *right;
  struct avl_node *parent;
  int height;
} ds_avl_node_t;

typedef struct avl {
  ds_avl_node_t *root;
  ds_avl_node_t *nil;
  size_t size;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} ds_avl_t;

ds_avl_t *FUNC(ds_avl_create)(void);
ds_avl_t *FUNC(ds_avl_create_alloc)(void *(*alloc)(size_t),
                                    void (*free)(void *));
ds_avl_node_t *FUNC(ds_avl_search)(ds_avl_t *tree, ds_avl_node_t *data,
                                   int (*compare)(ds_avl_node_t *,
                                                  ds_avl_node_t *));
void FUNC(ds_avl_insert)(ds_avl_t *tree, ds_avl_node_t *data,
                         int (*compare)(ds_avl_node_t *, ds_avl_node_t *));
void FUNC(ds_avl_inorder_walk_helper)(ds_avl_t *tree, ds_avl_node_t *node,
                                      void (*callback)(ds_avl_node_t *));
void FUNC(ds_avl_inorder_walk)(ds_avl_t *tree, ds_avl_node_t *node,
                               void (*callback)(ds_avl_node_t *));
void FUNC(ds_avl_inorder_walk_tree)(ds_avl_t *tree,
                                    void (*callback)(ds_avl_node_t *));
void FUNC(ds_avl_preorder_walk_helper)(ds_avl_t *tree, ds_avl_node_t *node,
                                       void (*callback)(ds_avl_node_t *));
void FUNC(ds_avl_preorder_walk)(ds_avl_t *tree, ds_avl_node_t *node,
                                void (*callback)(ds_avl_node_t *));
void FUNC(ds_avl_preorder_walk_tree)(ds_avl_t *tree,
                                     void (*callback)(ds_avl_node_t *));
void FUNC(ds_avl_postorder_walk_helper)(ds_avl_t *tree, ds_avl_node_t *node,
                                        void (*callback)(ds_avl_node_t *));
void FUNC(ds_avl_postorder_walk)(ds_avl_t *tree, ds_avl_node_t *node,
                                 void (*callback)(ds_avl_node_t *));
void FUNC(ds_avl_postorder_walk_tree)(ds_avl_t *tree,
                                      void (*callback)(ds_avl_node_t *));
int FUNC(ds_avl_get_height)(ds_avl_t *tree, ds_avl_node_t *node);
int FUNC(ds_avl_get_balance)(ds_avl_t *tree, ds_avl_node_t *node);
void FUNC(ds_avl_update_height)(ds_avl_t *tree, ds_avl_node_t *node);
ds_avl_node_t *FUNC(ds_avl_left_rotate)(ds_avl_t *tree, ds_avl_node_t *node);
ds_avl_node_t *FUNC(ds_avl_right_rotate)(ds_avl_t *tree, ds_avl_node_t *node);
ds_avl_node_t *FUNC(ds_avl_balance)(ds_avl_t *tree, ds_avl_node_t *node);
ds_avl_node_t *FUNC(ds_avl_min_node)(ds_avl_t *tree, ds_avl_node_t *node);
void FUNC(ds_avl_transplant)(ds_avl_t *tree, ds_avl_node_t *u,
                             ds_avl_node_t *v);

void FUNC(ds_avl_delete_node)(ds_avl_t *tree, ds_avl_node_t *data);
void FUNC(ds_avl_destroy_node)(ds_avl_t *tree, ds_avl_node_t *node,
                               void (*destroy)(ds_avl_node_t *));
void FUNC(ds_avl_destroy_recursive)(ds_avl_t *tree, ds_avl_node_t *node,
                                    void (*destroy)(ds_avl_node_t *));
void FUNC(ds_avl_destroy_tree)(ds_avl_t *tree,
                               void (*destroy)(ds_avl_node_t *));
void FUNC(ds_avl_delete_tree)(ds_avl_t *tree);
bool FUNC(ds_avl_is_empty)(ds_avl_t *tree);
ds_avl_node_t *
    FUNC(ds_avl_clone_recursive)(ds_avl_t *tree, ds_avl_t *new_tree,
                                 ds_avl_node_t *node,
                                 ds_avl_node_t *(*clone_node)(ds_avl_node_t *));
ds_avl_t *FUNC(ds_avl_clone)(ds_avl_t *tree,
                             ds_avl_node_t *(*clone_node)(ds_avl_node_t *));

#ifdef __cplusplus
}
#endif

#endif /* DS_AVL_H */
