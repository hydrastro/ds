#ifndef DS_CUCKOO_FILTER_H
#define DS_CUCKOO_FILTER_H

/*
 * Cuckoo filter (Fan et al., 2014): approximate set membership that, unlike
 * a Bloom filter, supports deletion. Each item is stored as a 16-bit
 * fingerprint in one of two buckets of four slots (partial-key cuckoo
 * hashing); the false-positive rate is about 8 / 2^16 ~ 0.012% and load
 * reaches ~95% before inserts start failing.
 *
 * Items are fed as hashes computed by the caller; a 64-bit hash is best for
 * very large filters. Only delete items that were inserted: deleting a
 * never-inserted item can remove another item's fingerprint.
 *
 * When an insert needs more than the maximum number of kicks, the item is
 * still stored: the fingerprint displaced last is kept in a victim slot, so
 * no inserted item is ever lost. The filter is then full, and further
 * inserts return DS_ERR_OVERFLOW until a deletion frees space.
 *
 * Storage: dynamic (ds_cuckoo_filter_init) or a caller array of
 * buckets * 4 unsigned shorts (ds_cuckoo_filter_init_buffer). The bucket
 * count must be a power of two; ds_cuckoo_filter_buckets_for() sizes it.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_CUCKOO_SLOTS 4

typedef struct ds_cuckoo_filter {
  unsigned short *table;
  size_t buckets;
  size_t count;
  unsigned long rng;
  unsigned short victim;
  size_t victim_bucket;
  ds_context_t *context;
  bool fixed;
} ds_cuckoo_filter_t;

size_t ds_cuckoo_filter_buckets_for(size_t items);
ds_status_t ds_cuckoo_filter_init(ds_cuckoo_filter_t *filter, size_t buckets,
                                  ds_context_t *context);
ds_status_t ds_cuckoo_filter_init_buffer(ds_cuckoo_filter_t *filter,
                                         size_t buckets, unsigned short *table);
void ds_cuckoo_filter_destroy(ds_cuckoo_filter_t *filter);
ds_status_t ds_cuckoo_filter_insert(ds_cuckoo_filter_t *filter,
                                    unsigned long hash);
bool ds_cuckoo_filter_contains(const ds_cuckoo_filter_t *filter,
                               unsigned long hash);
ds_status_t ds_cuckoo_filter_remove(ds_cuckoo_filter_t *filter,
                                    unsigned long hash);
size_t ds_cuckoo_filter_count(const ds_cuckoo_filter_t *filter);
void ds_cuckoo_filter_clear(ds_cuckoo_filter_t *filter);

#ifdef __cplusplus
}
#endif

#endif /* DS_CUCKOO_FILTER_H */
