#ifndef DS_VISIBILITY_H
#define DS_VISIBILITY_H

/*
 * Visibility maps of a set of non-crossing line segments (they may share
 * endpoints or touch): from every endpoint, rays are shot in both
 * directions along an axis until they hit a segment. The vertical
 * visibility map (vertical rays up and down; the trapezoidal map's
 * vertical extensions) and the horizontal visibility map (rays right and
 * left) are computed by a plane sweep in O(n log n) expected time.
 *
 * The sweep status is a treap ordered bottom to top, where non-crossing
 * segments are strictly ordered (ties between collinear overlapping
 * pieces fall back to the index); queries at an event coordinate run
 * against the status both before the segments ending there leave and
 * after the segments starting there arrive.
 *
 * Numerics: every ordering decision (one segment below another, a segment
 * above or below a ray's origin) is the exact sign of an orientation test
 * (algo/robust_predicates, within its orient2d input range), so nearly
 * touching segments are never misordered by rounding. The reported hit
 * coordinates (up_at, down_at) are interpolated in floating point and
 * round; segments degenerate in the sweep direction are compared by
 * coordinates.
 *
 * Segments merely touching the ray's origin are not hits. Segments along
 * the ray direction (vertical ones in the vertical map) are hit at their
 * nearer endpoint when it lies strictly beyond the origin and are ignored
 * when they contain the origin; for such a segment's own endpoints the
 * ray that runs along it reports the segment itself, hit at its other
 * endpoint.
 *
 * Results that depend on crossing segments are unspecified.
 */

#include "context.h"
#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_VISIBILITY_NONE (~(size_t)0)

typedef enum ds_visibility_axis {
  DS_VISIBILITY_VERTICAL,  /* rays towards +y ("up") and -y */
  DS_VISIBILITY_HORIZONTAL /* rays towards +x ("up") and -x */
} ds_visibility_axis_t;

typedef struct ds_visibility_segment {
  double x1;
  double y1;
  double x2;
  double y2;
} ds_visibility_segment_t;

typedef struct ds_visibility_ray {
  size_t up;    /* segment hit by the ray in the positive direction */
  double up_at; /* coordinate along the ray axis where it is hit */
  size_t down;  /* likewise in the negative direction */
  double down_at;
} ds_visibility_ray_t;

/* out has 2 * count entries: out[2 i] for (x1, y1) of segment i,
 * out[2 i + 1] for (x2, y2). DS_ERR_INVALID for a zero-length segment or a
 * non-finite coordinate. */
ds_status_t ds_visibility_map(const ds_visibility_segment_t *segments,
                              size_t count, ds_visibility_axis_t axis,
                              ds_visibility_ray_t *out, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_VISIBILITY_H */
