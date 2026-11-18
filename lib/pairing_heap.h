#ifndef DS_PAIRING_HEAP_H
#define DS_PAIRING_HEAP_H

#include "common.h"
#include "context.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pairing_heap_node {
  struct pairing_heap_node *child;
  struct pairing_heap_node *sibling;
  struct pairing_heap_node *prev;
} ds_pairing_heap_node_t;

typedef int (*ds_pairing_heap_compare_fn)(ds_pairing_heap_node_t *,
                                          ds_pairing_heap_node_t *, void *);

typedef struct pairing_heap {
  ds_pairing_heap_node_t *root;
  ds_pairing_heap_node_t *nil;
  size_t size;
  int (*compare)(ds_pairing_heap_node_t *, ds_pairing_heap_node_t *);
  ds_context_t *context; /* NULL for legacy allocators */
  ds_pairing_heap_compare_fn compare_with;
  void *compare_user;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_pairing_heap_t;

/* NULL when memory runs out, or without an allocator and a deallocator */
ds_pairing_heap_t *ds_pairing_heap_create(
    int (*compare)(ds_pairing_heap_node_t *, ds_pairing_heap_node_t *));
ds_pairing_heap_t *ds_pairing_heap_create_alloc(
    int (*compare)(ds_pairing_heap_node_t *, ds_pairing_heap_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *));
void ds_pairing_heap_node_init(ds_pairing_heap_node_t *node);
void ds_pairing_heap_insert(ds_pairing_heap_t *heap,
                            ds_pairing_heap_node_t *node);
ds_pairing_heap_node_t *ds_pairing_heap_peek(ds_pairing_heap_t *heap);
ds_pairing_heap_node_t *ds_pairing_heap_extract(ds_pairing_heap_t *heap);
void ds_pairing_heap_decrease_key(ds_pairing_heap_t *heap,
                                  ds_pairing_heap_node_t *node);
bool ds_pairing_heap_is_empty(ds_pairing_heap_t *heap);
size_t ds_pairing_heap_size(ds_pairing_heap_t *heap);
void ds_pairing_heap_merge(ds_pairing_heap_t *heap, ds_pairing_heap_t *other);
void ds_pairing_heap_walk(ds_pairing_heap_t *heap,
                          void (*callback)(ds_pairing_heap_node_t *));
void ds_pairing_heap_destroy(ds_pairing_heap_t *heap,
                             void (*destroy)(ds_pairing_heap_node_t *));

/* Context owns internal allocations and must outlive the container; NULL
 * selects the default. Comparison/user state must remain stable and alive.
 * Output is NULL on failure. Legacy constructors retain allocator behavior;
 * public layout changes require recompilation. */
ds_status_t ds_pairing_heap_create_context(ds_pairing_heap_compare_fn compare,
                                           void *user, ds_context_t *context,
                                           ds_pairing_heap_t **out);

typedef ds_status_t (*ds_pairing_heap_visit_fn)(ds_pairing_heap_node_t *,
                                                void *);
typedef ds_status_t (*ds_pairing_heap_clone_fn)(ds_pairing_heap_node_t *,
                                                ds_pairing_heap_node_t **,
                                                void *);
typedef void (*ds_pairing_heap_destroy_fn)(ds_pairing_heap_node_t *, void *);
/* Checked access preserves out on absence/error. Splay lookup/insertion can
 * change shape even on absence/DS_EXISTS. Insertions require detached records.
 * Walk propagates the first non-DS_OK visitor status. Callbacks may not mutate
 * or destroy the container or retain borrowed views. */
ds_status_t ds_pairing_heap_insert_ex(ds_pairing_heap_t *container,
                                      ds_pairing_heap_node_t *node);

ds_status_t ds_pairing_heap_walk_with(const ds_pairing_heap_t *container,
                                      ds_pairing_heap_visit_fn visit,
                                      void *user);
/* Clone copies records and the comparison policy, not necessarily the shape.
 * NULL context inherits storage ownership. Each successful callback returns a
 * fresh, equivalent record. Failure leaves out NULL, rolls back completed
 * clones, and leaves the unfinished allocation with its callback. Negative
 * errors propagate; positive clone results map to DS_ERR_CALLBACK. Destroy
 * may free payloads but must not inspect or mutate the container. */
ds_status_t ds_pairing_heap_clone_with(const ds_pairing_heap_t *container,
                                       ds_pairing_heap_clone_fn clone,
                                       ds_pairing_heap_destroy_fn destroy,
                                       void *user, ds_context_t *context,
                                       ds_pairing_heap_t **out);
void ds_pairing_heap_destroy_with(ds_pairing_heap_t *container,
                                  ds_pairing_heap_destroy_fn destroy,
                                  void *user);

/* Meld is atomic on validation/overflow failure and rejects self-meld.
 * Compare function and user pointers must match. Only caller-owned nodes move;
 * internal allocations/sentinels stay with their owners, so contexts may
 * differ. Successful meld empties other. Extract preserves out on
 * DS_EMPTY/error. */
ds_status_t ds_pairing_heap_merge_ex(ds_pairing_heap_t *heap,
                                     ds_pairing_heap_t *other);
ds_status_t ds_pairing_heap_extract_ex(ds_pairing_heap_t *heap,
                                       ds_pairing_heap_node_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_PAIRING_HEAP_H */
