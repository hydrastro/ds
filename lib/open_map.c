#include "open_map.h"
#include <limits.h>
#include <string.h>

#define OM_EMPTY 0U
#define OM_FULL 1U
#define OM_DELETED 2U
#define OM_PENDING 3U /* only during an in-place rebuild */
#define OM_NONE (~(size_t)0)
#define OM_MIN_CAPACITY 8U

typedef union om_max {
  long l;
  double d;
  void *p;
  long double ld;
} om_max_t;
struct om_align_probe {
  char c;
  om_max_t u;
};
#define OM_ALIGN (offsetof(struct om_align_probe, u))

/* ---- probe sequences ----------------------------------------------------- */

static size_t om_linear(unsigned long hash, size_t i, size_t mask, void *user) {
  (void)user;
  return ((size_t)hash + i) & mask;
}

static size_t om_quadratic(unsigned long hash, size_t i, size_t mask,
                           void *user) {
  (void)user;
  return ((size_t)hash + (i * (i + 1)) / 2) & mask;
}

static size_t om_double(unsigned long hash, size_t i, size_t mask, void *user) {
  size_t step = (size_t)((hash >> 16) ^ (hash >> 7)) | 1U;
  (void)user;
  return ((size_t)hash + i * step) & mask;
}

static const ds_open_map_probe_t om_linear_probe = {"linear", om_linear};
static const ds_open_map_probe_t om_quadratic_probe = {"quadratic",
                                                       om_quadratic};
static const ds_open_map_probe_t om_double_probe = {"double", om_double};

const ds_open_map_probe_t *ds_open_map_linear_probe(void) {
  return &om_linear_probe;
}
const ds_open_map_probe_t *ds_open_map_quadratic_probe(void) {
  return &om_quadratic_probe;
}
const ds_open_map_probe_t *ds_open_map_double_probe(void) {
  return &om_double_probe;
}

/* ---- helpers ------------------------------------------------------------- */

static unsigned long om_mix(unsigned long h) {
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

static size_t om_round(size_t n) {
  return (n + OM_ALIGN - 1) / OM_ALIGN * OM_ALIGN;
}

static size_t om_max_load(size_t capacity) { return capacity - capacity / 8; }

static size_t om_pos(const ds_open_map_t *map, unsigned long h, size_t i) {
  return map->config.probe->position(h, i, map->capacity - 1,
                                     map->config.probe_user);
}

static unsigned char *om_key(const ds_open_map_t *map, size_t i) {
  return map->keys + i * map->config.key_size;
}

static unsigned char *om_value(const ds_open_map_t *map, size_t i) {
  return map->config.value_size == 0 ? om_key(map, i)
                                     : map->values + i * map->config.value_size;
}

static void om_swap_bytes(unsigned char *a, unsigned char *b, size_t n) {
  unsigned char t;
  size_t i;
  for (i = 0; i < n; i++) {
    t = a[i];
    a[i] = b[i];
    b[i] = t;
  }
}

/* Exchange the entry at pos with the one held in the scratch slot. */
static void om_swap_scratch(ds_open_map_t *map, size_t pos) {
  om_swap_bytes(om_key(map, pos), om_key(map, map->capacity),
                map->config.key_size);
  if (map->config.value_size > 0) {
    om_swap_bytes(om_value(map, pos), om_value(map, map->capacity),
                  map->config.value_size);
  }
}

size_t ds_open_map_buffer_size(size_t capacity, size_t key_size,
                               size_t value_size) {
  return OM_ALIGN - 1 + om_round(capacity * sizeof(unsigned long)) +
         om_round(capacity * sizeof(size_t)) +
         om_round((capacity + 1) * key_size) +
         om_round((capacity + 1) * value_size) + capacity;
}

/* Carve a block into the per-slot arrays. */
static void om_carve(ds_open_map_t *map, void *block, size_t capacity) {
  unsigned char *p = (unsigned char *)block;
  size_t misalign = (size_t)p % OM_ALIGN;
  if (misalign != 0) {
    p += OM_ALIGN - misalign;
  }
  map->hashes = (unsigned long *)(void *)p;
  p += om_round(capacity * sizeof(unsigned long));
  map->probes = (size_t *)(void *)p;
  p += om_round(capacity * sizeof(size_t));
  map->keys = p;
  p += om_round((capacity + 1) * map->config.key_size);
  map->values = map->config.value_size > 0 ? p : NULL;
  p += om_round((capacity + 1) * map->config.value_size);
  map->state = p;
  map->block = block;
  map->capacity = capacity;
  memset(map->state, (int)OM_EMPTY, capacity);
}

/*
 * Place the entry held in the scratch slot (hash ch, starting at probe ci)
 * by the map's discipline. PENDING slots (in-place rebuild) are swapped
 * with: the waiting entry then restarts from its home. Returns the final
 * position of the entry that was in scratch when called.
 */
static size_t om_place(ds_open_map_t *map, unsigned long ch, size_t ci) {
  size_t pos;
  size_t newpos = OM_NONE;
  bool cur_new = true;
  bool steal;
  unsigned int st;
  unsigned long th;
  size_t tp;
  for (;;) {
    pos = om_pos(map, ch, ci);
    st = map->state[pos];
    if (st == OM_EMPTY || st == OM_DELETED) {
      if (st == OM_DELETED) {
        map->deleted--;
      }
      memcpy(om_key(map, pos), om_key(map, map->capacity),
             map->config.key_size);
      if (map->config.value_size > 0) {
        memcpy(om_value(map, pos), om_value(map, map->capacity),
               map->config.value_size);
      }
      map->state[pos] = (unsigned char)OM_FULL;
      map->hashes[pos] = ch;
      map->probes[pos] = ci;
      return cur_new ? pos : newpos;
    }
    if (st == OM_PENDING) {
      om_swap_scratch(map, pos);
      th = map->hashes[pos];
      map->state[pos] = (unsigned char)OM_FULL;
      map->hashes[pos] = ch;
      map->probes[pos] = ci;
      if (cur_new) {
        newpos = pos;
      }
      cur_new = false;
      ch = th;
      ci = 0;
      continue;
    }
    steal = map->config.discipline == DS_OPEN_MAP_LCFS ||
            (map->config.discipline == DS_OPEN_MAP_ROBIN_HOOD &&
             map->probes[pos] < ci);
    if (steal) {
      om_swap_scratch(map, pos);
      th = map->hashes[pos];
      tp = map->probes[pos];
      map->hashes[pos] = ch;
      map->probes[pos] = ci;
      if (cur_new) {
        newpos = pos;
        cur_new = false;
      } else if (pos == newpos) {
        cur_new = true; /* the new entry itself was displaced */
      }
      ch = th;
      ci = tp;
    }
    ci++;
  }
}

static size_t om_find(const ds_open_map_t *map, const void *key,
                      unsigned long h) {
  size_t i;
  size_t pos;
  unsigned int st;
  for (i = 0; i < map->capacity; i++) {
    pos = om_pos(map, h, i);
    st = map->state[pos];
    if (st == OM_EMPTY) {
      return OM_NONE;
    }
    if (st == OM_FULL && map->hashes[pos] == h &&
        map->config.equal(om_key(map, pos), key, map->config.user)) {
      return pos;
    }
  }
  return OM_NONE;
}

void ds_open_map_compact(ds_open_map_t *map) {
  size_t i;
  if (map->capacity == 0) {
    return;
  }
  for (i = 0; i < map->capacity; i++) {
    map->state[i] =
        (unsigned char)(map->state[i] == OM_FULL ? OM_PENDING : OM_EMPTY);
  }
  map->deleted = 0;
  for (i = 0; i < map->capacity; i++) {
    if (map->state[i] != OM_PENDING) {
      continue;
    }
    memcpy(om_key(map, map->capacity), om_key(map, i), map->config.key_size);
    if (map->config.value_size > 0) {
      memcpy(om_value(map, map->capacity), om_value(map, i),
             map->config.value_size);
    }
    map->state[i] = (unsigned char)OM_EMPTY;
    (void)om_place(map, map->hashes[i], 0);
  }
}

static ds_status_t om_alloc(ds_open_map_t *map, size_t capacity) {
  size_t bytes;
  void *block;
  if (capacity > ((size_t)-1) / 64 ||
      (map->config.key_size + map->config.value_size) >
          ((size_t)-1) / 2 / (capacity + 1)) {
    return DS_ERR_OVERFLOW;
  }
  bytes = ds_open_map_buffer_size(capacity, map->config.key_size,
                                  map->config.value_size);
  block = ds_context_alloc(map->config.context, bytes);
  if (block == NULL) {
    return DS_ERR_ALLOC;
  }
  om_carve(map, block, capacity);
  return DS_OK;
}

static ds_status_t om_resize(ds_open_map_t *map, size_t capacity) {
  ds_open_map_t old = *map;
  ds_status_t status;
  size_t i;
  if (map->fixed) {
    return DS_ERR_OVERFLOW;
  }
  status = om_alloc(map, capacity);
  if (status != DS_OK) {
    return status;
  }
  map->deleted = 0;
  for (i = 0; i < old.capacity; i++) {
    if (old.state[i] != OM_FULL) {
      continue;
    }
    memcpy(om_key(map, map->capacity), om_key(&old, i), map->config.key_size);
    if (map->config.value_size > 0) {
      memcpy(om_value(map, map->capacity), om_value(&old, i),
             map->config.value_size);
    }
    (void)om_place(map, old.hashes[i], 0);
  }
  ds_context_free(map->config.context, old.block);
  return DS_OK;
}

/* Make room for one more entry. */
static ds_status_t om_make_room(ds_open_map_t *map) {
  size_t c;
  if (map->capacity == 0) {
    return om_resize(map, OM_MIN_CAPACITY);
  }
  if (map->size + map->deleted + 1 <= om_max_load(map->capacity)) {
    return DS_OK;
  }
  if (map->deleted > 0 && (map->fixed || map->size + 1 <= map->capacity / 2)) {
    ds_open_map_compact(map);
    if (map->size + 1 <= om_max_load(map->capacity)) {
      return DS_OK;
    }
  }
  if (map->fixed) {
    return DS_ERR_OVERFLOW;
  }
  c = map->capacity * 2;
  if (c < map->capacity) {
    return DS_ERR_OVERFLOW;
  }
  return om_resize(map, c);
}

void ds_open_map_config_init(ds_open_map_config_t *config) {
  config->key_size = 0;
  config->value_size = 0;
  config->hash = NULL;
  config->equal = NULL;
  config->user = NULL;
  config->probe = NULL;
  config->probe_user = NULL;
  config->discipline = DS_OPEN_MAP_FCFS;
  config->context = NULL;
}

static ds_status_t om_setup(ds_open_map_t *map,
                            const ds_open_map_config_t *config) {
  if (map == NULL || config == NULL || config->hash == NULL ||
      config->equal == NULL) {
    return DS_ERR_NULL;
  }
  if (config->key_size == 0 || (int)config->discipline < 0 ||
      (int)config->discipline > (int)DS_OPEN_MAP_ROBIN_HOOD) {
    return DS_ERR_INVALID;
  }
  if (config->probe != NULL && config->probe->position == NULL) {
    return DS_ERR_INVALID;
  }
  map->config = *config;
  if (map->config.probe == NULL) {
    map->config.probe = &om_linear_probe;
  }
  map->state = NULL;
  map->hashes = NULL;
  map->probes = NULL;
  map->keys = NULL;
  map->values = NULL;
  map->block = NULL;
  map->capacity = 0;
  map->size = 0;
  map->deleted = 0;
  map->fixed = false;
  return DS_OK;
}

ds_status_t ds_open_map_init(ds_open_map_t *map,
                             const ds_open_map_config_t *config,
                             size_t initial_capacity) {
  ds_status_t status = om_setup(map, config);
  if (status != DS_OK) {
    return status;
  }
  return initial_capacity > 0 ? ds_open_map_reserve(map, initial_capacity)
                              : DS_OK;
}

ds_status_t ds_open_map_init_buffer(ds_open_map_t *map,
                                    const ds_open_map_config_t *config,
                                    void *buffer, size_t capacity) {
  ds_status_t status = om_setup(map, config);
  if (status != DS_OK) {
    return status;
  }
  if (buffer == NULL) {
    return DS_ERR_NULL;
  }
  if (capacity < OM_MIN_CAPACITY || (capacity & (capacity - 1)) != 0) {
    return DS_ERR_INVALID;
  }
  om_carve(map, buffer, capacity);
  map->fixed = true;
  return DS_OK;
}

void ds_open_map_destroy(ds_open_map_t *map) {
  if (map == NULL) {
    return;
  }
  if (!map->fixed) {
    ds_context_free(map->config.context, map->block);
  }
  map->block = NULL;
  map->state = NULL;
  map->capacity = 0;
  map->size = 0;
  map->deleted = 0;
}

static void *om_emplace(ds_open_map_t *map, const void *key, bool *out_inserted,
                        ds_status_t *out_status) {
  unsigned long h = om_mix(map->config.hash(key, map->config.user));
  size_t pos = map->capacity > 0 ? om_find(map, key, h) : OM_NONE;
  ds_status_t status;
  *out_inserted = false;
  *out_status = DS_OK;
  if (pos != OM_NONE) {
    return om_value(map, pos);
  }
  status = om_make_room(map);
  if (status != DS_OK) {
    *out_status = status;
    return NULL;
  }
  memcpy(om_key(map, map->capacity), key, map->config.key_size);
  pos = om_place(map, h, 0);
  map->size++;
  *out_inserted = true;
  return om_value(map, pos);
}

void *ds_open_map_emplace(ds_open_map_t *map, const void *key,
                          bool *out_inserted) {
  bool inserted;
  ds_status_t status;
  void *slot = om_emplace(map, key, &inserted, &status);
  if (out_inserted != NULL) {
    *out_inserted = inserted;
  }
  return slot;
}

ds_status_t ds_open_map_put(ds_open_map_t *map, const void *key,
                            const void *value) {
  bool inserted;
  ds_status_t status;
  void *slot;
  if (key == NULL || (value == NULL && map->config.value_size > 0)) {
    return DS_ERR_NULL;
  }
  slot = om_emplace(map, key, &inserted, &status);
  if (slot == NULL) {
    return status;
  }
  if (map->config.value_size > 0) {
    memcpy(slot, value, map->config.value_size);
  }
  return DS_OK;
}

ds_status_t ds_open_map_insert(ds_open_map_t *map, const void *key,
                               const void *value) {
  bool inserted;
  ds_status_t status;
  void *slot;
  if (key == NULL || (value == NULL && map->config.value_size > 0)) {
    return DS_ERR_NULL;
  }
  slot = om_emplace(map, key, &inserted, &status);
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

void *ds_open_map_get(const ds_open_map_t *map, const void *key) {
  size_t pos;
  if (map->size == 0) {
    return NULL;
  }
  pos = om_find(map, key, om_mix(map->config.hash(key, map->config.user)));
  return pos == OM_NONE ? NULL : om_value(map, pos);
}

bool ds_open_map_contains(const ds_open_map_t *map, const void *key) {
  return ds_open_map_get(map, key) != NULL;
}

ds_status_t ds_open_map_remove(ds_open_map_t *map, const void *key,
                               void *out_value) {
  size_t pos;
  if (map->size == 0) {
    return DS_NOT_FOUND;
  }
  pos = om_find(map, key, om_mix(map->config.hash(key, map->config.user)));
  if (pos == OM_NONE) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && map->config.value_size > 0) {
    memcpy(out_value, om_value(map, pos), map->config.value_size);
  }
  map->state[pos] = (unsigned char)OM_DELETED;
  map->size--;
  map->deleted++;
  return DS_OK;
}

void ds_open_map_clear(ds_open_map_t *map) {
  if (map->capacity > 0) {
    memset(map->state, (int)OM_EMPTY, map->capacity);
  }
  map->size = 0;
  map->deleted = 0;
}

ds_status_t ds_open_map_reserve(ds_open_map_t *map, size_t count) {
  size_t c = map->capacity == 0 ? OM_MIN_CAPACITY : map->capacity;
  if (map->capacity > 0 && count <= om_max_load(map->capacity)) {
    if (map->deleted > 0 && count > om_max_load(map->capacity) - map->deleted) {
      ds_open_map_compact(map);
    }
    return DS_OK;
  }
  if (map->fixed) {
    return DS_ERR_OVERFLOW;
  }
  while (om_max_load(c) < count) {
    if (c > ((size_t)-1) / 4) {
      return DS_ERR_OVERFLOW;
    }
    c *= 2;
  }
  return om_resize(map, c);
}

size_t ds_open_map_size(const ds_open_map_t *map) { return map->size; }

size_t ds_open_map_capacity(const ds_open_map_t *map) { return map->capacity; }

size_t ds_open_map_probe_length(const ds_open_map_t *map, const void *key) {
  size_t pos;
  if (map->size == 0) {
    return OM_NONE;
  }
  pos = om_find(map, key, om_mix(map->config.hash(key, map->config.user)));
  return pos == OM_NONE ? OM_NONE : map->probes[pos];
}

bool ds_open_map_next(const ds_open_map_t *map, size_t *cursor,
                      const void **out_key, void **out_value) {
  size_t i;
  for (i = *cursor; i < map->capacity; i++) {
    if (map->state[i] == OM_FULL) {
      if (out_key != NULL) {
        *out_key = om_key(map, i);
      }
      if (out_value != NULL) {
        *out_value = map->config.value_size > 0 ? om_value(map, i) : NULL;
      }
      *cursor = i + 1;
      return true;
    }
  }
  *cursor = map->capacity;
  return false;
}
