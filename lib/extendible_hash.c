#include "extendible_hash.h"
#include <limits.h>
#include <string.h>

#define EH_MAX_DEPTH ((unsigned int)(CHAR_BIT * sizeof(unsigned long) - 1))

typedef union eh_max {
  long l;
  double d;
  void *p;
  long double ld;
} eh_max_t;
struct eh_align_probe {
  char c;
  eh_max_t u;
};
#define EH_ALIGN (offsetof(struct eh_align_probe, u))

typedef struct ds_extendible_hash_bucket {
  unsigned int depth;
  size_t count;
} eh_bucket_t;

static unsigned long eh_mix(unsigned long h) {
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

static size_t eh_round(size_t n) {
  return (n + EH_ALIGN - 1) / EH_ALIGN * EH_ALIGN;
}

static unsigned char *eh_rec(const ds_extendible_hash_t *map, eh_bucket_t *b,
                             size_t i) {
  return (unsigned char *)(void *)b + eh_round(sizeof(eh_bucket_t)) +
         i * map->record_size;
}

static unsigned long eh_rec_hash(const unsigned char *rec) {
  unsigned long h;
  memcpy(&h, rec, sizeof h);
  return h;
}

static void *eh_rec_value(const ds_extendible_hash_t *map, unsigned char *rec) {
  return map->config.value_size == 0 ? rec + map->key_offset
                                     : rec + map->value_offset;
}

static size_t eh_dir_size(const ds_extendible_hash_t *map) {
  return (size_t)1 << map->global_depth;
}

static size_t eh_index(const ds_extendible_hash_t *map, unsigned long h) {
  return (size_t)(h & (((unsigned long)1 << map->global_depth) - 1UL));
}

static eh_bucket_t *eh_bucket_new(const ds_extendible_hash_t *map,
                                  unsigned int depth) {
  eh_bucket_t *b = (eh_bucket_t *)ds_context_alloc(
      map->config.context, eh_round(sizeof(eh_bucket_t)) +
                               map->config.bucket_capacity * map->record_size);
  if (b != NULL) {
    b->depth = depth;
    b->count = 0;
  }
  return b;
}

static bool eh_find(const ds_extendible_hash_t *map, const void *key,
                    unsigned long h, eh_bucket_t **out_bucket,
                    size_t *out_index) {
  eh_bucket_t *b = map->directory[eh_index(map, h)];
  size_t i;
  unsigned char *rec;
  for (i = 0; i < b->count; i++) {
    rec = eh_rec(map, b, i);
    if (eh_rec_hash(rec) == h &&
        map->config.equal(rec + map->key_offset, key, map->config.user)) {
      *out_bucket = b;
      *out_index = i;
      return true;
    }
  }
  return false;
}

static ds_status_t eh_double(ds_extendible_hash_t *map) {
  size_t n = eh_dir_size(map);
  eh_bucket_t **dir;
  if (map->global_depth >= EH_MAX_DEPTH ||
      n > ((size_t)-1) / 2 / sizeof(eh_bucket_t *)) {
    return DS_ERR_OVERFLOW;
  }
  dir = (eh_bucket_t **)ds_context_realloc(map->config.context, map->directory,
                                           2 * n * sizeof(eh_bucket_t *));
  if (dir == NULL) {
    return DS_ERR_ALLOC;
  }
  memcpy(dir + n, dir, n * sizeof(eh_bucket_t *));
  map->directory = dir;
  map->global_depth++;
  map->full_depth = 0;
  return DS_OK;
}

/* Split the bucket at directory index idx on its next hash bit. */
static ds_status_t eh_split(ds_extendible_hash_t *map, size_t idx) {
  eh_bucket_t *old = map->directory[idx];
  eh_bucket_t *fresh;
  unsigned int d = old->depth;
  unsigned long bit;
  size_t i;
  size_t keep = 0;
  size_t n;
  size_t pattern;
  ds_status_t status;
  if (d >= EH_MAX_DEPTH) {
    return DS_ERR_OVERFLOW;
  }
  if (d == map->global_depth) {
    status = eh_double(map);
    if (status != DS_OK) {
      return status;
    }
  }
  fresh = eh_bucket_new(map, d + 1);
  if (fresh == NULL) {
    return DS_ERR_ALLOC;
  }
  bit = (unsigned long)1 << d;
  for (i = 0; i < old->count; i++) {
    if (eh_rec_hash(eh_rec(map, old, i)) & bit) {
      memcpy(eh_rec(map, fresh, fresh->count++), eh_rec(map, old, i),
             map->record_size);
    } else {
      if (keep != i) {
        memcpy(eh_rec(map, old, keep), eh_rec(map, old, i), map->record_size);
      }
      keep++;
    }
  }
  old->count = keep;
  old->depth = d + 1;
  if (d + 1 == map->global_depth) {
    map->full_depth += 2;
  }
  n = eh_dir_size(map);
  pattern = idx & (((size_t)1 << d) - 1);
  for (i = pattern; i < n; i += (size_t)1 << d) {
    if (i & ((size_t)1 << d)) {
      map->directory[i] = fresh;
    }
  }
  map->bucket_count++;
  return DS_OK;
}

static size_t eh_count_full_depth(const ds_extendible_hash_t *map) {
  size_t i;
  size_t n = eh_dir_size(map);
  size_t count = 0;
  for (i = 0; i < n; i++) {
    if (map->directory[i]->depth == map->global_depth) {
      count++; /* such a bucket has exactly one directory entry */
    }
  }
  return count;
}

static void eh_try_merge(ds_extendible_hash_t *map, size_t idx) {
  eh_bucket_t *b;
  eh_bucket_t *buddy;
  eh_bucket_t *keep;
  eh_bucket_t *gone;
  size_t bidx;
  size_t i;
  size_t n = eh_dir_size(map);
  size_t step;
  unsigned int d;
  bool merged = false;
  for (;;) {
    b = map->directory[idx];
    d = b->depth;
    if (d == 0) {
      break;
    }
    bidx = idx ^ ((size_t)1 << (d - 1));
    buddy = map->directory[bidx];
    if (buddy->depth != d ||
        b->count + buddy->count > map->config.bucket_capacity) {
      break;
    }
    keep = (idx & ((size_t)1 << (d - 1))) ? buddy : b;
    gone = keep == b ? buddy : b;
    memcpy(eh_rec(map, keep, keep->count), eh_rec(map, gone, 0),
           gone->count * map->record_size);
    keep->count += gone->count;
    keep->depth = d - 1;
    if (d == map->global_depth) {
      map->full_depth -= 2;
    }
    /* Only the entries congruent to the merged pattern can point here. */
    step = (size_t)1 << (d - 1);
    for (i = idx & (step - 1); i < n; i += step) {
      map->directory[i] = keep;
    }
    ds_context_free(map->config.context, gone);
    map->bucket_count--;
    idx &= step - 1;
    merged = true;
  }
  /* Halve the directory while no bucket needs the top bit. */
  while (merged && map->global_depth > 0 && map->full_depth == 0) {
    map->global_depth--;
    map->full_depth = eh_count_full_depth(map);
  }
}

void ds_extendible_hash_config_init(ds_extendible_hash_config_t *config) {
  config->key_size = 0;
  config->value_size = 0;
  config->hash = NULL;
  config->equal = NULL;
  config->user = NULL;
  config->bucket_capacity = 8;
  config->context = NULL;
}

ds_status_t ds_extendible_hash_init(ds_extendible_hash_t *map,
                                    const ds_extendible_hash_config_t *config) {
  if (map == NULL || config == NULL || config->hash == NULL ||
      config->equal == NULL) {
    return DS_ERR_NULL;
  }
  if (config->key_size == 0 || config->bucket_capacity == 0) {
    return DS_ERR_INVALID;
  }
  map->config = *config;
  map->key_offset = eh_round(sizeof(unsigned long));
  map->value_offset = eh_round(map->key_offset + config->key_size);
  map->record_size = eh_round(map->value_offset + config->value_size);
  map->global_depth = 0;
  map->full_depth = 1;
  map->size = 0;
  map->bucket_count = 1;
  map->directory =
      (eh_bucket_t **)ds_context_alloc(config->context, sizeof(eh_bucket_t *));
  if (map->directory == NULL) {
    return DS_ERR_ALLOC;
  }
  map->directory[0] = eh_bucket_new(map, 0);
  if (map->directory[0] == NULL) {
    ds_context_free(config->context, map->directory);
    map->directory = NULL;
    return DS_ERR_ALLOC;
  }
  return DS_OK;
}

void ds_extendible_hash_destroy(ds_extendible_hash_t *map) {
  size_t i;
  size_t n;
  eh_bucket_t *b;
  if (map == NULL || map->directory == NULL) {
    return;
  }
  n = eh_dir_size(map);
  /* A bucket's canonical entry is its lowest index; walking downwards
   * frees it only after every other entry that points to it was seen. */
  for (i = n; i-- > 0;) {
    b = map->directory[i];
    if (i < ((size_t)1 << b->depth)) {
      ds_context_free(map->config.context, b);
    }
  }
  ds_context_free(map->config.context, map->directory);
  map->directory = NULL;
  map->size = 0;
}

static void *eh_emplace(ds_extendible_hash_t *map, const void *key,
                        bool *out_inserted, ds_status_t *out_status) {
  unsigned long h = eh_mix(map->config.hash(key, map->config.user));
  eh_bucket_t *b;
  size_t i;
  unsigned char *rec;
  ds_status_t status;
  *out_inserted = false;
  *out_status = DS_OK;
  if (eh_find(map, key, h, &b, &i)) {
    return eh_rec_value(map, eh_rec(map, b, i));
  }
  for (;;) {
    b = map->directory[eh_index(map, h)];
    if (b->count < map->config.bucket_capacity) {
      break;
    }
    for (i = 0; i < b->count && eh_rec_hash(eh_rec(map, b, i)) == h; i++) {
    }
    if (i == b->count) {
      *out_status = DS_ERR_OVERFLOW; /* identical hashes cannot be split */
      return NULL;
    }
    status = eh_split(map, eh_index(map, h));
    if (status != DS_OK) {
      *out_status = status;
      return NULL;
    }
  }
  rec = eh_rec(map, b, b->count++);
  memcpy(rec, &h, sizeof h);
  memcpy(rec + map->key_offset, key, map->config.key_size);
  map->size++;
  *out_inserted = true;
  return eh_rec_value(map, rec);
}

void *ds_extendible_hash_emplace(ds_extendible_hash_t *map, const void *key,
                                 bool *out_inserted) {
  bool inserted;
  ds_status_t status;
  void *slot = eh_emplace(map, key, &inserted, &status);
  if (out_inserted != NULL) {
    *out_inserted = inserted;
  }
  return slot;
}

ds_status_t ds_extendible_hash_put(ds_extendible_hash_t *map, const void *key,
                                   const void *value) {
  bool inserted;
  ds_status_t status;
  void *slot;
  if (key == NULL || (value == NULL && map->config.value_size > 0)) {
    return DS_ERR_NULL;
  }
  slot = eh_emplace(map, key, &inserted, &status);
  if (slot == NULL) {
    return status;
  }
  if (map->config.value_size > 0) {
    memcpy(slot, value, map->config.value_size);
  }
  return DS_OK;
}

ds_status_t ds_extendible_hash_insert(ds_extendible_hash_t *map,
                                      const void *key, const void *value) {
  bool inserted;
  ds_status_t status;
  void *slot;
  if (key == NULL || (value == NULL && map->config.value_size > 0)) {
    return DS_ERR_NULL;
  }
  slot = eh_emplace(map, key, &inserted, &status);
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

void *ds_extendible_hash_get(const ds_extendible_hash_t *map, const void *key) {
  eh_bucket_t *b;
  size_t i;
  if (map->size == 0 ||
      !eh_find(map, key, eh_mix(map->config.hash(key, map->config.user)), &b,
               &i)) {
    return NULL;
  }
  return eh_rec_value(map, eh_rec(map, b, i));
}

bool ds_extendible_hash_contains(const ds_extendible_hash_t *map,
                                 const void *key) {
  return ds_extendible_hash_get(map, key) != NULL;
}

ds_status_t ds_extendible_hash_remove(ds_extendible_hash_t *map,
                                      const void *key, void *out_value) {
  unsigned long h;
  eh_bucket_t *b;
  size_t i;
  if (map->size == 0) {
    return DS_NOT_FOUND;
  }
  h = eh_mix(map->config.hash(key, map->config.user));
  if (!eh_find(map, key, h, &b, &i)) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && map->config.value_size > 0) {
    memcpy(out_value, eh_rec(map, b, i) + map->value_offset,
           map->config.value_size);
  }
  b->count--;
  if (i != b->count) {
    memcpy(eh_rec(map, b, i), eh_rec(map, b, b->count), map->record_size);
  }
  map->size--;
  eh_try_merge(map, eh_index(map, h));
  return DS_OK;
}

void ds_extendible_hash_clear(ds_extendible_hash_t *map) {
  ds_extendible_hash_config_t config = map->config;
  eh_bucket_t *b;
  size_t i;
  size_t n = eh_dir_size(map);
  /* Keep bucket 0's memory as the single bucket of the empty map. */
  for (i = n; i-- > 1;) {
    b = map->directory[i];
    if (i < ((size_t)1 << b->depth)) {
      ds_context_free(config.context, b);
    }
  }
  map->directory[0]->depth = 0;
  map->directory[0]->count = 0;
  map->global_depth = 0;
  map->full_depth = 1;
  map->bucket_count = 1;
  map->size = 0;
}

size_t ds_extendible_hash_size(const ds_extendible_hash_t *map) {
  return map->size;
}

unsigned int ds_extendible_hash_global_depth(const ds_extendible_hash_t *map) {
  return map->global_depth;
}

size_t ds_extendible_hash_bucket_count(const ds_extendible_hash_t *map) {
  return map->bucket_count;
}

bool ds_extendible_hash_next(const ds_extendible_hash_t *map,
                             ds_extendible_hash_cursor_t *cursor,
                             const void **out_key, void **out_value) {
  eh_bucket_t *b;
  unsigned char *rec;
  size_t n = eh_dir_size(map);
  while (cursor->entry < n) {
    b = map->directory[cursor->entry];
    if (cursor->entry < ((size_t)1 << b->depth) && cursor->index < b->count) {
      rec = eh_rec(map, b, cursor->index);
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
    cursor->entry++;
    cursor->index = 0;
  }
  return false;
}
