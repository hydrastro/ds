#include "linear_hash_map.h"
#include <limits.h>
#include <string.h>

typedef union lh_max {
  long l;
  double d;
  void *p;
  long double ld;
} lh_max_t;
struct lh_align_probe {
  char c;
  lh_max_t u;
};
#define LH_ALIGN (offsetof(struct lh_align_probe, u))

static unsigned long lh_mix(unsigned long h) {
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

static size_t lh_round(size_t n) {
  return (n + LH_ALIGN - 1) / LH_ALIGN * LH_ALIGN;
}

static ds_vec_t *lh_bucket(const ds_linear_hash_map_t *map, size_t i) {
  return (ds_vec_t *)(void *)(map->buckets.data + i * sizeof(ds_vec_t));
}

static size_t lh_count(const ds_linear_hash_map_t *map) {
  return map->buckets.size;
}

static size_t lh_round_size(const ds_linear_hash_map_t *map) {
  return map->config.initial_buckets << map->level;
}

static size_t lh_address(const ds_linear_hash_map_t *map, unsigned long h) {
  unsigned long m = (unsigned long)lh_round_size(map);
  size_t a = (size_t)(h % m);
  if (a < map->split) {
    a = (size_t)(h % (2 * m));
  }
  return a;
}

static unsigned long lh_rec_hash(const unsigned char *rec) {
  unsigned long h;
  memcpy(&h, rec, sizeof h);
  return h;
}

static unsigned char *lh_rec(const ds_vec_t *b, size_t i) {
  return b->data + i * b->elem_size;
}

static void *lh_rec_value(const ds_linear_hash_map_t *map, unsigned char *rec) {
  return map->config.value_size == 0 ? rec + map->key_offset
                                     : rec + map->value_offset;
}

static double lh_load(const ds_linear_hash_map_t *map) {
  return (double)map->size /
         ((double)map->buckets.size * (double)map->config.bucket_capacity);
}

static bool lh_find(const ds_linear_hash_map_t *map, const void *key,
                    unsigned long h, size_t *out_bucket, size_t *out_index) {
  size_t b = lh_address(map, h);
  const ds_vec_t *bucket = lh_bucket(map, b);
  size_t i;
  unsigned char *rec;
  for (i = 0; i < bucket->size; i++) {
    rec = lh_rec(bucket, i);
    if (lh_rec_hash(rec) == h &&
        map->config.equal(rec + map->key_offset, key, map->config.user)) {
      *out_bucket = b;
      *out_index = i;
      return true;
    }
  }
  return false;
}

/* Split bucket `split` into itself and a new last bucket. Leaves the map
 * unchanged if memory runs out. */
static ds_status_t lh_split(ds_linear_hash_map_t *map) {
  size_t m = lh_round_size(map);
  size_t s = map->split;
  ds_vec_t fresh;
  ds_vec_t *src;
  ds_vec_t *dst;
  size_t movers = 0;
  size_t i;
  size_t keep;
  unsigned long h;
  ds_status_t status;
  status = ds_vec_init(&fresh, map->record_size, map->config.context);
  if (status != DS_OK) {
    return status;
  }
  src = lh_bucket(map, s);
  for (i = 0; i < src->size; i++) {
    if ((size_t)(lh_rec_hash(lh_rec(src, i)) % (2 * (unsigned long)m)) != s) {
      movers++;
    }
  }
  status = ds_vec_reserve(&fresh, movers);
  if (status == DS_OK) {
    status = ds_vec_push(&map->buckets, &fresh);
  }
  if (status != DS_OK) {
    ds_vec_destroy(&fresh);
    return status;
  }
  src = lh_bucket(map, s); /* the bucket array may have moved */
  dst = lh_bucket(map, lh_count(map) - 1);
  keep = 0;
  for (i = 0; i < src->size; i++) {
    h = lh_rec_hash(lh_rec(src, i));
    if ((size_t)(h % (2 * (unsigned long)m)) != s) {
      (void)ds_vec_push(dst, lh_rec(src, i));
    } else {
      if (keep != i) {
        memcpy(lh_rec(src, keep), lh_rec(src, i), map->record_size);
      }
      keep++;
    }
  }
  src->size = keep;
  map->split++;
  if (map->split == m) {
    map->level++;
    map->split = 0;
  }
  return DS_OK;
}

/* Merge the last bucket back into its buddy. */
static ds_status_t lh_merge(ds_linear_hash_map_t *map) {
  size_t level = map->level;
  size_t split = map->split;
  ds_vec_t *last;
  ds_vec_t *target;
  ds_status_t status;
  if (lh_count(map) <= map->config.initial_buckets) {
    return DS_ERR_STATE;
  }
  if (split == 0) {
    level--;
    split = map->config.initial_buckets << level;
  }
  split--;
  last = lh_bucket(map, lh_count(map) - 1);
  target = lh_bucket(map, split);
  status = ds_vec_append(target, last->data, last->size);
  if (status != DS_OK) {
    return status;
  }
  ds_vec_destroy(last);
  map->buckets.size--;
  map->level = level;
  map->split = split;
  return DS_OK;
}

void ds_linear_hash_map_config_init(ds_linear_hash_map_config_t *config) {
  config->key_size = 0;
  config->value_size = 0;
  config->hash = NULL;
  config->equal = NULL;
  config->user = NULL;
  config->split = DS_LINEAR_HASH_LOAD;
  config->initial_buckets = 4;
  config->bucket_capacity = 4;
  config->max_load = 0.8;
  config->min_load = 0.3;
  config->context = NULL;
}

ds_status_t ds_linear_hash_map_init(ds_linear_hash_map_t *map,
                                    const ds_linear_hash_map_config_t *config) {
  size_t i;
  ds_vec_t b;
  ds_status_t status;
  if (map == NULL || config == NULL || config->hash == NULL ||
      config->equal == NULL) {
    return DS_ERR_NULL;
  }
  if (config->key_size == 0 || config->initial_buckets == 0 ||
      config->bucket_capacity == 0 || !(config->max_load > 0) ||
      config->min_load < 0 || !(config->min_load < config->max_load / 2) ||
      (config->split != DS_LINEAR_HASH_LOAD &&
       config->split != DS_LINEAR_HASH_OVERFLOW)) {
    return DS_ERR_INVALID;
  }
  map->config = *config;
  map->level = 0;
  map->split = 0;
  map->size = 0;
  map->key_offset = lh_round(sizeof(unsigned long));
  map->value_offset = lh_round(map->key_offset + config->key_size);
  map->record_size = lh_round(map->value_offset + config->value_size);
  status = ds_vec_init(&map->buckets, sizeof(ds_vec_t), config->context);
  if (status != DS_OK) {
    return status;
  }
  for (i = 0; i < config->initial_buckets; i++) {
    status = ds_vec_init(&b, map->record_size, config->context);
    if (status == DS_OK) {
      status = ds_vec_push(&map->buckets, &b);
    }
    if (status != DS_OK) {
      ds_linear_hash_map_destroy(map);
      return status;
    }
  }
  return DS_OK;
}

void ds_linear_hash_map_destroy(ds_linear_hash_map_t *map) {
  size_t i;
  if (map == NULL) {
    return;
  }
  for (i = 0; i < lh_count(map); i++) {
    ds_vec_destroy(lh_bucket(map, i));
  }
  ds_vec_destroy(&map->buckets);
  map->size = 0;
}

static void *lh_emplace(ds_linear_hash_map_t *map, const void *key,
                        bool *out_inserted, ds_status_t *out_status) {
  unsigned long h = lh_mix(map->config.hash(key, map->config.user));
  size_t b;
  size_t i;
  unsigned char *rec;
  ds_vec_t *bucket;
  *out_inserted = false;
  *out_status = DS_OK;
  if (lh_find(map, key, h, &b, &i)) {
    return lh_rec_value(map, lh_rec(lh_bucket(map, b), i));
  }
  b = lh_address(map, h);
  bucket = lh_bucket(map, b);
  rec = (unsigned char *)ds_vec_emplace(bucket);
  if (rec == NULL) {
    *out_status = DS_ERR_ALLOC;
    return NULL;
  }
  memcpy(rec, &h, sizeof h);
  memcpy(rec + map->key_offset, key, map->config.key_size);
  map->size++;
  *out_inserted = true;
  if (map->config.split == DS_LINEAR_HASH_OVERFLOW) {
    if (bucket->size > map->config.bucket_capacity) {
      (void)lh_split(map);
    }
  } else {
    while (lh_load(map) > map->config.max_load) {
      if (lh_split(map) != DS_OK) {
        break; /* growth is best effort; the entry is stored */
      }
    }
  }
  (void)lh_find(map, key, h, &b, &i);
  return lh_rec_value(map, lh_rec(lh_bucket(map, b), i));
}

void *ds_linear_hash_map_emplace(ds_linear_hash_map_t *map, const void *key,
                                 bool *out_inserted) {
  bool inserted;
  ds_status_t status;
  void *slot = lh_emplace(map, key, &inserted, &status);
  if (out_inserted != NULL) {
    *out_inserted = inserted;
  }
  return slot;
}

ds_status_t ds_linear_hash_map_put(ds_linear_hash_map_t *map, const void *key,
                                   const void *value) {
  bool inserted;
  ds_status_t status;
  void *slot;
  if (key == NULL || (value == NULL && map->config.value_size > 0)) {
    return DS_ERR_NULL;
  }
  slot = lh_emplace(map, key, &inserted, &status);
  if (slot == NULL) {
    return status;
  }
  if (map->config.value_size > 0) {
    memcpy(slot, value, map->config.value_size);
  }
  return DS_OK;
}

ds_status_t ds_linear_hash_map_insert(ds_linear_hash_map_t *map,
                                      const void *key, const void *value) {
  bool inserted;
  ds_status_t status;
  void *slot;
  if (key == NULL || (value == NULL && map->config.value_size > 0)) {
    return DS_ERR_NULL;
  }
  slot = lh_emplace(map, key, &inserted, &status);
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

void *ds_linear_hash_map_get(const ds_linear_hash_map_t *map, const void *key) {
  size_t b;
  size_t i;
  if (map->size == 0 ||
      !lh_find(map, key, lh_mix(map->config.hash(key, map->config.user)), &b,
               &i)) {
    return NULL;
  }
  return lh_rec_value(map, lh_rec(lh_bucket(map, b), i));
}

bool ds_linear_hash_map_contains(const ds_linear_hash_map_t *map,
                                 const void *key) {
  return ds_linear_hash_map_get(map, key) != NULL;
}

ds_status_t ds_linear_hash_map_remove(ds_linear_hash_map_t *map,
                                      const void *key, void *out_value) {
  size_t b;
  size_t i;
  ds_vec_t *bucket;
  if (map->size == 0 ||
      !lh_find(map, key, lh_mix(map->config.hash(key, map->config.user)), &b,
               &i)) {
    return DS_NOT_FOUND;
  }
  bucket = lh_bucket(map, b);
  if (out_value != NULL && map->config.value_size > 0) {
    memcpy(out_value, lh_rec(bucket, i) + map->value_offset,
           map->config.value_size);
  }
  (void)ds_vec_swap_remove(bucket, i, NULL);
  map->size--;
  if (map->config.min_load > 0) {
    while (lh_count(map) > map->config.initial_buckets &&
           lh_load(map) < map->config.min_load) {
      if (lh_merge(map) != DS_OK) {
        break;
      }
    }
  }
  return DS_OK;
}

void ds_linear_hash_map_clear(ds_linear_hash_map_t *map) {
  while (lh_count(map) > map->config.initial_buckets) {
    ds_vec_destroy(lh_bucket(map, lh_count(map) - 1));
    map->buckets.size--;
  }
  {
    size_t i;
    for (i = 0; i < lh_count(map); i++) {
      ds_vec_clear(lh_bucket(map, i));
    }
  }
  map->level = 0;
  map->split = 0;
  map->size = 0;
}

size_t ds_linear_hash_map_size(const ds_linear_hash_map_t *map) {
  return map->size;
}

size_t ds_linear_hash_map_bucket_count(const ds_linear_hash_map_t *map) {
  return lh_count(map);
}

bool ds_linear_hash_map_next(const ds_linear_hash_map_t *map,
                             ds_linear_hash_map_cursor_t *cursor,
                             const void **out_key, void **out_value) {
  ds_vec_t *b;
  unsigned char *rec;
  while (cursor->bucket < lh_count(map)) {
    b = lh_bucket(map, cursor->bucket);
    if (cursor->index < b->size) {
      rec = lh_rec(b, cursor->index);
      if (out_key != NULL) {
        *out_key = rec + map->key_offset;
      }
      if (out_value != NULL) {
        *out_value =
            map->config.value_size > 0 ? rec + map->value_offset : NULL;
      }
      cursor->index++;
      return true;
    }
    cursor->bucket++;
    cursor->index = 0;
  }
  return false;
}
