#ifndef DS_LINEAR_HASH_MAP_H
#define DS_LINEAR_HASH_MAP_H

/*
 * Linear hashing (Litwin, 1980): a dynamic hash map that grows and shrinks
 * one bucket at a time, with keys and values stored by value.
 *
 * Buckets are split in a fixed round-robin order given by a split pointer,
 * not necessarily the bucket that overflowed: bucket s splits into s and
 * s + N * 2^level, its entries rehashed with one more bit. Addressing uses
 * h mod (N * 2^level), or one more bit for buckets already split this
 * round. So the table never rehashes everything at once and the per
 * operation worst case stays O(bucket size), which is why linear hashing is
 * the classic choice for files and incremental growth.
 *
 * Split policies (ds_linear_hash_split_t):
 *   DS_LINEAR_HASH_LOAD      controlled splitting: split while the load
 *                            (entries / (buckets * bucket_capacity))
 *                            exceeds max_load; merge below min_load
 *   DS_LINEAR_HASH_OVERFLOW  uncontrolled splitting: split once whenever
 *                            an insertion overflows its bucket's nominal
 *                            capacity (Litwin's original rule)
 * Buckets are elastic (they hold any number of entries); bucket_capacity
 * is the nominal size used by the policies.
 *
 * Storage comes from a ds_context_t. Entry pointers returned by get and
 * emplace stay valid until the next insertion or removal. Hash/equal have
 * rh_map's shape.
 *
 * Complexity: expected O(bucket_capacity) per operation, no global rehash.
 * Concurrency: a map may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include "vec.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long (*ds_linear_hash_map_hash_func_t)(const void *key,
                                                        void *user);
typedef bool (*ds_linear_hash_map_equal_func_t)(const void *a, const void *b,
                                                void *user);

typedef enum ds_linear_hash_split {
  DS_LINEAR_HASH_LOAD = 0,
  DS_LINEAR_HASH_OVERFLOW = 1
} ds_linear_hash_split_t;

typedef struct ds_linear_hash_map_config {
  size_t key_size;
  size_t value_size; /* may be 0 for a set */
  ds_linear_hash_map_hash_func_t hash;
  ds_linear_hash_map_equal_func_t equal;
  void *user;
  ds_linear_hash_split_t split;
  size_t initial_buckets; /* N, default 4 */
  size_t bucket_capacity; /* nominal entries per bucket, default 4 */
  double max_load;        /* default 0.8 (DS_LINEAR_HASH_LOAD) */
  double min_load;        /* default 0.3; 0 disables merging */
  ds_context_t *context;
} ds_linear_hash_map_config_t;

typedef struct ds_linear_hash_map {
  ds_vec_t buckets; /* of ds_vec_t, each holding records */
  size_t level;
  size_t split; /* next bucket to split */
  size_t size;
  size_t record_size;
  size_t key_offset;
  size_t value_offset;
  ds_linear_hash_map_config_t config;
} ds_linear_hash_map_t;

void ds_linear_hash_map_config_init(ds_linear_hash_map_config_t *config);
ds_status_t ds_linear_hash_map_init(ds_linear_hash_map_t *map,
                                    const ds_linear_hash_map_config_t *config);
void ds_linear_hash_map_destroy(ds_linear_hash_map_t *map);

ds_status_t ds_linear_hash_map_put(ds_linear_hash_map_t *map, const void *key,
                                   const void *value);
ds_status_t ds_linear_hash_map_insert(ds_linear_hash_map_t *map,
                                      const void *key, const void *value);
void *ds_linear_hash_map_emplace(ds_linear_hash_map_t *map, const void *key,
                                 bool *out_inserted);
void *ds_linear_hash_map_get(const ds_linear_hash_map_t *map, const void *key);
bool ds_linear_hash_map_contains(const ds_linear_hash_map_t *map,
                                 const void *key);
ds_status_t ds_linear_hash_map_remove(ds_linear_hash_map_t *map,
                                      const void *key, void *out_value);
void ds_linear_hash_map_clear(ds_linear_hash_map_t *map);

size_t ds_linear_hash_map_size(const ds_linear_hash_map_t *map);
size_t ds_linear_hash_map_bucket_count(const ds_linear_hash_map_t *map);
/* Iterate: zero the cursor first. The map must not change meanwhile. */
typedef struct ds_linear_hash_map_cursor {
  size_t bucket;
  size_t index;
} ds_linear_hash_map_cursor_t;
bool ds_linear_hash_map_next(const ds_linear_hash_map_t *map,
                             ds_linear_hash_map_cursor_t *cursor,
                             const void **out_key, void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* DS_LINEAR_HASH_MAP_H */
