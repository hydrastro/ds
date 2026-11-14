/*
 * A network whose links come and go: a fully dynamic minimum spanning
 * forest keeps the cheapest backbone through every change, and the
 * heaviest backbone link between two sites (the bottleneck any cheaper
 * new link would remove) is one query away. Every so often the backbone
 * is compared with a fresh Kruskal run.
 */
#include "ds.h"
#include "ds_algo.h"
#include <stdio.h>
#include <stdlib.h>

#define SITES 100
#define ROUNDS 3000

static unsigned long seed = 2024;
static unsigned long next(void) {
  seed = (seed * 1103515245ul + 12345ul) & 0xFFFFFFFFul;
  return seed >> 8;
}

int main(void) {
  ds_dyn_msf_t g;
  size_t live[4 * SITES];
  size_t nlive = 0, round, id, worst;
  unsigned long added = 0, removed = 0, mismatches = 0;
  if (ds_dyn_msf_init(&g, SITES, NULL) != DS_OK) {
    return 1;
  }
  for (round = 0; round < ROUNDS; round++) {
    if (nlive < 3 * SITES && (nlive == 0 || next() % 100 < 65)) {
      size_t u = next() % SITES, v = next() % SITES;
      double cost = (double)(1 + next() % 1000);
      if (ds_dyn_msf_insert(&g, u, v, cost, &id) == DS_OK) {
        live[nlive++] = id;
        added++;
      }
    } else {
      size_t k = next() % nlive;
      (void)ds_dyn_msf_delete(&g, live[k]);
      live[k] = live[--nlive];
      removed++;
    }
    if (round % 1000 == 999) { /* against Kruskal over the live links */
      size_t u[4 * SITES], v[4 * SITES], i;
      double w[4 * SITES], total = 0.0, diff;
      unsigned char in[4 * SITES];
      for (i = 0; i < nlive; i++) {
        const ds_dyn_msf_edge_t *e = &g.edge[live[i]];
        u[i] = e->end[0];
        v[i] = e->end[1];
        w[i] = e->weight;
      }
      (void)ds_kruskal(SITES, u, v, w, nlive, in, &total, NULL);
      for (i = 0; i < nlive; i++) {
        mismatches += (in[i] != 0) != ds_dyn_msf_in_forest(&g, live[i]);
      }
      diff = total - ds_dyn_msf_weight(&g);
      printf("after %lu changes: %lu links, backbone of %lu costs %.0f "
             "(Kruskal: %.0f, %s)\n",
             (unsigned long)round + 1, (unsigned long)nlive,
             (unsigned long)ds_dyn_msf_forest_edges(&g), ds_dyn_msf_weight(&g),
             total, diff < 0.5 && diff > -0.5 ? "same" : "DIFFERENT");
    }
  }
  if (ds_dyn_msf_path_max(&g, 0, SITES - 1, &worst) == DS_OK) {
    printf("bottleneck between sites 0 and %d: a link of cost %.0f; a new "
           "link cheaper than that would join the backbone\n",
           SITES - 1, g.edge[worst].weight);
  }
  printf("%lu links added, %lu removed, %lu structures rebuilt, %lu "
         "mismatches with Kruskal\n",
         added, removed, g.rebuilds, mismatches);
  ds_dyn_msf_destroy(&g);
  return mismatches == 0 ? 0 : 1;
}
