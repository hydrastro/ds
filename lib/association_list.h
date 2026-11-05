#ifndef DS_ASSOCIATION_LIST_H
#define DS_ASSOCIATION_LIST_H

/*
 * Association lists and property lists: a linked list of (key, value)
 * pairs searched sequentially, the simplest dictionary (and the classic
 * Lisp environment representation).
 *
 * Two disciplines over one structure:
 *   association list  ds_alist_push adds a pair at the front even if the
 *                     key is present, shadowing older bindings until
 *                     ds_alist_pop / ds_alist_remove; lookup finds the
 *                     most recent binding, ds_alist_lookup_all every one
 *   property list     ds_alist_put replaces the value of an existing
 *                     indicator (key) or adds it: each key appears once
 * Keys and values are stored by value (fixed sizes) in node arrays with
 * index links; key equality is pluggable.
 *
 * Concurrency: a list may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_ALIST_END (~(size_t)0)

typedef bool (*ds_alist_equal_func_t)(const void *a, const void *b, void *user);

typedef struct ds_alist {
  unsigned char *keys;
  unsigned char *values;
  size_t *next;
  size_t head;
  size_t size;
  size_t slots;
  size_t capacity;
  size_t free_list;
  size_t key_size;
  size_t value_size;
  ds_alist_equal_func_t equal;
  void *user;
  ds_context_t *context;
} ds_alist_t;

typedef bool (*ds_alist_visit_func_t)(const void *key, const void *value,
                                      void *user);

ds_status_t ds_alist_init(ds_alist_t *l, size_t key_size, size_t value_size,
                          ds_alist_equal_func_t equal, void *user,
                          ds_context_t *context);
void ds_alist_destroy(ds_alist_t *l);
void ds_alist_clear(ds_alist_t *l);
size_t ds_alist_size(const ds_alist_t *l);
/* New binding at the front (shadows older ones). */
ds_status_t ds_alist_push(ds_alist_t *l, const void *key, const void *value);
/* Removes the front binding. */
ds_status_t ds_alist_pop(ds_alist_t *l, void *out_key, void *out_value);
/* Most recent value bound to key (pointer into the list, or NULL). */
void *ds_alist_lookup(const ds_alist_t *l, const void *key);
/* Every binding of key, most recent first. */
size_t ds_alist_lookup_all(const ds_alist_t *l, const void *key,
                           ds_alist_visit_func_t f, void *user);
/* Property-list put: replaces the most recent binding or adds one at the
 * front. DS_EXISTS when replaced. */
ds_status_t ds_alist_put(ds_alist_t *l, const void *key, const void *value);
ds_status_t ds_alist_get(const ds_alist_t *l, const void *key, void *out_value);
/* Removes the most recent binding (all of them when `all`); DS_NOT_FOUND
 * if there were none. */
ds_status_t ds_alist_remove(ds_alist_t *l, const void *key, bool all);
/* Front to back. */
void ds_alist_foreach(const ds_alist_t *l, ds_alist_visit_func_t f, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_ASSOCIATION_LIST_H */
