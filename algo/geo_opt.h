#ifndef DS_ALGO_GEO_OPT_H
#define DS_ALGO_GEO_OPT_H

/*
 * Geometric optimisation problems: optimisation problems induced by a
 * collection of geometric objects, where the geometry admits algorithms
 * faster than general optimisation. Points are (x, y) pairs in a double
 * array.
 *
 *   ds_geo_min_circle       the smallest enclosing circle (Welzl's
 *                           randomised incremental algorithm, O(n)
 *                           expected; the shuffle is seeded)
 *   ds_geo_center_on_line   the smallest enclosing circle whose centre lies
 *                           on a given line, by Megiddo's prune and search:
 *                           each round pairs the points, finds the median
 *                           of the pairs' equidistant points, decides on
 *                           which side the optimum lies and discards a
 *                           quarter of the points; O(n)
 *   ds_geo_lp2              two-variable linear programming (maximise
 *                           cx x + cy y subject to a x + b y <= c) by
 *                           Seidel's randomised incremental algorithm,
 *                           O(n) expected, reporting infeasible and
 *                           unbounded programs
 *   ds_geo_fermat_weber     the point minimising the weighted sum of
 *                           distances (Weiszfeld's iteration with the
 *                           Vardi-Zhang correction at data points)
 *
 * Memory comes from the ds_context_t.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

ds_status_t ds_geo_min_circle(const double *xy, size_t n, unsigned long seed,
                              double *out_cx, double *out_cy, double *out_r,
                              ds_context_t *context);
/* The line through (px, py) with direction (dx, dy). */
ds_status_t ds_geo_center_on_line(const double *xy, size_t n, double px,
                                  double py, double dx, double dy,
                                  double *out_cx, double *out_cy, double *out_r,
                                  ds_context_t *context);
/* abc: m constraints a x + b y <= c (3 doubles each). DS_NOT_FOUND when
 * infeasible, DS_ERR_RANGE when unbounded (out gets a feasible point far
 * along the improving direction). */
ds_status_t ds_geo_lp2(const double *abc, size_t m, double cx, double cy,
                       unsigned long seed, double *out_x, double *out_y,
                       ds_context_t *context);
/* weights may be NULL (all 1); iterations (0: 1000); tolerance (0: 1e-12)
 * on the step length. */
ds_status_t ds_geo_fermat_weber(const double *xy, const double *weights,
                                size_t n, size_t iterations, double tolerance,
                                double *out_x, double *out_y);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_GEO_OPT_H */
