#include "lru_cache.h"
#include <string.h>

#define LRU_NONE (~(size_t)0)

static unsigned char *lru_key(const ds_lru_cache_t *c, size_t n) {
  return c->keys + n * c->key_size;
}

static unsigned char *lru_value(const ds_lru_cache_t *c, size_t n) {
  return c->values + n * c->value_size;
}

static void lru_unlink(ds_lru_cache_t *c, size_t n) {
  if (c->prev[n] != LRU_NONE) {
    c->next[c->prev[n]] = c->next[n];
  } else {
    c->head = c->next[n];
  }
  if (c->next[n] != LRU_NONE) {
    c->prev[c->next[n]] = c->prev[n];
  } else {
    c->tail = c->prev[n];
  }
}

static void lru_push_front(ds_lru_cache_t *c, size_t n) {
  c->prev[n] = LRU_NONE;
  c->next[n] = c->head;
  if (c->head != LRU_NONE) {
    c->prev[c->head] = n;
  } else {
    c->tail = n;
  }
  c->head = n;
}

static size_t lru_node_of(const ds_lru_cache_t *c, const void *key) {
  const size_t *slot = (const size_t *)ds_rh_map_get(&c->index, key);
  return slot == NULL ? LRU_NONE : *slot;
}

static void lru_reset_free_list(ds_lru_cache_t *c) {
  size_t i;
  for (i = 0; i < c->capacity; i++) {
    c->next[i] = i + 1 < c->capacity ? i + 1 : LRU_NONE;
  }
  c->free_head = c->capacity > 0 ? 0 : LRU_NONE;
  c->head = LRU_NONE;
  c->tail = LRU_NONE;
  c->size = 0;
}

ds_status_t ds_lru_cache_init(ds_lru_cache_t *cache,
                              const ds_rh_map_config_t *config,
                              size_t capacity) {
  ds_rh_map_config_t map_config;
  ds_status_t status;
  size_t limit = ~(size_t)0;
  if (cache == NULL || config == NULL) {
    return DS_ERR_NULL;
  }
  if (capacity == 0 || config->key_size == 0) {
    return DS_ERR_INVALID;
  }
  if (capacity > limit / config->key_size ||
      (config->value_size > 0 && capacity > limit / config->value_size) ||
      capacity > limit / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  map_config = *config;
  map_config.value_size = sizeof(size_t);
  status = ds_rh_map_init(&cache->index, &map_config, capacity);
  if (status != DS_OK) {
    return status;
  }
  cache->context =
      config->context != NULL ? config->context : ds_default_context();
  cache->capacity = capacity;
  cache->key_size = config->key_size;
  cache->value_size = config->value_size;
  cache->keys = (unsigned char *)ds_context_alloc(cache->context,
                                                  capacity * config->key_size);
  cache->values = (unsigned char *)ds_context_alloc(
      cache->context,
      config->value_size > 0 ? capacity * config->value_size : 1);
  cache->prev =
      (size_t *)ds_context_alloc(cache->context, capacity * sizeof(size_t));
  cache->next =
      (size_t *)ds_context_alloc(cache->context, capacity * sizeof(size_t));
  if (cache->keys == NULL || cache->values == NULL || cache->prev == NULL ||
      cache->next == NULL) {
    ds_lru_cache_destroy(cache);
    return DS_ERR_ALLOC;
  }
  lru_reset_free_list(cache);
  return DS_OK;
}

void ds_lru_cache_destroy(ds_lru_cache_t *cache) {
  if (cache == NULL) {
    return;
  }
  ds_rh_map_destroy(&cache->index);
  ds_context_free(cache->context, cache->keys);
  ds_context_free(cache->context, cache->values);
  ds_context_free(cache->context, cache->prev);
  ds_context_free(cache->context, cache->next);
  cache->keys = NULL;
  cache->values = NULL;
  cache->prev = NULL;
  cache->next = NULL;
  cache->size = 0;
  cache->capacity = 0;
}

void *ds_lru_cache_get(ds_lru_cache_t *cache, const void *key) {
  size_t n = lru_node_of(cache, key);
  if (n == LRU_NONE) {
    return NULL;
  }
  if (cache->head != n) {
    lru_unlink(cache, n);
    lru_push_front(cache, n);
  }
  return cache->value_size > 0 ? lru_value(cache, n) : NULL;
}

void *ds_lru_cache_peek(const ds_lru_cache_t *cache, const void *key) {
  size_t n = lru_node_of(cache, key);
  if (n == LRU_NONE || cache->value_size == 0) {
    return NULL;
  }
  return lru_value(cache, n);
}

ds_status_t ds_lru_cache_put(ds_lru_cache_t *cache, const void *key,
                             const void *value, void *evicted_key,
                             void *evicted_value, bool *out_evicted) {
  size_t n;
  ds_status_t status;
  if (out_evicted != NULL) {
    *out_evicted = false;
  }
  if (key == NULL || (value == NULL && cache->value_size > 0)) {
    return DS_ERR_NULL;
  }
  n = lru_node_of(cache, key);
  if (n != LRU_NONE) {
    if (cache->value_size > 0) {
      memmove(lru_value(cache, n), value, cache->value_size);
    }
    if (cache->head != n) {
      lru_unlink(cache, n);
      lru_push_front(cache, n);
    }
    return DS_EXISTS;
  }
  if (cache->free_head == LRU_NONE) {
    /* Full: recycle the least recently used node. */
    n = cache->tail;
    if (evicted_key != NULL) {
      memcpy(evicted_key, lru_key(cache, n), cache->key_size);
    }
    if (evicted_value != NULL && cache->value_size > 0) {
      memcpy(evicted_value, lru_value(cache, n), cache->value_size);
    }
    if (out_evicted != NULL) {
      *out_evicted = true;
    }
    ds_rh_map_remove(&cache->index, lru_key(cache, n), NULL);
    lru_unlink(cache, n);
    cache->size--;
  } else {
    n = cache->free_head;
    cache->free_head = cache->next[n];
  }
  memcpy(lru_key(cache, n), key, cache->key_size);
  if (cache->value_size > 0) {
    memcpy(lru_value(cache, n), value, cache->value_size);
  }
  status = ds_rh_map_put(&cache->index, lru_key(cache, n), &n);
  if (status != DS_OK) {
    /* Cannot happen (the map was sized for capacity), but stay consistent. */
    cache->next[n] = cache->free_head;
    cache->free_head = n;
    return status;
  }
  lru_push_front(cache, n);
  cache->size++;
  return DS_OK;
}

ds_status_t ds_lru_cache_remove(ds_lru_cache_t *cache, const void *key,
                                void *out_value) {
  size_t n = lru_node_of(cache, key);
  if (n == LRU_NONE) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && cache->value_size > 0) {
    memcpy(out_value, lru_value(cache, n), cache->value_size);
  }
  ds_rh_map_remove(&cache->index, lru_key(cache, n), NULL);
  lru_unlink(cache, n);
  cache->next[n] = cache->free_head;
  cache->free_head = n;
  cache->size--;
  return DS_OK;
}

const void *ds_lru_cache_oldest_key(const ds_lru_cache_t *cache) {
  return cache->tail == LRU_NONE ? NULL : lru_key(cache, cache->tail);
}

void ds_lru_cache_clear(ds_lru_cache_t *cache) {
  ds_rh_map_clear(&cache->index);
  lru_reset_free_list(cache);
}

size_t ds_lru_cache_size(const ds_lru_cache_t *cache) { return cache->size; }

size_t ds_lru_cache_capacity(const ds_lru_cache_t *cache) {
  return cache->capacity;
}
