#ifndef DS_DIET_H
#define DS_DIET_H

/*
 * Discrete interval encoding trees (diets, Erwig): sets of integers kept
 * as a balanced search tree of maximal disjoint intervals [lo, hi], so
 * dense runs cost one node however long they are. Adjacent and
 * overlapping intervals are merged on insertion; deletion splits an
 * interval when it removes its middle.
 *
 * Elements are unsigned long. The tree is an AVL tree in node arrays.
 * Besides single elements and ranges: membership, the interval holding an
 * element, the number of elements, the number of intervals, minimum and
 * maximum, and ordered interval iteration.
 *
 * Concurrency: a diet may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_diet {
  unsigned long *lo;
  unsigned long *hi;
  size_t *left;
  size_t *right;
  unsigned char *height;
  size_t root;
  size_t intervals;
  size_t slots;
  size_t capacity;
  size_t free_list;
  ds_context_t *context;
} ds_diet_t;

typedef bool (*ds_diet_visit_func_t)(unsigned long lo, unsigned long hi,
                                     void *user);

ds_status_t ds_diet_init(ds_diet_t *d, ds_context_t *context);
void ds_diet_destroy(ds_diet_t *d);
void ds_diet_clear(ds_diet_t *d);
/* Adds [lo, hi] (lo <= hi); merges with neighbours. */
ds_status_t ds_diet_insert_range(ds_diet_t *d, unsigned long lo,
                                 unsigned long hi);
ds_status_t ds_diet_insert(ds_diet_t *d, unsigned long x);
/* Removes [lo, hi]; DS_NOT_FOUND if nothing was removed. */
ds_status_t ds_diet_remove_range(ds_diet_t *d, unsigned long lo,
                                 unsigned long hi);
ds_status_t ds_diet_remove(ds_diet_t *d, unsigned long x);
bool ds_diet_contains(const ds_diet_t *d, unsigned long x);
/* The maximal interval holding x. */
ds_status_t ds_diet_interval_of(const ds_diet_t *d, unsigned long x,
                                unsigned long *out_lo, unsigned long *out_hi);
size_t ds_diet_intervals(const ds_diet_t *d);
/* Elements in the set (saturates at ULONG_MAX). */
unsigned long ds_diet_count(const ds_diet_t *d);
ds_status_t ds_diet_min(const ds_diet_t *d, unsigned long *out);
ds_status_t ds_diet_max(const ds_diet_t *d, unsigned long *out);
void ds_diet_foreach(const ds_diet_t *d, ds_diet_visit_func_t f, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_DIET_H */
