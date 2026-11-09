#include "ds.h"
#include "ds_algo.h"
#include <math.h>
#include <stdio.h>

/* A dense graph stored as an adjacency matrix, read by graph-view
 * algorithms through an adapter, and cross-checked against a dense matrix
 * algorithm on the exported weights. The matrix answers "is there an edge"
 * in O(1) and still enumerates each row in O(degree). */

#define N 6

int main(void) {
  ds_adj_matrix_t m;
  ds_gview_t view;
  double dense[N * N];
  double dist[N];
  size_t parent[N];
  size_t path[N];
  size_t len;
  size_t i;

  if (ds_adj_matrix_init(&m, N, false, NULL) != DS_OK) {
    return 1;
  }
  (void)ds_adj_matrix_set(&m, 0, 1, 7.0);
  (void)ds_adj_matrix_set(&m, 0, 2, 9.0);
  (void)ds_adj_matrix_set(&m, 0, 5, 14.0);
  (void)ds_adj_matrix_set(&m, 1, 2, 10.0);
  (void)ds_adj_matrix_set(&m, 1, 3, 15.0);
  (void)ds_adj_matrix_set(&m, 2, 3, 11.0);
  (void)ds_adj_matrix_set(&m, 2, 5, 2.0);
  (void)ds_adj_matrix_set(&m, 3, 4, 6.0);
  (void)ds_adj_matrix_set(&m, 4, 5, 9.0);
  printf("edge 2-5: %s, degree(2)=%lu\n",
         ds_adj_matrix_has(&m, 2, 5) ? "yes" : "no",
         (unsigned long)ds_adj_matrix_degree(&m, 2));

  ds_gview_from_adj_matrix(&m, &view);
  if (ds_gview_dijkstra(&view, 0, dist, parent, NULL) != DS_OK) {
    ds_adj_matrix_destroy(&m);
    return 1;
  }
  len = ds_gview_path(parent, 4, path, N);
  printf("0 -> 4: distance %.0f via", dist[4]);
  for (i = 0; i < len; i++) {
    printf(" %lu", (unsigned long)path[i]);
  }
  printf("\n");

  /* the same distances from Floyd-Warshall on the exported matrix */
  ds_adj_matrix_to_dense(&m, HUGE_VAL, 0.0, dense);
  ds_gview_floyd_warshall(dense, N, NULL);
  for (i = 0; i < N; i++) {
    if (dense[i] > dist[i] || dense[i] < dist[i]) {
      printf("mismatch at %lu\n", (unsigned long)i);
      ds_adj_matrix_destroy(&m);
      return 1;
    }
  }
  printf("Floyd-Warshall agrees on all %d distances\n", N);
  ds_adj_matrix_destroy(&m);
  return 0;
}
