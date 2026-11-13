#ifndef DS_ALGO_VORONOI_H
#define DS_ALGO_VORONOI_H

/*
 * Voronoi diagrams of points in the plane, as the dual of the Delaunay
 * triangulation of algo/delaunay.
 *
 * Every Delaunay triangle gives a Voronoi vertex, its circumcenter, and
 * every Delaunay edge a Voronoi edge: a segment between the circumcenters
 * of the two triangles that share it, or for a convex-hull edge a ray from
 * the circumcenter of its triangle, perpendicular to the edge and away
 * from the triangle. The cell of a site lists the vertices of the
 * triangles around it in counter-clockwise order; the cell of a hull site
 * is unbounded, closed off by two rays (cell_ray) from its first and last
 * vertex. The combinatorics come from the exact predicates behind the
 * triangulation. Vertex coordinates are floating point, with the
 * orientation determinant in their denominator evaluated exactly, so a
 * nearly flat triangle gives a far-away vertex placed about right rather
 * than an arbitrary one; cocircular sites give coinciding vertices
 * (zero-length edges).
 *
 * Special inputs. A site equal to an earlier one has an empty cell and
 * twin[s] names that earlier site (twin[s] == s otherwise). When the
 * distinct sites are collinear there are no vertices: the edges are whole
 * lines (both vertex ends DS_VORONOI_NONE) between neighbours along the
 * line, and the cells are empty lists, available bounded only through
 * ds_voronoi_cell_clip.
 *
 * ds_voronoi_cell_clip intersects a cell with a box by clipping the box
 * against the bisector of the site and each Delaunay neighbour: a convex
 * polygon of at most 4 + neighbours vertices, for drawing, areas or
 * nearest-site regions. It works from the sites alone (not the computed
 * vertices), so it is as accurate for far-away vertices as for near ones,
 * and the clipped cells tile the box; where several bisectors cross at
 * one point (cocircular sites) the polygon may repeat that point.
 *
 * Memory comes from the context.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include "delaunay.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_VORONOI_NONE (~(size_t)0)

typedef struct ds_voronoi_edge {
  /* the sites it separates: walking from origin along dir, site[0] is on
   * the right and site[1] on the left */
  size_t site[2];
  /* its ends; vertex[1] is DS_VORONOI_NONE for a ray, both for a line */
  size_t vertex[2];
  double origin[2]; /* vertex[0], or a point of a line */
  double dir[2];    /* towards vertex[1], or the way to infinity */
} ds_voronoi_edge_t;

typedef struct ds_voronoi {
  size_t sites;
  size_t vertices; /* one per Delaunay triangle */
  double *vertex;  /* x, y per vertex */
  size_t edges;
  ds_voronoi_edge_t *edge;
  size_t *cell_start;  /* sites + 1 */
  size_t *cell_vertex; /* site s: [cell_start[s], cell_start[s + 1]), CCW */
  bool *unbounded;     /* per site */
  /* 4 per site: for an unbounded cell the directions of the rays from its
   * first vertex and from its last vertex (zero otherwise) */
  double *cell_ray;
  size_t *twin;      /* per site: itself, or the earlier equal site */
  size_t *nbr_start; /* sites + 1: Delaunay neighbours in nbr */
  size_t *nbr;
  ds_context_t *context;
} ds_voronoi_t;

/* triangulates xy (n points as x, y pairs) and builds the diagram */
ds_status_t ds_voronoi_build(ds_voronoi_t *vd, const double *xy, size_t n,
                             ds_context_t *context);
/* the same from a triangulation of xy built by ds_delaunay_build;
 * DS_ERR_INVALID if its adjacency is inconsistent */
ds_status_t ds_voronoi_from_delaunay(ds_voronoi_t *vd, const ds_delaunay_t *dt,
                                     const double *xy, ds_context_t *context);
void ds_voronoi_destroy(ds_voronoi_t *vd);

/* The cell of site s cut to the box x0 <= x <= x1, y0 <= y <= y1 (box =
 * {x0, y0, x1, y1}), counter-clockwise, as out[2i], out[2i + 1]; *count
 * vertices (0 for an empty cell or a box it misses). DS_ERR_RANGE with
 * *count set to the room needed when cap (in vertices) is short;
 * DS_ERR_INVALID for an empty or non-finite box. xy are the sites. */
ds_status_t ds_voronoi_cell_clip(const ds_voronoi_t *vd, const double *xy,
                                 size_t s, const double box[4], double *out,
                                 size_t cap, size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_VORONOI_H */
