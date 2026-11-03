#ifndef DS_OPEN_MAP_H
#define DS_OPEN_MAP_H

/*
 * Open-addressing hash map with a pluggable probe sequence and a pluggable
 * collision discipline; keys and values stored by value.
 *
 * Probe sequences (ds_open_map_probe_t; any function giving the i-th slot
 * of a key's sequence, i = 0 being its home slot, can be plugged in, as
 * long as it visits every slot of a power-of-two table):
 *   ds_open_map_linear_probe()     h + i              (primary clustering)
 *   ds_open_map_quadratic_probe()  h + i(i+1)/2       (secondary clustering)
 *   ds_open_map_double_probe()     h + i * h2, h2 odd (double hashing: the
 *                                  step comes from other hash bits)
 *
 * Disciplines decide who keeps a contested slot during insertion:
 *   DS_OPEN_MAP_FCFS        first come, first served: the newcomer walks on
 *                           (classic open addressing)
 *   DS_OPEN_MAP_LCFS        last come, first served (Poblete and Munro):
 *                           the newcomer takes the slot and the resident
 *                           moves on along its own sequence
 *   DS_OPEN_MAP_ROBIN_HOOD  the key further from its home keeps the slot
 *                           (Celis), on any probe sequence
 * LCFS and Robin Hood keep the expected probe count the same but cut its
 * variance sharply. rh_map.h is the specialised linear Robin Hood table.
 *
 * Removal leaves tombstones, which insertions reuse; when live entries plus
 * tombstones reach 7/8 of the table it is rebuilt: in place (dropping the
 * tombstones, no allocation) when at most half full, otherwise doubled.
 * Every entry stores its full hash, so rebuilding never calls the hash
 * function and most mismatches are rejected without calling equal.
 *
 * Storage: dynamic (ds_open_map_init) through a ds_context_t, or one caller
 * block of DS_OPEN_MAP_BUFFER_SIZE(capacity, key_size, value_size) bytes,
 * capacity a power of two (ds_open_map_init_buffer): never allocates,
 * DS_ERR_OVERFLOW once 7/8 full of live entries.
 *
 * Pointers returned by get/emplace stay valid until the next insertion or
 * removal. Complexity: expected O(1) get, put, remove at bounded load.
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

typedef unsigned long (*ds_open_map_hash_func_t)(const void *key, void *user);
typedef bool (*ds_open_map_equal_func_t)(const void *a, const void *b,
                                         void *user);

typedef struct ds_open_map_probe {
  const char *name;
  /* Slot of probe i (0 = home) for a key whose mixed hash is `hash`, in a
   * table of mask + 1 slots. */
  size_t (*position)(unsigned long hash, size_t i, size_t mask, void *user);
} ds_open_map_probe_t;

typedef enum ds_open_map_discipline {
  DS_OPEN_MAP_FCFS = 0,
  DS_OPEN_MAP_LCFS = 1,
  DS_OPEN_MAP_ROBIN_HOOD = 2
} ds_open_map_discipline_t;

typedef struct ds_open_map_config {
  size_t key_size;
  size_t value_size; /* may be 0 for a set */
  ds_open_map_hash_func_t hash;
  ds_open_map_equal_func_t equal;
  void *user;
  const ds_open_map_probe_t *probe; /* NULL: linear */
  void *probe_user;
  ds_open_map_discipline_t discipline;
  ds_context_t *context;
} ds_open_map_config_t;

typedef struct ds_open_map {
  unsigned char *state;  /* empty / full / deleted per slot */
  unsigned long *hashes; /* mixed hash per slot */
  size_t *probes;        /* probe index at which the entry sits */
  unsigned char *keys;   /* capacity + 1 slots; the last one is scratch */
  unsigned char *values; /* capacity + 1 slots */
  size_t capacity;
  size_t size;
  size_t deleted;
  ds_open_map_config_t config;
  void *block; /* the single allocation / caller block */
  bool fixed;
} ds_open_map_t;

/* Bytes for a caller block holding `capacity` slots. */
#define DS_OPEN_MAP_BUFFER_SIZE(capacity, key_size, value_size)                \
  (ds_open_map_buffer_size((capacity), (key_size), (value_size)))

const ds_open_map_probe_t *ds_open_map_linear_probe(void);
const ds_open_map_probe_t *ds_open_map_quadratic_probe(void);
const ds_open_map_probe_t *ds_open_map_double_probe(void);

void ds_open_map_config_init(ds_open_map_config_t *config);
size_t ds_open_map_buffer_size(size_t capacity, size_t key_size,
                               size_t value_size);
ds_status_t ds_open_map_init(ds_open_map_t *map,
                             const ds_open_map_config_t *config,
                             size_t initial_capacity);
ds_status_t ds_open_map_init_buffer(ds_open_map_t *map,
                                    const ds_open_map_config_t *config,
                                    void *buffer, size_t capacity);
void ds_open_map_destroy(ds_open_map_t *map);

ds_status_t ds_open_map_put(ds_open_map_t *map, const void *key,
                            const void *value);
ds_status_t ds_open_map_insert(ds_open_map_t *map, const void *key,
                               const void *value);
void *ds_open_map_emplace(ds_open_map_t *map, const void *key,
                          bool *out_inserted);
void *ds_open_map_get(const ds_open_map_t *map, const void *key);
bool ds_open_map_contains(const ds_open_map_t *map, const void *key);
ds_status_t ds_open_map_remove(ds_open_map_t *map, const void *key,
                               void *out_value);
void ds_open_map_clear(ds_open_map_t *map);
ds_status_t ds_open_map_reserve(ds_open_map_t *map, size_t count);
/* Rebuild in place, dropping tombstones (never allocates). */
void ds_open_map_compact(ds_open_map_t *map);

size_t ds_open_map_size(const ds_open_map_t *map);
size_t ds_open_map_capacity(const ds_open_map_t *map);
/* Probe index of a present key (0 = in its home slot), or (size_t)-1. */
size_t ds_open_map_probe_length(const ds_open_map_t *map, const void *key);
bool ds_open_map_next(const ds_open_map_t *map, size_t *cursor,
                      const void **out_key, void **out_value);

#ifdef __cplusplus
}
#endif

#endif /* DS_OPEN_MAP_H */
