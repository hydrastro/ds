#ifndef DS_HASH_HEAP_H
#define DS_HASH_HEAP_H

/*
 * Hash heaps: a binary heap of (key, priority) entries plus a hash table
 * from key to heap position, so an entry can be found, re-prioritised or
 * removed by its key in O(log n) without the caller tracking handles
 * (lib/indexed_heap needs small integer ids; this takes arbitrary keys).
 *
 * Keys and priorities are stored by value (fixed sizes); key hashing and
 * equality and the priority order are pluggable. The top is the entry
 * whose priority compares smallest.
 *
 * Concurrency: a heap may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*ds_hash_heap_hash_func_t)(const void *key, void *user);
typedef bool (*ds_hash_heap_equal_func_t)(const void *a, const void *b,
                                          void *user);
typedef int (*ds_hash_heap_compare_func_t)(const void *a, const void *b,
                                           void *user);

typedef struct ds_hash_heap {
  unsigned char *keys; /* heap order */
  unsigned char *prios;
  size_t size;
  size_t capacity;
  size_t *table; /* heap position + 1, 0 empty */
  size_t slots;  /* power of two */
  size_t key_size;
  size_t prio_size;
  ds_hash_heap_hash_func_t hash;
  ds_hash_heap_equal_func_t equal;
  ds_hash_heap_compare_func_t compare;
  void *user;
  unsigned char *tmp; /* one entry */
  ds_context_t *context;
} ds_hash_heap_t;

ds_status_t ds_hash_heap_init(ds_hash_heap_t *h, size_t key_size,
                              size_t prio_size, ds_hash_heap_hash_func_t hash,
                              ds_hash_heap_equal_func_t equal,
                              ds_hash_heap_compare_func_t compare, void *user,
                              ds_context_t *context);
void ds_hash_heap_destroy(ds_hash_heap_t *h);
size_t ds_hash_heap_size(const ds_hash_heap_t *h);
/* Inserts, or changes the priority of a present key (DS_EXISTS). */
ds_status_t ds_hash_heap_push(ds_hash_heap_t *h, const void *key,
                              const void *prio);
ds_status_t ds_hash_heap_peek(const ds_hash_heap_t *h, void *out_key,
                              void *out_prio);
ds_status_t ds_hash_heap_pop(ds_hash_heap_t *h, void *out_key, void *out_prio);
ds_status_t ds_hash_heap_priority(const ds_hash_heap_t *h, const void *key,
                                  void *out_prio);
ds_status_t ds_hash_heap_remove(ds_hash_heap_t *h, const void *key,
                                void *out_prio);
bool ds_hash_heap_contains(const ds_hash_heap_t *h, const void *key);

#ifdef __cplusplus
}
#endif

#endif /* DS_HASH_HEAP_H */
