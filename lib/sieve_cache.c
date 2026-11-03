#include "sieve_cache.h"
#include <string.h>

#define SV_NONE (~(size_t)0)

static unsigned char *sv_key(const ds_sieve_cache_t *c, size_t n) {
  return c->keys + n * c->key_size;
}

static unsigned char *sv_value(const ds_sieve_cache_t *c, size_t n) {
  return c->values + n * c->value_size;
}

static size_t sv_node_of(const ds_sieve_cache_t *c, const void *key) {
  const size_t *slot = (const size_t *)ds_rh_map_get(&c->index, key);
  return slot == NULL ? SV_NONE : *slot;
}

static void sv_unlink(ds_sieve_cache_t *c, size_t n) {
  if (c->prev[n] != SV_NONE) {
    c->next[c->prev[n]] = c->next[n];
  } else {
    c->head = c->next[n];
  }
  if (c->next[n] != SV_NONE) {
    c->prev[c->next[n]] = c->prev[n];
  } else {
    c->tail = c->prev[n];
  }
}

static void sv_push_head(ds_sieve_cache_t *c, size_t n) {
  c->prev[n] = SV_NONE;
  c->next[n] = c->head;
  if (c->head != SV_NONE) {
    c->prev[c->head] = n;
  } else {
    c->tail = n;
  }
  c->head = n;
}

/* Detach node n from the queue and the index, keeping the hand valid. */
static void sv_drop(ds_sieve_cache_t *c, size_t n) {
  if (c->hand == n) {
    c->hand = c->prev[n];
  }
  ds_rh_map_remove(&c->index, sv_key(c, n), NULL);
  sv_unlink(c, n);
  c->size--;
}

static size_t sv_pick_victim(ds_sieve_cache_t *c) {
  size_t n = c->hand != SV_NONE ? c->hand : c->tail;
  while (c->visited[n]) {
    c->visited[n] = 0;
    n = c->prev[n] != SV_NONE ? c->prev[n] : c->tail;
  }
  return n;
}

static void sv_reset(ds_sieve_cache_t *c) {
  size_t i;
  for (i = 0; i < c->capacity; i++) {
    c->next[i] = i + 1 < c->capacity ? i + 1 : SV_NONE;
  }
  c->free_head = 0;
  c->head = SV_NONE;
  c->tail = SV_NONE;
  c->hand = SV_NONE;
  c->size = 0;
}

ds_status_t ds_sieve_cache_init(ds_sieve_cache_t *cache,
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
  cache->visited =
      (unsigned char *)ds_context_calloc(cache->context, capacity, 1);
  cache->prev =
      (size_t *)ds_context_alloc(cache->context, capacity * sizeof(size_t));
  cache->next =
      (size_t *)ds_context_alloc(cache->context, capacity * sizeof(size_t));
  if (cache->keys == NULL || cache->values == NULL || cache->visited == NULL ||
      cache->prev == NULL || cache->next == NULL) {
    ds_sieve_cache_destroy(cache);
    return DS_ERR_ALLOC;
  }
  sv_reset(cache);
  return DS_OK;
}

void ds_sieve_cache_destroy(ds_sieve_cache_t *cache) {
  if (cache == NULL) {
    return;
  }
  ds_rh_map_destroy(&cache->index);
  ds_context_free(cache->context, cache->keys);
  ds_context_free(cache->context, cache->values);
  ds_context_free(cache->context, cache->visited);
  ds_context_free(cache->context, cache->prev);
  ds_context_free(cache->context, cache->next);
  cache->keys = NULL;
  cache->values = NULL;
  cache->visited = NULL;
  cache->prev = NULL;
  cache->next = NULL;
  cache->size = 0;
  cache->capacity = 0;
}

void *ds_sieve_cache_get(ds_sieve_cache_t *cache, const void *key) {
  size_t n = sv_node_of(cache, key);
  if (n == SV_NONE) {
    return NULL;
  }
  cache->visited[n] = 1;
  return cache->value_size > 0 ? sv_value(cache, n) : NULL;
}

void *ds_sieve_cache_peek(const ds_sieve_cache_t *cache, const void *key) {
  size_t n = sv_node_of(cache, key);
  if (n == SV_NONE || cache->value_size == 0) {
    return NULL;
  }
  return sv_value(cache, n);
}

ds_status_t ds_sieve_cache_put(ds_sieve_cache_t *cache, const void *key,
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
  n = sv_node_of(cache, key);
  if (n != SV_NONE) {
    if (cache->value_size > 0) {
      memmove(sv_value(cache, n), value, cache->value_size);
    }
    cache->visited[n] = 1;
    return DS_EXISTS;
  }
  if (cache->free_head == SV_NONE) {
    n = sv_pick_victim(cache);
    if (evicted_key != NULL) {
      memcpy(evicted_key, sv_key(cache, n), cache->key_size);
    }
    if (evicted_value != NULL && cache->value_size > 0) {
      memcpy(evicted_value, sv_value(cache, n), cache->value_size);
    }
    if (out_evicted != NULL) {
      *out_evicted = true;
    }
    cache->hand = n; /* sv_drop moves it to the next newer entry */
    sv_drop(cache, n);
  } else {
    n = cache->free_head;
    cache->free_head = cache->next[n];
  }
  memcpy(sv_key(cache, n), key, cache->key_size);
  if (cache->value_size > 0) {
    memcpy(sv_value(cache, n), value, cache->value_size);
  }
  cache->visited[n] = 0;
  status = ds_rh_map_put(&cache->index, sv_key(cache, n), &n);
  if (status != DS_OK) {
    cache->next[n] = cache->free_head;
    cache->free_head = n;
    return status;
  }
  sv_push_head(cache, n);
  cache->size++;
  return DS_OK;
}

ds_status_t ds_sieve_cache_remove(ds_sieve_cache_t *cache, const void *key,
                                  void *out_value) {
  size_t n = sv_node_of(cache, key);
  if (n == SV_NONE) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && cache->value_size > 0) {
    memcpy(out_value, sv_value(cache, n), cache->value_size);
  }
  sv_drop(cache, n);
  cache->next[n] = cache->free_head;
  cache->free_head = n;
  return DS_OK;
}

void ds_sieve_cache_clear(ds_sieve_cache_t *cache) {
  ds_rh_map_clear(&cache->index);
  memset(cache->visited, 0, cache->capacity);
  sv_reset(cache);
}

size_t ds_sieve_cache_size(const ds_sieve_cache_t *cache) {
  return cache->size;
}

size_t ds_sieve_cache_capacity(const ds_sieve_cache_t *cache) {
  return cache->capacity;
}
