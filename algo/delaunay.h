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
 *
 * Constraints: ds_delaunay_constrain then inserts segments between input
 * points, one at a time, by Sloan's method ("A fast algorithm for
 * generating constrained Delaunay triangulations", 1993). The edges a
 * segment crosses are found by a walk from its first end; each is flipped
 * once its quadrilateral is strictly convex (one always is), new edges
 * that still cross going back in the queue, until the segment is an edge;
 * then the new edges are flipped back to locally Delaunay. Flips need no
 * special case for any configuration of crossed triangles, such as a
 * vertex they surround. The result is the constrained Delaunay
 * triangulation:
 * every segment is a union of edges, and no triangle's circumcircle holds
 * a point visible from its inside (every unconstrained edge is locally
 * Delaunay). A segment through other input points is split at them.
 * Segments may share ends but must not cross: crossing ones are refused.
 * The triangle count does not change; flags mark constrained edges.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
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
  /* 3 per triangle: the edge opposite vertex k is (part of) a constraint;
   * NULL until ds_delaunay_constrain */
  unsigned char *fixed;
  ds_context_t *context;
} ds_delaunay_t;

/* xy holds n points as (x, y) pairs; DS_ERR_INVALID for a non-finite
 * coordinate */
ds_status_t ds_delaunay_build(ds_delaunay_t *dt, const double *xy, size_t n,
                              ds_context_t *context);
void ds_delaunay_destroy(ds_delaunay_t *dt);
/* insert count segments, each a pair of point indices into the xy the
 * triangulation was built from (a duplicate point stands for its earlier
 * equal), in order. DS_ERR_INVALID when a segment crosses an earlier one,
 * DS_ERR_RANGE for an index out of range, DS_ERR_STATE without
 * triangles; *inserted (optional) gets how many segments were inserted
 * before a failure, and the triangulation stays valid and constrained
 * Delaunay for those. A segment between equal points is ignored. */
ds_status_t ds_delaunay_constrain(ds_delaunay_t *dt, const double *xy,
                                  const size_t *segments, size_t count,
                                  size_t *inserted);
/* whether the edge of triangle t opposite its vertex k is constrained */
bool ds_delaunay_is_constrained(const ds_delaunay_t *dt, size_t t, unsigned k);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_DELAUNAY_H */
