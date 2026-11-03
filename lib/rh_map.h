#ifndef DS_RH_MAP_H
#define DS_RH_MAP_H

/*
 * Robin Hood hash map: open addressing with linear probing, keys and values
 * stored by value.
 *
 * On insertion an entry that is closer to its home slot yields to one that
 * is further away ("take from the rich"), which keeps probe lengths short
 * and their variance low even at high load. Deletion shifts the following
 * entries back instead of leaving tombstones, so lookups never slow down
 * after many removals. The full hash of each entry is stored, so most
 * mismatches are rejected without calling the equality function and
 * resizing never rehashes keys.
 *
 * Keys and values live in two separate arrays, so a value pointer has the
 * alignment of value_size-sized array elements and can be cast to the
 * user's type. Pointers returned by get/emplace stay valid until the next
 * insertion or removal.
 *
 * The user hash is mixed before use, so a weak hash (e.g. the identity on
 * integers) still spreads well. Ready-made hash/equal functions for
 * unsigned long, size_t and C strings (char * keys) are provided.
 *
 * Storage: dynamic (ds_rh_map_init, grows at 7/8 load) or four caller
 * arrays of `capacity` entries, capacity a power of two
 * (ds_rh_map_init_buffer, never allocates; DS_ERR_OVERFLOW beyond 7/8).
 *
 * Complexity: expected O(1) get, put, remove; O(capacity) iteration.
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

typedef unsigned long (*ds_rh_map_hash_func_t)(const void *key, void *user);
typedef bool (*ds_rh_map_equal_func_t)(const void *a, const void *b,
                                       void *user);

typedef struct ds_rh_map_config {
  size_t key_size;
  size_t value_size;
  ds_rh_map_hash_func_t hash;
  ds_rh_map_equal_func_t equal;
  void *user;
  ds_context_t *context;
} ds_rh_map_config_t;

typedef struct ds_rh_map {
  unsigned char *keys;
  unsigned char *values;
  unsigned long *hashes;
  unsigned int *dist; /* probe distance + 1; 0 marks an empty slot */
  size_t capacity;
  size_t size;
  size_t key_size;
  size_t value_size;
  ds_rh_map_hash_func_t hash;
  ds_rh_map_equal_func_t equal;
  void *user;
  ds_context_t *context;
  bool fixed;
} ds_rh_map_t;

void ds_rh_map_config_init(ds_rh_map_config_t *config);
ds_status_t ds_rh_map_init(ds_rh_map_t *map, const ds_rh_map_config_t *config,
                           size_t initial_capacity);
ds_status_t ds_rh_map_init_buffer(ds_rh_map_t *map,
                                  const ds_rh_map_config_t *config, void *keys,
                                  void *values, unsigned long *hashes,
                                  unsigned int *dist, size_t capacity);
void ds_rh_map_destroy(ds_rh_map_t *map);

ds_status_t ds_rh_map_put(ds_rh_map_t *map, const void *key, const void *value);
ds_status_t ds_rh_map_insert(ds_rh_map_t *map, const void *key,
                             const void *value);
void *ds_rh_map_emplace(ds_rh_map_t *map, const void *key, bool *out_inserted);
void *ds_rh_map_get(const ds_rh_map_t *map, const void *key);
bool ds_rh_map_contains(const ds_rh_map_t *map, const void *key);
ds_status_t ds_rh_map_remove(ds_rh_map_t *map, const void *key,
                             void *out_value);
void ds_rh_map_clear(ds_rh_map_t *map);
ds_status_t ds_rh_map_reserve(ds_rh_map_t *map, size_t count);

size_t ds_rh_map_size(const ds_rh_map_t *map);
size_t ds_rh_map_capacity(const ds_rh_map_t *map);
bool ds_rh_map_next(const ds_rh_map_t *map, size_t *cursor,
                    const void **out_key, void **out_value);

unsigned long ds_rh_map_hash_ulong(const void *key, void *user);
bool ds_rh_map_equal_ulong(const void *a, const void *b, void *user);
unsigned long ds_rh_map_hash_size(const void *key, void *user);
bool ds_rh_map_equal_size(const void *a, const void *b, void *user);
unsigned long ds_rh_map_hash_cstr(const void *key, void *user);
bool ds_rh_map_equal_cstr(const void *a, const void *b, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_RH_MAP_H */
