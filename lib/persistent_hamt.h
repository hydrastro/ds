#ifndef DS_PERSISTENT_HAMT_H
#define DS_PERSISTENT_HAMT_H

/*
 * Persistent hash array mapped trie in CHAMP form (Steindorfer & Vinju,
 * "Optimizing Hash-Array Mapped Tries for Fast and Lean Immutable JVM
 * Collections", 2015).
 *
 * Each node consumes 5 bits of the key's hash and keeps two 32-bit
 * bitmaps: datamap marks slots holding an entry inline, nodemap slots
 * holding a sub-node, each stored compactly in popcount order. Keys whose
 * full hashes collide end in a collision node. Deletion keeps the trie
 * canonical (a sub-node left with a single entry is inlined into its
 * parent), so equal maps have equal shapes, and ds_phamt_equals can skip
 * every subtree the two versions share.
 *
 * Versions: a ds_phamt_t is a cheap handle (root, size, config). Updates
 * path-copy and return a new version while the old one stays valid;
 * nodes and entries are reference counted and shared between versions.
 * Passing the same handle as input and output updates it in place
 * instead: nodes held only by that version are edited without copying,
 * so a map that is not shared behaves like a mutable HAMT.
 *
 * Ownership: put takes ownership of key and value on success (they are
 * destroyed with the last version holding them, through the config
 * callbacks); on failure they stay with the caller. The config must
 * outlive every version built from it. No locking: versions sharing
 * nodes must be used from one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*ds_phamt_hash_func_t)(const void *key, void *user);
typedef bool (*ds_phamt_equal_func_t)(const void *a, const void *b, void *user);
typedef void (*ds_phamt_destroy_func_t)(void *ptr, void *user);
typedef bool (*ds_phamt_visit_func_t)(const void *key, void *value, void *user);

typedef struct ds_phamt_config {
  ds_phamt_hash_func_t hash;
  ds_phamt_equal_func_t equal; /* NULL: pointer identity */
  ds_phamt_destroy_func_t destroy_key;
  ds_phamt_destroy_func_t destroy_value;
  void *user;
  ds_context_t *context;
} ds_phamt_config_t;

typedef struct ds_phamt_leaf {
  size_t refs;
  size_t hash;
  void *key;
  void *value;
} ds_phamt_leaf_t;

typedef struct ds_phamt_node {
  size_t refs;
  unsigned long datamap; /* bitmap nodes */
  unsigned long nodemap;
  size_t leaves; /* inline entries */
  size_t children;
  int collision;
  /* followed by ds_phamt_leaf_t *[leaves], ds_phamt_node *[children] */
} ds_phamt_node_t;

typedef struct ds_phamt {
  ds_phamt_node_t *root; /* NULL when empty */
  size_t size;
  const ds_phamt_config_t *config;
} ds_phamt_t;

#define DS_PHAMT_MAX_DEPTH (sizeof(size_t) * CHAR_BIT / 5 + 3)

typedef struct ds_phamt_iter {
  const ds_phamt_node_t *node[DS_PHAMT_MAX_DEPTH];
  size_t pos[DS_PHAMT_MAX_DEPTH];
  size_t depth;
} ds_phamt_iter_t;

void ds_phamt_config_init(ds_phamt_config_t *config);
void ds_phamt_init(ds_phamt_t *map, const ds_phamt_config_t *config);
/* O(1): out becomes another handle on the same version */
void ds_phamt_copy(const ds_phamt_t *map, ds_phamt_t *out);
void ds_phamt_release(ds_phamt_t *map);

/* out = map with key -> value; DS_OK when added, DS_EXISTS when the
 * entry was replaced (the old key and value go with the old leaf).
 * out may be map (in-place update) or a fresh handle. */
ds_status_t ds_phamt_put(const ds_phamt_t *map, void *key, void *value,
                         ds_phamt_t *out);
/* out = map without key; DS_NOT_FOUND leaves out a copy of map */
ds_status_t ds_phamt_remove(const ds_phamt_t *map, const void *key,
                            ds_phamt_t *out);
ds_status_t ds_phamt_get(const ds_phamt_t *map, const void *key,
                         void **out_value);
bool ds_phamt_contains(const ds_phamt_t *map, const void *key);
size_t ds_phamt_size(const ds_phamt_t *map);
/* same keys mapping to equal values (value_equal NULL: identity) */
bool ds_phamt_equals(const ds_phamt_t *a, const ds_phamt_t *b,
                     ds_phamt_equal_func_t value_equal, void *user);
/* hash order; stops early when visit returns false (DS_STOP) */
ds_status_t ds_phamt_visit(const ds_phamt_t *map, ds_phamt_visit_func_t visit,
                           void *user);
void ds_phamt_iter_init(ds_phamt_iter_t *it, const ds_phamt_t *map);
bool ds_phamt_iter_next(ds_phamt_iter_t *it, const void **key, void **value);
/* nodes reachable from this version (shared ones included) */
size_t ds_phamt_node_count(const ds_phamt_t *map);

#ifdef __cplusplus
}
#endif

#endif /* DS_PERSISTENT_HAMT_H */
