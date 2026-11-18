#ifndef DS_TREAP_H
#define DS_TREAP_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct treap_node {
  struct treap_node *left;
  struct treap_node *right;
  struct treap_node *parent;
  unsigned long priority;
} ds_treap_node_t;

typedef int (*ds_treap_compare_fn)(const ds_treap_node_t *,
                                   const ds_treap_node_t *, void *user);
typedef ds_status_t (*ds_treap_visit_fn)(ds_treap_node_t *, void *user);
typedef void (*ds_treap_destroy_fn)(ds_treap_node_t *, void *user);
typedef ds_status_t (*ds_treap_clone_fn)(const ds_treap_node_t *, void *user,
                                         ds_treap_node_t **out);
typedef struct ds_treap_config {
  ds_treap_compare_fn compare;
  void *user;
  ds_context_t *context;
  unsigned long seed;
  unsigned long (*priority)(void *user); /* NULL: internal 32-bit xorshift */
  void *priority_user;
} ds_treap_config_t;

typedef struct treap {
  ds_treap_node_t *root;
  ds_treap_node_t *nil;
  size_t size;
  unsigned long seed;
  int (*compare)(ds_treap_node_t *, ds_treap_node_t *);
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
  ds_context_t *context;
  bool use_context;
  ds_treap_compare_fn compare_with;
  void *user;
  unsigned long (*priority)(void *);
  void *priority_user;
} ds_treap_t;

/* Nodes are intrusive, unique by comparator, and must not already be linked.
 * Compare/priority callbacks must not mutate the tree. Context/user outlive it.
 * All operations use constant C-stack space even for a degenerate priority
 * stream. Search/update remain O(height), expected O(log n) only with suitable
 * priorities independent of key order. Cloning preserves shape and priorities.
 */
void ds_treap_config_init(ds_treap_config_t *config);
ds_status_t ds_treap_create_with(const ds_treap_config_t *config,
                                 ds_treap_t **out);
/* DS_EXISTS leaves the incoming node unchanged. */
ds_status_t ds_treap_insert_ex(ds_treap_t *treap, ds_treap_node_t *node);
ds_status_t ds_treap_find_ex(ds_treap_t *treap, ds_treap_node_t *key,
                             ds_treap_node_t **out);
ds_status_t ds_treap_remove_ex(ds_treap_t *treap, ds_treap_node_t *key,
                               ds_treap_node_t **out);
ds_status_t ds_treap_walk_with(ds_treap_t *treap, ds_treap_visit_fn visit,
                               void *user);
void ds_treap_destroy_with(ds_treap_t *treap, ds_treap_destroy_fn destroy,
                           void *user);
/* Failed clone callbacks release their own partial result. Successful clones
 * are distinct unlinked nodes; any later error releases them through destroy.
 * Positive callback statuses and DS_OK with NULL are callback errors. */
ds_status_t ds_treap_clone_with(const ds_treap_t *treap,
                                ds_treap_clone_fn clone,
                                ds_treap_destroy_fn destroy, void *user,
                                ds_treap_t **out);

/* NULL when memory runs out, or without an allocator and a deallocator */
ds_treap_t *ds_treap_create(int (*compare)(ds_treap_node_t *,
                                           ds_treap_node_t *));
ds_treap_t *
ds_treap_create_alloc(int (*compare)(ds_treap_node_t *, ds_treap_node_t *),
                      unsigned long seed, void *(*allocator)(size_t),
                      void (*deallocator)(void *));
void ds_treap_node_init(ds_treap_node_t *node);
void ds_treap_insert(ds_treap_t *treap, ds_treap_node_t *node);
ds_treap_node_t *ds_treap_search(ds_treap_t *treap, ds_treap_node_t *node);
ds_treap_node_t *ds_treap_remove(ds_treap_t *treap, ds_treap_node_t *node);
ds_treap_node_t *ds_treap_minimum(ds_treap_t *treap, ds_treap_node_t *node);
ds_treap_node_t *ds_treap_maximum(ds_treap_t *treap, ds_treap_node_t *node);
bool ds_treap_is_empty(ds_treap_t *treap);
void ds_treap_destroy_node(ds_treap_t *treap, ds_treap_node_t *node,
                           void (*destroy)(ds_treap_node_t *));
void ds_treap_inorder_walk(ds_treap_t *treap, ds_treap_node_t *node,
                           void (*callback)(ds_treap_node_t *));
void ds_treap_inorder_walk_tree(ds_treap_t *treap,
                                void (*callback)(ds_treap_node_t *));
void ds_treap_destroy(ds_treap_t *treap, void (*destroy)(ds_treap_node_t *));
/* Direct clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_treap_t *ds_treap_clone(ds_treap_t *treap,
                           ds_treap_node_t *(*clone_node)(ds_treap_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t ds_treap_clone_ex(const ds_treap_t *treap,
                              ds_treap_node_t *(*clone_node)(ds_treap_node_t *),
                              void (*destroy_clone)(ds_treap_node_t *),
                              ds_treap_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_TREAP_H */
