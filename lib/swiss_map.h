#ifndef DS_SWISS_MAP_H
#define DS_SWISS_MAP_H

/*
 * Swiss table: an open-addressing hash map in the style of Abseil's
 * flat_hash_map / Rust's hashbrown, with keys and values stored by value.
 *
 * Every slot has a one-byte control code: empty, deleted, or "full" plus
 * the low 7 bits of the key's hash (H2). Lookups probe whole groups of
 * control bytes at a time (one machine word: 8 slots with a 64-bit unsigned
 * long, 4 with 32-bit), matching H2 with word-parallel bit tricks, so a
 * lookup typically compares the key only once and stops at the first group
 * that contains an empty slot. Groups are probed triangularly, which visits
 * every group of a power-of-two table. The rest of the hash (H1) picks the
 * starting position. The user hash is mixed first, so weak hashes work.
 *
 * Removal leaves a tombstone; when tombstones fill the growth budget the
 * table is rehashed in place without allocating (the same algorithm as
 * Abseil's drop-deletes), and only a genuinely full table grows. Maximum
 * load is 7/8.
 *
 * Storage: dynamic (ds_swiss_map_init) through a ds_context_t, or caller
 * buffers (ds_swiss_map_init_buffer): control bytes of
 * DS_SWISS_MAP_CTRL_SIZE(capacity) bytes, capacity keys and capacity
 * values, capacity a power of two >= DS_SWISS_MAP_GROUP; never allocates,
 * DS_ERR_OVERFLOW once 7/8 full. Hash and equality functions have the same
 * shape as rh_map's, so ds_rh_map_hash_ulong() and friends can be used.
 *
 * Pointers returned by get/emplace stay valid until the next insertion or
 * removal. Complexity: expected O(1) get, put, remove; O(capacity)
 * iteration and rehash.
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

#define DS_SWISS_MAP_GROUP (sizeof(unsigned long))
#define DS_SWISS_MAP_CTRL_SIZE(capacity) ((capacity) + DS_SWISS_MAP_GROUP)

typedef unsigned long (*ds_swiss_map_hash_func_t)(const void *key, void *user);
typedef bool (*ds_swiss_map_equal_func_t)(const void *a, const void *b,
                                          void *user);

typedef struct ds_swiss_map_config {
  size_t key_size;
  size_t value_size; /* may be 0 for a set */
  ds_swiss_map_hash_func_t hash;
  ds_swiss_map_equal_func_t equal;
  void *user;
  ds_context_t *context;
} ds_swiss_map_config_t;

typedef struct ds_swiss_map {
  unsigned char *ctrl; /* capacity + GROUP bytes; the tail mirrors the head */
  unsigned char *keys;
  unsigned char *values;
  size_t capacity;
  size_t size;
  size_t deleted;
  size_t growth_left;
  ds_swiss_map_config_t config;
  bool fixed;
} ds_swiss_map_t;

void ds_swiss_map_config_init(ds_swiss_map_config_t *config);
ds_status_t ds_swiss_map_init(ds_swiss_map_t *map,
                              const ds_swiss_map_config_t *config,
                              size_t initial_capacity);
ds_status_t ds_swiss_map_init_buffer(ds_swiss_map_t *map,
                                     const ds_swiss_map_config_t *config,
                                     unsigned char *ctrl, void *keys,
                                     void *values, size_t capacity);
void ds_swiss_map_destroy(ds_swiss_map_t *map);

/* Insert or overwrite. */
ds_status_t ds_swiss_map_put(ds_swiss_map_t *map, const void *key,
                             const void *value);
/* Insert only: DS_EXISTS when the key is present (nothing changes). */
ds_status_t ds_swiss_map_insert(ds_swiss_map_t *map, const void *key,
                                const void *value);
/* The value slot for key, inserting the key (value uninitialized) when
 * absent. NULL on allocation failure or overflow. */
void *ds_swiss_map_emplace(ds_swiss_map_t *map, const void *key,
                           bool *out_inserted);
void *ds_swiss_map_get(const ds_swiss_map_t *map, const void *key);
bool ds_swiss_map_contains(const ds_swiss_map_t *map, const void *key);
ds_status_t ds_swiss_map_remove(ds_swiss_map_t *map, const void *key,
                                void *out_value);
void ds_swiss_map_clear(ds_swiss_map_t *map);
/* Make room for `count` entries without further rehashing. */
ds_status_t ds_swiss_map_reserve(ds_swiss_map_t *map, size_t count);
/* Rehash in place, dropping tombstones (never allocates). */
void ds_swiss_map_compact(ds_swiss_map_t *map);

size_t ds_swiss_map_size(const ds_swiss_map_t *map);
size_t ds_swiss_map_capacity(const ds_swiss_map_t *map);
/* Iterate: start with *cursor = 0. The map must not change meanwhile. */
bool ds_swiss_map_next(const ds_swiss_map_t *map, size_t *cursor,
                       const void **out_key, void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* DS_SWISS_MAP_H */
