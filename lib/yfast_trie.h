#ifndef DS_YFAST_TRIE_H
#define DS_YFAST_TRIE_H

/*
 * Y-fast tries (Willard): predecessor search over w-bit integer keys in
 * O(n) space, values stored by value.
 *
 * The keys are split into buckets of Theta(w) consecutive keys; an X-fast
 * trie (lib/xfast_trie, over the map backend chosen at init) indexes the
 * buckets by representative, the lower boundary of the key range each
 * bucket covers. A query finds its bucket with one X-fast floor, O(log w)
 * expected, then searches the bucket. A bucket splits above 2w keys and
 * merges with a neighbour below w/2, which changes the X-fast trie (O(w))
 * only once per Theta(w) updates: amortized O(1).
 *
 * Buckets are instances of the ordered-map capability (lib/ordered_iface)
 * chosen at init: sorted arrays (the default; updates move up to 2w
 * elements, O(w) cache-friendly word moves) or treaps (O(log w) updates,
 * the bound of Willard's original), or any other backend.
 */

#include "context.h"
#include "map_iface.h"
#include "ordered_iface.h"
#include "status.h"
#include "xfast_trie.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_yfast {
  unsigned bits;
  size_t value_size;
  size_t target;   /* w */
  ds_xfast_t reps; /* representative -> bucket id */
  const ds_ordered_ops_t *bucket_ops;
  void **buckets; /* bucket instances by id */
  size_t nbuckets;
  size_t bucket_capacity;
  size_t *free_ids;
  size_t nfree;
  size_t count;
  ds_context_t *context;
} ds_yfast_t;

/* sorted-array buckets */
ds_status_t ds_yfast_init(ds_yfast_t *t, unsigned bits, size_t value_size,
                          const ds_map_ops_t *ops, ds_context_t *context);
/* bucket_ops NULL selects sorted arrays */
ds_status_t ds_yfast_init_buckets(ds_yfast_t *t, unsigned bits,
                                  size_t value_size, const ds_map_ops_t *ops,
                                  const ds_ordered_ops_t *bucket_ops,
                                  ds_context_t *context);
void ds_yfast_destroy(ds_yfast_t *t);
size_t ds_yfast_size(const ds_yfast_t *t);
ds_status_t ds_yfast_insert(ds_yfast_t *t, unsigned long key,
                            const void *value);
ds_status_t ds_yfast_remove(ds_yfast_t *t, unsigned long key, void *out_value);
void *ds_yfast_find(const ds_yfast_t *t, unsigned long key);
ds_status_t ds_yfast_floor(const ds_yfast_t *t, unsigned long x,
                           unsigned long *out);
ds_status_t ds_yfast_ceiling(const ds_yfast_t *t, unsigned long x,
                             unsigned long *out);
ds_status_t ds_yfast_predecessor(const ds_yfast_t *t, unsigned long x,
                                 unsigned long *out);
ds_status_t ds_yfast_successor(const ds_yfast_t *t, unsigned long x,
                               unsigned long *out);
ds_status_t ds_yfast_min(const ds_yfast_t *t, unsigned long *out);
ds_status_t ds_yfast_max(const ds_yfast_t *t, unsigned long *out);
/* buckets in use (for inspecting the balance) */
size_t ds_yfast_buckets(const ds_yfast_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DS_YFAST_TRIE_H */
