#ifndef DS_LRU_CACHE_H
#define DS_LRU_CACHE_H

/*
 * LRU cache: a fixed-capacity key/value cache that evicts the least
 * recently used entry when full. Keys and values are stored by value.
 *
 * A Robin Hood map (rh_map.h) maps each key to a node index; nodes are kept
 * in a recency list linked by indices. All memory is allocated once at init
 * (capacity entries), so the cache never allocates afterwards.
 *
 * get() counts as a use and moves the entry to the front; peek() does not.
 * put() inserts or updates; when it evicts, the evicted pair can be copied
 * out (e.g. to write back a dirty entry).
 *
 * Complexity: get, peek, put, remove expected O(1).
 * Concurrency: a cache may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "rh_map.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_lru_cache {
  ds_rh_map_t index; /* key -> node */
  unsigned char *keys;
  unsigned char *values;
  size_t *prev;
  size_t *next;
  size_t head; /* most recently used */
  size_t tail; /* least recently used */
  size_t free_head;
  size_t size;
  size_t capacity;
  size_t key_size;
  size_t value_size;
  ds_context_t *context;
} ds_lru_cache_t;

/* config.key_size, value_size, hash, equal and user describe the entries;
 * config.context supplies the memory. */
ds_status_t ds_lru_cache_init(ds_lru_cache_t *cache,
                              const ds_rh_map_config_t *config,
                              size_t capacity);
void ds_lru_cache_destroy(ds_lru_cache_t *cache);

void *ds_lru_cache_get(ds_lru_cache_t *cache, const void *key);
void *ds_lru_cache_peek(const ds_lru_cache_t *cache, const void *key);
ds_status_t ds_lru_cache_put(ds_lru_cache_t *cache, const void *key,
                             const void *value, void *evicted_key,
                             void *evicted_value, bool *out_evicted);
ds_status_t ds_lru_cache_remove(ds_lru_cache_t *cache, const void *key,
                                void *out_value);
const void *ds_lru_cache_oldest_key(const ds_lru_cache_t *cache);
void ds_lru_cache_clear(ds_lru_cache_t *cache);
size_t ds_lru_cache_size(const ds_lru_cache_t *cache);
size_t ds_lru_cache_capacity(const ds_lru_cache_t *cache);

#ifdef __cplusplus
}
#endif

#endif /* DS_LRU_CACHE_H */
