#ifndef DS_MAP_IFACE_H
#define DS_MAP_IFACE_H

/*
 * The associative-map capability: what a composite structure needs from
 * the map it is built on, so the backend can be chosen by the caller
 * (X-fast tries take their per-level prefix tables this way, and caches,
 * indexes or unique tables can do the same).
 *
 * A backend is a ds_map_ops_t: init/destroy plus put (insert or replace),
 * get, remove, size and an iteration cursor over keys and values stored by
 * value, with a hash and equality callback (and user pointer) chosen at
 * init. Its traits say what a consumer may rely on:
 *
 *   DS_MAP_STABLE_VALUES  value pointers from get stay valid across
 *                         insertions (not the case for open addressing)
 *   DS_MAP_EXPECTED_O1    expected O(1) operations under the hashing
 *                         assumptions (a parent's bounds inherit this)
 *   DS_MAP_WORST_O1_GET   worst-case O(1) lookups
 *
 * ds_map_t pairs an ops table with an instance allocated from the
 * context. Two backends ship: Robin Hood hashing (lib/rh_map) and the
 * Swiss table (lib/swiss_map); any other map can be adapted by filling in
 * a ds_map_ops_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long (*ds_map_hash_fn)(const void *key, void *user);
typedef bool (*ds_map_equal_fn)(const void *a, const void *b, void *user);

#define DS_MAP_STABLE_VALUES 0x1u
#define DS_MAP_EXPECTED_O1 0x2u
#define DS_MAP_WORST_O1_GET 0x4u

typedef struct ds_map_ops {
  const char *name;
  unsigned traits;
  size_t instance_size;
  ds_status_t (*init)(void *map, size_t key_size, size_t value_size,
                      ds_map_hash_fn hash, ds_map_equal_fn equal, void *user,
                      ds_context_t *context);
  void (*destroy)(void *map);
  ds_status_t (*put)(void *map, const void *key, const void *value);
  void *(*get)(const void *map, const void *key);
  /* DS_NOT_FOUND when absent; out_value (optional) gets the value */
  ds_status_t (*remove)(void *map, const void *key, void *out_value);
  size_t (*size)(const void *map);
  bool (*next)(const void *map, size_t *cursor, const void **key, void **value);
} ds_map_ops_t;

typedef struct ds_map {
  const ds_map_ops_t *ops;
  void *impl;
  ds_context_t *context;
} ds_map_t;

const ds_map_ops_t *ds_map_rh_ops(void);
const ds_map_ops_t *ds_map_swiss_ops(void);

/* ops NULL selects ds_map_rh_ops() */
ds_status_t ds_map_init(ds_map_t *map, const ds_map_ops_t *ops, size_t key_size,
                        size_t value_size, ds_map_hash_fn hash,
                        ds_map_equal_fn equal, void *user,
                        ds_context_t *context);
void ds_map_destroy(ds_map_t *map);
ds_status_t ds_map_put(ds_map_t *map, const void *key, const void *value);
void *ds_map_get(const ds_map_t *map, const void *key);
ds_status_t ds_map_remove(ds_map_t *map, const void *key, void *out_value);
size_t ds_map_size(const ds_map_t *map);
bool ds_map_next(const ds_map_t *map, size_t *cursor, const void **key,
                 void **value);

/* hash/equality for keys that are one unsigned long */
unsigned long ds_map_hash_ulong(const void *key, void *user);
bool ds_map_equal_ulong(const void *a, const void *b, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_MAP_IFACE_H */
