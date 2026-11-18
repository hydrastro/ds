#ifndef DS_HEAP_H
#define DS_HEAP_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct heap_node {
  size_t index;
} ds_heap_node_t;

typedef struct heap {
  ds_heap_node_t **data;
  size_t size;
  size_t capacity;
  ds_heap_node_t *nil;
  ds_context_t *context; /* NULL for allocator-pair constructors */
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_heap_t;

void ds_heap_swap(ds_heap_node_t **a, ds_heap_node_t **b);
void ds_heapify_up(ds_heap_t *heap, size_t index,
                   int (*compare)(ds_heap_node_t *, ds_heap_node_t *));
void ds_heapify_down(ds_heap_t *heap, size_t index,
                     int (*compare)(ds_heap_node_t *, ds_heap_node_t *));
/* NULL when memory runs out, for a capacity whose array cannot be sized,
 * or without an allocator and a deallocator */
ds_heap_t *ds_heap_create(size_t capacity);
ds_heap_t *ds_heap_create_alloc(size_t capacity, void *(*allocator)(size_t),
                                void (*deallocator)(void *));
/* Direct insert: when the array cannot grow, the node is not inserted
 * (heap->size is unchanged); insert_ex reports it */
void ds_heap_insert(ds_heap_t *heap, ds_heap_node_t *node,
                    int (*compare)(ds_heap_node_t *, ds_heap_node_t *));
/* DS_ERR_ALLOC (or DS_ERR_OVERFLOW) when the array cannot grow, with the
 * heap unchanged */
ds_status_t ds_heap_insert_ex(ds_heap_t *heap, ds_heap_node_t *node,
                              int (*compare)(ds_heap_node_t *,
                                             ds_heap_node_t *));
void *ds_heap_extract_root(ds_heap_t *heap,
                           int (*compare)(ds_heap_node_t *, ds_heap_node_t *));
void *ds_heap_peek_root(ds_heap_t *heap);
bool ds_heap_is_empty(ds_heap_t *heap);
void ds_heap_destroy(ds_heap_t *heap, void (*destroy)(ds_heap_node_t *));
/* Direct clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_heap_t *ds_heap_clone(ds_heap_t *heap,
                         ds_heap_node_t *(*clone_node)(ds_heap_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t ds_heap_clone_ex(const ds_heap_t *heap,
                             ds_heap_node_t *(*clone_node)(ds_heap_node_t *),
                             void (*destroy_clone)(ds_heap_node_t *),
                             ds_heap_t **out);

/* Context-owned internal storage; NULL context chooses the default.
 * The context must outlive the container. On failure *out is NULL. */
ds_status_t ds_heap_create_context(size_t capacity, ds_context_t *context,
                                   ds_heap_t **out);

/* Callback policies require stable topology during calls. Views are borrowed.
 * Walk stops on the first non-DS_OK result; find leaves out unchanged on
 * absence/error. Intrusive insertions require a detached non-sentinel node.
 * Checked pop returns DS_EMPTY without changing out. */
typedef ds_status_t (*ds_heap_visit_fn)(ds_heap_node_t *, void *);
typedef int (*ds_heap_compare_fn)(ds_heap_node_t *, ds_heap_node_t *, void *);
typedef ds_status_t (*ds_heap_clone_fn)(ds_heap_node_t *, ds_heap_node_t **,
                                        void *);
typedef void (*ds_heap_destroy_fn)(ds_heap_node_t *, void *);
ds_status_t ds_heap_walk_with(ds_heap_t *container, ds_heap_visit_fn visit,
                              void *user);
ds_status_t ds_heap_find_with(ds_heap_t *container, ds_heap_node_t *key,
                              ds_heap_compare_fn compare, void *user,
                              ds_heap_node_t **out);
/* Comparison policy must define the same ordering for all operations on a
 * heap. Insert is atomic on allocation failure. Extract returns DS_EMPTY and
 * preserves out on an empty heap; detached nodes have index SIZE_MAX. */
ds_status_t ds_heap_insert_with(ds_heap_t *heap, ds_heap_node_t *node,
                                ds_heap_compare_fn compare, void *user);
ds_status_t ds_heap_extract_with(ds_heap_t *heap, ds_heap_compare_fn compare,
                                 void *user, ds_heap_node_t **out);
/* NULL context inherits the source owner. Clone must return a fresh equivalent
 * node on DS_OK. A failing callback owns its unfinished node. Completed nodes
 * are rolled back via destroy; positive clone statuses map to DS_ERR_CALLBACK.
 * Output is NULL on failure. Destroy callbacks may release payloads but may
 * not inspect or mutate the container during destruction. */
ds_status_t ds_heap_clone_with(const ds_heap_t *container,
                               ds_heap_clone_fn clone,
                               ds_heap_destroy_fn destroy, void *user,
                               ds_context_t *context, ds_heap_t **out);
void ds_heap_destroy_with(ds_heap_t *container, ds_heap_destroy_fn destroy,
                          void *user);

/* Reverse traversal visits occupied slots in reverse walk_with order. */
ds_status_t ds_heap_walk_reverse_with(ds_heap_t *container,
                                      ds_heap_visit_fn visit, void *user);

/* Addressable O(log n) repair/removal by the node's index. Update follows a
 * caller's key change in either direction. All other keys keep their ordering.
 * The node must be initialized (e.g. previously inserted); membership is
 * checked against the index. Removal leaves index SIZE_MAX. Neither operation
 * allocates. */
ds_status_t ds_heap_update_with(ds_heap_t *heap, ds_heap_node_t *node,
                                ds_heap_compare_fn compare, void *user);
ds_status_t ds_heap_remove_with(ds_heap_t *heap, ds_heap_node_t *node,
                                ds_heap_compare_fn compare, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_HEAP_H */
