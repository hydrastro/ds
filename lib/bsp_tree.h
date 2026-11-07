#ifndef DS_BSP_TREE_H
#define DS_BSP_TREE_H

/*
 * Binary space partitions by arbitrary hyperplanes.
 *
 * ds_bsp_tree_t, the BSP-tree for points in d <= 8 dimensions: a binary
 * tree whose nodes split space by a hyperplane n.x = c (n.x < c goes to the
 * negative side). Full leaf buckets split along the principal direction of
 * their points (power iteration on the covariance) at the median
 * projection, or along a caller policy. Box queries prune with the
 * hyperplanes; nearest neighbours use the distances to the half-spaces on
 * the path as lower bounds.
 *
 * ds_cell_tree_t, Guenther's cell tree for polygons in the plane: polygons
 * (simple, possibly concave) are decomposed into convex pieces (triangles,
 * by ear clipping), and each piece is indexed in every cell of a line
 * partition that it overlaps. Point queries report the polygons containing
 * a point; window queries the polygons meeting a box (each once).
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

#define DS_BSP_MAX_DIM 8

typedef bool (*ds_bsp_visit_func_t)(const double *point, size_t value,
                                    void *user);

/* Choose a hyperplane for the n points (n x d, row-major): write the normal
 * (d doubles) and offset; scratch holds n doubles for the policy's use.
 * false declines (the leaf stays oversized). */
typedef bool (*ds_bsp_plane_func_t)(const double *points, size_t n, size_t dim,
                                    double *scratch, double *normal,
                                    double *offset, void *user);

typedef struct ds_bsp_tree {
  size_t dim;
  size_t bucket;
  ds_bsp_plane_func_t plane;
  void *plane_user;
  double *normal; /* per node: d */
  double *offset;
  size_t *neg; /* (size_t)-1 for a leaf */
  size_t *pos;
  size_t *parent;
  double **points;
  size_t **values;
  size_t *count;
  size_t *capacity;
  size_t nodes;
  size_t node_capacity;
  size_t free_list;
  size_t root;
  size_t size;
  ds_context_t *context;
} ds_bsp_tree_t;

/* bucket >= 2 (0: 16); plane NULL: principal direction at the median. */
ds_status_t ds_bsp_tree_init(ds_bsp_tree_t *t, size_t dim, size_t bucket,
                             ds_bsp_plane_func_t plane, void *plane_user,
                             ds_context_t *context);
void ds_bsp_tree_destroy(ds_bsp_tree_t *t);
size_t ds_bsp_tree_size(const ds_bsp_tree_t *t);
size_t ds_bsp_tree_depth(const ds_bsp_tree_t *t);
ds_status_t ds_bsp_tree_insert(ds_bsp_tree_t *t, const double *point,
                               size_t value);
ds_status_t ds_bsp_tree_remove(ds_bsp_tree_t *t, const double *point,
                               size_t value);
ds_status_t ds_bsp_tree_query(const ds_bsp_tree_t *t, const double *box,
                              ds_bsp_visit_func_t visit, void *user);
ds_status_t ds_bsp_tree_nearest(const ds_bsp_tree_t *t, const double *point,
                                size_t k, size_t *out_values, double *out_dist,
                                size_t *out_found);
bool ds_bsp_tree_valid(const ds_bsp_tree_t *t);

/* The default policy: principal direction, median offset. */
bool ds_bsp_principal_plane(const double *points, size_t n, size_t dim,
                            double *scratch, double *normal, double *offset,
                            void *user);

/* ---- cell tree ------------------------------------------------------------
 */

/* polygon value; false stops */
typedef bool (*ds_cell_visit_func_t)(size_t value, void *user);

typedef struct ds_cell_tree {
  size_t bucket;
  size_t max_depth;
  /* polygons */
  double **poly; /* 2 * vertex count */
  size_t *poly_n;
  size_t *poly_first; /* its pieces: [first, first + n - 2) */
  size_t *poly_value;
  unsigned char *poly_alive;
  unsigned *poly_stamp;
  size_t polys;
  size_t poly_capacity;
  size_t poly_free;
  /* convex pieces (triangles), appended per polygon */
  double *piece; /* 6 per piece */
  size_t *piece_poly;
  size_t pieces;
  size_t piece_capacity;
  size_t live_pieces;
  /* partition */
  double *line; /* per node: a, b, c of a x + b y < c */
  size_t *neg;
  size_t *pos;
  size_t *depth;
  size_t **items; /* leaf: pieces */
  size_t *item_count;
  size_t *item_capacity;
  size_t nodes;
  size_t node_capacity;
  size_t root;
  size_t size;
  unsigned epoch;
  ds_context_t *context;
} ds_cell_tree_t;

/* bucket >= 2 pieces per cell (0: 8); max_depth (0: 32). */
ds_status_t ds_cell_tree_init(ds_cell_tree_t *t, size_t bucket,
                              size_t max_depth, ds_context_t *context);
void ds_cell_tree_destroy(ds_cell_tree_t *t);
size_t ds_cell_tree_size(const ds_cell_tree_t *t);
size_t ds_cell_tree_pieces(const ds_cell_tree_t *t);
/* xy: n >= 3 vertices (x0, y0, x1, y1, ...) of a simple polygon, either
 * orientation. The handle identifies it for removal. */
ds_status_t ds_cell_tree_insert(ds_cell_tree_t *t, const double *xy, size_t n,
                                size_t value, size_t *out_handle);
ds_status_t ds_cell_tree_remove(ds_cell_tree_t *t, size_t handle);
/* Polygons containing (x, y) (boundary included), each once. */
ds_status_t ds_cell_tree_query_point(ds_cell_tree_t *t, double x, double y,
                                     ds_cell_visit_func_t visit, void *user);
/* Polygons meeting the closed box (x0, y0, x1, y1), each once. */
ds_status_t ds_cell_tree_query_box(ds_cell_tree_t *t, const double *box,
                                   ds_cell_visit_func_t visit, void *user);

/* Triangulate a simple polygon by ear clipping: out receives (n - 2)
 * triangles as vertex indices (3 per triangle). DS_ERR_INVALID when the
 * polygon is degenerate. */
ds_status_t ds_cell_tree_triangulate(const double *xy, size_t n, size_t *out,
                                     ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_BSP_TREE_H */
