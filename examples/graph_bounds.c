/*
 * Semidefinite programs as bounds for hard graph problems. Lovasz's theta
 * lies between the independence number and the clique cover number, and
 * the exact searches of algo/graph_np show where in that window it falls.
 * Goemans and Williamson's relaxation bounds the maximum cut, and random
 * hyperplanes through its vectors turn it into a cut. Last, a small
 * semidefinite program read from the SDPA format, with its certificate.
 */
#include "ds.h"
#include "ds_algo.h"
#include <stdio.h>

#define MAXN 16

static const char *example = "\"SDPA example 1\"\n"
                             "3 = m\n"
                             "1 = blocks\n"
                             "2 = block sizes\n"
                             "{48, -8, 20}\n"
                             "0 1 1 1 -11\n0 1 2 2 23\n1 1 1 1 10\n1 1 1 2 4\n"
                             "2 1 2 2 -8\n3 1 1 2 -8\n3 1 2 2 -2\n";

static void bounds(const char *name, size_t n, const size_t *u, const size_t *v,
                   size_t m) {
  unsigned char adj[MAXN * MAXN];
  unsigned char co[MAXN * MAXN];
  size_t set[MAXN];
  size_t color[MAXN];
  size_t alpha = 0;
  size_t cover = 0;
  size_t i;
  size_t j;
  double theta = 0.0;
  for (i = 0; i < n * n; i++) {
    adj[i] = 0;
  }
  for (i = 0; i < m; i++) {
    adj[u[i] * n + v[i]] = 1;
    adj[v[i] * n + u[i]] = 1;
  }
  for (i = 0; i < n;
       i++) { /* the complement: its colourings are clique covers */
    for (j = 0; j < n; j++) {
      co[i * n + j] = (unsigned char)(i != j && !adj[i * n + j]);
    }
  }
  if (ds_max_independent_set(adj, n, set, &alpha, NULL) != DS_OK ||
      ds_chromatic_number(co, n, color, &cover, 100000, NULL) != DS_OK ||
      ds_sdp_lovasz_theta(n, u, v, m, &theta, NULL) != DS_OK) {
    printf("%s: failed\n", name);
    return;
  }
  printf("%-14s independence %lu <= theta %.6f <= clique cover %lu\n", name,
         (unsigned long)alpha, theta, (unsigned long)cover);
}

int main(void) {
  size_t u[64];
  size_t v[64];
  double w[64];
  unsigned char side[MAXN];
  size_t m = 0;
  size_t i;
  size_t j;
  double cut = 0.0;
  double bound = 0.0;
  double best = 0.0;
  unsigned long s;
  ds_sdp_t p;
  ds_sdp_solution_t sol;
  static const size_t pu[15] = {0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  static const size_t pv[15] = {1, 2, 3, 4, 0, 5, 6, 7, 8, 9, 7, 8, 9, 5, 6};
  for (i = 0; i < 5; i++) { /* the 5-cycle */
    u[i] = i;
    v[i] = (i + 1) % 5;
  }
  bounds("5-cycle", 5, u, v, 5);
  bounds("Petersen", 10, pu, pv, 15);
  for (i = 0; i < 7; i++) { /* the 7-cycle with one chord */
    u[i] = i;
    v[i] = (i + 1) % 7;
  }
  u[7] = 0;
  v[7] = 3;
  bounds("7-cycle+chord", 7, u, v, 8);

  /* max cut of a 12-vertex graph: SDP bound, rounded cut, exact optimum */
  for (i = 0; i < 12; i++) {
    for (j = i + 1; j < 12; j++) {
      unsigned long h = ((i * 12 + j) * 2654435761ul & 0xFFFFFFFFul) % 1000ul;
      if (h < 400) {
        u[m] = i;
        v[m] = j;
        w[m] = 1.0 + (double)(h % 3);
        m++;
      }
    }
  }
  if (ds_sdp_maxcut(12, u, v, w, m, 2024, 64, side, &cut, &bound, NULL) !=
      DS_OK) {
    return 1;
  }
  for (s = 0; s < (1ul << 12); s++) {
    double c = 0.0;
    for (i = 0; i < m; i++) {
      if ((s >> u[i] & 1) != (s >> v[i] & 1)) {
        c += w[i];
      }
    }
    if (c > best) {
      best = c;
    }
  }
  printf("max cut (%lu weighted edges): rounded %.0f, exact %.0f, SDP bound "
         "%.4f (%.1f%% of it)\n",
         (unsigned long)m, cut, best, bound, 100.0 * cut / bound);

  if (ds_sdp_parse_sdpa(&p, example, NULL) != DS_OK ||
      ds_sdp_solve(&p, NULL, &sol) != DS_OK) {
    return 1;
  }
  printf("SDPA example 1: %s after %lu iterations, max <C, X> = %.6f, "
         "min a^T y = %.6f at y = (%.4f, %.4f, %.4f)\n",
         sol.status == DS_SDP_OPTIMAL ? "optimal" : "not solved",
         (unsigned long)sol.iterations, sol.primal_objective,
         sol.dual_objective, sol.y[0], sol.y[1], sol.y[2]);
  ds_sdp_solution_destroy(&sol);
  ds_sdp_destroy(&p);
  return 0;
}
