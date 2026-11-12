#include "map_iface.h"
#include "cuckoo_map.h"
#include "extendible_hash.h"
#include "linear_hash_map.h"
#include "rh_map.h"
#include "swiss_map.h"
#include <limits.h>
#include <string.h>

/* put through emplace, so every backend reports DS_EXISTS on replacement
 * (as ds_rh_map_put does) */
static ds_status_t mi_put_slot(void *slot, bool inserted, const void *value,
                               size_t value_size) {
  if (slot == NULL) {
    return DS_ERR_ALLOC;
  }
  if (value_size > 0) {
    memmove(slot, value, value_size);
  }
  return inserted ? DS_OK : DS_EXISTS;
}

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
  ds_swiss_map_t *m = (ds_swiss_map_t *)map;
  bool inserted = false;
  void *slot = ds_swiss_map_emplace(m, key, &inserted);
  return mi_put_slot(slot, inserted, value, m->config.value_size);
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

/* ---- cuckoo backend ---- */

static ds_status_t mi_ck_init(void *map, size_t key_size, size_t value_size,
                              ds_map_hash_fn hash, ds_map_equal_fn equal,
                              void *user, ds_context_t *context) {
  ds_cuckoo_map_config_t c;
  ds_cuckoo_map_config_init(&c);
  c.key_size = key_size;
  c.value_size = value_size;
  c.hash = hash;
  c.equal = equal;
  c.user = user;
  c.context = context;
  return ds_cuckoo_map_init((ds_cuckoo_map_t *)map, &c, 0);
}

static void mi_ck_destroy(void *map) {
  ds_cuckoo_map_destroy((ds_cuckoo_map_t *)map);
}

static ds_status_t mi_ck_put(void *map, const void *key, const void *value) {
  ds_cuckoo_map_t *m = (ds_cuckoo_map_t *)map;
  bool inserted = false;
  void *slot = ds_cuckoo_map_emplace(m, key, &inserted);
  return mi_put_slot(slot, inserted, value, m->config.value_size);
}

static void *mi_ck_get(const void *map, const void *key) {
  return ds_cuckoo_map_get((const ds_cuckoo_map_t *)map, key);
}

static ds_status_t mi_ck_remove(void *map, const void *key, void *out) {
  return ds_cuckoo_map_remove((ds_cuckoo_map_t *)map, key, out);
}

static size_t mi_ck_size(const void *map) {
  return ds_cuckoo_map_size((const ds_cuckoo_map_t *)map);
}

static bool mi_ck_next(const void *map, size_t *cursor, const void **key,
                       void **value) {
  return ds_cuckoo_map_next((const ds_cuckoo_map_t *)map, cursor, key, value);
}

static const ds_map_ops_t mi_ck_ops = {"cuckoo",
                                       DS_MAP_EXPECTED_O1 | DS_MAP_WORST_O1_GET,
                                       sizeof(ds_cuckoo_map_t),
                                       mi_ck_init,
                                       mi_ck_destroy,
                                       mi_ck_put,
                                       mi_ck_get,
                                       mi_ck_remove,
                                       mi_ck_size,
                                       mi_ck_next};

/* Two-part cursors (bucket, index) are packed into the size_t cursor with
 * a split chosen from the map's current bucket count: the bucket takes the
 * high bits its largest number needs, the index the rest. The map does
 * not change during an iteration, so every call computes the same split.
 * A position that does not fit (a bucket holding more records than the
 * remaining bits can count) ends the iteration with the cursor set to
 * DS_MAP_CURSOR_OVERFLOW, which ds_map_next_status reports. */
#define MI_W (sizeof(size_t) * CHAR_BIT)

static unsigned mi_index_bits(size_t buckets) {
  unsigned b = 0;
  size_t last = buckets == 0 ? 0 : buckets - 1;
  while (last != 0) {
    b++;
    last >>= 1;
  }
  return (unsigned)MI_W - b;
}

static void mi_unpack(size_t cursor, unsigned ib, size_t *bucket,
                      size_t *index) {
  if (ib >= MI_W) {
    *bucket = 0;
    *index = cursor;
  } else {
    *bucket = cursor >> ib;
    *index = cursor & (((size_t)1 << ib) - 1);
  }
}

/* false when (bucket, index) does not fit; all-ones is reserved */
static bool mi_pack(size_t bucket, size_t index, unsigned ib, size_t *cursor) {
  if (ib >= MI_W) {
    if (bucket != 0 || index == (size_t)-1) {
      return false;
    }
    *cursor = index;
    return true;
  }
  if (index >= ((size_t)1 << ib) - 1) {
    return false;
  }
  *cursor = (bucket << ib) | index;
  return true;
}

/* ---- linear hashing backend ---- */

static ds_status_t mi_lh_init(void *map, size_t key_size, size_t value_size,
                              ds_map_hash_fn hash, ds_map_equal_fn equal,
                              void *user, ds_context_t *context) {
  ds_linear_hash_map_config_t c;
  ds_linear_hash_map_config_init(&c);
  c.key_size = key_size;
  c.value_size = value_size;
  c.hash = hash;
  c.equal = equal;
  c.user = user;
  c.context = context;
  return ds_linear_hash_map_init((ds_linear_hash_map_t *)map, &c);
}

static void mi_lh_destroy(void *map) {
  ds_linear_hash_map_destroy((ds_linear_hash_map_t *)map);
}

static ds_status_t mi_lh_put(void *map, const void *key, const void *value) {
  ds_linear_hash_map_t *m = (ds_linear_hash_map_t *)map;
  bool inserted = false;
  void *slot = ds_linear_hash_map_emplace(m, key, &inserted);
  return mi_put_slot(slot, inserted, value, m->config.value_size);
}

static void *mi_lh_get(const void *map, const void *key) {
  return ds_linear_hash_map_get((const ds_linear_hash_map_t *)map, key);
}

static ds_status_t mi_lh_remove(void *map, const void *key, void *out) {
  return ds_linear_hash_map_remove((ds_linear_hash_map_t *)map, key, out);
}

static size_t mi_lh_size(const void *map) {
  return ds_linear_hash_map_size((const ds_linear_hash_map_t *)map);
}

static bool mi_lh_next(const void *map, size_t *cursor, const void **key,
                       void **value) {
  const ds_linear_hash_map_t *m = (const ds_linear_hash_map_t *)map;
  ds_linear_hash_map_cursor_t c;
  unsigned ib = mi_index_bits(ds_linear_hash_map_bucket_count(m));
  bool ok;
  if (*cursor == DS_MAP_CURSOR_OVERFLOW) {
    return false;
  }
  mi_unpack(*cursor, ib, &c.bucket, &c.index);
  ok = ds_linear_hash_map_next(m, &c, key, value);
  if (ok && !mi_pack(c.bucket, c.index, ib, cursor)) {
    *cursor = DS_MAP_CURSOR_OVERFLOW;
    return false;
  }
  return ok;
}

static const ds_map_ops_t mi_lh_ops = {
    "linear hashing", DS_MAP_EXPECTED_O1, sizeof(ds_linear_hash_map_t),
    mi_lh_init,       mi_lh_destroy,      mi_lh_put,
    mi_lh_get,        mi_lh_remove,       mi_lh_size,
    mi_lh_next};

/* ---- extendible hashing backend ---- */

static ds_status_t mi_eh_init(void *map, size_t key_size, size_t value_size,
                              ds_map_hash_fn hash, ds_map_equal_fn equal,
                              void *user, ds_context_t *context) {
  ds_extendible_hash_config_t c;
  ds_extendible_hash_config_init(&c);
  c.key_size = key_size;
  c.value_size = value_size;
  c.hash = hash;
  c.equal = equal;
  c.user = user;
  c.context = context;
  return ds_extendible_hash_init((ds_extendible_hash_t *)map, &c);
}

static void mi_eh_destroy(void *map) {
  ds_extendible_hash_destroy((ds_extendible_hash_t *)map);
}

static ds_status_t mi_eh_put(void *map, const void *key, const void *value) {
  ds_extendible_hash_t *m = (ds_extendible_hash_t *)map;
  bool inserted = false;
  void *slot = ds_extendible_hash_emplace(m, key, &inserted);
  return mi_put_slot(slot, inserted, value, m->config.value_size);
}

static void *mi_eh_get(const void *map, const void *key) {
  return ds_extendible_hash_get((const ds_extendible_hash_t *)map, key);
}

static ds_status_t mi_eh_remove(void *map, const void *key, void *out) {
  return ds_extendible_hash_remove((ds_extendible_hash_t *)map, key, out);
}

static size_t mi_eh_size(const void *map) {
  return ds_extendible_hash_size((const ds_extendible_hash_t *)map);
}

static bool mi_eh_next(const void *map, size_t *cursor, const void **key,
                       void **value) {
  const ds_extendible_hash_t *m = (const ds_extendible_hash_t *)map;
  ds_extendible_hash_cursor_t c;
  unsigned ib;
  bool ok;
  if (*cursor == DS_MAP_CURSOR_OVERFLOW) {
    return false;
  }
  /* entries index the directory of 2^global_depth pointers */
  ib = ds_extendible_hash_global_depth(m) >= MI_W
           ? 0
           : mi_index_bits((size_t)1 << ds_extendible_hash_global_depth(m));
  mi_unpack(*cursor, ib, &c.entry, &c.index);
  ok = ds_extendible_hash_next(m, &c, key, value);
  if (ok && !mi_pack(c.entry, c.index, ib, cursor)) {
    *cursor = DS_MAP_CURSOR_OVERFLOW;
    return false;
  }
  return ok;
}

static const ds_map_ops_t mi_eh_ops = {"extendible hashing",
                                       DS_MAP_EXPECTED_O1,
                                       sizeof(ds_extendible_hash_t),
                                       mi_eh_init,
                                       mi_eh_destroy,
                                       mi_eh_put,
                                       mi_eh_get,
                                       mi_eh_remove,
                                       mi_eh_size,
                                       mi_eh_next};

const ds_map_ops_t *ds_map_rh_ops(void) { return &mi_rh_ops; }
const ds_map_ops_t *ds_map_cuckoo_ops(void) { return &mi_ck_ops; }
const ds_map_ops_t *ds_map_linear_hash_ops(void) { return &mi_lh_ops; }
const ds_map_ops_t *ds_map_extendible_ops(void) { return &mi_eh_ops; }
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
  map->ops = NULL;
  map->impl = NULL;
  /* every backend grows by realloc */
  st = ds_context_require(context, DS_RESOURCE_REALLOC);
  if (st != DS_OK) {
    return st;
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

ds_status_t ds_map_next_status(const ds_map_t *map, size_t *cursor,
                               const void **key, void **value) {
  if (map == NULL || map->ops == NULL || cursor == NULL) {
    return DS_ERR_NULL;
  }
  if (map->ops->next(map->impl, cursor, key, value)) {
    return DS_OK;
  }
  return *cursor == DS_MAP_CURSOR_OVERFLOW ? DS_ERR_OVERFLOW : DS_NOT_FOUND;
}

unsigned long ds_map_hash_ulong(const void *key, void *user) {
  return ds_rh_map_hash_ulong(key, user);
}

bool ds_map_equal_ulong(const void *a, const void *b, void *user) {
  return ds_rh_map_equal_ulong(a, b, user);
}
