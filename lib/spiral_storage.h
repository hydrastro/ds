#ifndef DS_SPIRAL_STORAGE_H
#define DS_SPIRAL_STORAGE_H

/*
 * ds_spiral_map_t, Martin's spiral storage: a dynamic hash table that grows
 * and shrinks one slot at a time with no directory.
 *
 * A key's hash gives h in [0, 1). With growth factor d > 1 and the current
 * start S, the key lives in logical slot floor(d^x) for
 * x = ceil(S - h) + h, so the slots in use run from first = d^S to
 * floor(d first), and slot j holds the keys whose d^x falls in [j, j + 1)
 * (d^x is computed as d^h times d, d, ... until it reaches first, the same
 * way every time, so slot boundaries stay consistent as S moves). Growing
 * advances first by one: that slot retires and its keys spread over the d slots
 * that appear at the top (for d = 2, slot j splits into 2j and 2j + 1);
 * shrinking undoes it. Lower slots are therefore fuller than upper ones, the
 * characteristic spiral load profile. The active slots are kept in a circular
 * physical array indexed by the logical slot number.
 *
 * Keys and values are stored by value; hashing and equality are callbacks
 * with a user pointer. The table grows when the average chain exceeds
 * max_load and shrinks below a quarter of it. Memory comes from the
 * ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long (*ds_spiral_hash_func_t)(const void *key, void *user);
typedef bool (*ds_spiral_equal_func_t)(const void *a, const void *b,
                                       void *user);
typedef bool (*ds_spiral_visit_func_t)(const void *key, void *value,
                                       void *user);

typedef struct ds_spiral_map {
  size_t key_size;
  size_t value_size;
  ds_spiral_hash_func_t hash;
  ds_spiral_equal_func_t equal;
  void *user;
  double growth; /* d */
  double max_load;
  unsigned long first; /* lowest active logical slot */
  /* physical circular array of chains */
  size_t *head; /* entry index or (size_t)-1 */
  size_t cap;   /* physical slots, >= active slots */
  /* entries */
  unsigned char *keys;
  unsigned char *values;
  size_t *next;
  size_t entries;
  size_t entry_capacity;
  size_t free_list;
  size_t size;
  ds_context_t *context;
} ds_spiral_map_t;

/* growth d in (1, 16] (0: 2), max_load > 0 (0: 2). */
ds_status_t ds_spiral_map_init(ds_spiral_map_t *m, size_t key_size,
                               size_t value_size, ds_spiral_hash_func_t hash,
                               ds_spiral_equal_func_t equal, void *user,
                               double growth, double max_load,
                               ds_context_t *context);
void ds_spiral_map_destroy(ds_spiral_map_t *m);
size_t ds_spiral_map_size(const ds_spiral_map_t *m);
/* active logical slots: [first, last) */
unsigned long ds_spiral_map_first_slot(const ds_spiral_map_t *m);
unsigned long ds_spiral_map_end_slot(const ds_spiral_map_t *m);
/* the logical slot a key belongs to */
unsigned long ds_spiral_map_slot_of(const ds_spiral_map_t *m, const void *key);
/* DS_EXISTS (value replaced) when the key is present. */
ds_status_t ds_spiral_map_put(ds_spiral_map_t *m, const void *key,
                              const void *value);
ds_status_t ds_spiral_map_get(const ds_spiral_map_t *m, const void *key,
                              void *out_value);
ds_status_t ds_spiral_map_remove(ds_spiral_map_t *m, const void *key);
ds_status_t ds_spiral_map_walk(ds_spiral_map_t *m, ds_spiral_visit_func_t visit,
                               void *user);
/* Every key sits in the chain of its slot; counts agree. */
bool ds_spiral_map_valid(const ds_spiral_map_t *m);

#ifdef __cplusplus
}
#endif

#endif /* DS_SPIRAL_STORAGE_H */
