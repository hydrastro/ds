#ifndef DS_COALESCED_MAP_H
#define DS_COALESCED_MAP_H

/*
 * Coalesced hashing (Williams, 1959; analysed by Vitter): chaining inside
 * the table. Keys and values are stored by value in an array of slots;
 * colliding keys are linked through slot indices, so chains from different
 * home addresses may merge ("coalesce"). Like open addressing there is no
 * per-entry allocation; like chaining, lookups follow only links.
 *
 * The table has `capacity` slots of which the last `cellar` ones are never
 * home addresses: overflow goes there first, which delays coalescing. With
 * the address region at about 86% of the table (Vitter's optimum, the
 * default) unsuccessful searches cost close to separate chaining. A cellar
 * of 0 is plain coalesced hashing.
 *
 * Insertion policies (ds_coalesced_map_policy_t):
 *   DS_COALESCED_LISCH  late insertion: the new key is appended at the end
 *                       of its chain (the classic algorithm)
 *   DS_COALESCED_EISCH  early insertion: linked right after the home slot,
 *                       which keeps recently inserted keys near the front
 * Free slots are taken from the highest index down, so the cellar fills
 * first.
 *
 * Removal unlinks the entry and re-inserts the entries that followed it in
 * its chain, so the table never needs tombstones and stays as if the key
 * had never been inserted.
 *
 * Storage: dynamic through a ds_context_t (doubling when no slot is free),
 * or one caller block of ds_coalesced_map_buffer_size() bytes (never
 * allocates, DS_ERR_OVERFLOW when full: it may be filled to 100%).
 * Hash/equal have rh_map's shape.
 *
 * Complexity: expected O(1) get/put at bounded load; remove O(chain).
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

typedef unsigned long (*ds_coalesced_map_hash_func_t)(const void *key,
                                                      void *user);
typedef bool (*ds_coalesced_map_equal_func_t)(const void *a, const void *b,
                                              void *user);

typedef enum ds_coalesced_map_policy {
  DS_COALESCED_LISCH = 0,
  DS_COALESCED_EISCH = 1
} ds_coalesced_map_policy_t;

typedef struct ds_coalesced_map_config {
  size_t key_size;
  size_t value_size; /* may be 0 for a set */
  ds_coalesced_map_hash_func_t hash;
  ds_coalesced_map_equal_func_t equal;
  void *user;
  ds_coalesced_map_policy_t policy;
  /* Address region as a fraction of the table, in percent (1..100,
   * default 86); the rest is the cellar. */
  unsigned int address_percent;
  ds_context_t *context;
} ds_coalesced_map_config_t;

typedef struct ds_coalesced_map {
  size_t *next;          /* link per slot, (size_t)-1 = end */
  size_t *prev;          /* back link, (size_t)-1 = chain head */
  unsigned long *hashes; /* mixed hash per slot */
  unsigned char *used;
  unsigned char *keys;   /* capacity + 1 slots; the last one is scratch */
  unsigned char *values; /* capacity + 1 slots */
  size_t capacity;
  size_t address; /* home addresses are 0 .. address - 1 */
  size_t size;
  size_t free_hint; /* free slots are searched downwards from here */
  ds_coalesced_map_config_t config;
  void *block;
  bool fixed;
} ds_coalesced_map_t;

void ds_coalesced_map_config_init(ds_coalesced_map_config_t *config);
size_t ds_coalesced_map_buffer_size(const ds_coalesced_map_config_t *config,
                                    size_t capacity);
ds_status_t ds_coalesced_map_init(ds_coalesced_map_t *map,
                                  const ds_coalesced_map_config_t *config,
                                  size_t initial_capacity);
ds_status_t
ds_coalesced_map_init_buffer(ds_coalesced_map_t *map,
                             const ds_coalesced_map_config_t *config,
                             void *buffer, size_t capacity);
void ds_coalesced_map_destroy(ds_coalesced_map_t *map);

ds_status_t ds_coalesced_map_put(ds_coalesced_map_t *map, const void *key,
                                 const void *value);
ds_status_t ds_coalesced_map_insert(ds_coalesced_map_t *map, const void *key,
                                    const void *value);
void *ds_coalesced_map_emplace(ds_coalesced_map_t *map, const void *key,
                               bool *out_inserted);
void *ds_coalesced_map_get(const ds_coalesced_map_t *map, const void *key);
bool ds_coalesced_map_contains(const ds_coalesced_map_t *map, const void *key);
ds_status_t ds_coalesced_map_remove(ds_coalesced_map_t *map, const void *key,
                                    void *out_value);
void ds_coalesced_map_clear(ds_coalesced_map_t *map);

size_t ds_coalesced_map_size(const ds_coalesced_map_t *map);
size_t ds_coalesced_map_capacity(const ds_coalesced_map_t *map);
/* Links followed by a lookup of a present key (0 = in its home slot), or
 * (size_t)-1. */
size_t ds_coalesced_map_probe_length(const ds_coalesced_map_t *map,
                                     const void *key);
bool ds_coalesced_map_next(const ds_coalesced_map_t *map, size_t *cursor,
                           const void **out_key, void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* DS_COALESCED_MAP_H */
