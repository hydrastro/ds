#ifndef DS_EXTENDIBLE_HASH_H
#define DS_EXTENDIBLE_HASH_H

/*
 * Extendible hashing (Fagin, Nievergelt, Pippenger and Strong, 1979): a
 * dynamic hash map made of fixed-capacity buckets reached through a
 * directory of 2^global_depth pointers, keys and values stored by value.
 *
 * A key's bucket is the directory entry indexed by the low global_depth bits
 * of its (mixed) hash. Each bucket has a local depth: the number of low bits
 * its keys share; 2^(global - local) directory entries point to it. A full
 * bucket splits on its next bit, and only when its local depth equals the
 * global depth does the directory double. Removal merges a bucket with its
 * buddy when both fit in one, and halves the directory when every bucket
 * allows it. A lookup therefore touches one directory entry and one bucket,
 * which is why the scheme suits disk pages.
 *
 * More than bucket_capacity keys sharing every hash bit cannot be separated;
 * such an insertion returns DS_ERR_OVERFLOW.
 *
 * Storage comes from a ds_context_t. Entry pointers returned by get and
 * emplace stay valid until the next insertion or removal. Hash/equal have
 * rh_map's shape.
 *
 * Complexity: O(bucket_capacity) get, put, remove (plus an occasional
 * directory doubling or halving).
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

typedef unsigned long (*ds_extendible_hash_hash_func_t)(const void *key,
                                                        void *user);
typedef bool (*ds_extendible_hash_equal_func_t)(const void *a, const void *b,
                                                void *user);

typedef struct ds_extendible_hash_config {
  size_t key_size;
  size_t value_size; /* may be 0 for a set */
  ds_extendible_hash_hash_func_t hash;
  ds_extendible_hash_equal_func_t equal;
  void *user;
  size_t bucket_capacity; /* entries per bucket, default 8 */
  ds_context_t *context;
} ds_extendible_hash_config_t;

struct ds_extendible_hash_bucket;

typedef struct ds_extendible_hash {
  struct ds_extendible_hash_bucket **directory;
  unsigned int global_depth;
  size_t full_depth; /* buckets whose local depth is the global depth */
  size_t bucket_count;
  size_t size;
  size_t record_size;
  size_t key_offset;
  size_t value_offset;
  ds_extendible_hash_config_t config;
} ds_extendible_hash_t;

typedef struct ds_extendible_hash_cursor {
  size_t entry;
  size_t index;
} ds_extendible_hash_cursor_t;

void ds_extendible_hash_config_init(ds_extendible_hash_config_t *config);
ds_status_t ds_extendible_hash_init(ds_extendible_hash_t *map,
                                    const ds_extendible_hash_config_t *config);
void ds_extendible_hash_destroy(ds_extendible_hash_t *map);

ds_status_t ds_extendible_hash_put(ds_extendible_hash_t *map, const void *key,
                                   const void *value);
ds_status_t ds_extendible_hash_insert(ds_extendible_hash_t *map,
                                      const void *key, const void *value);
void *ds_extendible_hash_emplace(ds_extendible_hash_t *map, const void *key,
                                 bool *out_inserted);
void *ds_extendible_hash_get(const ds_extendible_hash_t *map, const void *key);
bool ds_extendible_hash_contains(const ds_extendible_hash_t *map,
                                 const void *key);
ds_status_t ds_extendible_hash_remove(ds_extendible_hash_t *map,
                                      const void *key, void *out_value);
void ds_extendible_hash_clear(ds_extendible_hash_t *map);

size_t ds_extendible_hash_size(const ds_extendible_hash_t *map);
unsigned int ds_extendible_hash_global_depth(const ds_extendible_hash_t *map);
size_t ds_extendible_hash_bucket_count(const ds_extendible_hash_t *map);
/* Iterate: zero the cursor first. The map must not change meanwhile. */
bool ds_extendible_hash_next(const ds_extendible_hash_t *map,
                             ds_extendible_hash_cursor_t *cursor,
                             const void **out_key, void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* DS_EXTENDIBLE_HASH_H */
