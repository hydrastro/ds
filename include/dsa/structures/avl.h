#ifndef DS_AVL_H
#define DS_AVL_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <dsa/structures/tree_walk.h>
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
  ds_context_t *context; /* NULL for allocator-pair constructors */
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_avl_t;

/* NULL when memory runs out, or without an allocator and a deallocator */
/* Context owns all internal storage and must outlive the container. NULL
 * context selects the default. On failure *out is NULL. The context is
 * borrowed. */
ds_status_t ds_avl_create_context(ds_context_t *context, ds_avl_t **out);

ds_avl_t *ds_avl_create(void);
ds_avl_t *ds_avl_create_alloc(void *(*alloc)(size_t), void (*free)(void *));
ds_avl_node_t *ds_avl_search(ds_avl_t *tree, ds_avl_node_t *data,
                             int (*compare)(ds_avl_node_t *, ds_avl_node_t *));
void ds_avl_insert(ds_avl_t *tree, ds_avl_node_t *data,
                   int (*compare)(ds_avl_node_t *, ds_avl_node_t *));
void ds_avl_inorder_walk_helper(ds_avl_t *tree, ds_avl_node_t *node,
                                void (*callback)(ds_avl_node_t *));
void ds_avl_inorder_walk(ds_avl_t *tree, ds_avl_node_t *node,
                         void (*callback)(ds_avl_node_t *));
void ds_avl_inorder_walk_tree(ds_avl_t *tree,
                              void (*callback)(ds_avl_node_t *));
void ds_avl_preorder_walk_helper(ds_avl_t *tree, ds_avl_node_t *node,
                                 void (*callback)(ds_avl_node_t *));
void ds_avl_preorder_walk(ds_avl_t *tree, ds_avl_node_t *node,
                          void (*callback)(ds_avl_node_t *));
void ds_avl_preorder_walk_tree(ds_avl_t *tree,
                               void (*callback)(ds_avl_node_t *));
void ds_avl_postorder_walk_helper(ds_avl_t *tree, ds_avl_node_t *node,
                                  void (*callback)(ds_avl_node_t *));
void ds_avl_postorder_walk(ds_avl_t *tree, ds_avl_node_t *node,
                           void (*callback)(ds_avl_node_t *));
void ds_avl_postorder_walk_tree(ds_avl_t *tree,
                                void (*callback)(ds_avl_node_t *));
int ds_avl_get_height(ds_avl_t *tree, ds_avl_node_t *node);
int ds_avl_get_balance(ds_avl_t *tree, ds_avl_node_t *node);
void ds_avl_update_height(ds_avl_t *tree, ds_avl_node_t *node);
ds_avl_node_t *ds_avl_left_rotate(ds_avl_t *tree, ds_avl_node_t *node);
ds_avl_node_t *ds_avl_right_rotate(ds_avl_t *tree, ds_avl_node_t *node);
ds_avl_node_t *ds_avl_balance(ds_avl_t *tree, ds_avl_node_t *node);
ds_avl_node_t *ds_avl_min_node(ds_avl_t *tree, ds_avl_node_t *node);
void ds_avl_transplant(ds_avl_t *tree, ds_avl_node_t *u, ds_avl_node_t *v);

void ds_avl_delete_node(ds_avl_t *tree, ds_avl_node_t *data);
void ds_avl_destroy_node(ds_avl_t *tree, ds_avl_node_t *node,
                         void (*destroy)(ds_avl_node_t *));
void ds_avl_destroy_recursive(ds_avl_t *tree, ds_avl_node_t *node,
                              void (*destroy)(ds_avl_node_t *));
void ds_avl_destroy_tree(ds_avl_t *tree, void (*destroy)(ds_avl_node_t *));
void ds_avl_delete_tree(ds_avl_t *tree);
bool ds_avl_is_empty(ds_avl_t *tree);
ds_avl_node_t *
ds_avl_clone_recursive(ds_avl_t *tree, ds_avl_t *new_tree, ds_avl_node_t *node,
                       ds_avl_node_t *(*clone_node)(ds_avl_node_t *));
/* Direct clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_avl_t *ds_avl_clone(ds_avl_t *tree,
                       ds_avl_node_t *(*clone_node)(ds_avl_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t ds_avl_clone_ex(const ds_avl_t *tree,
                            ds_avl_node_t *(*clone_node)(ds_avl_node_t *),
                            void (*destroy_clone)(ds_avl_node_t *),
                            ds_avl_t **out);

typedef int (*ds_avl_compare_fn)(ds_avl_node_t *a, ds_avl_node_t *b,
                                 void *user);
typedef ds_status_t (*ds_avl_visit_fn)(ds_avl_node_t *node, void *user);
typedef ds_status_t (*ds_avl_clone_fn)(ds_avl_node_t *source, void *user,
                                       ds_avl_node_t **out_node);
typedef void (*ds_avl_destroy_fn)(ds_avl_node_t *node, void *user);
/* Comparators/visitors must not mutate the tree. Insertion nodes must be
 * detached; this API does not perform an O(n) membership scan.
 * Equal keys return DS_EXISTS without insertion.
 * Find preserves *out on DS_NOT_FOUND. Walks are iterative and propagate the
 * first non-DS_OK visitor status. root must belong to tree (or be its nil). */
ds_status_t ds_avl_insert_with(ds_avl_t *tree, ds_avl_node_t *node,
                               ds_avl_compare_fn compare, void *user);
ds_status_t ds_avl_find_with(const ds_avl_t *tree, ds_avl_node_t *key,
                             ds_avl_compare_fn compare, void *user,
                             ds_avl_node_t **out);
ds_status_t ds_avl_traverse_with(const ds_avl_t *tree, ds_avl_node_t *root,
                                 ds_tree_walk_order_t order,
                                 ds_avl_visit_fn visit, void *user);
ds_status_t ds_avl_walk_with(const ds_avl_t *tree, ds_avl_visit_fn visit,
                             void *user);
/* Destroys container storage; optional destroy releases each intrusive payload.
 * The callback must not inspect/traverse/mutate the tree during destruction. */
void ds_avl_destroy_with(ds_avl_t *tree, ds_avl_destroy_fn destroy, void *user);
/* clone must return DS_OK and a fresh non-NULL node with equivalent key/data.
 * On callback failure it owns any unfinished node. Completed nodes roll back
 * through destroy. Negative callback statuses propagate; positive statuses or
 * DS_OK with NULL become DS_ERR_CALLBACK. NULL context inherits source storage
 * policy; an explicit context selects destination internal storage. Callbacks
 * control payload allocation. On failure *out is NULL, source unchanged. */
ds_status_t ds_avl_clone_with(const ds_avl_t *tree, ds_avl_clone_fn clone,
                              ds_avl_destroy_fn destroy, void *user,
                              ds_context_t *context, ds_avl_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_AVL_H */
