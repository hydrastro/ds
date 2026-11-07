#ifndef DS_JUMP_LIST_H
#define DS_JUMP_LIST_H

/*
 * ds_jump_list_t, the jump list of Andersson and Ottmann: a sorted doubly
 * linked list with two levels of extra links spanning geometrically
 * increasing distances, a uniquely represented dictionary (its shape
 * depends only on the set of keys) with O(n^(1/3)) worst-case search,
 * insertion and removal.
 *
 * With items numbered 1..n in order, item i links forward to item
 * i + floor(i^(1/3)) whenever that item exists, and the items at the
 * cubes 1, 8, 27, ..., floor(n^(1/3))^3 link backward to the previous
 * cube. A search walks the top level back from the last cube, the jumps
 * forward within that block, and finally the list itself. An update only
 * re-aims the links around the change, at the cubes behind it and at the
 * end of the list: O(n^(1/3)) of them.
 *
 * Elements are stored by value (elem_size bytes) and ordered by a
 * comparator with a user pointer; keys are unique. Memory comes from the
 * ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ds_jump_list_compare_func_t)(const void *a, const void *b,
                                           void *user);
typedef bool (*ds_jump_list_visit_func_t)(const void *elem, void *user);

typedef struct ds_jump_list {
  size_t elem_size;
  ds_jump_list_compare_func_t compare;
  void *user;
  unsigned char *data;
  size_t *next;
  size_t *prev;
  size_t *jump; /* item i -> item i + floor(i^(1/3)) */
  size_t *back; /* cube item c^3 -> (c-1)^3 */
  size_t nodes;
  size_t capacity;
  size_t free_list;
  size_t head;
  size_t tail;
  size_t *cubes; /* cubes[c - 1]: the item at c^3 */
  size_t cube_count;
  size_t cube_capacity;
  size_t *window; /* scratch */
  size_t window_capacity;
  size_t size;
  size_t steps; /* links followed by the last search */
  ds_context_t *context;
} ds_jump_list_t;

ds_status_t ds_jump_list_init(ds_jump_list_t *l, size_t elem_size,
                              ds_jump_list_compare_func_t compare, void *user,
                              ds_context_t *context);
void ds_jump_list_destroy(ds_jump_list_t *l);
size_t ds_jump_list_size(const ds_jump_list_t *l);
/* DS_EXISTS when an equal key is present (nothing changes). */
ds_status_t ds_jump_list_insert(ds_jump_list_t *l, const void *elem);
ds_status_t ds_jump_list_remove(ds_jump_list_t *l, const void *key);
ds_status_t ds_jump_list_find(ds_jump_list_t *l, const void *key, void *out);
/* The largest element <= key. */
ds_status_t ds_jump_list_floor(ds_jump_list_t *l, const void *key, void *out);
/* Links followed by the last search (a measure of the n^(1/3) bound). */
size_t ds_jump_list_steps(const ds_jump_list_t *l);
ds_status_t ds_jump_list_walk(const ds_jump_list_t *l,
                              ds_jump_list_visit_func_t visit, void *user);
/* Order, and every link exactly where the definition puts it. */
bool ds_jump_list_valid(const ds_jump_list_t *l);

#ifdef __cplusplus
}
#endif

#endif /* DS_JUMP_LIST_H */
