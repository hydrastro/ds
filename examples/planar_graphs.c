/*
 * Planarity: the cube graph is tested, embedded (its faces listed) and
 * drawn on an integer grid with straight edges; adding two diagonals
 * makes it non-planar, and the test returns a Kuratowski subdivision as
 * proof. Outerplanarity and a maximal planar subgraph of K6 round it off.
 */
#include "ds_algo.h"
#include <stdio.h>
#include <string.h>

#define MAXV 16
#define MAXE 64

/* both directions of every edge, as a CSR view */
static void make_view(size_t n, const size_t *u, const size_t *v, size_t m,
                      size_t *off, size_t *tg, ds_gview_csr_t *csr,
                      ds_gview_t *view) {
  size_t i;
  size_t cur[MAXV];
  memset(off, 0, (n + 1) * sizeof(size_t));
  for (i = 0; i < m; i++) {
    off[u[i] + 1]++;
    off[v[i] + 1]++;
  }
  for (i = 0; i < n; i++) {
    off[i + 1] += off[i];
    cur[i] = off[i];
  }
  for (i = 0; i < m; i++) {
    tg[cur[u[i]]++] = v[i];
    tg[cur[v[i]]++] = u[i];
  }
  csr->vertex_count = n;
  csr->offsets = off;
  csr->targets = tg;
  csr->weights = NULL;
  ds_gview_from_csr(csr, view);
}

int main(void) {
  /* the cube: vertices are 3-bit strings, edges flip one bit */
  size_t u[MAXE], v[MAXE], m = 0, i, b;
  size_t off[MAXV + 1], tg[2 * MAXE];
  size_t x[MAXV], y[MAXV], face[2 * MAXE], faces;
  size_t cu[MAXE], cv[MAXE], cn = 0;
  bool planar = false, outer = false, valid = false;
  bool keep[MAXE];
  size_t kept = 0;
  ds_gview_csr_t csr;
  ds_gview_t g;
  ds_planar_embedding_t e;
  ds_kuratowski_kind_t kind;
  for (i = 0; i < 8; i++) {
    for (b = 1; b < 8; b <<= 1) {
      if ((i ^ b) > i) {
        u[m] = i;
        v[m] = i ^ b;
        m++;
      }
    }
  }
  make_view(8, u, v, m, off, tg, &csr, &g);
  if (ds_planarity_test(&g, &planar, &e, NULL) != DS_OK || !planar) {
    return 1;
  }
  (void)ds_planar_embedding_check(&e, &valid);
  ds_planar_faces(&e, face, &faces);
  printf("cube: planar, %lu faces (V - E + F = %lu - %lu + %lu = 2), "
         "embedding %s\n",
         (unsigned long)faces, 8ul, (unsigned long)m, (unsigned long)faces,
         valid ? "verified" : "INVALID");
  for (b = 0; b < faces; b++) { /* each face as its cycle of vertices */
    size_t h0 = 0, h;
    while (face[h0] != b) {
      h0++;
    }
    printf("  face %lu:", (unsigned long)b);
    h = h0;
    do {
      printf(" %lu", (unsigned long)ds_planar_source(&e, h));
      h = ds_planar_next(&e, h);
    } while (h != h0);
    printf("\n");
  }
  if (ds_planar_draw(&e, x, y) == DS_OK) {
    char grid[7][13];
    size_t r, c;
    memset(grid, '.', sizeof(grid));
    for (i = 0; i < 8; i++) {
      grid[6 - y[i]][x[i]] = (char)('0' + i);
    }
    printf("straight-line drawing on the 13 x 7 grid (no edges cross):\n");
    for (r = 0; r < 7; r++) {
      printf("  ");
      for (c = 0; c < 13; c++) {
        putchar(grid[r][c]);
      }
      printf("\n");
    }
  }
  (void)ds_outerplanarity_test(&g, &outer, NULL, NULL);
  printf("outerplanar: %s\n", outer ? "yes"
                                    : "no (no drawing has every "
                                      "vertex on the outer face)");
  ds_planar_embedding_destroy(&e);

  /* two long diagonals of the cube: K3,3 appears */
  u[m] = 0;
  v[m] = 7;
  m++;
  u[m] = 1;
  v[m] = 6;
  m++;
  make_view(8, u, v, m, off, tg, &csr, &g);
  (void)ds_planarity_test(&g, &planar, NULL, NULL);
  if (!planar &&
      ds_planarity_kuratowski(&g, cu, cv, MAXE, &cn, &kind, NULL) == DS_OK) {
    printf("cube + 0-7 + 1-6: not planar; certificate: a subdivided %s with "
           "%lu edges:",
           kind == DS_KURATOWSKI_K5 ? "K5" : "K3,3", (unsigned long)cn);
    for (i = 0; i < cn; i++) {
      printf(" %lu-%lu", (unsigned long)cu[i], (unsigned long)cv[i]);
    }
    printf("\n");
  }

  /* K6 has 15 edges; a planar graph on 6 vertices at most 3 * 6 - 6 */
  m = 0;
  for (i = 0; i < 6; i++) {
    for (b = i + 1; b < 6; b++) {
      u[m] = i;
      v[m] = b;
      m++;
    }
  }
  if (ds_planarity_maximal_subgraph(6, u, v, m, keep, &kept, NULL) == DS_OK) {
    printf("maximal planar subgraph of K6: %lu of %lu edges kept; dropped:",
           (unsigned long)kept, (unsigned long)m);
    for (i = 0; i < m; i++) {
      if (!keep[i]) {
        printf(" %lu-%lu", (unsigned long)u[i], (unsigned long)v[i]);
      }
    }
    printf("\n");
  }
  return 0;
}
