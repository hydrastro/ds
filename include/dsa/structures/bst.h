#ifndef DS_BST_H
#define DS_BST_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <dsa/structures/tree_walk.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bst_node {
  struct bst_node *left;
  struct bst_node *right;
  struct bst_node *parent;
} ds_bst_node_t;

typedef struct bst {
  ds_bst_node_t *root;
  ds_bst_node_t *nil;
  size_t size;
  ds_context_t *context; /* NULL for allocator-pair constructors */
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_bst_t;

/* NULL when memory runs out, or without an allocator and a deallocator */
/* Context owns all internal storage and must outlive the container. NULL
 * context selects the default. On failure *out is NULL. The context is
 * borrowed. */
ds_status_t ds_bst_create_context(ds_context_t *context, ds_bst_t **out);

ds_bst_t *ds_bst_create(void);
ds_bst_t *ds_bst_create_alloc(void *(*allocator)(size_t),
                              void (*deallocator)(void *));
void ds_bst_insert(ds_bst_t *tree, ds_bst_node_t *node,
                   int (*compare)(ds_bst_node_t *, ds_bst_node_t *));
ds_bst_node_t *ds_bst_search(ds_bst_t *tree, ds_bst_node_t *data,
                             int (*compare)(ds_bst_node_t *, ds_bst_node_t *));
ds_bst_node_t *ds_bst_minimum(ds_bst_t *tree, ds_bst_node_t *node);
ds_bst_node_t *ds_bst_maximum(ds_bst_t *tree, ds_bst_node_t *node);
void ds_bst_transplant(ds_bst_t *tree, ds_bst_node_t *u, ds_bst_node_t *v);
void ds_bst_delete_node(ds_bst_t *tree, ds_bst_node_t *node);
void ds_bst_destroy_node(ds_bst_t *tree, ds_bst_node_t *node,
                         void (*destroy)(ds_bst_node_t *));
void ds_bst_destroy_recursive(ds_bst_t *tree, ds_bst_node_t *root,
                              void (*destroy)(ds_bst_node_t *));
void ds_bst_destroy_tree(ds_bst_t *tree, void (*destroy)(ds_bst_node_t *));
void ds_bst_delete_tree(ds_bst_t *tree);
void ds_bst_inorder_walk_helper(ds_bst_t *tree, ds_bst_node_t *node,
                                void (*callback)(void *));
void ds_bst_inorder_walk(ds_bst_t *tree, ds_bst_node_t *node,
                         void (*callback)(void *));
void ds_bst_inorder_walk_tree(ds_bst_t *tree, void (*callback)(void *));
void ds_bst_preorder_walk_helper(ds_bst_t *tree, ds_bst_node_t *node,
                                 void (*callback)(void *));
void ds_bst_preorder_walk(ds_bst_t *tree, ds_bst_node_t *node,
                          void (*callback)(void *));
void ds_bst_preorder_walk_tree(ds_bst_t *tree, void (*callback)(void *));
void ds_bst_postorder_walk_helper(ds_bst_t *tree, ds_bst_node_t *node,
                                  void (*callback)(void *));
void ds_bst_postorder_walk(ds_bst_t *tree, ds_bst_node_t *node,
                           void (*callback)(void *));
void ds_bst_postorder_walk_tree(ds_bst_t *tree, void (*callback)(void *));
ds_bst_node_t *ds_bst_successor(ds_bst_t *tree, ds_bst_node_t *node_x);
ds_bst_node_t *ds_bst_predecessor(ds_bst_t *tree, ds_bst_node_t *node_x);

ds_bst_node_t *
ds_bst_clone_recursive(ds_bst_t *tree, ds_bst_t *new_tree, ds_bst_node_t *node,
                       ds_bst_node_t *(*clone_node)(ds_bst_node_t *));
/* Direct clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_bst_t *ds_bst_clone(ds_bst_t *tree,
                       ds_bst_node_t *(*clone_node)(ds_bst_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t ds_bst_clone_ex(const ds_bst_t *tree,
                            ds_bst_node_t *(*clone_node)(ds_bst_node_t *),
                            void (*destroy_clone)(ds_bst_node_t *),
                            ds_bst_t **out);

typedef int (*ds_bst_compare_fn)(ds_bst_node_t *a, ds_bst_node_t *b,
                                 void *user);
typedef ds_status_t (*ds_bst_visit_fn)(ds_bst_node_t *node, void *user);
typedef ds_status_t (*ds_bst_clone_fn)(ds_bst_node_t *source, void *user,
                                       ds_bst_node_t **out_node);
typedef void (*ds_bst_destroy_fn)(ds_bst_node_t *node, void *user);
/* Comparators/visitors must not mutate the tree. Insertion nodes must be
 * detached; this API does not perform an O(n) membership scan.
 * Equal keys are permitted, as in the original insertion API.
 * Find preserves *out on DS_NOT_FOUND. Walks are iterative and propagate the
 * first non-DS_OK visitor status. root must belong to tree (or be its nil). */
ds_status_t ds_bst_insert_with(ds_bst_t *tree, ds_bst_node_t *node,
                               ds_bst_compare_fn compare, void *user);
ds_status_t ds_bst_find_with(const ds_bst_t *tree, ds_bst_node_t *key,
                             ds_bst_compare_fn compare, void *user,
                             ds_bst_node_t **out);
ds_status_t ds_bst_traverse_with(const ds_bst_t *tree, ds_bst_node_t *root,
                                 ds_tree_walk_order_t order,
                                 ds_bst_visit_fn visit, void *user);
ds_status_t ds_bst_walk_with(const ds_bst_t *tree, ds_bst_visit_fn visit,
                             void *user);
/* Destroys container storage; optional destroy releases each intrusive payload.
 * The callback must not inspect/traverse/mutate the tree during destruction. */
void ds_bst_destroy_with(ds_bst_t *tree, ds_bst_destroy_fn destroy, void *user);
/* clone must return DS_OK and a fresh non-NULL node with equivalent key/data.
 * On callback failure it owns any unfinished node. Completed nodes roll back
 * through destroy. Negative callback statuses propagate; positive statuses or
 * DS_OK with NULL become DS_ERR_CALLBACK. NULL context inherits source storage
 * policy; an explicit context selects destination internal storage. Callbacks
 * control payload allocation. On failure *out is NULL, source unchanged. */
ds_status_t ds_bst_clone_with(const ds_bst_t *tree, ds_bst_clone_fn clone,
                              ds_bst_destroy_fn destroy, void *user,
                              ds_context_t *context, ds_bst_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_BST_H */
