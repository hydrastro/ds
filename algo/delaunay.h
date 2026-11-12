#ifndef DS_ALGO_DELAUNAY_H
#define DS_ALGO_DELAUNAY_H

/*
 * Delaunay triangulation of points in the plane, built on the exact
 * predicates of algo/robust_predicates.
 *
 * Points are inserted one at a time (Bowyer-Watson): the triangles whose
 * circumcircle contains the new point form a star-shaped cavity, found by
 * a search from the triangle containing the point, and the cavity is
 * replaced by a fan around the point. The convex hull is handled with
 * ghost triangles (one per hull edge, joined to a vertex at infinity), so
 * points outside the hull need no bounding super-triangle. Every decision
 * (orientation of a point against an edge, a point inside a circumcircle)
 * is the exact sign of orient2d or incircle, within their documented input
 * ranges, so degenerate and nearly degenerate inputs give a valid
 * triangulation: no flipped or overlapping triangles, every triangle's
 * circumcircle empty of other input points (cocircular points may be
 * triangulated either way).
 *
 * Insertion order follows a Hilbert curve over the bounding box (via
 * algo/space_order) and the containing triangle is found by a walk from the
 * previous insertion, which keeps walks short on typical inputs: about
 * O(n log n) time in practice; a step bound falls back to a scan, so no
 * input can make a walk loop.
 *
 * Duplicates: a point equal to an earlier one is not inserted and belongs
 * to no triangle (ds_delaunay_t.duplicates counts them). Fewer than three
 * distinct points, or all of them collinear, give zero triangles.
 *
 * Output: triangles as vertex index triples in counter-clockwise order,
 * and for each triangle the neighbouring triangle across the edge opposite
 * each vertex (DS_DELAUNAY_NONE on the convex hull). Memory comes from the
 * context.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_DELAUNAY_NONE (~(size_t)0)

typedef struct ds_delaunay {
  size_t triangles;  /* finite triangles */
  size_t *tri;       /* 3 vertex indices per triangle, counter-clockwise */
  size_t *adj;       /* 3 per triangle: across the edge opposite vertex k */
  size_t points;     /* input points */
  size_t duplicates; /* points equal to an earlier point, not inserted */
  ds_context_t *context;
} ds_delaunay_t;

/* xy holds n points as (x, y) pairs; DS_ERR_INVALID for a non-finite
 * coordinate */
ds_status_t ds_delaunay_build(ds_delaunay_t *dt, const double *xy, size_t n,
                              ds_context_t *context);
void ds_delaunay_destroy(ds_delaunay_t *dt);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_DELAUNAY_H */
