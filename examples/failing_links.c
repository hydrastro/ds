/*
 * Links of a network fail one by one. A deletions-only minimum spanning
 * forest keeps the cheapest backbone and names the link that replaces
 * each failed backbone link. A topology tree over a relay tree (at most
 * three links per relay) answers path sums of relay delays while the
 * relay tree is rewired.
 */
#include "ds.h"
#include <stdio.h>

#define SIDE 8
#define NODES (SIDE * SIDE)

static void add(void *out, const void *a, const void *b, void *user) {
  (void)user;
  *(long *)out = *(const long *)a + *(const long *)b;
}

int main(void) {
  size_t u[4 * NODES], v[4 * NODES];
  double w[4 * NODES];
  size_t m = 0, i, rep;
  ds_dmsf_t g;
  ds_topology_tree_t t;
  long delay[16];
  long sum = 0;
  /* a grid of sites, link costs from a fixed pattern */
  for (i = 0; i < NODES; i++) {
    if (i % SIDE + 1 < SIDE) {
      u[m] = i;
      v[m] = i + 1;
      w[m] = (double)((i * 37 + 11) % 23 + 1);
      m++;
    }
    if (i + SIDE < NODES) {
      u[m] = i;
      v[m] = i + SIDE;
      w[m] = (double)((i * 53 + 7) % 29 + 1);
      m++;
    }
  }
  if (ds_dmsf_init(&g, NODES, u, v, w, m, NULL) != DS_OK) {
    return 1;
  }
  printf("%lu links, backbone of %lu links costs %.0f\n", (unsigned long)m,
         (unsigned long)ds_dmsf_forest_edges(&g), ds_dmsf_weight(&g));
  for (i = 0; i < m; i += 7) { /* every seventh link fails */
    bool was = ds_dmsf_in_forest(&g, i);
    (void)ds_dmsf_delete(&g, i, &rep);
    if (was && rep != DS_DMSF_NONE) {
      printf("  backbone link %lu-%lu (%.0f) failed: %lu-%lu (%.0f) "
             "replaces it\n",
             (unsigned long)u[i], (unsigned long)v[i], w[i],
             (unsigned long)u[rep], (unsigned long)v[rep], w[rep]);
    } else if (was) {
      printf("  backbone link %lu-%lu failed: the network splits\n",
             (unsigned long)u[i], (unsigned long)v[i]);
    }
  }
  printf("after the failures: %lu links, backbone of %lu costs %.0f, "
         "corner sites %s, check %s\n",
         (unsigned long)ds_dmsf_edges(&g),
         (unsigned long)ds_dmsf_forest_edges(&g), ds_dmsf_weight(&g),
         ds_dmsf_connected(&g, 0, NODES - 1) ? "connected" : "apart",
         ds_dmsf_check(&g) ? "passed" : "FAILED");
  ds_dmsf_destroy(&g);

  /* relays 0..15 as a binary tree, delay of relay r = r + 1 */
  for (i = 0; i < 16; i++) {
    delay[i] = (long)i + 1;
  }
  if (ds_topology_tree_init(&t, 16, sizeof(long), delay, add, NULL, NULL) !=
      DS_OK) {
    return 1;
  }
  for (i = 1; i < 16; i++) {
    (void)ds_topology_tree_link(&t, i, (i - 1) / 2);
  }
  (void)ds_topology_tree_path_value(&t, 15, 8, &sum);
  printf("relay path 15 -> 7 -> 3 -> 8: total delay %ld\n", sum);
  (void)ds_topology_tree_cut(&t, 7, 3); /* relay 7 rehomed under 12 */
  (void)ds_topology_tree_link(&t, 7, 12);
  (void)ds_topology_tree_path_value(&t, 15, 8, &sum);
  printf("relay 7 rehomed under relay 12: path 15 -> 7 -> 12 -> 5 -> 2 -> 0 "
         "-> 1 -> 3 -> 8 delays %ld; the update rebuilt %lu clusters\n",
         sum, (unsigned long)ds_topology_tree_work(&t));
  ds_topology_tree_destroy(&t);
  return 0;
}
