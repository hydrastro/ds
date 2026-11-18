#include <dsa/structures/slot_map.h>
#include <string.h>

#define SLOT_MAP_SIZE_MAX (~(size_t)0)
#define SLOT_MAP_NONE SLOT_MAP_SIZE_MAX
#define SLOT_MAP_MIN_CAPACITY 8

static unsigned char *slot_map_value(const ds_slot_map_t *map, size_t dense) {
  return map->values + dense * map->elem_size;
}

static void *slot_map_resize_block(ds_context_t *context, void *block,
                                   size_t bytes) {
  if (block == NULL) {
    return ds_context_alloc(context, bytes);
  }
  return ds_context_realloc(context, block, bytes);
}

/* Grows the blocks to capacity. The bookkeeping blocks are resized in
 * place; the values move to a new block, into which stage (when given) is
 * copied at the dense end before the old block goes, so stage may be one of
 * the map's own values. Capacity only changes once every block succeeded,
 * so a failure leaves the map consistent (with some blocks merely larger
 * than needed). */
static ds_status_t slot_map_set_capacity(ds_slot_map_t *map, size_t capacity,
                                         const void *stage) {
  void *block;
  unsigned char *values;
  if (map->fixed) {
    return DS_ERR_OVERFLOW;
  }
  if (capacity > SLOT_MAP_SIZE_MAX / map->elem_size ||
      capacity > SLOT_MAP_SIZE_MAX / sizeof(ds_slot_map_slot_t)) {
    return DS_ERR_OVERFLOW;
  }
  block = slot_map_resize_block(map->context, map->dense_slot,
                                capacity * sizeof(size_t));
  if (block == NULL) {
    return DS_ERR_ALLOC;
  }
  map->dense_slot = (size_t *)block;
  block = slot_map_resize_block(map->context, map->slots,
                                capacity * sizeof(ds_slot_map_slot_t));
  if (block == NULL) {
    return DS_ERR_ALLOC;
  }
  map->slots = (ds_slot_map_slot_t *)block;
  values = (unsigned char *)ds_context_alloc(map->context,
                                             capacity * map->elem_size);
  if (values == NULL) {
    return DS_ERR_ALLOC;
  }
  if (map->size > 0) {
    memcpy(values, map->values, map->size * map->elem_size);
  }
  if (stage != NULL) {
    memcpy(values + map->size * map->elem_size, stage, map->elem_size);
  }
  ds_context_free(map->context, map->values);
  map->values = values;
  map->capacity = capacity;
  return DS_OK;
}

/* Makes room for one more value and copies value (when given) to the dense
 * end. value may be one of the map's own: growth stages it in the new
 * block, and in place the dense end is free room. */
static ds_status_t slot_map_make_room(ds_slot_map_t *map, const void *value) {
  size_t capacity;
  if (map->free_head == SLOT_MAP_NONE && map->slot_count == map->capacity) {
    if (map->fixed) {
      return DS_ERR_OVERFLOW;
    }
    if (map->capacity < SLOT_MAP_MIN_CAPACITY) {
      capacity = SLOT_MAP_MIN_CAPACITY;
    } else if (map->capacity > SLOT_MAP_SIZE_MAX / 2) {
      return DS_ERR_OVERFLOW;
    } else {
      capacity = map->capacity * 2;
    }
    return slot_map_set_capacity(map, capacity, value);
  }
  if (value != NULL) {
    memcpy(slot_map_value(map, map->size), value, map->elem_size);
  }
  return DS_OK;
}

/* a free slot; slot_map_make_room has made sure there is one */
static size_t slot_map_acquire(ds_slot_map_t *map) {
  size_t slot;
  if (map->free_head != SLOT_MAP_NONE) {
    slot = map->free_head;
    map->free_head = map->slots[slot].index;
  } else {
    slot = map->slot_count++;
    map->slots[slot].generation = 0;
  }
  return slot;
}

static void slot_map_release(ds_slot_map_t *map, size_t slot) {
  map->slots[slot].generation++;
  if (map->slots[slot].generation == 0) {
    return; /* wrapped: retire the slot forever */
  }
  map->slots[slot].index = map->free_head;
  map->free_head = slot;
}

ds_status_t ds_slot_map_init(ds_slot_map_t *map, size_t elem_size,
                             ds_context_t *context) {
  if (map == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  map->values = NULL;
  map->dense_slot = NULL;
  map->slots = NULL;
  map->size = 0;
  map->capacity = 0;
  map->slot_count = 0;
  map->free_head = SLOT_MAP_NONE;
  map->elem_size = elem_size;
  map->context = context != NULL ? context : ds_default_context();
  map->fixed = false;
  return DS_OK;
}

ds_status_t ds_slot_map_init_buffer(ds_slot_map_t *map, size_t elem_size,
                                    void *values, ds_slot_map_slot_t *slots,
                                    size_t *dense_slot, size_t capacity) {
  if (map == NULL || (capacity > 0 && (values == NULL || slots == NULL ||
                                       dense_slot == NULL))) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  map->values = (unsigned char *)values;
  map->dense_slot = dense_slot;
  map->slots = slots;
  map->size = 0;
  map->capacity = capacity;
  map->slot_count = 0;
  map->free_head = SLOT_MAP_NONE;
  map->elem_size = elem_size;
  map->context = NULL;
  map->fixed = true;
  return DS_OK;
}

void ds_slot_map_destroy(ds_slot_map_t *map) {
  if (map == NULL) {
    return;
  }
  if (!map->fixed) {
    ds_context_free(map->context, map->values);
    ds_context_free(map->context, map->dense_slot);
    ds_context_free(map->context, map->slots);
    map->values = NULL;
    map->dense_slot = NULL;
    map->slots = NULL;
    map->capacity = 0;
  }
  map->size = 0;
  map->slot_count = 0;
  map->free_head = SLOT_MAP_NONE;
}

static void *slot_map_emplace_from(ds_slot_map_t *map,
                                   ds_slot_map_handle_t *out_handle,
                                   const void *value) {
  size_t slot;
  size_t dense;
  if (slot_map_make_room(map, value) != DS_OK) {
    return NULL;
  }
  slot = slot_map_acquire(map);
  map->slots[slot].generation++; /* even (free) -> odd (occupied) */
  dense = map->size++;
  map->slots[slot].index = dense;
  map->dense_slot[dense] = slot;
  if (out_handle != NULL) {
    out_handle->index = slot;
    out_handle->generation = map->slots[slot].generation;
  }
  return slot_map_value(map, dense);
}

void *ds_slot_map_emplace(ds_slot_map_t *map,
                          ds_slot_map_handle_t *out_handle) {
  return slot_map_emplace_from(map, out_handle, NULL);
}

ds_status_t ds_slot_map_insert(ds_slot_map_t *map, const void *value,
                               ds_slot_map_handle_t *out_handle) {
  if (value == NULL) {
    return DS_ERR_NULL;
  }
  if (map->free_head == SLOT_MAP_NONE && map->slot_count == map->capacity &&
      map->fixed) {
    return DS_ERR_OVERFLOW;
  }
  if (slot_map_emplace_from(map, out_handle, value) == NULL) {
    return map->fixed ? DS_ERR_OVERFLOW : DS_ERR_ALLOC;
  }
  return DS_OK;
}

bool ds_slot_map_contains(const ds_slot_map_t *map,
                          ds_slot_map_handle_t handle) {
  unsigned long generation;
  if (handle.index >= map->slot_count) {
    return false;
  }
  generation = map->slots[handle.index].generation;
  return generation == handle.generation && (generation & 1UL) != 0;
}

void *ds_slot_map_get(ds_slot_map_t *map, ds_slot_map_handle_t handle) {
  if (!ds_slot_map_contains(map, handle)) {
    return NULL;
  }
  return slot_map_value(map, map->slots[handle.index].index);
}

ds_status_t ds_slot_map_remove(ds_slot_map_t *map, ds_slot_map_handle_t handle,
                               void *out) {
  size_t dense;
  size_t last;
  size_t moved_slot;
  if (!ds_slot_map_contains(map, handle)) {
    return DS_NOT_FOUND;
  }
  dense = map->slots[handle.index].index;
  if (out != NULL) {
    memcpy(out, slot_map_value(map, dense), map->elem_size);
  }
  last = map->size - 1;
  if (dense != last) {
    memcpy(slot_map_value(map, dense), slot_map_value(map, last),
           map->elem_size);
    moved_slot = map->dense_slot[last];
    map->dense_slot[dense] = moved_slot;
    map->slots[moved_slot].index = dense;
  }
  map->size--;
  slot_map_release(map, handle.index);
  return DS_OK;
}

void ds_slot_map_clear(ds_slot_map_t *map) {
  size_t i;
  for (i = 0; i < map->size; i++) {
    slot_map_release(map, map->dense_slot[i]);
  }
  map->size = 0;
}

ds_status_t ds_slot_map_reserve(ds_slot_map_t *map, size_t capacity) {
  if (capacity <= map->capacity) {
    return DS_OK;
  }
  return slot_map_set_capacity(map, capacity, NULL);
}

size_t ds_slot_map_size(const ds_slot_map_t *map) { return map->size; }

size_t ds_slot_map_capacity(const ds_slot_map_t *map) { return map->capacity; }

void *ds_slot_map_data(ds_slot_map_t *map) { return map->values; }

ds_status_t ds_slot_map_handle_at(const ds_slot_map_t *map, size_t dense_index,
                                  ds_slot_map_handle_t *out_handle) {
  size_t slot;
  if (out_handle == NULL) {
    return DS_ERR_NULL;
  }
  if (dense_index >= map->size) {
    return DS_ERR_RANGE;
  }
  slot = map->dense_slot[dense_index];
  out_handle->index = slot;
  out_handle->generation = map->slots[slot].generation;
  return DS_OK;
}
