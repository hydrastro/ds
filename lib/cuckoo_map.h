#ifndef DS_CUCKOO_MAP_H
#define DS_CUCKOO_MAP_H

/*
 * Cuckoo hash map (Pagh and Rodler, 2001), generalised: d-ary choices,
 * bucketized slots and a stash; keys and values stored by value.
 *
 * Every key may live in one of `ways` buckets (2..4) chosen by independent
 * hashes of its (mixed) hash, each bucket holding `slots` entries (1..8).
 * Lookups therefore probe at most ways * slots positions plus the stash:
 * worst-case O(1). An insertion that finds its buckets full searches
 * breadth-first for the shortest chain of displacements ending in a free
 * slot (as libcuckoo does) and only then moves entries, so a failed
 * insertion changes nothing. When no chain is found within the search
 * budget the key goes to a small stash; when the stash is full too, a
 * dynamic map doubles, a fixed one reports DS_ERR_OVERFLOW. More than
 * ways * slots + stash keys with the same full hash cannot be stored at any
 * size; such an insertion (detected as a failure below 1/8 load) returns
 * DS_ERR_OVERFLOW instead of growing without bound.
 *
 * Typical loads: 2 ways x 4 slots ~95%, 2 x 1 ~50%, 3 x 1 ~91%; the
 * classic two-table cuckoo hashing is ways = 2, slots = 1.
 *
 * Storage: dynamic through a ds_context_t, or one caller block of
 * ds_cuckoo_map_buffer_size(&config, bucket_count) bytes, bucket_count a
 * power of two (never allocates). Hash/equal have rh_map's shape.
 *
 * Pointers returned by get/emplace stay valid until the next insertion or
 * removal. Complexity: get and remove O(ways * slots); put expected O(1).
 * Concurrency: a map may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_CUCKOO_MAP_MAX_WAYS 4
#define DS_CUCKOO_MAP_MAX_SLOTS 8

typedef unsigned long (*ds_cuckoo_map_hash_func_t)(const void *key, void *user);
typedef bool (*ds_cuckoo_map_equal_func_t)(const void *a, const void *b,
                                           void *user);

typedef struct ds_cuckoo_map_config {
  size_t key_size;
  size_t value_size; /* may be 0 for a set */
  ds_cuckoo_map_hash_func_t hash;
  ds_cuckoo_map_equal_func_t equal;
  void *user;
  unsigned int ways;  /* candidate buckets per key, 2..4 (default 2) */
  unsigned int slots; /* entries per bucket, 1..8 (default 4) */
  size_t stash;       /* stash entries (default 4) */
  size_t search;      /* BFS budget in buckets (default 256) */
  ds_context_t *context;
} ds_cuckoo_map_config_t;

typedef struct ds_cuckoo_map {
  unsigned long *hashes; /* per slot (buckets * slots + stash) */
  unsigned char *used;
  unsigned char *keys;
  unsigned char *values;
  size_t bucket_count;
  size_t size;
  size_t stash_used;
  unsigned long seed; /* changes on every rehash */
  ds_cuckoo_map_config_t config;
  void *block;
  bool fixed;
} ds_cuckoo_map_t;

void ds_cuckoo_map_config_init(ds_cuckoo_map_config_t *config);
size_t ds_cuckoo_map_buffer_size(const ds_cuckoo_map_config_t *config,
                                 size_t bucket_count);
ds_status_t ds_cuckoo_map_init(ds_cuckoo_map_t *map,
                               const ds_cuckoo_map_config_t *config,
                               size_t initial_capacity);
ds_status_t ds_cuckoo_map_init_buffer(ds_cuckoo_map_t *map,
                                      const ds_cuckoo_map_config_t *config,
                                      void *buffer, size_t bucket_count);
void ds_cuckoo_map_destroy(ds_cuckoo_map_t *map);

ds_status_t ds_cuckoo_map_put(ds_cuckoo_map_t *map, const void *key,
                              const void *value);
ds_status_t ds_cuckoo_map_insert(ds_cuckoo_map_t *map, const void *key,
                                 const void *value);
void *ds_cuckoo_map_emplace(ds_cuckoo_map_t *map, const void *key,
                            bool *out_inserted);
void *ds_cuckoo_map_get(const ds_cuckoo_map_t *map, const void *key);
bool ds_cuckoo_map_contains(const ds_cuckoo_map_t *map, const void *key);
ds_status_t ds_cuckoo_map_remove(ds_cuckoo_map_t *map, const void *key,
                                 void *out_value);
void ds_cuckoo_map_clear(ds_cuckoo_map_t *map);
ds_status_t ds_cuckoo_map_reserve(ds_cuckoo_map_t *map, size_t count);

size_t ds_cuckoo_map_size(const ds_cuckoo_map_t *map);
/* Slots in the table proper (bucket_count * slots). */
size_t ds_cuckoo_map_capacity(const ds_cuckoo_map_t *map);
size_t ds_cuckoo_map_stash_size(const ds_cuckoo_map_t *map);
bool ds_cuckoo_map_next(const ds_cuckoo_map_t *map, size_t *cursor,
                        const void **out_key, void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* DS_CUCKOO_MAP_H */
