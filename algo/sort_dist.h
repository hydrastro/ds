#ifndef DS_ALGO_SORT_DIST_H
#define DS_ALGO_SORT_DIST_H

/*
 * Distribution sorts: they place items by (part of) their key instead of
 * only comparing them. Keys come from callbacks, so any element type
 * works:
 *   ds_key_func_t        unsigned long key (sort.h)
 *   ds_index_key_func_t  small integer key / bucket number (sort_classic.h)
 *   ds_byte_at_func_t    byte string: byte `depth` of the key, or -1 past
 *                        its end (for every depth >= its length), which
 *                        sorts before every byte
 *
 * Generic bucket engine:
 *   ds_bucket_sort        bucket (bin) sort / distribution sort: a bucket
 *                         function, stable distribution into scratch,
 *                         then a pluggable sort per bucket
 *   ds_range_sort         bucket sort whose buckets split the key range
 *                         evenly
 * Integer keys:
 *   ds_pigeonhole_sort    one hole per key value, items moved by value
 *   ds_rapid_sort_ulong   keys only: count, then write each key back as
 *                         often as it occurred
 *   ds_restricted_universe_sort  keys in [0, U): pigeonhole when U is
 *                         small against n, LSD radix otherwise
 *   ds_histogram_sort     interpolation sort: n buckets by linear
 *                         interpolation of the key, histogram, placement,
 *                         insertion sort of each bucket
 *   ds_address_calculation_sort  order-preserving hash into sorted lists
 *   ds_flash_sort         Neubert's flashsort: ~0.43 n classes,
 *                         in-place cycle permutation, insertion sort
 *   ds_linear_probing_sort  items placed by interpolation into a table of
 *                         2n slots, collisions kept sorted by shifting
 *                         (linear probing), then compacted
 *   ds_distributive_partitioning_sort  Dobosiewicz: n buckets by
 *                         interpolation between min, median and max,
 *                         applied again to every bucket
 * Byte-string keys:
 *   ds_msd_radix_sort     top-down (MSD) radix sort, stable, scratch
 *   ds_american_flag_sort in-place MSD radix sort (cycle permutation)
 *   ds_multikey_quicksort three-way radix quicksort (Bentley-Sedgewick)
 *   ds_blind_sort         builds a blind trie (branch depths only, one
 *                         full comparison per insertion) and walks it
 * Others:
 *   ds_postman_sort       MSD distribution over a list of key fields
 *                         (country, city, street, ...)
 *   ds_shuffle_sort       sorts the first n/8 items, uses them as n/8
 *                         bucket splitters for the rest
 *   ds_j_sort             strand sort below 40 items, shuffle sort above
 *
 * Every sort is iterative or recurses only O(log n) deep. Stability is
 * noted per function. Buffers are caller-provided where a size is known
 * up front; otherwise working memory comes from the ds_context_t.
 *
 * Runs:
 *   ds_unshuffle_sort     Kagel's UnShuffle sort: items are dealt onto
 *                         piles (double-ended queues kept in order, an
 *                         item going to the back or front of the first
 *                         pile that takes it, else onto a new pile), then
 *                         the piles are merged through a heap of their
 *                         heads; Theta(n) on sorted input, about
 *                         n K / 2 + n log K for K piles
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include "elem.h"
#include "sort.h"
#include "sort_classic.h"
#include "sort_misc.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ds_byte_at_func_t)(const void *elem, size_t depth, void *user);

/* bucket(elem) in [0, buckets) (else DS_ERR_RANGE, nothing moved), and the
 * bucket order must agree with compare. scratch: count elements, counts:
 * buckets + 1 entries. per_bucket NULL: insertion sort. Stable when
 * per_bucket is. */
ds_status_t ds_bucket_sort(void *base, size_t count, size_t elem_size,
                           ds_index_key_func_t bucket, size_t buckets,
                           ds_compare_func_t compare, void *user,
                           ds_sort_func_t per_bucket, void *scratch,
                           size_t *counts);
/* Buckets of equal key width over [min key, max key]. Stable (insertion
 * sort per bucket). */
ds_status_t ds_range_sort(void *base, size_t count, size_t elem_size,
                          ds_key_func_t key, ds_compare_func_t compare,
                          void *user, size_t buckets, void *scratch,
                          size_t *counts);
/* key(elem) < range; counts: range + 1 entries; scratch: count elements.
 * Stable. */
ds_status_t ds_pigeonhole_sort(void *base, size_t count, size_t elem_size,
                               ds_index_key_func_t key, void *user,
                               size_t range, size_t *counts, void *scratch);
/* values in [min_value, min_value + range); counts: range entries. */
ds_status_t ds_rapid_sort_ulong(unsigned long *values, size_t count,
                                unsigned long min_value, size_t range,
                                size_t *counts);
ds_status_t ds_restricted_universe_sort(void *base, size_t count,
                                        size_t elem_size,
                                        ds_index_key_func_t key, void *user,
                                        size_t universe, ds_context_t *context);
/* compare must agree with key order. scratch: count elements, counts:
 * count + 1 entries. Stable. ds_interpolation_sort is the same. */
ds_status_t ds_histogram_sort(void *base, size_t count, size_t elem_size,
                              ds_key_func_t key, ds_compare_func_t compare,
                              void *user, void *scratch, size_t *counts);
ds_status_t ds_interpolation_sort(void *base, size_t count, size_t elem_size,
                                  ds_key_func_t key, ds_compare_func_t compare,
                                  void *user, void *scratch, size_t *counts);
/* buckets 0: count. Stable. */
ds_status_t ds_address_calculation_sort(void *base, size_t count,
                                        size_t elem_size, ds_key_func_t key,
                                        ds_compare_func_t compare, void *user,
                                        size_t buckets, ds_context_t *context);
/* classes 0: about 0.43 count. Not stable. */
ds_status_t ds_flash_sort(void *base, size_t count, size_t elem_size,
                          ds_key_func_t key, ds_compare_func_t compare,
                          void *user, size_t classes, ds_context_t *context);
/* Stable. DS_ERR_OVERFLOW (input unchanged) if the overflow area runs
 * out (heavily skewed keys). */
ds_status_t ds_linear_probing_sort(void *base, size_t count, size_t elem_size,
                                   ds_key_func_t key, ds_compare_func_t compare,
                                   void *user, ds_context_t *context);
/* Not stable. */
ds_status_t
ds_distributive_partitioning_sort(void *base, size_t count, size_t elem_size,
                                  ds_key_func_t key, ds_compare_func_t compare,
                                  void *user, ds_context_t *context);

/* The byte-string sorts keep their pending segments on a stack that
 * grows with key length; it starts on the C stack and only takes memory
 * from the context for very long shared prefixes. */
/* Stable; scratch: count elements. */
ds_status_t ds_msd_radix_sort(void *base, size_t count, size_t elem_size,
                              ds_byte_at_func_t byte_at, void *user,
                              void *scratch, ds_context_t *context);
/* Not stable; moves elements in place. */
ds_status_t ds_american_flag_sort(void *base, size_t count, size_t elem_size,
                                  ds_byte_at_func_t byte_at, void *user,
                                  ds_context_t *context);
/* Not stable; in place. Also known as three-way radix quicksort. */
ds_status_t ds_multikey_quicksort(void *base, size_t count, size_t elem_size,
                                  ds_byte_at_func_t byte_at, void *user,
                                  ds_context_t *context);
/* Stable. */
ds_status_t ds_blind_sort(void *base, size_t count, size_t elem_size,
                          ds_byte_at_func_t byte_at, void *user,
                          ds_context_t *context);

typedef struct ds_postman_field {
  ds_index_key_func_t key; /* value in [0, range) */
  size_t range;
} ds_postman_field_t;
/* Lexicographic by fields[0], fields[1], ... Stable. */
ds_status_t ds_postman_sort(void *base, size_t count, size_t elem_size,
                            const ds_postman_field_t *fields,
                            size_t field_count, void *user,
                            ds_context_t *context);

/* Not stable. */
ds_status_t ds_shuffle_sort(void *base, size_t count, size_t elem_size,
                            ds_compare_func_t compare, void *user,
                            ds_context_t *context);
ds_status_t ds_j_sort(void *base, size_t count, size_t elem_size,
                      ds_compare_func_t compare, void *user,
                      ds_context_t *context);

/* out_piles (may be NULL) receives the number of piles. Needs O(n)
 * scratch from the context. */
ds_status_t ds_unshuffle_sort(void *base, size_t count, size_t elem_size,
                              ds_compare_func_t compare, void *user,
                              size_t *out_piles, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_SORT_DIST_H */
