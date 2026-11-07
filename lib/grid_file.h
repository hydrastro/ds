#ifndef DS_GRID_FILE_H
#define DS_GRID_FILE_H

/*
 * Grid-partitioned bucket files for points (and boxes) in d <= 8
 * dimensions, every item carrying a size_t value. Coordinates must be
 * finite; a box is 2d doubles, low corner then high corner (closed).
 *
 * ds_fixed_grid_t, the fixed-grid method: a uniform grid of k_1 x ... x
 * k_d cells over a domain box, each cell a bucket (points outside the
 * domain go to the nearest border cell).
 *
 * ds_grid_file_t, the grid file of Nievergelt, Hinterberger and Sevcik:
 * one linear scale of split values per axis, a d-dimensional directory
 * of cells, and buckets of `bucket` points whose regions are boxes of
 * cells (several cells may share a bucket). An overflowing bucket first
 * splits its cell range; a bucket owning a single cell refines a scale
 * (a new split value at the median, adding a directory slice) first.
 * Removal merges a bucket with an adjacent buddy when both fit in two
 * thirds of a bucket. Cells are half-open, [s_i, s_i+1).
 *
 * ds_twin_grid_file_t, the twin grid file (Hutflesz, Six, Widmayer): two
 * grid files share the points; an insertion goes to whichever twin has
 * room in its bucket for the point, raising bucket occupancy.
 *
 * ds_grid_file2_t, the two-level grid file (Hinrichs): the directory is
 * itself partitioned by a coarse top-level grid into directory pages, each
 * a grid file over its region with at most `page_cells` cells.
 *
 * ds_mlgrid_file_t, the multilayer grid file (Six, Widmayer) for boxes: a
 * stack of grid files; a box lives in the first layer where it lies inside
 * a single bucket region, boxes cut by a later split move one layer down,
 * and boxes fitting no layer go to an overflow list.
 *
 * Memory comes from the ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_GRID_MAX_DIM 8

/* item: a point (d doubles) or a box (2d doubles); false stops. */
typedef bool (*ds_grid_visit_func_t)(const double *item, size_t value,
                                     void *user);

/* ---- fixed grid ---------------------------------------------------------- */

typedef struct ds_fixed_grid {
  size_t dim;
  double domain[2 * DS_GRID_MAX_DIM];
  size_t cells[DS_GRID_MAX_DIM];
  size_t cell_count;
  double **points; /* per cell */
  size_t **values;
  size_t *count;
  size_t *capacity;
  size_t size;
  ds_context_t *context;
} ds_fixed_grid_t;

/* cells: per-axis counts, each >= 1, product <= 2^24. */
ds_status_t ds_fixed_grid_init(ds_fixed_grid_t *g, size_t dim,
                               const double *domain, const size_t *cells,
                               ds_context_t *context);
void ds_fixed_grid_destroy(ds_fixed_grid_t *g);
size_t ds_fixed_grid_size(const ds_fixed_grid_t *g);
/* The cell (row-major index) a point maps to. */
size_t ds_fixed_grid_cell(const ds_fixed_grid_t *g, const double *point);
ds_status_t ds_fixed_grid_insert(ds_fixed_grid_t *g, const double *point,
                                 size_t value);
ds_status_t ds_fixed_grid_remove(ds_fixed_grid_t *g, const double *point,
                                 size_t value);
ds_status_t ds_fixed_grid_query(const ds_fixed_grid_t *g, const double *box,
                                ds_grid_visit_func_t visit, void *user);
ds_status_t ds_fixed_grid_nearest(const ds_fixed_grid_t *g, const double *point,
                                  size_t k, size_t *out_values,
                                  double *out_dist, size_t *out_found);

/* ---- grid file
 * ---------------------------------------------------------------- */

typedef struct ds_grid_file {
  size_t dim;
  size_t width; /* doubles per item: dim, or 2 * dim for boxes */
  size_t bucket;
  size_t max_cells;
  double *scale[DS_GRID_MAX_DIM]; /* sorted split values */
  size_t scale_count[DS_GRID_MAX_DIM];
  size_t scale_capacity[DS_GRID_MAX_DIM];
  size_t *dir; /* row-major, last axis fastest: bucket per cell */
  size_t dir_size;
  /* buckets */
  size_t *lo; /* per bucket: d cell indices (inclusive range) */
  size_t *hi;
  size_t *count; /* free buckets: next free */
  size_t *capacity;
  double **items;
  size_t **values;
  unsigned char *alive;
  unsigned *stamp;
  unsigned epoch;
  size_t buckets;
  size_t bucket_capacity;
  size_t free_list;
  size_t live_buckets;
  size_t size;
  ds_context_t *context;
} ds_grid_file_t;

/* bucket >= 2 (0: 16); max_cells bounds the directory (0: 2^22). */
ds_status_t ds_grid_file_init(ds_grid_file_t *g, size_t dim, size_t bucket,
                              size_t max_cells, ds_context_t *context);
void ds_grid_file_destroy(ds_grid_file_t *g);
size_t ds_grid_file_size(const ds_grid_file_t *g);
size_t ds_grid_file_buckets(const ds_grid_file_t *g);
size_t ds_grid_file_cells(const ds_grid_file_t *g);
ds_status_t ds_grid_file_insert(ds_grid_file_t *g, const double *point,
                                size_t value);
ds_status_t ds_grid_file_remove(ds_grid_file_t *g, const double *point,
                                size_t value);
/* Exact match: the first value stored at the point. */
ds_status_t ds_grid_file_find(const ds_grid_file_t *g, const double *point,
                              size_t *out_value);
ds_status_t ds_grid_file_query(ds_grid_file_t *g, const double *box,
                               ds_grid_visit_func_t visit, void *user);
ds_status_t ds_grid_file_nearest(const ds_grid_file_t *g, const double *point,
                                 size_t k, size_t *out_values, double *out_dist,
                                 size_t *out_found);
/* The region of the bucket holding the point (2d doubles, half-open). */
ds_status_t ds_grid_file_region(const ds_grid_file_t *g, const double *point,
                                double *out_region);
/* Directory, scales, regions and counts agree. */
bool ds_grid_file_valid(const ds_grid_file_t *g);

/* ---- twin grid file
 * ------------------------------------------------------------ */

typedef struct ds_twin_grid_file {
  ds_grid_file_t twin[2];
} ds_twin_grid_file_t;

ds_status_t ds_twin_grid_file_init(ds_twin_grid_file_t *g, size_t dim,
                                   size_t bucket, ds_context_t *context);
void ds_twin_grid_file_destroy(ds_twin_grid_file_t *g);
size_t ds_twin_grid_file_size(const ds_twin_grid_file_t *g);
size_t ds_twin_grid_file_buckets(const ds_twin_grid_file_t *g);
ds_status_t ds_twin_grid_file_insert(ds_twin_grid_file_t *g,
                                     const double *point, size_t value);
ds_status_t ds_twin_grid_file_remove(ds_twin_grid_file_t *g,
                                     const double *point, size_t value);
ds_status_t ds_twin_grid_file_query(ds_twin_grid_file_t *g, const double *box,
                                    ds_grid_visit_func_t visit, void *user);
ds_status_t ds_twin_grid_file_nearest(const ds_twin_grid_file_t *g,
                                      const double *point, size_t k,
                                      size_t *out_values, double *out_dist,
                                      size_t *out_found);

/* ---- two-level grid file
 * ------------------------------------------------------- */

typedef struct ds_grid_file2 {
  ds_grid_file_t top;    /* directory and regions only */
  ds_grid_file_t *pages; /* per top bucket */
  size_t page_capacity;
  size_t page_cells;
  size_t bucket;
  size_t size;
  ds_context_t *context;
} ds_grid_file2_t;

/* page_cells >= 4 (0: 256): directory cells per page. */
ds_status_t ds_grid_file2_init(ds_grid_file2_t *g, size_t dim, size_t bucket,
                               size_t page_cells, ds_context_t *context);
void ds_grid_file2_destroy(ds_grid_file2_t *g);
size_t ds_grid_file2_size(const ds_grid_file2_t *g);
size_t ds_grid_file2_pages(const ds_grid_file2_t *g);
ds_status_t ds_grid_file2_insert(ds_grid_file2_t *g, const double *point,
                                 size_t value);
ds_status_t ds_grid_file2_remove(ds_grid_file2_t *g, const double *point,
                                 size_t value);
ds_status_t ds_grid_file2_query(ds_grid_file2_t *g, const double *box,
                                ds_grid_visit_func_t visit, void *user);
bool ds_grid_file2_valid(const ds_grid_file2_t *g);

/* ---- multilayer grid file
 * ------------------------------------------------------- */

typedef struct ds_mlgrid_file {
  size_t dim;
  size_t bucket;
  size_t max_layers;
  ds_grid_file_t *layer;
  size_t layers;
  double *overflow; /* boxes fitting no layer */
  size_t *overflow_values;
  size_t overflow_count;
  size_t overflow_capacity;
  size_t size;
  ds_context_t *context;
} ds_mlgrid_file_t;

/* max_layers >= 1 (0: 8). */
ds_status_t ds_mlgrid_file_init(ds_mlgrid_file_t *g, size_t dim, size_t bucket,
                                size_t max_layers, ds_context_t *context);
void ds_mlgrid_file_destroy(ds_mlgrid_file_t *g);
size_t ds_mlgrid_file_size(const ds_mlgrid_file_t *g);
size_t ds_mlgrid_file_layers(const ds_mlgrid_file_t *g);
ds_status_t ds_mlgrid_file_insert(ds_mlgrid_file_t *g, const double *box,
                                  size_t value);
ds_status_t ds_mlgrid_file_remove(ds_mlgrid_file_t *g, const double *box,
                                  size_t value);
/* Boxes meeting the closed query box. */
ds_status_t ds_mlgrid_file_query(ds_mlgrid_file_t *g, const double *box,
                                 ds_grid_visit_func_t visit, void *user);
bool ds_mlgrid_file_valid(const ds_mlgrid_file_t *g);

#ifdef __cplusplus
}
#endif

#endif /* DS_GRID_FILE_H */
