#ifndef DS_TWO_CHOICE_MAP_H
#define DS_TWO_CHOICE_MAP_H

/*
 * Multiple-choice hash maps: each key may live in any of d candidate
 * buckets (d independent hashes) and is placed in the least loaded one,
 * which brings the maximum bucket load down from Theta(log n / log log n)
 * to log log n / log d + O(1) (the power of two choices).
 *
 * Placement policies:
 *   DS_CHOICE_GREEDY  d-choice (2-choice for d = 2) hashing: d buckets
 *                     anywhere in the table, ties broken at random
 *   DS_CHOICE_LEFT    d-left hashing (Vocking): the table is split into
 *                     d groups, choice i falls in group i, ties go to the
 *                     leftmost group (asymmetry improves the bound to
 *                     log log n / (d phi_d))
 *
 * Buckets hold up to bucket_size entries (keys and values by value, fixed
 * sizes); when every candidate bucket is full the table doubles (a put
 * fails with DS_ERR_OVERFLOW if eight doublings in a row cannot make room,
 * i.e. the hash keeps sending keys to the same buckets). The hash
 * is one callback taking a seed (the choice index), so any family works.
 *
 * Concurrency: a map may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*ds_choice_hash_func_t)(const void *key, size_t seed,
                                        void *user);
typedef bool (*ds_choice_equal_func_t)(const void *a, const void *b,
                                       void *user);

typedef enum ds_choice_policy {
  DS_CHOICE_GREEDY = 0,
  DS_CHOICE_LEFT
} ds_choice_policy_t;

typedef struct ds_two_choice_map {
  unsigned char *keys;
  unsigned char *values;
  size_t *load; /* per bucket */
  size_t buckets;
  size_t bucket_size;
  size_t choices;
  size_t size;
  size_t key_size;
  size_t value_size;
  ds_choice_policy_t policy;
  ds_choice_hash_func_t hash;
  ds_choice_equal_func_t equal;
  void *user;
  unsigned long rng;
  ds_context_t *context;
} ds_two_choice_map_t;

/* choices >= 2 (0: 2); bucket_size >= 1 (0: 4). */
ds_status_t ds_two_choice_map_init(ds_two_choice_map_t *m, size_t key_size,
                                   size_t value_size,
                                   ds_choice_hash_func_t hash,
                                   ds_choice_equal_func_t equal, void *user,
                                   size_t choices, size_t bucket_size,
                                   ds_choice_policy_t policy,
                                   ds_context_t *context);
void ds_two_choice_map_destroy(ds_two_choice_map_t *m);
void ds_two_choice_map_clear(ds_two_choice_map_t *m);
size_t ds_two_choice_map_size(const ds_two_choice_map_t *m);
/* DS_EXISTS (value replaced) for a present key. */
ds_status_t ds_two_choice_map_put(ds_two_choice_map_t *m, const void *key,
                                  const void *value);
/* Pointer to the stored value, or NULL. */
void *ds_two_choice_map_get(const ds_two_choice_map_t *m, const void *key);
ds_status_t ds_two_choice_map_remove(ds_two_choice_map_t *m, const void *key,
                                     void *out_value);
/* Largest bucket load (the quantity the choices keep small). */
size_t ds_two_choice_map_max_load(const ds_two_choice_map_t *m);
void ds_two_choice_map_foreach(const ds_two_choice_map_t *m,
                               bool (*visit)(const void *key, const void *value,
                                             void *user),
                               void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_TWO_CHOICE_MAP_H */
