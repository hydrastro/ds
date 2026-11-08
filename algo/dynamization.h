#ifndef DS_DYNAMIZATION_H
#define DS_DYNAMIZATION_H

/*
 * Dynamization of static data structures (Bentley & Saxe's logarithmic
 * method) for decomposable search problems: a query over a union of sets
 * is answered by combining the answers over the parts.
 *
 * The user supplies a static structure as callbacks: build one from an
 * array of items (by value, elem_size bytes each, with stable item ids),
 * destroy it, and query it, accumulating into a caller-owned result (the
 * combination of partial answers is the user's: a sum, a minimum, a list
 * of hits...). The dynamization keeps levels 0, 1, 2, ... where level i
 * holds a multiple of base^i items, at most (base - 1) base^i, each level
 * one static structure. An insertion carries items upwards like an
 * increment in base `base` and rebuilds the first level that can take
 * them: O(log_base n) structures per query, each item rebuilt
 * O((base - 1) log_base n) times. base 2 is the binary method; larger
 * bases make queries cheaper and updates dearer.
 *
 * Deletions are weak: the item is marked dead (queries can test
 * ds_dynamization_is_alive with the ids given to build) and, once dead
 * items exceed rebuild_ratio times the live ones, everything is rebuilt
 * from the live items (global rebuilding), one structure per nonzero
 * base-`base` digit of the live count. For invertible problems, a second
 * dynamization of deleted items whose answers are subtracted is the
 * alternative.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ds_dynamization;

typedef struct ds_dynamization_ops {
  /* NULL on failure */
  void *(*build)(const void *items, const size_t *ids, size_t count,
                 void *user);
  void (*destroy)(void *structure, void *user);
  ds_status_t (*query)(const void *structure, const void *query, void *result,
                       const struct ds_dynamization *dyn, void *user);
  void *user;
} ds_dynamization_ops_t;

typedef struct ds_dynamization_level {
  void *structure; /* NULL when empty */
  unsigned char *items;
  size_t *ids;
  size_t count;
  size_t capacity;
} ds_dynamization_level_t;

typedef struct ds_dynamization {
  size_t elem_size;
  size_t base;
  ds_dynamization_ops_t ops;
  ds_dynamization_level_t *levels;
  size_t level_count;
  size_t level_capacity;
  unsigned char *dead; /* by id */
  size_t ids;          /* next id */
  size_t dead_capacity;
  size_t live;
  size_t dead_count; /* dead items still stored */
  double rebuild_ratio;
  size_t rebuilds;
  ds_context_t *context;
} ds_dynamization_t;

/* base >= 2; rebuild_ratio > 0 (1.0: rebuild when half the stored items
 * are dead) */
ds_status_t ds_dynamization_init(ds_dynamization_t *dyn, size_t elem_size,
                                 size_t base, double rebuild_ratio,
                                 const ds_dynamization_ops_t *ops,
                                 ds_context_t *context);
void ds_dynamization_destroy(ds_dynamization_t *dyn);
ds_status_t ds_dynamization_insert(ds_dynamization_t *dyn, const void *item,
                                   size_t *out_id);
/* DS_ERR_RANGE for unknown or already deleted ids */
ds_status_t ds_dynamization_remove(ds_dynamization_t *dyn, size_t id);
bool ds_dynamization_is_alive(const ds_dynamization_t *dyn, size_t id);
/* calls ops.query on every non-empty level, stopping at an error */
ds_status_t ds_dynamization_query(const ds_dynamization_t *dyn,
                                  const void *query, void *result);
/* rebuild from the live items now */
ds_status_t ds_dynamization_rebuild(ds_dynamization_t *dyn);
size_t ds_dynamization_size(const ds_dynamization_t *dyn);
/* non-empty levels (static structures a query visits) */
size_t ds_dynamization_structures(const ds_dynamization_t *dyn);

#ifdef __cplusplus
}
#endif

#endif /* DS_DYNAMIZATION_H */
