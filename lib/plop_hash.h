#ifndef DS_PLOP_HASH_H
#define DS_PLOP_HASH_H

/*
 * PLOP-hashing (piecewise linear order preserving hashing; Kriegel &
 * Seeger): a multidimensional, order-preserving dynamic hashing scheme
 * without a directory.
 *
 * Each axis is cut into slices, kept in key order (an ordered boundary
 * array maps a coordinate to its slice in O(log s)); the cells of the
 * resulting grid are the buckets. When the load factor exceeds its bound
 * the file expands by splitting one slice of one axis, linear-hashing
 * style: per axis a pointer runs over the slices of the current round
 * (each round doubles the slices of that axis) and the axes take turns
 * by rounds. A split adds a hyperplane of new cells; their addresses are
 * appended at the end, so the address of any cell is computed from the
 * expansion that created its youngest slice and a mixed-radix offset over
 * the other axes, with no directory at all. Buckets are pages of
 * page_size records with overflow pages chained behind them.
 *
 * The split point is the midpoint of the slice (within the given domain)
 * or the median of the records in the slice, which adapts the partition
 * to skewed data. Records are points (dims doubles) with a payload stored
 * by value. Exact-match lookup, deletion and order-preserving box queries
 * (only the cells whose slices meet the box are visited).
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_PLOP_MAX_DIMS 16

typedef enum ds_plop_split { DS_PLOP_MIDPOINT, DS_PLOP_MEDIAN } ds_plop_split_t;

typedef struct ds_plop_bucket {
  unsigned char *records; /* (dims doubles + payload) each */
  size_t count;
  size_t capacity;
} ds_plop_bucket_t;

typedef struct ds_plop_axis {
  double *bound; /* sorted lower bounds of the slices (bound[0] = -inf) */
  size_t *slice; /* slice id in that order */
  size_t *born;  /* per slice id: expansion that created it (+1; 0 = initial) */
  size_t slices;
  size_t capacity;
  size_t pointer; /* next slice (in id order) to split this round */
  size_t round;   /* slices at the start of the round */
} ds_plop_axis_t;

typedef struct ds_plop_hash {
  size_t dims;
  size_t payload_size;
  size_t record_size;
  size_t page_size;
  double max_load;
  ds_plop_split_t split;
  double lo[DS_PLOP_MAX_DIMS];
  double hi[DS_PLOP_MAX_DIMS];
  ds_plop_axis_t axis[DS_PLOP_MAX_DIMS];
  size_t turn; /* axis expanding now */
  /* expansion log: axis, first address, slice counts of every axis */
  size_t *exp_axis;
  size_t *exp_start;
  size_t *exp_counts; /* expansions * dims */
  size_t expansions;
  size_t exp_capacity;
  size_t exp_counts_capacity;
  ds_plop_bucket_t *buckets;
  size_t bucket_count;
  size_t bucket_capacity;
  size_t records;
  ds_context_t *context;
} ds_plop_hash_t;

typedef bool (*ds_plop_visit_fn)(const double *point, const void *payload,
                                 void *user);

/* lo/hi: the expected domain per axis (used for midpoints; points outside
 * are still accepted). max_load in (0, 1] of page_size * buckets. */
ds_status_t ds_plop_init(ds_plop_hash_t *h, size_t dims, const double *lo,
                         const double *hi, size_t payload_size,
                         size_t page_size, double max_load,
                         ds_plop_split_t split, ds_context_t *context);
void ds_plop_destroy(ds_plop_hash_t *h);
ds_status_t ds_plop_insert(ds_plop_hash_t *h, const double *point,
                           const void *payload);
/* the payload of a record at exactly this point (DS_ERR_RANGE: none) */
ds_status_t ds_plop_find(const ds_plop_hash_t *h, const double *point,
                         void *payload_out);
/* removes one record at exactly this point */
ds_status_t ds_plop_remove(ds_plop_hash_t *h, const double *point);
/* records with lo[d] <= x[d] <= hi[d]; visit returns false to stop */
ds_status_t ds_plop_range(const ds_plop_hash_t *h, const double *lo,
                          const double *hi, ds_plop_visit_fn visit, void *user);
size_t ds_plop_size(const ds_plop_hash_t *h);
size_t ds_plop_buckets(const ds_plop_hash_t *h);
/* overflow pages over all buckets */
size_t ds_plop_overflow_pages(const ds_plop_hash_t *h);
double ds_plop_load(const ds_plop_hash_t *h);

#ifdef __cplusplus
}
#endif

#endif /* DS_PLOP_HASH_H */
