#ifndef DS_SIEVE_CACHE_H
#define DS_SIEVE_CACHE_H

/*
 * SIEVE cache (Zhang et al., NSDI 2024): a fixed-capacity key/value cache
 * whose eviction is simpler than LRU and usually achieves a lower miss
 * ratio on web and storage workloads.
 *
 * Entries sit in a FIFO queue, each with a "visited" bit. A hit only sets
 * the bit (no list manipulation, unlike LRU). On eviction a hand sweeps from
 * the oldest entry towards the newest, clearing visited bits, and evicts the
 * first unvisited entry; the hand keeps its position between evictions.
 *
 * Keys map to node indices through rh_map; all memory is allocated at init,
 * so the cache never allocates afterwards.
 *
 * Complexity: get, put, remove expected O(1); eviction amortized O(1).
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

typedef struct ds_sieve_cache {
  ds_rh_map_t index; /* key -> node */
  unsigned char *keys;
  unsigned char *values;
  unsigned char *visited;
  size_t *prev; /* towards the newer end */
  size_t *next; /* towards the older end */
  size_t head;  /* newest */
  size_t tail;  /* oldest */
  size_t hand;
  size_t free_head;
  size_t size;
  size_t capacity;
  size_t key_size;
  size_t value_size;
  ds_context_t *context;
} ds_sieve_cache_t;

ds_status_t ds_sieve_cache_init(ds_sieve_cache_t *cache,
                                const ds_rh_map_config_t *config,
                                size_t capacity);
void ds_sieve_cache_destroy(ds_sieve_cache_t *cache);
void *ds_sieve_cache_get(ds_sieve_cache_t *cache, const void *key);
void *ds_sieve_cache_peek(const ds_sieve_cache_t *cache, const void *key);
ds_status_t ds_sieve_cache_put(ds_sieve_cache_t *cache, const void *key,
                               const void *value, void *evicted_key,
                               void *evicted_value, bool *out_evicted);
ds_status_t ds_sieve_cache_remove(ds_sieve_cache_t *cache, const void *key,
                                  void *out_value);
void ds_sieve_cache_clear(ds_sieve_cache_t *cache);
size_t ds_sieve_cache_size(const ds_sieve_cache_t *cache);
size_t ds_sieve_cache_capacity(const ds_sieve_cache_t *cache);

#ifdef __cplusplus
}
#endif

#endif /* DS_SIEVE_CACHE_H */
