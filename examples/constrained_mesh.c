/*
 * Constrained Delaunay triangulation of a room with a wall: the outline
 * and the wall are constraint segments, so no triangle crosses them, and
 * every other edge stays as Delaunay as the constraints allow. The wall
 * runs through a point on it, where it is split.
 */
#include "ds_algo.h"
#include <stdio.h>

int main(void) {
  /* a 6 x 4 room, a wall from (3, 0) up to (3, 3) passing (3, 1.5), and
   * scattered points inside */
  static const double xy[] = {0, 0,   6,   0,   6,   4,   0,   4,  3, 0,
                              3, 3,   3,   1.5, 1,   1,   2,   3,  4, 1,
                              5, 2.5, 1.5, 2,   4.5, 3.5, 2.2, 0.6};
  static const size_t walls[] = {0, 4, 4, 1, 1, 2, 2, 3, 3, 0, /* outline */
                                 4, 5};                        /* the wall */
  const size_t n = sizeof(xy) / sizeof(xy[0]) / 2;
  ds_delaunay_t dt;
  size_t inserted = 0;
  size_t t;
  size_t constrained = 0;
  if (ds_delaunay_build(&dt, xy, n, NULL) != DS_OK) {
    return 1;
  }
  printf("%lu points, %lu triangles\n", (unsigned long)n,
         (unsigned long)dt.triangles);
  if (ds_delaunay_constrain(&dt, xy, walls, 6, &inserted) != DS_OK) {
    return 1;
  }
  for (t = 0; t < dt.triangles; t++) {
    unsigned k;
    for (k = 0; k < 3; k++) {
      constrained += ds_delaunay_is_constrained(&dt, t, k) ? 1u : 0u;
    }
  }
  printf("%lu segments inserted; %lu triangle sides lie on them\n",
         (unsigned long)inserted, (unsigned long)constrained);
  for (t = 0; t < dt.triangles; t++) {
    const size_t *v = dt.tri + 3 * t;
    printf("  (%g %g) (%g %g) (%g %g)%s\n", xy[2 * v[0]], xy[2 * v[0] + 1],
           xy[2 * v[1]], xy[2 * v[1] + 1], xy[2 * v[2]], xy[2 * v[2] + 1],
           ds_delaunay_is_constrained(&dt, t, 0) ||
                   ds_delaunay_is_constrained(&dt, t, 1) ||
                   ds_delaunay_is_constrained(&dt, t, 2)
               ? "  on a wall"
               : "");
  }
  {
    /* a segment across the wall is refused */
    size_t across[2] = {7, 9};
    printf("a segment across the wall: %s\n",
           ds_delaunay_constrain(&dt, xy, across, 1, NULL) == DS_ERR_INVALID
               ? "refused"
               : "accepted?");
  }
  ds_delaunay_destroy(&dt);
  return 0;
}
