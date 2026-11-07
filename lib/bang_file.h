#ifndef DS_BANG_FILE_H
#define DS_BANG_FILE_H

/*
 * Bucket methods over the regular binary decomposition of a domain box in
 * d <= 8 dimensions: cells are halved cyclically through the axes, so a
 * cell is a bit-string prefix of the interleaved coordinates (each axis
 * quantised to 30 bits; points outside the domain fall into the border
 * cells). Every item carries a size_t value; coordinates must be finite;
 * a box is 2d doubles, low corner then high corner (closed).
 *
 * ds_excell_t, Tamminen's EXCELL (extendible cell) method: a directory of
 * 2^g cells indexed by the first g bits, buckets with a local depth that
 * split by halving, the directory doubling when a bucket of depth g
 * overflows (extendible hashing on the interleaved bits). Removal merges
 * buddy buckets and halves the directory when it can.
 *
 * ds_bang_file_t, Freeston's BANG file (balanced and nested grid): bucket
 * regions are cells that may nest, a point belongs to the innermost
 * region containing it, and an overflowing region carves out the
 * sub-cell holding closest to half of its points as a new nested region.
 * Underfull regions merge into their enclosing region.
 *
 * ds_rfile_t, the R-file of Hutflesz, Six and Widmayer for boxes: a box is
 * stored in the bucket of the smallest bucket cell enclosing it; an
 * overflowing bucket hands the boxes that fit in its fullest sub-cell to a
 * new bucket there, and boxes straddling the halving stay put.
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

#define DS_BANG_MAX_DIM 8
#define DS_BANG_BITS 30

/* item: a point (d doubles) or a box (2d doubles); false stops. */
typedef bool (*ds_bang_visit_func_t)(const double *item, size_t value,
                                     void *user);

/* ---- EXCELL ------------------------------------------------------------- */

typedef struct ds_excell {
  size_t dim;
  size_t bucket;
  size_t max_depth;
  double domain[2 * DS_BANG_MAX_DIM];
  size_t depth; /* global depth g */
  size_t *dir;  /* 2^g bucket ids */
  /* buckets */
  size_t *local;  /* local depth; free buckets: next free */
  size_t *prefix; /* first `local` bits */
  size_t *count;
  size_t *capacity;
  double **points;
  size_t **values;
  unsigned char *alive;
  size_t buckets;
  size_t bucket_capacity;
  size_t free_list;
  size_t live_buckets;
  size_t size;
  ds_context_t *context;
} ds_excell_t;

/* bucket >= 2 (0: 16); max_depth <= 28 bounds the directory (0: 20). */
ds_status_t ds_excell_init(ds_excell_t *e, size_t dim, const double *domain,
                           size_t bucket, size_t max_depth,
                           ds_context_t *context);
void ds_excell_destroy(ds_excell_t *e);
size_t ds_excell_size(const ds_excell_t *e);
size_t ds_excell_depth(const ds_excell_t *e);
size_t ds_excell_buckets(const ds_excell_t *e);
ds_status_t ds_excell_insert(ds_excell_t *e, const double *point, size_t value);
ds_status_t ds_excell_remove(ds_excell_t *e, const double *point, size_t value);
ds_status_t ds_excell_query(const ds_excell_t *e, const double *box,
                            ds_bang_visit_func_t visit, void *user);
ds_status_t ds_excell_nearest(const ds_excell_t *e, const double *point,
                              size_t k, size_t *out_values, double *out_dist,
                              size_t *out_found);
bool ds_excell_valid(const ds_excell_t *e);

/* ---- shared nested-cell trie ------------------------------------------------
 */

typedef struct ds_bang_base {
  size_t dim;
  size_t width; /* doubles per item */
  size_t bucket;
  double domain[2 * DS_BANG_MAX_DIM];
  /* trie nodes: cells */
  size_t *child; /* 2 per node */
  size_t *parent;
  size_t *depth;
  unsigned long *qlo; /* d per node: quantised low corner */
  size_t *node_bucket;
  size_t nodes;
  size_t node_capacity;
  size_t node_free;
  /* buckets */
  size_t *node; /* the bucket's cell; free buckets: next free */
  size_t *count;
  size_t *capacity;
  double **items;
  size_t **values;
  unsigned char *alive;
  size_t buckets;
  size_t bucket_capacity;
  size_t free_list;
  size_t live_buckets;
  size_t size;
  ds_context_t *context;
} ds_bang_base_t;

/* ---- BANG file
 * ------------------------------------------------------------------ */

typedef struct ds_bang_file {
  ds_bang_base_t base;
} ds_bang_file_t;

/* bucket >= 2 (0: 16). */
ds_status_t ds_bang_file_init(ds_bang_file_t *f, size_t dim,
                              const double *domain, size_t bucket,
                              ds_context_t *context);
void ds_bang_file_destroy(ds_bang_file_t *f);
size_t ds_bang_file_size(const ds_bang_file_t *f);
size_t ds_bang_file_regions(const ds_bang_file_t *f);
ds_status_t ds_bang_file_insert(ds_bang_file_t *f, const double *point,
                                size_t value);
ds_status_t ds_bang_file_remove(ds_bang_file_t *f, const double *point,
                                size_t value);
ds_status_t ds_bang_file_query(const ds_bang_file_t *f, const double *box,
                               ds_bang_visit_func_t visit, void *user);
ds_status_t ds_bang_file_nearest(const ds_bang_file_t *f, const double *point,
                                 size_t k, size_t *out_values, double *out_dist,
                                 size_t *out_found);
/* Every point sits in the bucket of its innermost region. */
bool ds_bang_file_valid(const ds_bang_file_t *f);

/* ---- R-file
 * ------------------------------------------------------------------------- */

typedef struct ds_rfile {
  ds_bang_base_t base;
} ds_rfile_t;

ds_status_t ds_rfile_init(ds_rfile_t *f, size_t dim, const double *domain,
                          size_t bucket, ds_context_t *context);
void ds_rfile_destroy(ds_rfile_t *f);
size_t ds_rfile_size(const ds_rfile_t *f);
size_t ds_rfile_buckets(const ds_rfile_t *f);
ds_status_t ds_rfile_insert(ds_rfile_t *f, const double *box, size_t value);
ds_status_t ds_rfile_remove(ds_rfile_t *f, const double *box, size_t value);
/* Boxes meeting the closed query box. */
ds_status_t ds_rfile_query(const ds_rfile_t *f, const double *box,
                           ds_bang_visit_func_t visit, void *user);
/* Every box sits in the smallest bucket cell enclosing it. */
bool ds_rfile_valid(const ds_rfile_t *f);

#ifdef __cplusplus
}
#endif

#endif /* DS_BANG_FILE_H */
