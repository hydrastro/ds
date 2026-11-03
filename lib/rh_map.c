#include "rh_map.h"
#include <limits.h>
#include <string.h>

#define RH_SIZE_MAX (~(size_t)0)
#define RH_MIN_CAPACITY 8

static unsigned long rh_mix(unsigned long h) {
#if ULONG_MAX > 0xFFFFFFFFUL
  h ^= h >> 33;
  h *= 0xFF51AFD7ED558CCDUL;
  h ^= h >> 33;
  h *= 0xC4CEB9FE1A85EC53UL;
  h ^= h >> 33;
#else
  h ^= h >> 16;
  h *= 0x85EBCA6BUL;
  h ^= h >> 13;
  h *= 0xC2B2AE35UL;
  h ^= h >> 16;
#endif
  return h;
}

static size_t rh_max_size(size_t capacity) {
  return capacity - capacity / 8;
}

static unsigned char *rh_key(const ds_rh_map_t *map, size_t i) {
  return map->keys + i * map->key_size;
}

static unsigned char *rh_value(const ds_rh_map_t *map, size_t i) {
  return map->values + i * map->value_size;
}

static size_t rh_home(const ds_rh_map_t *map, unsigned long hash) {
  return (size_t)hash & (map->capacity - 1);
}

static void rh_move(ds_rh_map_t *map, size_t dst, size_t src) {
  memcpy(rh_key(map, dst), rh_key(map, src), map->key_size);
  if (map->value_size > 0) {
    memcpy(rh_value(map, dst), rh_value(map, src), map->value_size);
  }
  map->hashes[dst] = map->hashes[src];
}

static size_t rh_find(const ds_rh_map_t *map, const void *key,
                      unsigned long hash) {
  size_t mask;
  size_t i;
  unsigned int d = 1;
  if (map->capacity == 0) {
    return RH_SIZE_MAX;
  }
  mask = map->capacity - 1;
  i = rh_home(map, hash);
  while (map->dist[i] >= d) {
    if (map->hashes[i] == hash && map->equal(rh_key(map, i), key, map->user)) {
      return i;
    }
    i = (i + 1) & mask;
    d++;
  }
  return RH_SIZE_MAX;
}

/* Place a key known to be absent; there must be a free slot. Returns the
 * slot index (its value is left uninitialised). */
static size_t rh_place(ds_rh_map_t *map, const void *key, unsigned long hash) {
  size_t mask = map->capacity - 1;
  size_t i = rh_home(map, hash);
  size_t e;
  size_t prev;
  unsigned int d = 1;
  while (map->dist[i] != 0 && map->dist[i] >= d) {
    i = (i + 1) & mask;
    d++;
  }
  /* Robin Hood displacement is equivalent to shifting the rest of the
   * cluster one slot to the right. */
  e = i;
  while (map->dist[e] != 0) {
    e = (e + 1) & mask;
  }
  while (e != i) {
    prev = (e - 1) & mask;
    rh_move(map, e, prev);
    map->dist[e] = map->dist[prev] + 1U;
    e = prev;
  }
  memcpy(rh_key(map, i), key, map->key_size);
  map->hashes[i] = hash;
  map->dist[i] = d;
  map->size++;
  return i;
}

static void *rh_alloc(ds_context_t *context, size_t bytes) {
  return ds_context_alloc(context, bytes == 0 ? 1 : bytes);
}

static ds_status_t rh_rehash(ds_rh_map_t *map, size_t capacity) {
  ds_rh_map_t next = *map;
  size_t i;
  if (map->fixed) {
    return DS_ERR_OVERFLOW;
  }
  if (capacity > RH_SIZE_MAX / sizeof(unsigned long) ||
      capacity > RH_SIZE_MAX / map->key_size ||
      (map->value_size > 0 && capacity > RH_SIZE_MAX / map->value_size)) {
    return DS_ERR_OVERFLOW;
  }
  next.capacity = capacity;
  next.size = 0;
  next.keys = (unsigned char *)rh_alloc(map->context, capacity * map->key_size);
  next.values =
      (unsigned char *)rh_alloc(map->context, capacity * map->value_size);
  next.hashes =
      (unsigned long *)rh_alloc(map->context, capacity * sizeof(unsigned long));
  next.dist = (unsigned int *)ds_context_calloc(map->context, capacity,
                                                sizeof(unsigned int));
  if (next.keys == NULL || next.values == NULL || next.hashes == NULL ||
      next.dist == NULL) {
    ds_context_free(map->context, next.keys);
    ds_context_free(map->context, next.values);
    ds_context_free(map->context, next.hashes);
    ds_context_free(map->context, next.dist);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < map->capacity; i++) {
    if (map->dist[i] != 0) {
      size_t j = rh_place(&next, rh_key(map, i), map->hashes[i]);
      if (map->value_size > 0) {
        memcpy(rh_value(&next, j), rh_value(map, i), map->value_size);
      }
    }
  }
  ds_context_free(map->context, map->keys);
  ds_context_free(map->context, map->values);
  ds_context_free(map->context, map->hashes);
  ds_context_free(map->context, map->dist);
  *map = next;
  return DS_OK;
}

static ds_status_t rh_make_room(ds_rh_map_t *map) {
  size_t capacity;
  if (map->capacity > 0 && map->size + 1 <= rh_max_size(map->capacity)) {
    return DS_OK;
  }
  if (map->fixed) {
    return DS_ERR_OVERFLOW;
  }
  if (map->capacity > RH_SIZE_MAX / 2) {
    return DS_ERR_OVERFLOW;
  }
  capacity = map->capacity == 0 ? RH_MIN_CAPACITY : map->capacity * 2;
  return rh_rehash(map, capacity);
}

static ds_status_t rh_check_config(const ds_rh_map_config_t *config) {
  if (config == NULL || config->hash == NULL || config->equal == NULL) {
    return DS_ERR_NULL;
  }
  return config->key_size == 0 ? DS_ERR_INVALID : DS_OK;
}

static void rh_apply_config(ds_rh_map_t *map,
                            const ds_rh_map_config_t *config) {
  map->keys = NULL;
  map->values = NULL;
  map->hashes = NULL;
  map->dist = NULL;
  map->capacity = 0;
  map->size = 0;
  map->key_size = config->key_size;
  map->value_size = config->value_size;
  map->hash = config->hash;
  map->equal = config->equal;
  map->user = config->user;
}

void ds_rh_map_config_init(ds_rh_map_config_t *config) {
  if (config == NULL) {
    return;
  }
  config->key_size = 0;
  config->value_size = 0;
  config->hash = NULL;
  config->equal = NULL;
  config->user = NULL;
  config->context = NULL;
}

ds_status_t ds_rh_map_init(ds_rh_map_t *map, const ds_rh_map_config_t *config,
                           size_t initial_capacity) {
  ds_status_t status;
  if (map == NULL) {
    return DS_ERR_NULL;
  }
  status = rh_check_config(config);
  if (status != DS_OK) {
    return status;
  }
  rh_apply_config(map, config);
  map->context =
      config->context != NULL ? config->context : ds_default_context();
  map->fixed = false;
  if (initial_capacity > 0) {
    status = ds_rh_map_reserve(map, initial_capacity);
  }
  return status;
}

ds_status_t ds_rh_map_init_buffer(ds_rh_map_t *map,
                                  const ds_rh_map_config_t *config, void *keys,
                                  void *values, unsigned long *hashes,
                                  unsigned int *dist, size_t capacity) {
  ds_status_t status;
  if (map == NULL || keys == NULL || hashes == NULL || dist == NULL ||
      (values == NULL && config != NULL && config->value_size > 0)) {
    return DS_ERR_NULL;
  }
  status = rh_check_config(config);
  if (status != DS_OK) {
    return status;
  }
  if (capacity < 2 || (capacity & (capacity - 1)) != 0) {
    return DS_ERR_INVALID; /* power of two required */
  }
  rh_apply_config(map, config);
  map->keys = (unsigned char *)keys;
  map->values = (unsigned char *)values;
  map->hashes = hashes;
  map->dist = dist;
  map->capacity = capacity;
  map->context = NULL;
  map->fixed = true;
  memset(dist, 0, capacity * sizeof(unsigned int));
  return DS_OK;
}

void ds_rh_map_destroy(ds_rh_map_t *map) {
  if (map == NULL) {
    return;
  }
  if (!map->fixed) {
    ds_context_free(map->context, map->keys);
    ds_context_free(map->context, map->values);
    ds_context_free(map->context, map->hashes);
    ds_context_free(map->context, map->dist);
    map->keys = NULL;
    map->values = NULL;
    map->hashes = NULL;
    map->dist = NULL;
    map->capacity = 0;
  }
  map->size = 0;
}

void *ds_rh_map_emplace(ds_rh_map_t *map, const void *key, bool *out_inserted) {
  unsigned long hash;
  size_t i;
  if (out_inserted != NULL) {
    *out_inserted = false;
  }
  if (key == NULL) {
    return NULL;
  }
  hash = rh_mix(map->hash(key, map->user));
  i = rh_find(map, key, hash);
  if (i != RH_SIZE_MAX) {
    return rh_value(map, i);
  }
  if (rh_make_room(map) != DS_OK) {
    return NULL;
  }
  i = rh_place(map, key, hash);
  if (map->value_size > 0) {
    memset(rh_value(map, i), 0, map->value_size);
  }
  if (out_inserted != NULL) {
    *out_inserted = true;
  }
  return rh_value(map, i);
}

static ds_status_t rh_store(ds_rh_map_t *map, const void *key,
                            const void *value, bool overwrite) {
  unsigned long hash;
  size_t i;
  ds_status_t status;
  if (key == NULL || (value == NULL && map->value_size > 0)) {
    return DS_ERR_NULL;
  }
  hash = rh_mix(map->hash(key, map->user));
  i = rh_find(map, key, hash);
  if (i != RH_SIZE_MAX) {
    if (overwrite && map->value_size > 0) {
      memmove(rh_value(map, i), value, map->value_size);
    }
    return DS_EXISTS;
  }
  status = rh_make_room(map);
  if (status != DS_OK) {
    return status;
  }
  i = rh_place(map, key, hash);
  if (map->value_size > 0) {
    memcpy(rh_value(map, i), value, map->value_size);
  }
  return DS_OK;
}

ds_status_t ds_rh_map_put(ds_rh_map_t *map, const void *key,
                          const void *value) {
  return rh_store(map, key, value, true);
}

ds_status_t ds_rh_map_insert(ds_rh_map_t *map, const void *key,
                             const void *value) {
  return rh_store(map, key, value, false);
}

void *ds_rh_map_get(const ds_rh_map_t *map, const void *key) {
  size_t i;
  if (key == NULL) {
    return NULL;
  }
  i = rh_find(map, key, rh_mix(map->hash(key, map->user)));
  return i == RH_SIZE_MAX ? NULL : rh_value(map, i);
}

bool ds_rh_map_contains(const ds_rh_map_t *map, const void *key) {
  return key != NULL &&
         rh_find(map, key, rh_mix(map->hash(key, map->user))) != RH_SIZE_MAX;
}

ds_status_t ds_rh_map_remove(ds_rh_map_t *map, const void *key,
                             void *out_value) {
  size_t mask;
  size_t i;
  size_t j;
  if (key == NULL) {
    return DS_ERR_NULL;
  }
  i = rh_find(map, key, rh_mix(map->hash(key, map->user)));
  if (i == RH_SIZE_MAX) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && map->value_size > 0) {
    memcpy(out_value, rh_value(map, i), map->value_size);
  }
  /* Backward-shift deletion: pull displaced successors one slot closer
   * to home, so no tombstone is left behind. */
  mask = map->capacity - 1;
  j = (i + 1) & mask;
  while (map->dist[j] > 1U) {
    rh_move(map, i, j);
    map->dist[i] = map->dist[j] - 1U;
    i = j;
    j = (j + 1) & mask;
  }
  map->dist[i] = 0;
  map->size--;
  return DS_OK;
}

void ds_rh_map_clear(ds_rh_map_t *map) {
  if (map->capacity > 0) {
    memset(map->dist, 0, map->capacity * sizeof(unsigned int));
  }
  map->size = 0;
}

ds_status_t ds_rh_map_reserve(ds_rh_map_t *map, size_t count) {
  size_t capacity =
      map->capacity < RH_MIN_CAPACITY ? RH_MIN_CAPACITY : map->capacity;
  while (rh_max_size(capacity) < count) {
    if (capacity > RH_SIZE_MAX / 2) {
      return DS_ERR_OVERFLOW;
    }
    capacity *= 2;
  }
  if (capacity == map->capacity) {
    return DS_OK;
  }
  return rh_rehash(map, capacity);
}

size_t ds_rh_map_size(const ds_rh_map_t *map) {
  return map->size;
}

size_t ds_rh_map_capacity(const ds_rh_map_t *map) {
  return map->capacity;
}

bool ds_rh_map_next(const ds_rh_map_t *map, size_t *cursor,
                    const void **out_key, void **out_value) {
  size_t i;
  for (i = *cursor; i < map->capacity; i++) {
    if (map->dist[i] != 0) {
      *cursor = i + 1;
      if (out_key != NULL) {
        *out_key = rh_key(map, i);
      }
      if (out_value != NULL) {
        *out_value = map->value_size > 0 ? rh_value(map, i) : NULL;
      }
      return true;
    }
  }
  *cursor = map->capacity;
  return false;
}

unsigned long ds_rh_map_hash_ulong(const void *key, void *user) {
  (void)user;
  return *(const unsigned long *)key;
}

bool ds_rh_map_equal_ulong(const void *a, const void *b, void *user) {
  (void)user;
  return *(const unsigned long *)a == *(const unsigned long *)b;
}

unsigned long ds_rh_map_hash_size(const void *key, void *user) {
  (void)user;
  return (unsigned long)*(const size_t *)key;
}

bool ds_rh_map_equal_size(const void *a, const void *b, void *user) {
  (void)user;
  return *(const size_t *)a == *(const size_t *)b;
}

unsigned long ds_rh_map_hash_cstr(const void *key, void *user) {
  const unsigned char *s = *(const unsigned char *const *)key;
  unsigned long h = 0x811C9DC5UL;
  (void)user;
  while (*s != 0) {
    h ^= *s++;
    h *= 0x01000193UL;
  }
  return h;
}

bool ds_rh_map_equal_cstr(const void *a, const void *b, void *user) {
  (void)user;
  return strcmp(*(const char *const *)a, *(const char *const *)b) == 0;
}
