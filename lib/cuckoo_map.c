#include "cuckoo_map.h"
#include <limits.h>
#include <string.h>

#define CK_NONE (~(size_t)0)
#define CK_SEARCH_MAX 256U
#define CK_MIN_BUCKETS 4U

typedef union ck_max {
  long l;
  double d;
  void *p;
  long double ld;
} ck_max_t;
struct ck_align_probe {
  char c;
  ck_max_t u;
};
#define CK_ALIGN (offsetof(struct ck_align_probe, u))

typedef struct ck_node {
  size_t bucket;
  size_t parent;     /* CK_NONE for a root */
  unsigned int slot; /* slot of the parent bucket whose entry moves here */
} ck_node_t;

static unsigned long ck_mix(unsigned long h) {
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

static size_t ck_round(size_t n) {
  return (n + CK_ALIGN - 1) / CK_ALIGN * CK_ALIGN;
}

static size_t ck_slots(const ds_cuckoo_map_t *map) {
  return map->bucket_count * map->config.slots;
}

static size_t ck_bucket(const ds_cuckoo_map_t *map, unsigned long h,
                        unsigned int way) {
  unsigned long x = h ^ (map->seed + 0x9E3779B9UL * (unsigned long)(way + 1));
  return (size_t)ck_mix(x) & (map->bucket_count - 1);
}

static unsigned char *ck_key(const ds_cuckoo_map_t *map, size_t i) {
  return map->keys + i * map->config.key_size;
}

static unsigned char *ck_value(const ds_cuckoo_map_t *map, size_t i) {
  return map->config.value_size == 0 ? ck_key(map, i)
                                     : map->values + i * map->config.value_size;
}

static void ck_move(ds_cuckoo_map_t *dst, size_t to, const ds_cuckoo_map_t *src,
                    size_t from) {
  memcpy(ck_key(dst, to), ck_key(src, from), dst->config.key_size);
  if (dst->config.value_size > 0) {
    memcpy(ck_value(dst, to), ck_value(src, from), dst->config.value_size);
  }
  dst->hashes[to] = src->hashes[from];
  dst->used[to] = 1;
}

size_t ds_cuckoo_map_buffer_size(const ds_cuckoo_map_config_t *config,
                                 size_t bucket_count) {
  size_t n = bucket_count * config->slots + config->stash;
  return CK_ALIGN - 1 + ck_round(n * sizeof(unsigned long)) +
         ck_round(n * config->key_size) + ck_round(n * config->value_size) + n;
}

static void ck_carve(ds_cuckoo_map_t *map, void *block, size_t bucket_count) {
  unsigned char *p = (unsigned char *)block;
  size_t misalign = (size_t)p % CK_ALIGN;
  size_t n = bucket_count * map->config.slots + map->config.stash;
  if (misalign != 0) {
    p += CK_ALIGN - misalign;
  }
  map->hashes = (unsigned long *)(void *)p;
  p += ck_round(n * sizeof(unsigned long));
  map->keys = p;
  p += ck_round(n * map->config.key_size);
  map->values = map->config.value_size > 0 ? p : NULL;
  p += ck_round(n * map->config.value_size);
  map->used = p;
  memset(map->used, 0, n);
  map->block = block;
  map->bucket_count = bucket_count;
  map->size = 0;
  map->stash_used = 0;
}

static size_t ck_find(const ds_cuckoo_map_t *map, const void *key,
                      unsigned long h) {
  unsigned int w;
  unsigned int s;
  size_t b;
  size_t i;
  size_t end;
  if (map->bucket_count == 0) {
    return CK_NONE;
  }
  for (w = 0; w < map->config.ways; w++) {
    b = ck_bucket(map, h, w);
    for (s = 0; s < map->config.slots; s++) {
      i = b * map->config.slots + s;
      if (map->used[i] && map->hashes[i] == h &&
          map->config.equal(ck_key(map, i), key, map->config.user)) {
        return i;
      }
    }
  }
  if (map->stash_used > 0) {
    end = ck_slots(map) + map->config.stash;
    for (i = ck_slots(map); i < end; i++) {
      if (map->used[i] && map->hashes[i] == h &&
          map->config.equal(ck_key(map, i), key, map->config.user)) {
        return i;
      }
    }
  }
  return CK_NONE;
}

static size_t ck_free_slot(const ds_cuckoo_map_t *map, size_t b) {
  unsigned int s;
  for (s = 0; s < map->config.slots; s++) {
    if (!map->used[b * map->config.slots + s]) {
      return b * map->config.slots + s;
    }
  }
  return CK_NONE;
}

static bool ck_is_candidate(const ds_cuckoo_map_t *map, unsigned long h,
                            size_t b) {
  unsigned int w;
  for (w = 0; w < map->config.ways; w++) {
    if (ck_bucket(map, h, w) == b) {
      return true;
    }
  }
  return false;
}

/*
 * Find a free slot for a key of hash h, moving entries along the shortest
 * displacement chain found breadth-first. Returns the freed slot (not yet
 * marked used) or CK_NONE; on CK_NONE the table may have had valid entries
 * relocated but holds the same set.
 */
static size_t ck_make_slot(ds_cuckoo_map_t *map, unsigned long h) {
  ck_node_t q[CK_SEARCH_MAX];
  size_t budget = map->config.search;
  size_t head = 0;
  size_t tail = 0;
  size_t free_slot;
  size_t cur;
  size_t src;
  size_t b2;
  unsigned int w;
  unsigned int s;
  if (budget > CK_SEARCH_MAX) {
    budget = CK_SEARCH_MAX;
  }
  for (w = 0; w < map->config.ways && tail < budget; w++) {
    q[tail].bucket = ck_bucket(map, h, w);
    q[tail].parent = CK_NONE;
    q[tail].slot = 0;
    tail++;
  }
  while (head < tail) {
    free_slot = ck_free_slot(map, q[head].bucket);
    if (free_slot != CK_NONE) {
      cur = head;
      while (q[cur].parent != CK_NONE) {
        src = q[q[cur].parent].bucket * map->config.slots + q[cur].slot;
        if (!map->used[src] ||
            !ck_is_candidate(map, map->hashes[src], q[cur].bucket)) {
          return CK_NONE; /* chain went stale (a repeated slot) */
        }
        ck_move(map, free_slot, map, src);
        map->used[src] = 0;
        free_slot = src;
        cur = q[cur].parent;
      }
      return free_slot;
    }
    for (s = 0; s < map->config.slots; s++) {
      src = q[head].bucket * map->config.slots + s;
      for (w = 0; w < map->config.ways; w++) {
        b2 = ck_bucket(map, map->hashes[src], w);
        if (b2 == q[head].bucket || tail >= budget) {
          continue;
        }
        q[tail].bucket = b2;
        q[tail].parent = head;
        q[tail].slot = s;
        tail++;
      }
    }
    head++;
  }
  return CK_NONE;
}

static size_t ck_stash_slot(const ds_cuckoo_map_t *map) {
  size_t i;
  size_t end = ck_slots(map) + map->config.stash;
  for (i = ck_slots(map); i < end; i++) {
    if (!map->used[i]) {
      return i;
    }
  }
  return CK_NONE;
}

/* A slot for an absent entry of hash h: in the table, else in the stash.
 * Returns CK_NONE when both are full. */
static size_t ck_place(ds_cuckoo_map_t *map, unsigned long h) {
  size_t slot = ck_make_slot(map, h);
  if (slot == CK_NONE) {
    slot = ck_stash_slot(map);
    if (slot != CK_NONE) {
      map->stash_used++;
    }
  }
  return slot;
}

static ds_status_t ck_alloc(ds_cuckoo_map_t *map, size_t bucket_count) {
  void *block;
  size_t n;
  if (bucket_count > ((size_t)-1) / 64 / DS_CUCKOO_MAP_MAX_SLOTS) {
    return DS_ERR_OVERFLOW;
  }
  n = bucket_count * map->config.slots + map->config.stash;
  if (map->config.key_size + map->config.value_size > ((size_t)-1) / 2 / n) {
    return DS_ERR_OVERFLOW;
  }
  block =
      ds_context_alloc(map->config.context,
                       ds_cuckoo_map_buffer_size(&map->config, bucket_count));
  if (block == NULL) {
    return DS_ERR_ALLOC;
  }
  ck_carve(map, block, bucket_count);
  return DS_OK;
}

/* Rebuild into `bucket_count` buckets with a fresh seed; on failure the
 * old table is kept. */
static ds_status_t ck_rehash(ds_cuckoo_map_t *map, size_t bucket_count) {
  ds_cuckoo_map_t old = *map;
  ds_status_t status;
  size_t i;
  size_t end = old.bucket_count == 0 ? 0 : ck_slots(&old) + old.config.stash;
  size_t slot;
  unsigned int attempt;
  for (attempt = 0; attempt < 8; attempt++) {
    status = ck_alloc(map, bucket_count);
    if (status != DS_OK) {
      *map = old;
      return status;
    }
    map->seed = ck_mix(old.seed + 0x6A09E667UL * (unsigned long)(attempt + 1));
    for (i = 0; i < end; i++) {
      if (!old.used[i]) {
        continue;
      }
      slot = ck_place(map, old.hashes[i]);
      if (slot == CK_NONE) {
        break;
      }
      ck_move(map, slot, &old, i);
      map->size++;
    }
    if (i == end) {
      ds_context_free(map->config.context, old.block);
      return DS_OK;
    }
    ds_context_free(map->config.context, map->block);
    bucket_count *= 2;
  }
  *map = old;
  return DS_ERR_INTERNAL;
}

void ds_cuckoo_map_config_init(ds_cuckoo_map_config_t *config) {
  config->key_size = 0;
  config->value_size = 0;
  config->hash = NULL;
  config->equal = NULL;
  config->user = NULL;
  config->ways = 2;
  config->slots = 4;
  config->stash = 4;
  config->search = CK_SEARCH_MAX;
  config->context = NULL;
}

static ds_status_t ck_setup(ds_cuckoo_map_t *map,
                            const ds_cuckoo_map_config_t *config) {
  if (map == NULL || config == NULL || config->hash == NULL ||
      config->equal == NULL) {
    return DS_ERR_NULL;
  }
  if (config->key_size == 0 || config->ways < 2 ||
      config->ways > DS_CUCKOO_MAP_MAX_WAYS || config->slots < 1 ||
      config->slots > DS_CUCKOO_MAP_MAX_SLOTS) {
    return DS_ERR_INVALID;
  }
  map->config = *config;
  if (map->config.search == 0) {
    map->config.search = CK_SEARCH_MAX;
  }
  map->hashes = NULL;
  map->used = NULL;
  map->keys = NULL;
  map->values = NULL;
  map->block = NULL;
  map->bucket_count = 0;
  map->size = 0;
  map->stash_used = 0;
  map->seed = 0x243F6A88UL;
  map->fixed = false;
  return DS_OK;
}

static size_t ck_buckets_for(const ds_cuckoo_map_config_t *config,
                             size_t count) {
  size_t b = CK_MIN_BUCKETS;
  /* aim for ~50% load with 1 slot and 2 ways, ~85% otherwise */
  size_t num = config->slots == 1 && config->ways == 2 ? 2 : 6;
  size_t den = config->slots == 1 && config->ways == 2 ? 1 : 5;
  while (b * config->slots < count / den * num + num) {
    if (b > ((size_t)-1) / 4) {
      return 0;
    }
    b *= 2;
  }
  return b;
}

ds_status_t ds_cuckoo_map_init(ds_cuckoo_map_t *map,
                               const ds_cuckoo_map_config_t *config,
                               size_t initial_capacity) {
  ds_status_t status = ck_setup(map, config);
  if (status != DS_OK) {
    return status;
  }
  return initial_capacity > 0 ? ds_cuckoo_map_reserve(map, initial_capacity)
                              : DS_OK;
}

ds_status_t ds_cuckoo_map_init_buffer(ds_cuckoo_map_t *map,
                                      const ds_cuckoo_map_config_t *config,
                                      void *buffer, size_t bucket_count) {
  ds_status_t status = ck_setup(map, config);
  if (status != DS_OK) {
    return status;
  }
  if (buffer == NULL) {
    return DS_ERR_NULL;
  }
  if (bucket_count == 0 || (bucket_count & (bucket_count - 1)) != 0) {
    return DS_ERR_INVALID;
  }
  ck_carve(map, buffer, bucket_count);
  map->fixed = true;
  return DS_OK;
}

void ds_cuckoo_map_destroy(ds_cuckoo_map_t *map) {
  if (map == NULL) {
    return;
  }
  if (!map->fixed) {
    ds_context_free(map->config.context, map->block);
  }
  map->block = NULL;
  map->used = NULL;
  map->bucket_count = 0;
  map->size = 0;
  map->stash_used = 0;
}

static void *ck_emplace(ds_cuckoo_map_t *map, const void *key,
                        bool *out_inserted, ds_status_t *out_status) {
  unsigned long h = ck_mix(map->config.hash(key, map->config.user));
  size_t slot = ck_find(map, key, h);
  ds_status_t status;
  *out_inserted = false;
  *out_status = DS_OK;
  if (slot != CK_NONE) {
    return ck_value(map, slot);
  }
  if (map->bucket_count == 0) {
    status = ck_rehash(map, ck_buckets_for(&map->config, 1));
    if (status != DS_OK) {
      *out_status = status;
      return NULL;
    }
  }
  for (;;) {
    slot = ck_place(map, h);
    if (slot != CK_NONE) {
      break;
    }
    /* Failing at under 1/8 load means many keys share a full hash: growing
     * cannot separate them. */
    if (map->fixed || map->size < ck_slots(map) / 8) {
      *out_status = DS_ERR_OVERFLOW;
      return NULL;
    }
    status = ck_rehash(map, map->bucket_count * 2);
    if (status != DS_OK) {
      *out_status = status;
      return NULL;
    }
  }
  memcpy(ck_key(map, slot), key, map->config.key_size);
  map->hashes[slot] = h;
  map->used[slot] = 1;
  map->size++;
  *out_inserted = true;
  return ck_value(map, slot);
}

void *ds_cuckoo_map_emplace(ds_cuckoo_map_t *map, const void *key,
                            bool *out_inserted) {
  bool inserted;
  ds_status_t status;
  void *slot = ck_emplace(map, key, &inserted, &status);
  if (out_inserted != NULL) {
    *out_inserted = inserted;
  }
  return slot;
}

ds_status_t ds_cuckoo_map_put(ds_cuckoo_map_t *map, const void *key,
                              const void *value) {
  bool inserted;
  ds_status_t status;
  void *slot;
  if (key == NULL || (value == NULL && map->config.value_size > 0)) {
    return DS_ERR_NULL;
  }
  slot = ck_emplace(map, key, &inserted, &status);
  if (slot == NULL) {
    return status;
  }
  if (map->config.value_size > 0) {
    memcpy(slot, value, map->config.value_size);
  }
  return DS_OK;
}

ds_status_t ds_cuckoo_map_insert(ds_cuckoo_map_t *map, const void *key,
                                 const void *value) {
  bool inserted;
  ds_status_t status;
  void *slot;
  if (key == NULL || (value == NULL && map->config.value_size > 0)) {
    return DS_ERR_NULL;
  }
  slot = ck_emplace(map, key, &inserted, &status);
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

void *ds_cuckoo_map_get(const ds_cuckoo_map_t *map, const void *key) {
  size_t slot;
  if (map->size == 0) {
    return NULL;
  }
  slot = ck_find(map, key, ck_mix(map->config.hash(key, map->config.user)));
  return slot == CK_NONE ? NULL : ck_value(map, slot);
}

bool ds_cuckoo_map_contains(const ds_cuckoo_map_t *map, const void *key) {
  return ds_cuckoo_map_get(map, key) != NULL;
}

ds_status_t ds_cuckoo_map_remove(ds_cuckoo_map_t *map, const void *key,
                                 void *out_value) {
  size_t slot;
  size_t b;
  size_t i;
  size_t end;
  if (map->size == 0) {
    return DS_NOT_FOUND;
  }
  slot = ck_find(map, key, ck_mix(map->config.hash(key, map->config.user)));
  if (slot == CK_NONE) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && map->config.value_size > 0) {
    memcpy(out_value, ck_value(map, slot), map->config.value_size);
  }
  map->used[slot] = 0;
  map->size--;
  if (slot >= ck_slots(map)) {
    map->stash_used--;
  } else if (map->stash_used > 0) {
    /* Pull a stashed entry that can use the freed bucket. */
    b = slot / map->config.slots;
    end = ck_slots(map) + map->config.stash;
    for (i = ck_slots(map); i < end; i++) {
      if (map->used[i] && ck_is_candidate(map, map->hashes[i], b)) {
        ck_move(map, slot, map, i);
        map->used[i] = 0;
        map->stash_used--;
        break;
      }
    }
  }
  return DS_OK;
}

void ds_cuckoo_map_clear(ds_cuckoo_map_t *map) {
  if (map->bucket_count > 0) {
    memset(map->used, 0, ck_slots(map) + map->config.stash);
  }
  map->size = 0;
  map->stash_used = 0;
}

ds_status_t ds_cuckoo_map_reserve(ds_cuckoo_map_t *map, size_t count) {
  size_t b = ck_buckets_for(&map->config, count);
  if (b == 0) {
    return DS_ERR_OVERFLOW;
  }
  if (b <= map->bucket_count) {
    return DS_OK;
  }
  if (map->fixed) {
    return DS_ERR_OVERFLOW;
  }
  return ck_rehash(map, b);
}

size_t ds_cuckoo_map_size(const ds_cuckoo_map_t *map) { return map->size; }

size_t ds_cuckoo_map_capacity(const ds_cuckoo_map_t *map) {
  return ck_slots(map);
}

size_t ds_cuckoo_map_stash_size(const ds_cuckoo_map_t *map) {
  return map->stash_used;
}

bool ds_cuckoo_map_next(const ds_cuckoo_map_t *map, size_t *cursor,
                        const void **out_key, void **out_value) {
  size_t i;
  size_t end = map->bucket_count == 0 ? 0 : ck_slots(map) + map->config.stash;
  for (i = *cursor; i < end; i++) {
    if (map->used[i]) {
      if (out_key != NULL) {
        *out_key = ck_key(map, i);
      }
      if (out_value != NULL) {
        *out_value = map->config.value_size > 0 ? ck_value(map, i) : NULL;
      }
      *cursor = i + 1;
      return true;
    }
  }
  *cursor = end;
  return false;
}
