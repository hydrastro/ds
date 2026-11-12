/*
 * One graph, several priority queues. The graph is plain CSR arrays read
 * through a graph view, never copied into an owning graph; Dijkstra runs
 * with each lib/pq_iface backend in turn and a reused workspace. Every
 * backend gives the same distances; what changes is the cost of each
 * operation, which the backend states in ops->costs and Dijkstra inherits
 * (V pushes and pops, up to E decreases).
 */
#include "ds.h"
#include "ds_algo.h"
#include <math.h>
#include <stdio.h>

#define V 8

static const char *cost_name(unsigned c) {
  switch (c) {
  case DS_PQ_COST_CONST:
    return "1";
  case DS_PQ_COST_SUBLOG:
    return "o(log)";
  case DS_PQ_COST_LOG:
    return "log";
  case DS_PQ_COST_LOG2:
    return "log^2";
  case DS_PQ_COST_LINEAR:
    return "n";
  default:
    return "-";
  }
}

int main(void) {
  /* edges of v are targets[offsets[v] .. offsets[v + 1]) */
  static const size_t offsets[V + 1] = {0, 3, 5, 7, 9, 11, 12, 13, 13};
  static const size_t targets[13] = {1, 2, 3, 4, 5, 4, 6, 5, 7, 6, 7, 7, 5};
  static const double weights[13] = {4, 1, 7, 2, 6, 1, 8, 3, 9, 1, 2, 2, 1};
  const ds_pq_ops_t *ops[5];
  ds_gview_csr_t csr;
  ds_gview_t g;
  ds_gview_workspace_t ws;
  int b;
  ops[0] = ds_pq_binary_ops();
  ops[1] = ds_pq_packed_ops();
  ops[2] = ds_pq_pairing_ops();
  ops[3] = ds_pq_leftist_ops();
  ops[4] = ds_pq_binomial_ops();
  csr.vertex_count = V;
  csr.offsets = offsets;
  csr.targets = targets;
  csr.weights = weights;
  ds_gview_from_csr(&csr, &g);
  if (ds_gview_workspace_init(&ws, V, NULL) != DS_OK) {
    return 1;
  }
  for (b = 0; b < 5; b++) {
    ds_gview_policy_t policy;
    double dist[V];
    size_t v;
    ds_gview_policy_init(&policy);
    policy.pq = ops[b];
    policy.workspace = &ws; /* handles, states and keys reused */
    if (ds_gview_dijkstra_with(&g, 0, dist, NULL, &policy) != DS_OK) {
      ds_gview_workspace_destroy(&ws);
      return 1;
    }
    printf("%-20s push %s/%s  pop %s/%s  decrease %s/%s   dist:", ops[b]->name,
           cost_name(ops[b]->costs.push.worst),
           cost_name(ops[b]->costs.push.amortized),
           cost_name(ops[b]->costs.pop.worst),
           cost_name(ops[b]->costs.pop.amortized),
           cost_name(ops[b]->costs.decrease.worst),
           cost_name(ops[b]->costs.decrease.amortized));
    for (v = 0; v < V; v++) {
      printf(" %g", dist[v]);
    }
    printf("\n");
  }
  ds_gview_workspace_destroy(&ws);
  return 0;
}
