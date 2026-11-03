#include "coalesced_map.h"
#include <limits.h>
#include <string.h>

#define CM_NONE (~(size_t)0)
#define CM_MIN_CAPACITY 8U

typedef union cm_max {
  long l;
  double d;
  void *p;
  long double ld;
} cm_max_t;
struct cm_align_probe {
  char c;
  cm_max_t u;
};
#define CM_ALIGN (offsetof(struct cm_align_probe, u))

static unsigned long cm_mix(unsigned long h) {
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

static size_t cm_round(size_t n) {
  return (n + CM_ALIGN - 1) / CM_ALIGN * CM_ALIGN;
}

static unsigned char *cm_key(const ds_coalesced_map_t *map, size_t i) {
  return map->keys + i * map->config.key_size;
}

static unsigned char *cm_value(const ds_coalesced_map_t *map, size_t i) {
  return map->config.value_size == 0 ? cm_key(map, i)
                                     : map->values + i * map->config.value_size;
}

static size_t cm_home(const ds_coalesced_map_t *map, unsigned long h) {
  return (size_t)(h % (unsigned long)map->address);
}

static void cm_copy(ds_coalesced_map_t *dst, size_t to,
                    const ds_coalesced_map_t *src, size_t from) {
  memcpy(cm_key(dst, to), cm_key(src, from), dst->config.key_size);
  if (dst->config.value_size > 0) {
    memcpy(cm_value(dst, to), cm_value(src, from), dst->config.value_size);
  }
}

size_t ds_coalesced_map_buffer_size(const ds_coalesced_map_config_t *config,
                                    size_t capacity) {
  return CM_ALIGN - 1 + 2 * cm_round(capacity * sizeof(size_t)) +
         cm_round(capacity * sizeof(unsigned long)) +
         cm_round((capacity + 1) * config->key_size) +
         cm_round((capacity + 1) * config->value_size) + capacity;
}

static void cm_carve(ds_coalesced_map_t *map, void *block, size_t capacity) {
  unsigned char *p = (unsigned char *)block;
  size_t misalign = (size_t)p % CM_ALIGN;
  if (misalign != 0) {
    p += CM_ALIGN - misalign;
  }
  map->next = (size_t *)(void *)p;
  p += cm_round(capacity * sizeof(size_t));
  map->prev = (size_t *)(void *)p;
  p += cm_round(capacity * sizeof(size_t));
  map->hashes = (unsigned long *)(void *)p;
  p += cm_round(capacity * sizeof(unsigned long));
  map->keys = p;
  p += cm_round((capacity + 1) * map->config.key_size);
  map->values = map->config.value_size > 0 ? p : NULL;
  p += cm_round((capacity + 1) * map->config.value_size);
  map->used = p;
  memset(map->used, 0, capacity);
  map->block = block;
  map->capacity = capacity;
  map->address = capacity / 100 * map->config.address_percent +
                 capacity % 100 * map->config.address_percent / 100;
  if (map->address == 0) {
    map->address = 1;
  }
  map->size = 0;
  map->free_hint = capacity;
}

static size_t cm_take_free(ds_coalesced_map_t *map) {
  size_t i = map->free_hint;
  while (i > 0) {
    i--;
    if (!map->used[i]) {
      map->free_hint = i;
      return i;
    }
  }
  map->free_hint = 0;
  return CM_NONE;
}

static void cm_release(ds_coalesced_map_t *map, size_t i) {
  map->used[i] = 0;
  if (i + 1 > map->free_hint) {
    map->free_hint = i + 1;
  }
}

/* Place the entry held in the scratch slot. Returns its slot or CM_NONE
 * when no slot is free (nothing changes then). */
static size_t cm_place(ds_coalesced_map_t *map, unsigned long h) {
  size_t home = cm_home(map, h);
  size_t f;
  size_t t;
  if (!map->used[home]) {
    f = home;
    map->next[f] = CM_NONE;
    map->prev[f] = CM_NONE;
  } else {
    f = cm_take_free(map);
    if (f == CM_NONE) {
      return CM_NONE;
    }
    if (map->config.policy == DS_COALESCED_EISCH) {
      t = map->next[home];
      map->next[home] = f;
      map->prev[f] = home;
      map->next[f] = t;
      if (t != CM_NONE) {
        map->prev[t] = f;
      }
    } else {
      t = home;
      while (map->next[t] != CM_NONE) {
        t = map->next[t];
      }
      map->next[t] = f;
      map->prev[f] = t;
      map->next[f] = CM_NONE;
    }
  }
  cm_copy(map, f, map, map->capacity);
  map->hashes[f] = h;
  map->used[f] = 1;
  return f;
}

static size_t cm_find(const ds_coalesced_map_t *map, const void *key,
                      unsigned long h, size_t *out_links) {
  size_t i;
  size_t links = 0;
  if (map->capacity == 0) {
    return CM_NONE;
  }
  i = cm_home(map, h);
  if (!map->used[i]) {
    return CM_NONE;
  }
  while (i != CM_NONE) {
    if (map->hashes[i] == h &&
        map->config.equal(cm_key(map, i), key, map->config.user)) {
      if (out_links != NULL) {
        *out_links = links;
      }
      return i;
    }
    i = map->next[i];
    links++;
  }
  return CM_NONE;
}

static ds_status_t cm_alloc(ds_coalesced_map_t *map, size_t capacity) {
  void *block;
  if (capacity > ((size_t)-1) / 64 ||
      map->config.key_size + map->config.value_size >
          ((size_t)-1) / 2 / (capacity + 1)) {
    return DS_ERR_OVERFLOW;
  }
  block =
      ds_context_alloc(map->config.context,
                       ds_coalesced_map_buffer_size(&map->config, capacity));
  if (block == NULL) {
    return DS_ERR_ALLOC;
  }
  cm_carve(map, block, capacity);
  return DS_OK;
}

static ds_status_t cm_resize(ds_coalesced_map_t *map, size_t capacity) {
  ds_coalesced_map_t old = *map;
  ds_status_t status;
  size_t i;
  if (map->fixed) {
    return DS_ERR_OVERFLOW;
  }
  status = cm_alloc(map, capacity);
  if (status != DS_OK) {
    *map = old;
    return status;
  }
  for (i = 0; i < old.capacity; i++) {
    if (old.used[i]) {
      cm_copy(map, map->capacity, &old, i);
      (void)cm_place(map, old.hashes[i]);
      map->size++;
    }
  }
  ds_context_free(map->config.context, old.block);
  return DS_OK;
}

void ds_coalesced_map_config_init(ds_coalesced_map_config_t *config) {
  config->key_size = 0;
  config->value_size = 0;
  config->hash = NULL;
  config->equal = NULL;
  config->user = NULL;
  config->policy = DS_COALESCED_LISCH;
  config->address_percent = 86;
  config->context = NULL;
}

static ds_status_t cm_setup(ds_coalesced_map_t *map,
                            const ds_coalesced_map_config_t *config) {
  if (map == NULL || config == NULL || config->hash == NULL ||
      config->equal == NULL) {
    return DS_ERR_NULL;
  }
  if (config->key_size == 0 || config->address_percent == 0 ||
      config->address_percent > 100 ||
      (config->policy != DS_COALESCED_LISCH &&
       config->policy != DS_COALESCED_EISCH)) {
    return DS_ERR_INVALID;
  }
  map->config = *config;
  map->next = NULL;
  map->prev = NULL;
  map->hashes = NULL;
  map->used = NULL;
  map->keys = NULL;
  map->values = NULL;
  map->block = NULL;
  map->capacity = 0;
  map->address = 0;
  map->size = 0;
  map->free_hint = 0;
  map->fixed = false;
  return DS_OK;
}

ds_status_t ds_coalesced_map_init(ds_coalesced_map_t *map,
                                  const ds_coalesced_map_config_t *config,
                                  size_t initial_capacity) {
  ds_status_t status = cm_setup(map, config);
  size_t c = CM_MIN_CAPACITY;
  if (status != DS_OK || initial_capacity == 0) {
    return status;
  }
  while (c < initial_capacity) {
    if (c > ((size_t)-1) / 4) {
      return DS_ERR_OVERFLOW;
    }
    c *= 2;
  }
  return cm_alloc(map, c);
}

ds_status_t
ds_coalesced_map_init_buffer(ds_coalesced_map_t *map,
                             const ds_coalesced_map_config_t *config,
                             void *buffer, size_t capacity) {
  ds_status_t status = cm_setup(map, config);
  if (status != DS_OK) {
    return status;
  }
  if (buffer == NULL) {
    return DS_ERR_NULL;
  }
  if (capacity == 0) {
    return DS_ERR_INVALID;
  }
  cm_carve(map, buffer, capacity);
  map->fixed = true;
  return DS_OK;
}

void ds_coalesced_map_destroy(ds_coalesced_map_t *map) {
  if (map == NULL) {
    return;
  }
  if (!map->fixed) {
    ds_context_free(map->config.context, map->block);
  }
  map->block = NULL;
  map->used = NULL;
  map->capacity = 0;
  map->size = 0;
}

static void *cm_emplace(ds_coalesced_map_t *map, const void *key,
                        bool *out_inserted, ds_status_t *out_status) {
  unsigned long h = cm_mix(map->config.hash(key, map->config.user));
  size_t slot = cm_find(map, key, h, NULL);
  ds_status_t status;
  *out_inserted = false;
  *out_status = DS_OK;
  if (slot != CM_NONE) {
    return cm_value(map, slot);
  }
  if (map->capacity == 0) {
    status = cm_alloc(map, CM_MIN_CAPACITY);
    if (status != DS_OK) {
      *out_status = status;
      return NULL;
    }
  }
  memcpy(cm_key(map, map->capacity), key, map->config.key_size);
  slot = cm_place(map, h);
  if (slot == CM_NONE) {
    status = cm_resize(map, map->capacity * 2);
    if (status != DS_OK) {
      *out_status = status;
      return NULL;
    }
    memcpy(cm_key(map, map->capacity), key, map->config.key_size);
    slot = cm_place(map, h);
  }
  map->size++;
  *out_inserted = true;
  return cm_value(map, slot);
}

void *ds_coalesced_map_emplace(ds_coalesced_map_t *map, const void *key,
                               bool *out_inserted) {
  bool inserted;
  ds_status_t status;
  void *slot = cm_emplace(map, key, &inserted, &status);
  if (out_inserted != NULL) {
    *out_inserted = inserted;
  }
  return slot;
}

ds_status_t ds_coalesced_map_put(ds_coalesced_map_t *map, const void *key,
                                 const void *value) {
  bool inserted;
  ds_status_t status;
  void *slot;
  if (key == NULL || (value == NULL && map->config.value_size > 0)) {
    return DS_ERR_NULL;
  }
  slot = cm_emplace(map, key, &inserted, &status);
  if (slot == NULL) {
    return status;
  }
  if (map->config.value_size > 0) {
    memcpy(slot, value, map->config.value_size);
  }
  return DS_OK;
}

ds_status_t ds_coalesced_map_insert(ds_coalesced_map_t *map, const void *key,
                                    const void *value) {
  bool inserted;
  ds_status_t status;
  void *slot;
  if (key == NULL || (value == NULL && map->config.value_size > 0)) {
    return DS_ERR_NULL;
  }
  slot = cm_emplace(map, key, &inserted, &status);
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

void *ds_coalesced_map_get(const ds_coalesced_map_t *map, const void *key) {
  size_t slot;
  if (map->size == 0) {
    return NULL;
  }
  slot =
      cm_find(map, key, cm_mix(map->config.hash(key, map->config.user)), NULL);
  return slot == CM_NONE ? NULL : cm_value(map, slot);
}

bool ds_coalesced_map_contains(const ds_coalesced_map_t *map, const void *key) {
  return ds_coalesced_map_get(map, key) != NULL;
}

ds_status_t ds_coalesced_map_remove(ds_coalesced_map_t *map, const void *key,
                                    void *out_value) {
  size_t p;
  size_t t;
  size_t nt;
  if (map->size == 0) {
    return DS_NOT_FOUND;
  }
  p = cm_find(map, key, cm_mix(map->config.hash(key, map->config.user)), NULL);
  if (p == CM_NONE) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && map->config.value_size > 0) {
    memcpy(out_value, cm_value(map, p), map->config.value_size);
  }
  if (map->prev[p] != CM_NONE) {
    map->next[map->prev[p]] = CM_NONE;
  }
  t = map->next[p];
  cm_release(map, p);
  map->size--;
  /* Re-insert what followed, as if the removed key had never been there.
   * Each re-insertion can reuse the slot it just vacated. */
  while (t != CM_NONE) {
    nt = map->next[t];
    cm_copy(map, map->capacity, map, t);
    cm_release(map, t);
    (void)cm_place(map, map->hashes[t]);
    t = nt;
  }
  return DS_OK;
}

void ds_coalesced_map_clear(ds_coalesced_map_t *map) {
  if (map->capacity > 0) {
    memset(map->used, 0, map->capacity);
  }
  map->size = 0;
  map->free_hint = map->capacity;
}

size_t ds_coalesced_map_size(const ds_coalesced_map_t *map) {
  return map->size;
}

size_t ds_coalesced_map_capacity(const ds_coalesced_map_t *map) {
  return map->capacity;
}

size_t ds_coalesced_map_probe_length(const ds_coalesced_map_t *map,
                                     const void *key) {
  size_t links = CM_NONE;
  if (map->size == 0) {
    return CM_NONE;
  }
  (void)cm_find(map, key, cm_mix(map->config.hash(key, map->config.user)),
                &links);
  return links;
}

bool ds_coalesced_map_next(const ds_coalesced_map_t *map, size_t *cursor,
                           const void **out_key, void **out_value) {
  size_t i;
  for (i = *cursor; i < map->capacity; i++) {
    if (map->used[i]) {
      if (out_key != NULL) {
        *out_key = cm_key(map, i);
      }
      if (out_value != NULL) {
        *out_value = map->config.value_size > 0 ? cm_value(map, i) : NULL;
      }
      *cursor = i + 1;
      return true;
    }
  }
  *cursor = map->capacity;
  return false;
}
