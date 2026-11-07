#ifndef DS_KDB_TREE_H
#define DS_KDB_TREE_H

/*
 * k-d tree relatives for points and boxes in d <= 8 dimensions, every item
 * carrying a size_t value. Coordinates must be finite. A box is 2d doubles:
 * the low corner followed by the high corner (closed).
 *
 * ds_kdb_tree_t, Robinson's k-d-B-tree: a balanced, paged k-d tree. Region
 * pages hold up to `fanout` disjoint regions that partition the page's own
 * region; point pages hold up to `bucket` points. An overflowing point
 * page splits at a median; an overflowing region page splits along the
 * best hyperplane through its regions, forcing the split down into every
 * child region it cuts. Pages may become underfull; all point pages stay
 * at the same depth. Points with equal coordinates that outnumber a bucket
 * share an oversized page.
 *
 * ds_akd_tree_t, the adaptive (optimised) k-d tree of Friedman, Bentley and
 * Finkel, built in bulk: each node splits along a data-driven axis (largest
 * spread or variance, or cyclic) at a chosen cut (median, midpoint of the
 * extent, or sliding midpoint), and points live in leaf buckets.
 *
 * ds_ekd_tree_t, the extended k-d tree (Matsuyama et al.) for boxes: cells
 * split cyclically through the axes, and a box is stored in every leaf
 * cell it meets; queries report each box once.
 *
 * ds_skd_tree_t, the spatial k-d tree (skd-tree) of Ooi et al. for boxes:
 * boxes are indexed by their centroids in a bucket k-d tree, and each node
 * keeps the bounding box of everything below it, so no box is duplicated.
 *
 * Regions and cells are half-open, [lo, hi), and the root covers all of
 * space. Recursion is bounded by the tree height. Memory comes from the
 * ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_KDB_MAX_DIM 8

/* item: a point (d doubles) or a box (2d doubles). false stops. */
typedef bool (*ds_kdb_visit_func_t)(const double *item, size_t value,
                                    void *user);

typedef enum ds_kdb_axis_rule {
  DS_KDB_AXIS_SPREAD = 0, /* largest extent of the coordinates */
  DS_KDB_AXIS_VARIANCE,   /* largest variance */
  DS_KDB_AXIS_CYCLIC      /* depth mod d */
} ds_kdb_axis_rule_t;

typedef enum ds_kdb_cut_rule {
  DS_KDB_CUT_MEDIAN = 0,      /* the median coordinate */
  DS_KDB_CUT_MIDPOINT,        /* the middle of the coordinates' extent */
  DS_KDB_CUT_SLIDING_MIDPOINT /* midpoint, slid to the nearest point when
                                 one side would be empty */
} ds_kdb_cut_rule_t;

/* ---- k-d-B-tree ------------------------------------------------------- */

typedef struct ds_kdb_page {
  size_t count;
  size_t capacity;
  double *data; /* region page: count * 2d regions; point page: count * d */
  size_t *ref;  /* region page: child pages; point page: values */
  unsigned char leaf;
  unsigned char axis; /* next axis of a cyclic point page split */
} ds_kdb_page_t;

typedef struct ds_kdb_tree {
  size_t dim;
  size_t bucket;
  size_t fanout;
  ds_kdb_axis_rule_t axis_rule;
  ds_kdb_page_t *pages;
  size_t page_count;
  size_t page_capacity;
  size_t free_list;
  size_t root;
  size_t size;
  size_t height;
  size_t *path; /* scratch: page, slot pairs */
  size_t path_capacity;
  ds_context_t *context;
} ds_kdb_tree_t;

/* bucket >= 2 (0: 16), fanout >= 3 (0: 16); axis_rule SPREAD or CYCLIC
 * (VARIANCE is treated as SPREAD). */
ds_status_t ds_kdb_tree_init(ds_kdb_tree_t *t, size_t dim, size_t bucket,
                             size_t fanout, ds_kdb_axis_rule_t axis_rule,
                             ds_context_t *context);
void ds_kdb_tree_destroy(ds_kdb_tree_t *t);
size_t ds_kdb_tree_size(const ds_kdb_tree_t *t);
size_t ds_kdb_tree_height(const ds_kdb_tree_t *t);
size_t ds_kdb_tree_pages(const ds_kdb_tree_t *t);
ds_status_t ds_kdb_tree_insert(ds_kdb_tree_t *t, const double *point,
                               size_t value);
/* Removes one (point, value) entry; DS_NOT_FOUND if absent. */
ds_status_t ds_kdb_tree_remove(ds_kdb_tree_t *t, const double *point,
                               size_t value);
/* Does the point occur (with any value)? */
bool ds_kdb_tree_contains(const ds_kdb_tree_t *t, const double *point);
/* Points inside the closed box. */
ds_status_t ds_kdb_tree_query(const ds_kdb_tree_t *t, const double *box,
                              ds_kdb_visit_func_t visit, void *user);
/* The k nearest points (Euclidean), closest first. */
ds_status_t ds_kdb_tree_nearest(const ds_kdb_tree_t *t, const double *point,
                                size_t k, size_t *out_values, double *out_dist,
                                size_t *out_found);
/* Regions are disjoint and nested, points lie in their regions, the
 * leaves share one depth, and the count matches. */
bool ds_kdb_tree_valid(const ds_kdb_tree_t *t);

/* ---- adaptive k-d tree ------------------------------------------------- */

typedef struct ds_akd_tree {
  size_t dim;
  size_t count;
  size_t nodes;
  size_t depth;   /* edges on the longest root-to-leaf path */
  double *points; /* reordered: count * d */
  size_t *values; /* reordered */
  size_t *axis;   /* per node; leaves: (size_t)-1 */
  double *cut;    /* per node: left < cut <= right */
  size_t *left;   /* internal: children; leaf: first point */
  size_t *right;  /* leaf: point count */
  double *bounds; /* per node: tight 2d bounding box of its points */
  ds_context_t *context;
} ds_akd_tree_t;

/* values may be NULL (the value is then the point's index); bucket >= 1
 * (0: 8). */
ds_status_t ds_akd_tree_build(ds_akd_tree_t *t, size_t dim,
                              const double *points, const size_t *values,
                              size_t n, size_t bucket,
                              ds_kdb_axis_rule_t axis_rule,
                              ds_kdb_cut_rule_t cut_rule,
                              ds_context_t *context);
void ds_akd_tree_destroy(ds_akd_tree_t *t);
size_t ds_akd_tree_depth(const ds_akd_tree_t *t);
/* Points inside the closed box. */
ds_status_t ds_akd_tree_query(const ds_akd_tree_t *t, const double *box,
                              ds_kdb_visit_func_t visit, void *user);
/* The k nearest points (Euclidean), closest first. */
ds_status_t ds_akd_tree_nearest(const ds_akd_tree_t *t, const double *point,
                                size_t k, size_t *out_values, double *out_dist,
                                size_t *out_found);

/* ---- boxes: shared object table -------------------------------------- */

typedef struct ds_kdb_objects {
  double *box; /* per id: 2d */
  size_t *value;
  unsigned char *alive;
  size_t count;
  size_t capacity;
  size_t free_list;
} ds_kdb_objects_t;

/* ---- extended k-d tree ----------------------------------------------------
 */

typedef struct ds_ekd_tree {
  size_t dim;
  size_t bucket;
  size_t max_depth;
  ds_kdb_objects_t objects;
  size_t *axis; /* per node; leaves: (size_t)-1 */
  double *cut;
  size_t *left;
  size_t *right;
  size_t **items; /* leaf buckets of object ids */
  size_t *item_count;
  size_t *item_capacity;
  size_t nodes;
  size_t node_capacity;
  size_t size;
  size_t entries; /* stored ids, duplicates included */
  size_t *stack;
  double *cells;
  size_t stack_capacity;
  ds_context_t *context;
} ds_ekd_tree_t;

/* bucket >= 1 (0: 8); max_depth bounds splitting (0: 32). */
ds_status_t ds_ekd_tree_init(ds_ekd_tree_t *t, size_t dim, size_t bucket,
                             size_t max_depth, ds_context_t *context);
void ds_ekd_tree_destroy(ds_ekd_tree_t *t);
size_t ds_ekd_tree_size(const ds_ekd_tree_t *t);
size_t ds_ekd_tree_entries(const ds_ekd_tree_t *t);
ds_status_t ds_ekd_tree_insert(ds_ekd_tree_t *t, const double *box,
                               size_t value);
ds_status_t ds_ekd_tree_remove(ds_ekd_tree_t *t, const double *box,
                               size_t value);
/* Boxes meeting the closed query box, each once. */
ds_status_t ds_ekd_tree_query(ds_ekd_tree_t *t, const double *box,
                              ds_kdb_visit_func_t visit, void *user);

/* ---- skd-tree ------------------------------------------------------------ */

typedef struct ds_skd_tree {
  size_t dim;
  size_t bucket;
  ds_kdb_objects_t objects;
  size_t *axis; /* per node; leaves: (size_t)-1 */
  double *cut;  /* centroid < cut goes left */
  size_t *left;
  size_t *right;
  size_t *parent;
  double *bounds; /* per node: 2d box of its objects (empty: lo > hi) */
  size_t **items;
  size_t *item_count;
  size_t *item_capacity;
  size_t nodes;
  size_t node_capacity;
  size_t free_list;
  size_t root;
  size_t size;
  size_t *stack;
  size_t stack_capacity;
  ds_context_t *context;
} ds_skd_tree_t;

/* bucket >= 2 (0: 8). */
ds_status_t ds_skd_tree_init(ds_skd_tree_t *t, size_t dim, size_t bucket,
                             ds_context_t *context);
void ds_skd_tree_destroy(ds_skd_tree_t *t);
size_t ds_skd_tree_size(const ds_skd_tree_t *t);
ds_status_t ds_skd_tree_insert(ds_skd_tree_t *t, const double *box,
                               size_t value);
ds_status_t ds_skd_tree_remove(ds_skd_tree_t *t, const double *box,
                               size_t value);
ds_status_t ds_skd_tree_query(ds_skd_tree_t *t, const double *box,
                              ds_kdb_visit_func_t visit, void *user);
/* Boxes containing the point. */
ds_status_t ds_skd_tree_query_point(ds_skd_tree_t *t, const double *point,
                                    ds_kdb_visit_func_t visit, void *user);
/* k nearest boxes by box distance, closest first. */
ds_status_t ds_skd_tree_nearest(ds_skd_tree_t *t, const double *point, size_t k,
                                size_t *out_values, double *out_dist,
                                size_t *out_found);
/* Bounds are exact, centroids follow the cuts, the count matches. */
bool ds_skd_tree_valid(const ds_skd_tree_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DS_KDB_TREE_H */
