#ifndef DS_RADIX_HEAP_H
#define DS_RADIX_HEAP_H

/*
 * Radix heap (Ahuja, Mehlhorn, Orlin and Tarjan, 1990): a monotone priority
 * queue over unsigned long keys with a by-value payload of elem_size bytes
 * (0 for keys only).
 *
 * Monotone means keys never go below the last key popped, which is exactly
 * what Dijkstra's algorithm and event simulations need. Entries live in
 * W + 1 buckets (W = bits of unsigned long): bucket 0 holds keys equal to
 * the last popped key, bucket i holds keys whose highest bit differing from
 * it is bit i - 1. Popping from an empty bucket 0 takes the first non-empty
 * bucket, makes its minimum the new reference and redistributes it into
 * lower buckets; each entry moves down at most W times.
 *
 * Pushing a key smaller than the last popped key returns DS_ERR_RANGE. A
 * redistribution reserves room in its target buckets first, so a failing
 * allocation leaves the heap unchanged.
 *
 * Complexity: push O(1); pop and peek O(W) amortized per entry. Entries with
 * equal keys come out in no particular order.
 * Concurrency: a heap may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include "vec.h"
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_RADIX_HEAP_BUCKETS (CHAR_BIT * sizeof(unsigned long) + 1)

typedef struct ds_radix_heap {
  ds_vec_t buckets[DS_RADIX_HEAP_BUCKETS];
  unsigned long last; /* last popped key; every key is >= last */
  size_t size;
  size_t elem_size;
  ds_context_t *context;
} ds_radix_heap_t;

ds_status_t ds_radix_heap_init(ds_radix_heap_t *heap, size_t elem_size,
                               ds_context_t *context);
void ds_radix_heap_destroy(ds_radix_heap_t *heap);
/* Empty the heap and reset the reference key to 0. */
void ds_radix_heap_clear(ds_radix_heap_t *heap);

/* elem may be NULL when elem_size is 0. */
ds_status_t ds_radix_heap_push(ds_radix_heap_t *heap, unsigned long key,
                               const void *elem);
/* Copy the minimum's key and payload out (either may be NULL) and remove
 * it. DS_EMPTY when empty. */
ds_status_t ds_radix_heap_pop(ds_radix_heap_t *heap, unsigned long *out_key,
                              void *out_elem);
/* Look at the minimum without removing it (may redistribute internally).
 * The payload pointer stays valid until the next push or pop. */
ds_status_t ds_radix_heap_peek(ds_radix_heap_t *heap, unsigned long *out_key,
                               void **out_elem);

size_t ds_radix_heap_size(const ds_radix_heap_t *heap);
bool ds_radix_heap_is_empty(const ds_radix_heap_t *heap);
unsigned long ds_radix_heap_last_key(const ds_radix_heap_t *heap);

#ifdef __cplusplus
}
#endif

#endif /* DS_RADIX_HEAP_H */
