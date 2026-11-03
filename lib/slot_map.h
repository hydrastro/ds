#ifndef DS_SLOT_MAP_H
#define DS_SLOT_MAP_H

/*
 * Slot map: a container of fixed-size values addressed by generational
 * handles.
 *
 * insert() returns a handle {slot index, generation}. remove() bumps the
 * slot's generation, so every handle that still points at the old value
 * becomes invalid instead of silently reaching whatever reuses the slot:
 * stale handles are detected, not dereferenced. A zero-initialised handle is
 * never valid.
 *
 * Values are kept dense (removal moves the last value into the hole), so
 * iterating over ds_slot_map_data() visits every live value with no gaps.
 * Value pointers are therefore only valid until the next insert or remove;
 * hold handles, not pointers.
 *
 * Generations are odd while a slot is occupied and even while it is free.
 * A slot whose generation would wrap around is retired for good, so a
 * handle can never become valid again by accident.
 *
 * Two storage modes: dynamic (ds_slot_map_init, grows through a
 * ds_context_t) or three caller arrays of `capacity` entries
 * (ds_slot_map_init_buffer, no allocation).
 *
 * Complexity: insert, remove, get, contains O(1); clear O(size).
 * Concurrency: a slot map may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_slot_map_handle {
  size_t index;
  unsigned long generation;
} ds_slot_map_handle_t;

typedef struct ds_slot_map_slot {
  size_t index; /* dense position while occupied, next free slot otherwise */
  unsigned long generation;
} ds_slot_map_slot_t;

typedef struct ds_slot_map {
  unsigned char *values;
  size_t *dense_slot;
  ds_slot_map_slot_t *slots;
  size_t size;
  size_t capacity;
  size_t slot_count;
  size_t free_head;
  size_t elem_size;
  ds_context_t *context;
  bool fixed;
} ds_slot_map_t;

ds_status_t FUNC(ds_slot_map_init)(ds_slot_map_t *map, size_t elem_size,
                                   ds_context_t *context);
ds_status_t FUNC(ds_slot_map_init_buffer)(ds_slot_map_t *map, size_t elem_size,
                                          void *values,
                                          ds_slot_map_slot_t *slots,
                                          size_t *dense_slot, size_t capacity);
void FUNC(ds_slot_map_destroy)(ds_slot_map_t *map);

ds_status_t FUNC(ds_slot_map_insert)(ds_slot_map_t *map, const void *value,
                                     ds_slot_map_handle_t *out_handle);
void *FUNC(ds_slot_map_emplace)(ds_slot_map_t *map,
                                ds_slot_map_handle_t *out_handle);
ds_status_t FUNC(ds_slot_map_remove)(ds_slot_map_t *map,
                                     ds_slot_map_handle_t handle, void *out);
void *FUNC(ds_slot_map_get)(ds_slot_map_t *map, ds_slot_map_handle_t handle);
bool FUNC(ds_slot_map_contains)(const ds_slot_map_t *map,
                                ds_slot_map_handle_t handle);
void FUNC(ds_slot_map_clear)(ds_slot_map_t *map);
ds_status_t FUNC(ds_slot_map_reserve)(ds_slot_map_t *map, size_t capacity);

size_t FUNC(ds_slot_map_size)(const ds_slot_map_t *map);
size_t FUNC(ds_slot_map_capacity)(const ds_slot_map_t *map);
void *FUNC(ds_slot_map_data)(ds_slot_map_t *map);
ds_status_t FUNC(ds_slot_map_handle_at)(const ds_slot_map_t *map,
                                        size_t dense_index,
                                        ds_slot_map_handle_t *out_handle);

#ifdef __cplusplus
}
#endif

#endif /* DS_SLOT_MAP_H */
