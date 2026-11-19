#ifndef DS_RBT_H
#define DS_RBT_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <dsa/structures/tree_walk.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A node keeps its color in the low bit of its parent pointer, so nodes
 * must be at least 2-aligned: a struct holding an unsigned long is. GCC
 * and clang are told so explicitly; other compilers need nothing. */
#if defined(__GNUC__)
#define DS_RBT_ALIGNED __attribute__((aligned(sizeof(long))))
#else
#define DS_RBT_ALIGNED
#endif

typedef struct rbt_node {
  unsigned long parent_color;
  struct rbt_node *right;
  struct rbt_node *left;
} DS_RBT_ALIGNED ds_rbt_node_t;

#define RBT_RED 0
#define RBT_BLACK 1

#define RBT_GET_PARENT_FROM_COLOR(color)                                      \
  ((ds_rbt_node_t *)((color) & (long unsigned int)~3))
#define RBT_GET_COLOR_FROM_COLOR(color) ((color) & 1)
#define RBT_IS_RED_FROM_COLOR(color)                                          \
  (RBT_GET_COLOR_FROM_COLOR(color) == RBT_RED)
#define RBT_IS_BLACK_FROM_COLOR(color)                                        \
  (RBT_GET_COLOR_FROM_COLOR(color) == RBT_BLACK)
#define RBT_SET_RED_FROM_COLOR(color) ((color) &= (long unsigned int)~1)
#define RBT_SET_BLACK_FROM_COLOR(color) ((color) |= (long unsigned int)1)

#define RBT_GET_PARENT_FROM_NODE(node)                                        \
  (RBT_GET_PARENT_FROM_COLOR((node)->parent_color))
#define RBT_GET_COLOR_FROM_NODE(node)                                         \
  (RBT_GET_COLOR_FROM_COLOR((node)->parent_color))
#define RBT_IS_RED_FROM_NODE(node)                                            \
  (RBT_IS_RED_FROM_COLOR(RBT_GET_COLOR_FROM_NODE(node)))
#define RBT_IS_BLACK_FROM_NODE(node)                                          \
  (RBT_IS_BLACK_FROM_COLOR(RBT_GET_COLOR_FROM_NODE(node)))
#define RBT_SET_RED_FROM_NODE(node)                                           \
  (RBT_SET_RED_FROM_COLOR((node)->parent_color))
#define RBT_SET_BLACK_FROM_NODE(node)                                         \
  (RBT_SET_BLACK_FROM_COLOR((node)->parent_color))
#define RBT_SET_COLOR_FROM_NODE(node, color)                                  \
  ((node)->parent_color = (unsigned long)RBT_GET_PARENT_FROM_NODE(node) |     \
                          ((long unsigned int)color))

typedef struct rbt {
  ds_rbt_node_t *root;
  ds_rbt_node_t *nil;
  size_t size;
  ds_context_t *context; /* borrowed allocation owner */

} DS_RBT_ALIGNED ds_rbt_t;

void ds_rbt_set_parent(ds_rbt_node_t *node, ds_rbt_node_t *parent);
void ds_rbt_set_parent_color(ds_rbt_node_t *node, ds_rbt_node_t *parent,
                             int color);
/* NULL when memory runs out */
/* Context owns all internal storage and must outlive the container. NULL
 * context selects the default. On failure *out is NULL. The context is
 * borrowed. */
ds_status_t ds_rbt_create_context(ds_context_t *context, ds_rbt_t **out);

ds_rbt_t *ds_rbt_create(ds_context_t *context);
ds_rbt_node_t *ds_rbt_minimum(ds_rbt_t *tree, ds_rbt_node_t *node_x);
ds_rbt_node_t *ds_rbt_maximum(ds_rbt_t *tree, ds_rbt_node_t *node_x);
ds_rbt_node_t *ds_rbt_successor(ds_rbt_t *tree, ds_rbt_node_t *node_x);
ds_rbt_node_t *ds_rbt_predecessor(ds_rbt_t *tree, ds_rbt_node_t *node_x);
ds_rbt_node_t *ds_rbt_search(ds_rbt_t *tree, ds_rbt_node_t *node_x,
                             ds_rbt_node_t *data,
                             int (*compare)(ds_rbt_node_t *, ds_rbt_node_t *));

ds_rbt_node_t *
ds_rbt_bigger_than(ds_rbt_t *tree, ds_rbt_node_t *node_x, ds_rbt_node_t *key,
                   int (*compare)(ds_rbt_node_t *, ds_rbt_node_t *));
ds_rbt_node_t *
ds_rbt_smaller_than(ds_rbt_t *tree, ds_rbt_node_t *node_x, ds_rbt_node_t *key,
                    int (*compare)(ds_rbt_node_t *, ds_rbt_node_t *));
void ds_rbt_left_rotate(ds_rbt_t *tree, ds_rbt_node_t *node_x);
void ds_rbt_right_rotate(ds_rbt_t *tree, ds_rbt_node_t *node_x);
void ds_rbt_insert_fixup(ds_rbt_t *tree, ds_rbt_node_t *node_z);
void ds_rbt_insert(ds_rbt_t *tree, ds_rbt_node_t *node_z,
                   int (*compare)(ds_rbt_node_t *, ds_rbt_node_t *));
void ds_rbt_inorder_walk_helper(ds_rbt_t *tree, ds_rbt_node_t *node,
                                void (*callback)(void *));
void ds_rbt_inorder_walk(ds_rbt_t *tree, ds_rbt_node_t *node,
                         void (*callback)(void *));
void ds_rbt_inorder_walk_tree(ds_rbt_t *tree, void (*callback)(void *));
void ds_rbt_preorder_walk_helper(ds_rbt_t *tree, ds_rbt_node_t *node,
                                 void (*callback)(void *));
void ds_rbt_preorder_walk(ds_rbt_t *tree, ds_rbt_node_t *node,
                          void (*callback)(void *));
void ds_rbt_preorder_walk_tree(ds_rbt_t *tree, void (*callback)(void *));
void ds_rbt_postorder_walk_helper(ds_rbt_t *tree, ds_rbt_node_t *node,
                                  void (*callback)(void *));
void ds_rbt_postorder_walk(ds_rbt_t *tree, ds_rbt_node_t *node,
                           void (*callback)(void *));
void ds_rbt_postorder_walk_tree(ds_rbt_t *tree, void (*callback)(void *));
void ds_rbt_transplant(ds_rbt_t *tree, ds_rbt_node_t *node_u,
                       ds_rbt_node_t *node_v);
void ds_rbt_delete_fixup(ds_rbt_t *tree, ds_rbt_node_t *node_x);
void ds_rbt_delete_node(ds_rbt_t *tree, ds_rbt_node_t *node_z);
void ds_rbt_delete_tree(ds_rbt_t *tree);
void ds_rbt_destroy_node(ds_rbt_t *tree, ds_rbt_node_t *node,
                         void (*destroy)(ds_rbt_node_t *));
void ds_rbt_destroy_recursive(ds_rbt_t *tree, ds_rbt_node_t *node,
                              void (*destroy)(ds_rbt_node_t *));
void ds_rbt_destroy_tree(ds_rbt_t *tree, void (*destroy)(ds_rbt_node_t *));
ds_rbt_node_t *
ds_rbt_clone_recursive(ds_rbt_t *tree, ds_rbt_t *new_tree, ds_rbt_node_t *node,
                       ds_rbt_node_t *(*clone_node)(ds_rbt_node_t *));
/* Direct clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_rbt_t *ds_rbt_clone(ds_rbt_t *tree,
                       ds_rbt_node_t *(*clone_node)(ds_rbt_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t ds_rbt_clone_ex(const ds_rbt_t *tree,
                            ds_rbt_node_t *(*clone_node)(ds_rbt_node_t *),
                            void (*destroy_clone)(ds_rbt_node_t *),
                            ds_rbt_t **out);

typedef int (*ds_rbt_compare_fn)(ds_rbt_node_t *a, ds_rbt_node_t *b,
                                 void *user);
typedef ds_status_t (*ds_rbt_visit_fn)(ds_rbt_node_t *node, void *user);
typedef ds_status_t (*ds_rbt_clone_fn)(ds_rbt_node_t *source, void *user,
                                       ds_rbt_node_t **out_node);
typedef void (*ds_rbt_destroy_fn)(ds_rbt_node_t *node, void *user);
/* Comparators/visitors must not mutate the tree. Insertion nodes must be
 * detached; this API does not perform an O(n) membership scan.
 * Equal keys are permitted, as in the original insertion API.
 * Find preserves *out on DS_NOT_FOUND. Walks are iterative and propagate the
 * first non-DS_OK visitor status. root must belong to tree (or be its nil). */
ds_status_t ds_rbt_insert_with(ds_rbt_t *tree, ds_rbt_node_t *node,
                               ds_rbt_compare_fn compare, void *user);
ds_status_t ds_rbt_find_with(const ds_rbt_t *tree, ds_rbt_node_t *key,
                             ds_rbt_compare_fn compare, void *user,
                             ds_rbt_node_t **out);
ds_status_t ds_rbt_traverse_with(const ds_rbt_t *tree, ds_rbt_node_t *root,
                                 ds_tree_walk_order_t order,
                                 ds_rbt_visit_fn visit, void *user);
ds_status_t ds_rbt_walk_with(const ds_rbt_t *tree, ds_rbt_visit_fn visit,
                             void *user);
/* Destroys container storage; optional destroy releases each intrusive
 * payload. The callback must not inspect/traverse/mutate the tree during
 * destruction. */
void ds_rbt_destroy_with(ds_rbt_t *tree, ds_rbt_destroy_fn destroy,
                         void *user);
/* clone must return DS_OK and a fresh non-NULL node with equivalent key/data.
 * On callback failure it owns any unfinished node. Completed nodes roll back
 * through destroy. Negative callback statuses propagate; positive statuses or
 * DS_OK with NULL become DS_ERR_CALLBACK. NULL context inherits source storage
 * policy; an explicit context selects destination internal storage. Callbacks
 * control payload allocation. On failure *out is NULL, source unchanged. */
ds_status_t ds_rbt_clone_with(const ds_rbt_t *tree, ds_rbt_clone_fn clone,
                              ds_rbt_destroy_fn destroy, void *user,
                              ds_context_t *context, ds_rbt_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_RBT_H */
