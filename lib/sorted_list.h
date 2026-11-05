#ifndef DS_SORTED_LIST_H
#define DS_SORTED_LIST_H

/*
 * Plain ordered and unordered sequences, the baselines every search
 * structure is measured against. Elements are stored by value.
 *
 * ds_sorted_array_t  a sorted array: binary search, lower/upper bound,
 *                    insertion and deletion by shifting, rank/select,
 *                    merging with another sorted array; duplicates allowed
 *                    or rejected (DS_EXISTS); dynamic or caller buffer.
 * ds_sorted_list_t   an ordered (sorted) singly linked list: nodes in
 *                    arrays with index links and a free list, sequential
 *                    search that stops early, ordered merge.
 * ds_unsorted_list_t an unsorted list: O(1) append, sequential search,
 *                    O(1) deletion by moving the last element into the
 *                    hole; dynamic or caller buffer.
 *
 * Concurrency: a structure may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ds_sorted_compare_func_t)(const void *a, const void *b,
                                        void *user);

typedef struct ds_sorted_array {
  unsigned char *data;
  size_t size;
  size_t capacity;
  size_t elem_size;
  ds_sorted_compare_func_t compare;
  void *user;
  bool unique;
  bool fixed;
  ds_context_t *context;
} ds_sorted_array_t;

ds_status_t ds_sorted_array_init(ds_sorted_array_t *a, size_t elem_size,
                                 ds_sorted_compare_func_t compare, void *user,
                                 bool unique, ds_context_t *context);
ds_status_t ds_sorted_array_init_buffer(ds_sorted_array_t *a, size_t elem_size,
                                        ds_sorted_compare_func_t compare,
                                        void *user, bool unique, void *buffer,
                                        size_t capacity);
void ds_sorted_array_destroy(ds_sorted_array_t *a);
void ds_sorted_array_clear(ds_sorted_array_t *a);
size_t ds_sorted_array_size(const ds_sorted_array_t *a);
/* Equal elements keep insertion order (inserted after existing ones). */
ds_status_t ds_sorted_array_insert(ds_sorted_array_t *a, const void *elem,
                                   size_t *out_index);
/* First element equal to key. */
ds_status_t ds_sorted_array_find(const ds_sorted_array_t *a, const void *key,
                                 size_t *out_index);
/* First index whose element is >= key (lower) or > key (upper). */
size_t ds_sorted_array_lower_bound(const ds_sorted_array_t *a, const void *key);
size_t ds_sorted_array_upper_bound(const ds_sorted_array_t *a, const void *key);
/* Removes the first element equal to key. */
ds_status_t ds_sorted_array_remove(ds_sorted_array_t *a, const void *key,
                                   void *out);
ds_status_t ds_sorted_array_remove_at(ds_sorted_array_t *a, size_t index,
                                      void *out);
const void *ds_sorted_array_at(const ds_sorted_array_t *a, size_t index);
/* Every element of src merged in (src unchanged; same elem_size). */
ds_status_t ds_sorted_array_merge(ds_sorted_array_t *a,
                                  const ds_sorted_array_t *src);

#define DS_SORTED_LIST_END (~(size_t)0)

typedef struct ds_sorted_list {
  unsigned char *data;
  size_t *next;
  size_t head;
  size_t size;
  size_t slots;
  size_t capacity;
  size_t free_list;
  size_t elem_size;
  ds_sorted_compare_func_t compare;
  void *user;
  bool unique;
  ds_context_t *context;
} ds_sorted_list_t;

ds_status_t ds_sorted_list_init(ds_sorted_list_t *l, size_t elem_size,
                                ds_sorted_compare_func_t compare, void *user,
                                bool unique, ds_context_t *context);
void ds_sorted_list_destroy(ds_sorted_list_t *l);
void ds_sorted_list_clear(ds_sorted_list_t *l);
size_t ds_sorted_list_size(const ds_sorted_list_t *l);
ds_status_t ds_sorted_list_insert(ds_sorted_list_t *l, const void *elem);
/* Node handle of the first element equal to key. */
ds_status_t ds_sorted_list_find(const ds_sorted_list_t *l, const void *key,
                                size_t *out_node);
ds_status_t ds_sorted_list_remove(ds_sorted_list_t *l, const void *key,
                                  void *out);
ds_status_t ds_sorted_list_pop_front(ds_sorted_list_t *l, void *out);
/* Traversal: first node, next node (DS_SORTED_LIST_END at the end). */
size_t ds_sorted_list_first(const ds_sorted_list_t *l);
size_t ds_sorted_list_next(const ds_sorted_list_t *l, size_t node);
const void *ds_sorted_list_get(const ds_sorted_list_t *l, size_t node);
/* Every element of src merged in, src emptied (same elem_size). */
ds_status_t ds_sorted_list_merge(ds_sorted_list_t *l, ds_sorted_list_t *src);

typedef struct ds_unsorted_list {
  unsigned char *data;
  size_t size;
  size_t capacity;
  size_t elem_size;
  ds_sorted_compare_func_t equal; /* 0 = equal */
  void *user;
  bool fixed;
  ds_context_t *context;
} ds_unsorted_list_t;

ds_status_t ds_unsorted_list_init(ds_unsorted_list_t *l, size_t elem_size,
                                  ds_sorted_compare_func_t equal, void *user,
                                  ds_context_t *context);
ds_status_t ds_unsorted_list_init_buffer(ds_unsorted_list_t *l,
                                         size_t elem_size,
                                         ds_sorted_compare_func_t equal,
                                         void *user, void *buffer,
                                         size_t capacity);
void ds_unsorted_list_destroy(ds_unsorted_list_t *l);
void ds_unsorted_list_clear(ds_unsorted_list_t *l);
size_t ds_unsorted_list_size(const ds_unsorted_list_t *l);
ds_status_t ds_unsorted_list_add(ds_unsorted_list_t *l, const void *elem);
ds_status_t ds_unsorted_list_find(const ds_unsorted_list_t *l, const void *key,
                                  size_t *out_index);
ds_status_t ds_unsorted_list_remove(ds_unsorted_list_t *l, const void *key,
                                    void *out);
ds_status_t ds_unsorted_list_remove_at(ds_unsorted_list_t *l, size_t index,
                                       void *out);
const void *ds_unsorted_list_at(const ds_unsorted_list_t *l, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* DS_SORTED_LIST_H */
