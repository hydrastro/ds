#ifndef DS_ALGO_GEOMETRY_H
#define DS_ALGO_GEOMETRY_H

/*
 * Computational geometry in the plane (and distances in any dimension).
 *
 * Orientation (ds_geo_orient) is the adaptive exact predicate of
 * robust_predicates.h: its sign is exact for coordinates in that header's
 * orient2d range, and its magnitude is approximate. Every decision built
 * on it is therefore exact: segment intersection, point in polygon,
 * hull membership, kernel side tests. Constructions are not: intersection
 * points, clipped vertices, areas, distances and calipers round as plain
 * double arithmetic does. Other geometric modules (geo_opt) still use
 * plain floating point.
 *
 *   distances      Euclidean, Manhattan, Chebyshev and Minkowski (L_p) in
 *                  d dimensions; Hausdorff distance between point sets
 *   predicates     orientation, segment intersection, point in polygon
 *                  (winding number, boundary detected), signed area
 *   convex hull    Andrew's monotone chain, Graham scan, Jarvis march and
 *                  quickhull behind one call; the hull vertices are the
 *                  extreme points
 *   calipers       diameter (farthest pair), width, and the minimum-area
 *                  enclosing rectangle (oriented bounding box); also the
 *                  axis-aligned bounding box
 *   closest pair   divide and conquer, O(n log n)
 *   polygons       kernel (star-shaped test) by half-plane clipping;
 *                  orthogonal convexity test of rectilinear polygons
 *   triangulation  optimal triangulation of a convex polygon under a
 *                  pluggable triangle weight (default: perimeter), O(n^3)
 *   slope select   k-th smallest slope among all point pairs by inversion
 *                  counting and bisection, O(n log n) per probe
 *   rasterizing    Bresenham lines and circles, Xiaolin Wu's antialiased
 *                  lines, through a plot callback
 *   bisector       perpendicular bisector of two points
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_point2 {
  double x;
  double y;
} ds_point2_t;

typedef enum ds_hull_algorithm {
  DS_HULL_ANDREW = 0,
  DS_HULL_GRAHAM = 1,
  DS_HULL_JARVIS = 2,
  DS_HULL_QUICKHULL = 3
} ds_hull_algorithm_t;

/* ---- distances ----------------------------------------------------------- */

double ds_geo_euclidean(const double *a, const double *b, size_t dim);
double ds_geo_manhattan(const double *a, const double *b, size_t dim);
double ds_geo_chebyshev(const double *a, const double *b, size_t dim);
/* L_p distance, p >= 1 (p = HUGE_VAL gives Chebyshev). */
double ds_geo_minkowski(const double *a, const double *b, size_t dim, double p);
/* Distance between two dim-dimensional points. */
typedef double (*ds_geo_metric_func_t)(const double *a, const double *b,
                                       size_t dim, void *user);
/* Hausdorff distance between point sets (row-major, dim columns), with a
 * pluggable metric (NULL: Euclidean). directed: only sup_a inf_b. */
double ds_geo_hausdorff(const double *a, size_t na, const double *b, size_t nb,
                        size_t dim, bool directed, ds_geo_metric_func_t metric,
                        void *user);

/* ---- predicates ---------------------------------------------------------- */

/* > 0 counter-clockwise, < 0 clockwise, 0 collinear; exact sign. */
double ds_geo_orient(ds_point2_t a, ds_point2_t b, ds_point2_t c);
/* Closed segments ab and cd share at least one point. */
bool ds_geo_segments_intersect(ds_point2_t a, ds_point2_t b, ds_point2_t c,
                               ds_point2_t d);
/* Twice the signed area (positive for counter-clockwise vertex order). */
double ds_geo_polygon_area2(const ds_point2_t *poly, size_t n);
/* 1 inside, 0 outside, -1 on the boundary. */
int ds_geo_point_in_polygon(const ds_point2_t *poly, size_t n, ds_point2_t p);
/* Perpendicular bisector of p and q as a x + b y = c. */
void ds_geo_bisector(ds_point2_t p, ds_point2_t q, double *out_a, double *out_b,
                     double *out_c);

/* ---- hulls and calipers -------------------------------------------------- */

/* Indices of the hull vertices in counter-clockwise order, starting at the
 * lowest-then-leftmost point, collinear points excluded. out_hull needs n
 * entries. */
ds_status_t ds_geo_convex_hull(const ds_point2_t *points, size_t n,
                               ds_hull_algorithm_t algorithm, size_t *out_hull,
                               size_t *out_count, ds_context_t *context);
/* On a convex polygon in counter-clockwise order (e.g. a hull). */
double ds_geo_convex_diameter(const ds_point2_t *poly, size_t n, size_t *out_i,
                              size_t *out_j);
double ds_geo_convex_width(const ds_point2_t *poly, size_t n);
/* Minimum-area enclosing rectangle: 4 corners counter-clockwise. */
double ds_geo_min_area_rect(const ds_point2_t *poly, size_t n,
                            ds_point2_t out_corners[4]);
void ds_geo_bounding_box(const ds_point2_t *points, size_t n,
                         ds_point2_t *out_min, ds_point2_t *out_max);
/* Closest pair of points (n >= 2): distance and indices. */
ds_status_t ds_geo_closest_pair(const ds_point2_t *points, size_t n,
                                double *out_distance, size_t *out_i,
                                size_t *out_j, ds_context_t *context);

/* ---- polygons ------------------------------------------------------------ */

/* Kernel of a simple polygon (counter-clockwise): the region from which
 * every point of the polygon is visible. Written to out (capacity n + 4);
 * an empty kernel (*out_count 0) means the polygon is not star-shaped. */
ds_status_t ds_geo_polygon_kernel(const ds_point2_t *poly, size_t n,
                                  ds_point2_t *out, size_t capacity,
                                  size_t *out_count, ds_context_t *context);
/* Rectilinear polygon whose every horizontal and vertical line meets it in
 * a connected set. */
bool ds_geo_orthogonally_convex(const ds_point2_t *poly, size_t n);

/* Weight of triangle (i, j, k) of a polygon. */
typedef double (*ds_geo_triangle_weight_func_t)(const ds_point2_t *poly,
                                                size_t i, size_t j, size_t k,
                                                void *user);
/* Optimal triangulation of a convex polygon: n - 2 triangles (3 indices
 * each) to out_triangles (may be NULL); minimum total weight returned in
 * *out_weight. weight NULL: perimeter. */
ds_status_t ds_geo_optimal_triangulation(const ds_point2_t *poly, size_t n,
                                         ds_geo_triangle_weight_func_t weight,
                                         void *user, size_t *out_triangles,
                                         double *out_weight,
                                         ds_context_t *context);

/* k-th smallest (0-based) finite slope among pairs with distinct x.
 * DS_ERR_RANGE when k is not below the number of such pairs. */
ds_status_t ds_geo_slope_selection(const ds_point2_t *points, size_t n,
                                   size_t k, double *out_slope,
                                   ds_context_t *context);

/* ---- rasterizing --------------------------------------------------------- */

typedef void (*ds_geo_plot_func_t)(long x, long y, void *user);
typedef void (*ds_geo_plot_aa_func_t)(long x, long y, double intensity,
                                      void *user);
void ds_geo_bresenham_line(long x0, long y0, long x1, long y1,
                           ds_geo_plot_func_t plot, void *user);
void ds_geo_bresenham_circle(long cx, long cy, long r, ds_geo_plot_func_t plot,
                             void *user);
void ds_geo_wu_line(double x0, double y0, double x1, double y1,
                    ds_geo_plot_aa_func_t plot, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_GEOMETRY_H */
