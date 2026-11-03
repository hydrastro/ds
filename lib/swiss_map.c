#include "swiss_map.h"
#include <limits.h>
#include <string.h>

#define SW_G DS_SWISS_MAP_GROUP
#define SW_EMPTY 0x80U
#define SW_DELETED 0xFEU
#define SW_LSB (~0UL / 0xFFUL)
#define SW_MSB (SW_LSB << 7)
#define SW_NONE (~(size_t)0)

static unsigned long sw_mix(unsigned long h) {
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

static unsigned int sw_lane_shift(size_t lane) {
  static const unsigned long one = 1;
  bool little = *(const unsigned char *)&one == 1;
  return (unsigned int)(8 * (little ? lane : SW_G - 1 - lane));
}

static unsigned long sw_load(const ds_swiss_map_t *map, size_t pos) {
  unsigned long w;
  memcpy(&w, map->ctrl + pos, sizeof w);
  return w;
}

static unsigned long sw_match_h2(unsigned long w, unsigned int h2) {
  unsigned long x = w ^ (SW_LSB * (unsigned long)h2);
  return (x - SW_LSB) & ~x & SW_MSB;
}

static unsigned long sw_match_empty(unsigned long w) {
  return w & ~(w << 6) & SW_MSB;
}

static unsigned long sw_match_free(unsigned long w) { /* empty or deleted */
  return w & ~(w << 7) & SW_MSB;
}

/* Lowest lane set in a match mask (mask != 0). */
static size_t sw_first_lane(unsigned long mask) {
  size_t lane;
  for (lane = 0; lane < SW_G; lane++) {
    if ((mask >> sw_lane_shift(lane)) & 0x80UL) {
      return lane;
    }
  }
  return 0;
}

static bool sw_lane_set(unsigned long mask, size_t lane) {
  return ((mask >> sw_lane_shift(lane)) & 0x80UL) != 0;
}

static size_t sw_max_load(size_t capacity) {
  if (capacity == 0) {
    return 0;
  }
  return capacity <= 8 ? capacity - 1 : capacity - capacity / 8;
}

static unsigned char *sw_key(const ds_swiss_map_t *map, size_t i) {
  return map->keys + i * map->config.key_size;
}

static unsigned char *sw_value(const ds_swiss_map_t *map, size_t i) {
  return map->config.value_size == 0 ? sw_key(map, i)
                                     : map->values + i * map->config.value_size;
}

static void sw_set_ctrl(ds_swiss_map_t *map, size_t i, unsigned int c) {
  map->ctrl[i] = (unsigned char)c;
  if (i < SW_G) {
    map->ctrl[map->capacity + i] = (unsigned char)c;
  }
}

static unsigned long sw_hash(const ds_swiss_map_t *map, const void *key) {
  return sw_mix(map->config.hash(key, map->config.user));
}

static size_t sw_find(const ds_swiss_map_t *map, const void *key,
                      unsigned long h) {
  size_t mask = map->capacity - 1;
  size_t pos;
  size_t step = 0;
  size_t lane;
  size_t idx;
  unsigned long w;
  unsigned long m;
  unsigned int h2 = (unsigned int)(h & 0x7FUL);
  if (map->capacity == 0) {
    return SW_NONE;
  }
  pos = (size_t)(h >> 7) & mask;
  for (;;) {
    w = sw_load(map, pos);
    m = sw_match_h2(w, h2);
    if (m != 0) {
      for (lane = 0; lane < SW_G; lane++) {
        if (sw_lane_set(m, lane)) {
          idx = (pos + lane) & mask;
          if (map->config.equal(sw_key(map, idx), key, map->config.user)) {
            return idx;
          }
        }
      }
    }
    if (sw_match_empty(w) != 0) {
      return SW_NONE;
    }
    step += SW_G;
    pos = (pos + step) & mask;
  }
}

/* First empty-or-deleted slot on the probe sequence of h. */
static size_t sw_find_free(const ds_swiss_map_t *map, unsigned long h) {
  size_t mask = map->capacity - 1;
  size_t pos = (size_t)(h >> 7) & mask;
  size_t step = 0;
  unsigned long m;
  for (;;) {
    m = sw_match_free(sw_load(map, pos));
    if (m != 0) {
      return (pos + sw_first_lane(m)) & mask;
    }
    step += SW_G;
    pos = (pos + step) & mask;
  }
}

static void sw_swap_bytes(unsigned char *a, unsigned char *b, size_t n) {
  unsigned char t;
  size_t i;
  for (i = 0; i < n; i++) {
    t = a[i];
    a[i] = b[i];
    b[i] = t;
  }
}

static void sw_reset_counts(ds_swiss_map_t *map) {
  map->deleted = 0;
  map->growth_left = sw_max_load(map->capacity) - map->size;
}

void ds_swiss_map_compact(ds_swiss_map_t *map) {
  size_t i;
  size_t mask = map->capacity - 1;
  size_t target;
  size_t start;
  unsigned long h;
  unsigned int c;
  if (map->capacity == 0) {
    return;
  }
  /* deleted -> empty, full -> deleted (meaning "still to place") */
  for (i = 0; i < map->capacity; i++) {
    c = map->ctrl[i];
    map->ctrl[i] =
        (unsigned char)(c == SW_DELETED || c == SW_EMPTY ? SW_EMPTY
                                                         : SW_DELETED);
  }
  memcpy(map->ctrl + map->capacity, map->ctrl, SW_G);
  for (i = 0; i < map->capacity; i++) {
    if (map->ctrl[i] != SW_DELETED) {
      continue;
    }
    h = sw_hash(map, sw_key(map, i));
    target = sw_find_free(map, h);
    start = (size_t)(h >> 7) & mask;
    if (((i - start) & mask) / SW_G == ((target - start) & mask) / SW_G) {
      sw_set_ctrl(map, i, (unsigned int)(h & 0x7FUL)); /* already in place */
      continue;
    }
    if (map->ctrl[target] == SW_EMPTY) {
      sw_set_ctrl(map, target, (unsigned int)(h & 0x7FUL));
      memcpy(sw_key(map, target), sw_key(map, i), map->config.key_size);
      if (map->config.value_size > 0) {
        memcpy(sw_value(map, target), sw_value(map, i), map->config.value_size);
      }
      sw_set_ctrl(map, i, SW_EMPTY);
    } else {
      /* target holds an entry still to place: swap and redo slot i */
      sw_set_ctrl(map, target, (unsigned int)(h & 0x7FUL));
      sw_swap_bytes(sw_key(map, target), sw_key(map, i), map->config.key_size);
      if (map->config.value_size > 0) {
        sw_swap_bytes(sw_value(map, target), sw_value(map, i),
                      map->config.value_size);
      }
      i--;
    }
  }
  sw_reset_counts(map);
}

static ds_status_t sw_alloc_arrays(const ds_swiss_map_t *map, size_t capacity,
                                   unsigned char **ctrl, unsigned char **keys,
                                   unsigned char **values) {
  ds_context_t *ctx = map->config.context;
  size_t ks = map->config.key_size;
  size_t vs = map->config.value_size;
  if (capacity > ((size_t)-1) / (ks > vs ? ks : vs) ||
      capacity > ((size_t)-1) - SW_G) {
    return DS_ERR_OVERFLOW;
  }
  *ctrl =
      (unsigned char *)ds_context_alloc(ctx, DS_SWISS_MAP_CTRL_SIZE(capacity));
  *keys = (unsigned char *)ds_context_alloc(ctx, capacity * ks);
  *values =
      vs > 0 ? (unsigned char *)ds_context_alloc(ctx, capacity * vs) : NULL;
  if (*ctrl == NULL || *keys == NULL || (vs > 0 && *values == NULL)) {
    ds_context_free(ctx, *ctrl);
    ds_context_free(ctx, *keys);
    ds_context_free(ctx, *values);
    return DS_ERR_ALLOC;
  }
  memset(*ctrl, (int)SW_EMPTY, DS_SWISS_MAP_CTRL_SIZE(capacity));
  return DS_OK;
}

static ds_status_t sw_resize(ds_swiss_map_t *map, size_t capacity) {
  ds_swiss_map_t old = *map;
  unsigned char *ctrl;
  unsigned char *keys;
  unsigned char *values;
  ds_status_t status;
  size_t i;
  size_t j;
  unsigned long h;
  if (map->fixed) {
    return DS_ERR_OVERFLOW;
  }
  status = sw_alloc_arrays(map, capacity, &ctrl, &keys, &values);
  if (status != DS_OK) {
    return status;
  }
  map->ctrl = ctrl;
  map->keys = keys;
  map->values = values;
  map->capacity = capacity;
  for (i = 0; i < old.capacity; i++) {
    if (old.ctrl[i] >= SW_EMPTY) {
      continue;
    }
    h = sw_hash(map, sw_key(&old, i));
    j = sw_find_free(map, h);
    sw_set_ctrl(map, j, (unsigned int)(h & 0x7FUL));
    memcpy(sw_key(map, j), sw_key(&old, i), map->config.key_size);
    if (map->config.value_size > 0) {
      memcpy(sw_value(map, j), sw_value(&old, i), map->config.value_size);
    }
  }
  sw_reset_counts(map);
  ds_context_free(map->config.context, old.ctrl);
  ds_context_free(map->config.context, old.keys);
  ds_context_free(map->config.context, old.values);
  return DS_OK;
}

static size_t sw_capacity_for(size_t count) {
  size_t c = SW_G * 2;
  while (sw_max_load(c) < count) {
    if (c > ((size_t)-1) / 4) {
      return 0;
    }
    c *= 2;
  }
  return c;
}

void ds_swiss_map_config_init(ds_swiss_map_config_t *config) {
  config->key_size = 0;
  config->value_size = 0;
  config->hash = NULL;
  config->equal = NULL;
  config->user = NULL;
  config->context = NULL;
}

static ds_status_t sw_check_config(const ds_swiss_map_config_t *config) {
  if (config == NULL || config->hash == NULL || config->equal == NULL) {
    return DS_ERR_NULL;
  }
  if (config->key_size == 0) {
    return DS_ERR_INVALID;
  }
  return DS_OK;
}

ds_status_t ds_swiss_map_init(ds_swiss_map_t *map,
                              const ds_swiss_map_config_t *config,
                              size_t initial_capacity) {
  ds_status_t status;
  if (map == NULL) {
    return DS_ERR_NULL;
  }
  status = sw_check_config(config);
  if (status != DS_OK) {
    return status;
  }
  map->config = *config;
  map->ctrl = NULL;
  map->keys = NULL;
  map->values = NULL;
  map->capacity = 0;
  map->size = 0;
  map->deleted = 0;
  map->growth_left = 0;
  map->fixed = false;
  if (initial_capacity > 0) {
    return ds_swiss_map_reserve(map, initial_capacity);
  }
  return DS_OK;
}

ds_status_t ds_swiss_map_init_buffer(ds_swiss_map_t *map,
                                     const ds_swiss_map_config_t *config,
                                     unsigned char *ctrl, void *keys,
                                     void *values, size_t capacity) {
  ds_status_t status;
  if (map == NULL || ctrl == NULL || keys == NULL) {
    return DS_ERR_NULL;
  }
  status = sw_check_config(config);
  if (status != DS_OK) {
    return status;
  }
  if (config->value_size > 0 && values == NULL) {
    return DS_ERR_NULL;
  }
  if (capacity < SW_G || (capacity & (capacity - 1)) != 0) {
    return DS_ERR_INVALID;
  }
  map->config = *config;
  map->ctrl = ctrl;
  map->keys = (unsigned char *)keys;
  map->values = (unsigned char *)values;
  map->capacity = capacity;
  map->size = 0;
  map->fixed = true;
  memset(ctrl, (int)SW_EMPTY, DS_SWISS_MAP_CTRL_SIZE(capacity));
  sw_reset_counts(map);
  return DS_OK;
}

void ds_swiss_map_destroy(ds_swiss_map_t *map) {
  if (map == NULL) {
    return;
  }
  if (!map->fixed) {
    ds_context_free(map->config.context, map->ctrl);
    ds_context_free(map->config.context, map->keys);
    ds_context_free(map->config.context, map->values);
  }
  map->ctrl = NULL;
  map->keys = NULL;
  map->values = NULL;
  map->capacity = 0;
  map->size = 0;
  map->deleted = 0;
  map->growth_left = 0;
}

static void *sw_emplace(ds_swiss_map_t *map, const void *key,
                        bool *out_inserted, ds_status_t *out_status) {
  unsigned long h = sw_hash(map, key);
  size_t i = sw_find(map, key, h);
  ds_status_t status;
  *out_inserted = false;
  *out_status = DS_OK;
  if (i != SW_NONE) {
    return sw_value(map, i);
  }
  if (map->capacity == 0) {
    status = sw_resize(map, SW_G * 2);
    if (status != DS_OK) {
      *out_status = status;
      return NULL;
    }
  }
  i = sw_find_free(map, h);
  if (map->growth_left == 0 && map->ctrl[i] == SW_EMPTY) {
    if (map->deleted > 0 &&
        (map->fixed || map->size <= map->capacity / 32 * 25)) {
      ds_swiss_map_compact(map);
    } else {
      status = map->fixed ? DS_ERR_OVERFLOW : sw_resize(map, map->capacity * 2);
      if (status != DS_OK) {
        *out_status = status;
        return NULL;
      }
    }
    if (map->growth_left == 0) {
      *out_status = DS_ERR_OVERFLOW;
      return NULL;
    }
    i = sw_find_free(map, h);
  }
  if (map->ctrl[i] == SW_DELETED) {
    map->deleted--;
  } else {
    map->growth_left--;
  }
  sw_set_ctrl(map, i, (unsigned int)(h & 0x7FUL));
  memcpy(sw_key(map, i), key, map->config.key_size);
  map->size++;
  *out_inserted = true;
  return sw_value(map, i);
}

void *ds_swiss_map_emplace(ds_swiss_map_t *map, const void *key,
                           bool *out_inserted) {
  bool inserted;
  ds_status_t status;
  void *slot = sw_emplace(map, key, &inserted, &status);
  if (out_inserted != NULL) {
    *out_inserted = inserted;
  }
  return slot;
}

ds_status_t ds_swiss_map_put(ds_swiss_map_t *map, const void *key,
                             const void *value) {
  bool inserted;
  ds_status_t status;
  void *slot;
  if (key == NULL || (value == NULL && map->config.value_size > 0)) {
    return DS_ERR_NULL;
  }
  slot = sw_emplace(map, key, &inserted, &status);
  if (slot == NULL) {
    return status;
  }
  if (map->config.value_size > 0) {
    memcpy(slot, value, map->config.value_size);
  }
  return DS_OK;
}

ds_status_t ds_swiss_map_insert(ds_swiss_map_t *map, const void *key,
                                const void *value) {
  bool inserted;
  ds_status_t status;
  void *slot;
  if (key == NULL || (value == NULL && map->config.value_size > 0)) {
    return DS_ERR_NULL;
  }
  slot = sw_emplace(map, key, &inserted, &status);
  if (slot == NULL) {
    return status;
  }
  if (!inserted) {
    return DS_EXISTS;
  }
  if (map->config.value_size > 0) {
    memcpy(slot, value, map->config.value_size);
  }
  return DS_OK;
}

void *ds_swiss_map_get(const ds_swiss_map_t *map, const void *key) {
  size_t i;
  if (map->size == 0) {
    return NULL;
  }
  i = sw_find(map, key, sw_hash(map, key));
  return i == SW_NONE ? NULL : sw_value(map, i);
}

bool ds_swiss_map_contains(const ds_swiss_map_t *map, const void *key) {
  return ds_swiss_map_get(map, key) != NULL;
}

ds_status_t ds_swiss_map_remove(ds_swiss_map_t *map, const void *key,
                                void *out_value) {
  size_t i;
  if (map->size == 0) {
    return DS_NOT_FOUND;
  }
  i = sw_find(map, key, sw_hash(map, key));
  if (i == SW_NONE) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && map->config.value_size > 0) {
    memcpy(out_value, sw_value(map, i), map->config.value_size);
  }
  sw_set_ctrl(map, i, SW_DELETED);
  map->size--;
  map->deleted++;
  return DS_OK;
}

void ds_swiss_map_clear(ds_swiss_map_t *map) {
  if (map->capacity == 0) {
    return;
  }
  memset(map->ctrl, (int)SW_EMPTY, DS_SWISS_MAP_CTRL_SIZE(map->capacity));
  map->size = 0;
  sw_reset_counts(map);
}

ds_status_t ds_swiss_map_reserve(ds_swiss_map_t *map, size_t count) {
  size_t c;
  if (count <= sw_max_load(map->capacity)) {
    if (count > map->size + map->growth_left && map->deleted > 0) {
      ds_swiss_map_compact(map);
    }
    return DS_OK;
  }
  if (map->fixed) {
    return DS_ERR_OVERFLOW;
  }
  c = sw_capacity_for(count);
  if (c == 0) {
    return DS_ERR_OVERFLOW;
  }
  return sw_resize(map, c);
}

size_t ds_swiss_map_size(const ds_swiss_map_t *map) {
  return map->size;
}

size_t ds_swiss_map_capacity(const ds_swiss_map_t *map) {
  return map->capacity;
}

bool ds_swiss_map_next(const ds_swiss_map_t *map, size_t *cursor,
                       const void **out_key, void **out_value) {
  size_t i;
  for (i = *cursor; i < map->capacity; i++) {
    if (map->ctrl[i] < SW_EMPTY) {
      if (out_key != NULL) {
        *out_key = sw_key(map, i);
      }
      if (out_value != NULL) {
        *out_value = map->config.value_size > 0 ? sw_value(map, i) : NULL;
      }
      *cursor = i + 1;
      return true;
    }
  }
  *cursor = map->capacity;
  return false;
}
