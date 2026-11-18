#ifndef DS_SHADOW_HEAP_H
#define DS_SHADOW_HEAP_H

/*
 * Shadow heap (Kuszmaul): a by-value binary min-heap whose newest
 * elements sit unordered in a "shadow" table at the end of the array.
 *
 *   push   appends to the shadow; when the shadow outgrows its threshold
 *          the shadow merge insert reorganises only the table's nodes and
 *          their ancestors (a bottom-up heapify over contiguous index
 *          ranges, level by level). The adapter advertises a conservative
 *          O(log n) amortized bound with the default threshold of log2 n
 *   merge  shadow merge: the smaller heap is attached behind the larger
 *          one and only the attached nodes and their ancestors are
 *          re-heapified
 *   peek   heap root or the smallest shadow element (scans the shadow)
 *   pop    folds the shadow in, then a normal delete-min
 *
 * The threshold is pluggable (config value; 0 picks floor(log2 n) + 1;
 * 1 behaves like an ordinary binary heap). Storage is growable from the
 * context or a fixed caller buffer (push returns DS_ERR_OVERFLOW when
 * full). The struct is public so a heap can live on the stack.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ds_shadow_heap_compare_func_t)(const void *a, const void *b,
                                             void *user);

typedef struct ds_shadow_heap {
  unsigned char *data;
  size_t count;      /* all elements */
  size_t heap_count; /* [0, heap_count) is heap-ordered, the rest shadow */
  size_t capacity;
  size_t elem_size;
  size_t threshold; /* 0: automatic */
  ds_shadow_heap_compare_func_t compare;
  void *user;
  bool fixed;
  ds_context_t *context;
  void (*moved)(const void *elem, size_t index, void *user);
  void *moved_user;
} ds_shadow_heap_t;

ds_status_t ds_shadow_heap_init(ds_shadow_heap_t *heap, size_t elem_size,
                                ds_shadow_heap_compare_func_t compare,
                                void *user, size_t threshold,
                                ds_context_t *context);
ds_status_t ds_shadow_heap_init_buffer(ds_shadow_heap_t *heap, size_t elem_size,
                                       ds_shadow_heap_compare_func_t compare,
                                       void *user, size_t threshold,
                                       void *buffer, size_t capacity);
void ds_shadow_heap_destroy(ds_shadow_heap_t *heap);
void ds_shadow_heap_clear(ds_shadow_heap_t *heap);
size_t ds_shadow_heap_size(const ds_shadow_heap_t *heap);
size_t ds_shadow_heap_shadow_size(const ds_shadow_heap_t *heap);
bool ds_shadow_heap_is_empty(const ds_shadow_heap_t *heap);

ds_status_t ds_shadow_heap_push(ds_shadow_heap_t *heap, const void *elem);
/* Smallest element copied to out: DS_EMPTY when empty. */
ds_status_t ds_shadow_heap_peek(const ds_shadow_heap_t *heap, void *out);
ds_status_t ds_shadow_heap_pop(ds_shadow_heap_t *heap, void *out);
/* Shadow merge insert of the whole shadow now. */
void ds_shadow_heap_flush(ds_shadow_heap_t *heap);
/* Shadow merge: dst receives every element of src (same elem_size and
 * order), src becomes empty. */
ds_status_t ds_shadow_heap_merge(ds_shadow_heap_t *dst, ds_shadow_heap_t *src);
/* Address tracking: placement/movement reports the new index, removal and
 * clear report (size_t)-1. Installing the callback reports existing items.
 * Callbacks update external metadata only. Merge reports source removals
 * before destination placements. */
void ds_shadow_heap_set_move_callback(ds_shadow_heap_t *heap,
                                      void (*moved)(const void *, size_t,
                                                    void *),
                                      void *user);
ds_status_t ds_shadow_heap_reserve(ds_shadow_heap_t *heap, size_t capacity);
const void *ds_shadow_heap_top(const ds_shadow_heap_t *heap);
const void *ds_shadow_heap_at(const ds_shadow_heap_t *heap, size_t index);
/* Update/repair is O(log n) in the ordered portion and O(1) in the shadow.
 * Removal may fold the shadow when its threshold changes: O(log^2 n) with
 * the default threshold. No allocation. elem may alias existing storage;
 * removal output must be outside it. Indices are tracked through moved. */
ds_status_t ds_shadow_heap_update_at(ds_shadow_heap_t *heap, size_t index,
                                     const void *elem);
ds_status_t ds_shadow_heap_repair_at(ds_shadow_heap_t *heap, size_t index);
ds_status_t ds_shadow_heap_remove_at(ds_shadow_heap_t *heap, size_t index,
                                     void *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_SHADOW_HEAP_H */
