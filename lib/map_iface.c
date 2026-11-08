#include "map_iface.h"
#include "rh_map.h"
#include "swiss_map.h"
#include <string.h>

/* ---- Robin Hood backend ---- */

static ds_status_t mi_rh_init(void *map, size_t key_size, size_t value_size,
                              ds_map_hash_fn hash, ds_map_equal_fn equal,
                              void *user, ds_context_t *context) {
  ds_rh_map_config_t c;
  ds_rh_map_config_init(&c);
  c.key_size = key_size;
  c.value_size = value_size;
  c.hash = hash;
  c.equal = equal;
  c.user = user;
  c.context = context;
  return ds_rh_map_init((ds_rh_map_t *)map, &c, 0);
}

static void mi_rh_destroy(void *map) { ds_rh_map_destroy((ds_rh_map_t *)map); }

static ds_status_t mi_rh_put(void *map, const void *key, const void *value) {
  return ds_rh_map_put((ds_rh_map_t *)map, key, value);
}

static void *mi_rh_get(const void *map, const void *key) {
  return ds_rh_map_get((const ds_rh_map_t *)map, key);
}

static ds_status_t mi_rh_remove(void *map, const void *key, void *out) {
  return ds_rh_map_remove((ds_rh_map_t *)map, key, out);
}

static size_t mi_rh_size(const void *map) {
  return ds_rh_map_size((const ds_rh_map_t *)map);
}

static bool mi_rh_next(const void *map, size_t *cursor, const void **key,
                       void **value) {
  return ds_rh_map_next((const ds_rh_map_t *)map, cursor, key, value);
}

static const ds_map_ops_t mi_rh_ops = {
    "robin hood",  DS_MAP_EXPECTED_O1, sizeof(ds_rh_map_t), mi_rh_init,
    mi_rh_destroy, mi_rh_put,          mi_rh_get,           mi_rh_remove,
    mi_rh_size,    mi_rh_next};

/* ---- Swiss table backend ---- */

static ds_status_t mi_sw_init(void *map, size_t key_size, size_t value_size,
                              ds_map_hash_fn hash, ds_map_equal_fn equal,
                              void *user, ds_context_t *context) {
  ds_swiss_map_config_t c;
  ds_swiss_map_config_init(&c);
  c.key_size = key_size;
  c.value_size = value_size;
  c.hash = hash;
  c.equal = equal;
  c.user = user;
  c.context = context;
  return ds_swiss_map_init((ds_swiss_map_t *)map, &c, 0);
}

static void mi_sw_destroy(void *map) {
  ds_swiss_map_destroy((ds_swiss_map_t *)map);
}

static ds_status_t mi_sw_put(void *map, const void *key, const void *value) {
  return ds_swiss_map_put((ds_swiss_map_t *)map, key, value);
}

static void *mi_sw_get(const void *map, const void *key) {
  return ds_swiss_map_get((const ds_swiss_map_t *)map, key);
}

static ds_status_t mi_sw_remove(void *map, const void *key, void *out) {
  return ds_swiss_map_remove((ds_swiss_map_t *)map, key, out);
}

static size_t mi_sw_size(const void *map) {
  return ds_swiss_map_size((const ds_swiss_map_t *)map);
}

static bool mi_sw_next(const void *map, size_t *cursor, const void **key,
                       void **value) {
  return ds_swiss_map_next((const ds_swiss_map_t *)map, cursor, key, value);
}

static const ds_map_ops_t mi_sw_ops = {
    "swiss table", DS_MAP_EXPECTED_O1, sizeof(ds_swiss_map_t),
    mi_sw_init,    mi_sw_destroy,      mi_sw_put,
    mi_sw_get,     mi_sw_remove,       mi_sw_size,
    mi_sw_next};

const ds_map_ops_t *ds_map_rh_ops(void) { return &mi_rh_ops; }
const ds_map_ops_t *ds_map_swiss_ops(void) { return &mi_sw_ops; }

/* ---- handle ---- */

ds_status_t ds_map_init(ds_map_t *map, const ds_map_ops_t *ops, size_t key_size,
                        size_t value_size, ds_map_hash_fn hash,
                        ds_map_equal_fn equal, void *user,
                        ds_context_t *context) {
  ds_status_t st;
  if (map == NULL || hash == NULL || equal == NULL) {
    return DS_ERR_NULL;
  }
  if (ops == NULL) {
    ops = ds_map_rh_ops();
  }
  map->ops = ops;
  map->context = context;
  map->impl = ds_context_alloc(context, ops->instance_size);
  if (map->impl == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ops->init(map->impl, key_size, value_size, hash, equal, user, context);
  if (st != DS_OK) {
    ds_context_free(context, map->impl);
    map->impl = NULL;
  }
  return st;
}

void ds_map_destroy(ds_map_t *map) {
  if (map == NULL || map->impl == NULL) {
    return;
  }
  map->ops->destroy(map->impl);
  ds_context_free(map->context, map->impl);
  map->impl = NULL;
}

ds_status_t ds_map_put(ds_map_t *map, const void *key, const void *value) {
  return map->ops->put(map->impl, key, value);
}

void *ds_map_get(const ds_map_t *map, const void *key) {
  return map->ops->get(map->impl, key);
}

ds_status_t ds_map_remove(ds_map_t *map, const void *key, void *out_value) {
  return map->ops->remove(map->impl, key, out_value);
}

size_t ds_map_size(const ds_map_t *map) { return map->ops->size(map->impl); }

bool ds_map_next(const ds_map_t *map, size_t *cursor, const void **key,
                 void **value) {
  return map->ops->next(map->impl, cursor, key, value);
}

unsigned long ds_map_hash_ulong(const void *key, void *user) {
  return ds_rh_map_hash_ulong(key, user);
}

bool ds_map_equal_ulong(const void *a, const void *b, void *user) {
  return ds_rh_map_equal_ulong(a, b, user);
}
