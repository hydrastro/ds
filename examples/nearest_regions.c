/*
 * Nearest-site regions: a Voronoi diagram from the Delaunay triangulation.
 *
 * Each site's cell, cut to a box, is the region closer to it than to any
 * other site; the cells tile the box, so their areas add up to the box's.
 * The unclipped diagram gives the same cells with exact combinatorics:
 * bounded cells as rings of vertices, hull cells closed off by two rays.
 */
#include "ds_algo.h"
#include <stdio.h>

static double area(const double *p, size_t m) {
  double a = 0.0;
  size_t i;
  for (i = 0; i < m; i++) {
    const double *u = p + 2 * i;
    const double *w = p + 2 * ((i + 1) % m);
    a += u[0] * w[1] - u[1] * w[0];
  }
  return a / 2.0;
}

int main(void) {
  /* a few "facilities" in a 10 x 10 town, one of them listed twice */
  static const double sites[] = {1.0, 1.0, 8.5, 1.5, 5.0, 5.0, 2.0,
                                 8.0, 9.0, 9.0, 5.0, 2.5, 5.0, 5.0};
  const size_t n = sizeof(sites) / sizeof(sites[0]) / 2;
  const double box[4] = {0.0, 0.0, 10.0, 10.0};
  ds_voronoi_t vd;
  double poly[2 * 32];
  double total = 0.0;
  size_t s;
  if (ds_voronoi_build(&vd, sites, n, NULL) != DS_OK) {
    return 1;
  }
  printf("%lu sites, %lu Voronoi vertices, %lu edges\n", (unsigned long)n,
         (unsigned long)vd.vertices, (unsigned long)vd.edges);
  for (s = 0; s < n; s++) {
    size_t m = 0;
    if (ds_voronoi_cell_clip(&vd, sites, s, box, poly, 32, &m) != DS_OK) {
      continue;
    }
    if (vd.twin[s] != s) {
      printf("site %lu repeats site %lu: no region of its own\n",
             (unsigned long)s, (unsigned long)vd.twin[s]);
      continue;
    }
    printf("site %lu (%.1f, %.1f): %s cell, %lu corners in the box, area "
           "%.2f\n",
           (unsigned long)s, sites[2 * s], sites[2 * s + 1],
           vd.unbounded[s] ? "unbounded" : "bounded", (unsigned long)m,
           area(poly, m));
    total += area(poly, m);
  }
  printf("total %.2f of %.2f\n", total, (box[2] - box[0]) * (box[3] - box[1]));
  ds_voronoi_destroy(&vd);
  return 0;
}
