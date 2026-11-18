#ifndef DS_SKIP_LIST_H
#define DS_SKIP_LIST_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DS_SKIP_LIST_MAX_LEVEL
#define DS_SKIP_LIST_MAX_LEVEL 32
#endif

typedef struct skip_list_node {
  struct skip_list_node **forward;
  size_t level;
} ds_skip_list_node_t;

typedef int (*ds_skip_list_compare_fn)(ds_skip_list_node_t *,
                                       ds_skip_list_node_t *, void *);

typedef struct skip_list {
  ds_skip_list_node_t *header;
  ds_skip_list_node_t *nil;
  size_t level;
  size_t max_level;
  size_t size;
  unsigned long seed;
  int (*compare)(ds_skip_list_node_t *, ds_skip_list_node_t *);
  ds_context_t *context; /* NULL for allocator functions */
  ds_skip_list_compare_fn compare_with;
  void *compare_user;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_skip_list_t;

/* NULL when memory runs out, or without an allocator and a deallocator.
 * max_level is at most DS_SKIP_LIST_MAX_LEVEL, also the default (0). */
ds_skip_list_t *ds_skip_list_create(int (*compare)(ds_skip_list_node_t *,
                                                   ds_skip_list_node_t *));
ds_skip_list_t *ds_skip_list_create_alloc(int (*compare)(ds_skip_list_node_t *,
                                                         ds_skip_list_node_t *),
                                          size_t max_level, unsigned long seed,
                                          void *(*allocator)(size_t),
                                          void (*deallocator)(void *));
void ds_skip_list_node_init(ds_skip_list_node_t *node);
/* Direct insert: when the node's links cannot be allocated, it is not
 * inserted (the size is unchanged); insert_ex reports it */
void ds_skip_list_insert(ds_skip_list_t *list, ds_skip_list_node_t *node);
/* DS_ERR_ALLOC when the node's links cannot be allocated, with the list
 * unchanged; DS_ERR_INVALID for the list's nil */
ds_status_t ds_skip_list_insert_ex(ds_skip_list_t *list,
                                   ds_skip_list_node_t *node);
ds_skip_list_node_t *ds_skip_list_search(ds_skip_list_t *list,
                                         ds_skip_list_node_t *node);
ds_skip_list_node_t *ds_skip_list_remove(ds_skip_list_t *list,
                                         ds_skip_list_node_t *node);
ds_skip_list_node_t *ds_skip_list_first(ds_skip_list_t *list);
ds_skip_list_node_t *ds_skip_list_next(ds_skip_list_t *list,
                                       ds_skip_list_node_t *node);
bool ds_skip_list_is_empty(ds_skip_list_t *list);
void ds_skip_list_delete_node(ds_skip_list_t *list, ds_skip_list_node_t *node);
void ds_skip_list_destroy_node(ds_skip_list_t *list, ds_skip_list_node_t *node,
                               void (*destroy)(ds_skip_list_node_t *));
void ds_skip_list_walk_forward(ds_skip_list_t *list, ds_skip_list_node_t *node,
                               void (*callback)(ds_skip_list_node_t *));
void ds_skip_list_destroy(ds_skip_list_t *list,
                          void (*destroy)(ds_skip_list_node_t *));
/* Direct clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_skip_list_t *
ds_skip_list_clone(ds_skip_list_t *list,
                   ds_skip_list_node_t *(*clone_node)(ds_skip_list_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when memory runs out (the clones are rolled back too). All
 * callbacks are required. On any error *out is NULL and the source is
 * unchanged. */
ds_status_t
ds_skip_list_clone_ex(const ds_skip_list_t *list,
                      ds_skip_list_node_t *(*clone_node)(ds_skip_list_node_t *),
                      void (*destroy_clone)(ds_skip_list_node_t *),
                      ds_skip_list_t **out);

/* Context owns internal allocations and must outlive the container; NULL
 * selects the default. Comparison/user state must remain stable and alive.
 * Output is NULL on failure. Allocator-pair constructors use the supplied
 * allocation functions. */
ds_status_t ds_skip_list_create_context(size_t max_level, unsigned long seed,
                                        ds_skip_list_compare_fn compare,
                                        void *user, ds_context_t *context,
                                        ds_skip_list_t **out);

typedef ds_status_t (*ds_skip_list_visit_fn)(ds_skip_list_node_t *, void *);
typedef ds_status_t (*ds_skip_list_clone_fn)(ds_skip_list_node_t *,
                                             ds_skip_list_node_t **, void *);
typedef void (*ds_skip_list_destroy_fn)(ds_skip_list_node_t *, void *);
/* Checked access preserves out on absence/error. Splay lookup/insertion can
 * change shape even on absence/DS_EXISTS. Insertions require detached records.
 * Walk propagates the first non-DS_OK visitor status. Callbacks may not mutate
 * or destroy the container or retain borrowed views. */
ds_status_t ds_skip_list_find_ex(ds_skip_list_t *container,
                                 ds_skip_list_node_t *key,
                                 ds_skip_list_node_t **out);
ds_status_t ds_skip_list_remove_ex(ds_skip_list_t *container,
                                   ds_skip_list_node_t *key,
                                   ds_skip_list_node_t **out);

ds_status_t ds_skip_list_walk_with(const ds_skip_list_t *container,
                                   ds_skip_list_visit_fn visit, void *user);
/* Clone copies records and the comparison policy, not necessarily the shape.
 * NULL context inherits storage ownership. Each successful callback returns a
 * fresh, equivalent record. Failure leaves out NULL, rolls back completed
 * clones, and leaves the unfinished allocation with its callback. Negative
 * errors propagate; positive clone results map to DS_ERR_CALLBACK. Destroy
 * may free payloads but must not inspect or mutate the container. */
ds_status_t ds_skip_list_clone_with(const ds_skip_list_t *container,
                                    ds_skip_list_clone_fn clone,
                                    ds_skip_list_destroy_fn destroy, void *user,
                                    ds_context_t *context,
                                    ds_skip_list_t **out);
void ds_skip_list_destroy_with(ds_skip_list_t *container,
                               ds_skip_list_destroy_fn destroy, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_SKIP_LIST_H */
