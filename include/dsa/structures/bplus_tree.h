#ifndef DS_BPLUS_TREE_H
#define DS_BPLUS_TREE_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bplus_tree_node {
  struct bplus_tree_internal_node *internal;
} ds_bplus_tree_node_t;

typedef struct bplus_tree_internal_node {
  int num_keys;
  bool is_leaf;
  /* routing keys (separators); for leaves these are the stored records */
  ds_bplus_tree_node_t **keys;
  /* children pointers (internal nodes only) */
  struct bplus_tree_internal_node **children;
  struct bplus_tree_internal_node *parent;
  /* leaf chain links (leaves only) */
  struct bplus_tree_internal_node *next;
  struct bplus_tree_internal_node *prev;
} ds_bplus_tree_internal_node_t;

typedef int (*ds_bplus_tree_compare_fn)(ds_bplus_tree_node_t *,
                                        ds_bplus_tree_node_t *, void *);

typedef struct bplus_tree {
  int order;
  /* the B* split policy: an overflowing node first hands a key to an
   * adjacent sibling with room, and two full siblings split into three
   * nodes about two-thirds full (only the root splits in two). Random
   * insertions fill leaves to about 85% instead of 70%. Deletion keeps
   * nodes but the root at least half full either way. */
  bool bstar;
  ds_bplus_tree_internal_node_t *root;
  ds_bplus_tree_internal_node_t *head; /* first (leftmost) leaf */
  ds_bplus_tree_node_t *nil;
  size_t size;
  int (*compare)(ds_bplus_tree_node_t *, ds_bplus_tree_node_t *);
  ds_context_t *context; /* borrowed allocation owner */
  ds_bplus_tree_compare_fn compare_with;
  void *compare_user;
} ds_bplus_tree_t;

/* NULL on allocation failure, invalid comparison policy or size overflow. */

ds_bplus_tree_t *ds_bplus_tree_create(int order, bool bstar,
                                      int (*compare)(ds_bplus_tree_node_t *,
                                                     ds_bplus_tree_node_t *),
                                      ds_context_t *context);
bool ds_bplus_tree_insert(ds_bplus_tree_t *tree, ds_bplus_tree_node_t *data);
/* DS_OK/new or DS_EXISTS/unchanged duplicate. All split nodes and B* scratch
 * are reserved before modifying the tree, so allocation failure leaves
 * records, separators and leaf links unchanged. Direct insert returns false
 * on duplicates and failures. Intrusive records remain caller-owned. */
ds_status_t ds_bplus_tree_insert_ex(ds_bplus_tree_t *tree,
                                    ds_bplus_tree_node_t *data);
ds_bplus_tree_node_t *ds_bplus_tree_search(ds_bplus_tree_t *tree,
                                           ds_bplus_tree_node_t *key);
bool ds_bplus_tree_remove(ds_bplus_tree_t *tree, ds_bplus_tree_node_t *key,
                          ds_bplus_tree_node_t **out_data);
ds_bplus_tree_node_t *ds_bplus_tree_minimum(ds_bplus_tree_t *tree);
ds_bplus_tree_node_t *ds_bplus_tree_maximum(ds_bplus_tree_t *tree);
bool ds_bplus_tree_is_empty(ds_bplus_tree_t *tree);
size_t ds_bplus_tree_size(ds_bplus_tree_t *tree);
void ds_bplus_tree_walk_forward(ds_bplus_tree_t *tree,
                                void (*callback)(ds_bplus_tree_node_t *));
void ds_bplus_tree_range_walk(ds_bplus_tree_t *tree, ds_bplus_tree_node_t *low,
                              ds_bplus_tree_node_t *high,
                              void (*callback)(ds_bplus_tree_node_t *));
void ds_bplus_tree_destroy(ds_bplus_tree_t *tree,
                           void (*destroy)(ds_bplus_tree_node_t *));
/* Direct clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_bplus_tree_t *ds_bplus_tree_clone(
    ds_bplus_tree_t *tree,
    ds_bplus_tree_node_t *(*clone_node)(ds_bplus_tree_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged.
 * Insertion failures also roll back every clone and return their status. */
ds_status_t ds_bplus_tree_clone_ex(
    const ds_bplus_tree_t *tree,
    ds_bplus_tree_node_t *(*clone_node)(ds_bplus_tree_node_t *),
    void (*destroy_clone)(ds_bplus_tree_node_t *), ds_bplus_tree_t **out);

/* Context owns internal allocations and must outlive the container; NULL
 * selects the default. Comparison/user state must remain stable and alive.
 * Output is NULL on failure. Allocator-pair constructors use the supplied
 * allocation functions. */
ds_status_t ds_bplus_tree_create_context(int order, bool bstar,
                                         ds_bplus_tree_compare_fn compare,
                                         void *user, ds_context_t *context,
                                         ds_bplus_tree_t **out);

typedef ds_status_t (*ds_bplus_tree_visit_fn)(ds_bplus_tree_node_t *, void *);
typedef ds_status_t (*ds_bplus_tree_clone_fn)(ds_bplus_tree_node_t *,
                                              ds_bplus_tree_node_t **, void *);
typedef void (*ds_bplus_tree_destroy_fn)(ds_bplus_tree_node_t *, void *);
/* Checked access preserves out on absence/error. Splay lookup/insertion can
 * change shape even on absence/DS_EXISTS. Insertions require detached records.
 * Walk propagates the first non-DS_OK visitor status. Callbacks may not mutate
 * or destroy the container or retain borrowed views. */
ds_status_t ds_bplus_tree_find_ex(ds_bplus_tree_t *container,
                                  ds_bplus_tree_node_t *key,
                                  ds_bplus_tree_node_t **out);
ds_status_t ds_bplus_tree_remove_ex(ds_bplus_tree_t *container,
                                    ds_bplus_tree_node_t *key,
                                    ds_bplus_tree_node_t **out);

ds_status_t ds_bplus_tree_walk_with(const ds_bplus_tree_t *container,
                                    ds_bplus_tree_visit_fn visit, void *user);
/* Clone copies records and the comparison policy, not necessarily the shape.
 * NULL context inherits storage ownership. Each successful callback returns a
 * fresh, equivalent record. Failure leaves out NULL, rolls back completed
 * clones, and leaves the unfinished allocation with its callback. Negative
 * errors propagate; positive clone results map to DS_ERR_CALLBACK. Destroy
 * may free payloads but must not inspect or mutate the container. */
ds_status_t ds_bplus_tree_clone_with(const ds_bplus_tree_t *container,
                                     ds_bplus_tree_clone_fn clone,
                                     ds_bplus_tree_destroy_fn destroy,
                                     void *user, ds_context_t *context,
                                     ds_bplus_tree_t **out);
void ds_bplus_tree_destroy_with(ds_bplus_tree_t *container,
                                ds_bplus_tree_destroy_fn destroy, void *user);

/* Inclusive range in the configured comparison order; low > high is invalid.
 */
ds_status_t ds_bplus_tree_range_with(ds_bplus_tree_t *tree,
                                     ds_bplus_tree_node_t *low,
                                     ds_bplus_tree_node_t *high,
                                     ds_bplus_tree_visit_fn visit, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_BPLUS_TREE_H */
