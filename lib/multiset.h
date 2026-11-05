#ifndef DS_MULTISET_H
#define DS_MULTISET_H

/*
 * Multisets (bags): unordered collections where an element may occur
 * many times, stored as (element, multiplicity) pairs in an open
 * addressing hash table (linear probing, backward-shift deletion, so no
 * tombstones). Elements are stored by value; hashing and equality are
 * pluggable with a user pointer.
 *
 * Besides adding and removing occurrences: multiplicity queries, the
 * number of distinct elements and of occurrences, and the bag algebra in
 * place: sum (multiplicities add), union (maximum), intersection
 * (minimum), difference (truncated subtraction), and inclusion.
 *
 * Concurrency: a multiset may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*ds_multiset_hash_func_t)(const void *elem, void *user);
typedef bool (*ds_multiset_equal_func_t)(const void *a, const void *b,
                                         void *user);

typedef struct ds_multiset {
  unsigned char *keys;
  size_t *counts; /* 0 = empty slot */
  size_t slots;   /* power of two (or 0) */
  size_t distinct;
  size_t total;
  size_t elem_size;
  ds_multiset_hash_func_t hash;
  ds_multiset_equal_func_t equal;
  void *user;
  ds_context_t *context;
} ds_multiset_t;

typedef bool (*ds_multiset_visit_func_t)(const void *elem, size_t count,
                                         void *user);

ds_status_t ds_multiset_init(ds_multiset_t *m, size_t elem_size,
                             ds_multiset_hash_func_t hash,
                             ds_multiset_equal_func_t equal, void *user,
                             ds_context_t *context);
void ds_multiset_destroy(ds_multiset_t *m);
void ds_multiset_clear(ds_multiset_t *m);
/* Adds k occurrences (k = 0 is a no-op). */
ds_status_t ds_multiset_add(ds_multiset_t *m, const void *elem, size_t k);
/* Removes up to k occurrences; *out_removed (may be NULL) says how many.
 * DS_NOT_FOUND when the element was absent. */
ds_status_t ds_multiset_remove(ds_multiset_t *m, const void *elem, size_t k,
                               size_t *out_removed);
size_t ds_multiset_count(const ds_multiset_t *m, const void *elem);
size_t ds_multiset_distinct(const ds_multiset_t *m);
size_t ds_multiset_total(const ds_multiset_t *m);
/* Visits each distinct element once with its multiplicity. */
void ds_multiset_foreach(const ds_multiset_t *m, ds_multiset_visit_func_t f,
                         void *user);
/* In-place algebra with another multiset of the same element size (and
 * compatible hash/equality). */
ds_status_t ds_multiset_sum(ds_multiset_t *m, const ds_multiset_t *other);
ds_status_t ds_multiset_union(ds_multiset_t *m, const ds_multiset_t *other);
ds_status_t ds_multiset_intersect(ds_multiset_t *m, const ds_multiset_t *other);
ds_status_t ds_multiset_subtract(ds_multiset_t *m, const ds_multiset_t *other);
/* Every multiplicity in a is at most that in b. */
bool ds_multiset_included(const ds_multiset_t *a, const ds_multiset_t *b);

#ifdef __cplusplus
}
#endif

#endif /* DS_MULTISET_H */
