#include "../lib/graph.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static size_t visit_order[64];
static size_t visit_count;

static bool record_visit(size_t vertex, void *data, void *user) {
  (void)data;
  (void)user;
  visit_order[visit_count++] = vertex;
  return true; /* continue traversal */
}

static size_t add_vertex(ds_graph_t *g, void *data) {
  size_t idx = 0;
  ds_status_t s = ds_graph_add_vertex(g, data, &idx);
  assert(s == DS_OK);
  return idx;
}

/* build a small DAG:
 *   0 -> 1 -> 3
 *   0 -> 2 -> 3
 * topological order must place 0 first, 3 last.
 */
static void test_topological(void) {
  ds_graph_t *g = ds_graph_create();
  size_t order[8];
  size_t count = 0;
  size_t i;
  size_t pos[4];
  ds_status_t s;
  for (i = 0; i < 4; ++i) {
    add_vertex(g, NULL);
  }
  ds_graph_add_edge(g, 0, 1, NULL);
  ds_graph_add_edge(g, 0, 2, NULL);
  ds_graph_add_edge(g, 1, 3, NULL);
  ds_graph_add_edge(g, 2, 3, NULL);
  s = ds_graph_topological_sort(g, order, 8, &count);
  assert(s == DS_OK);
  assert(count == 4);
  for (i = 0; i < 4; ++i) {
    pos[order[i]] = i;
  }
  assert(pos[0] < pos[1]);
  assert(pos[0] < pos[2]);
  assert(pos[1] < pos[3]);
  assert(pos[2] < pos[3]);
  ds_graph_destroy(g, NULL, NULL);
  printf("topological sort: ok\n");
}

static void test_bfs_dfs(void) {
  ds_graph_t *g = ds_graph_create();
  size_t i;
  for (i = 0; i < 5; ++i) {
    add_vertex(g, NULL);
  }
  /* 0-1, 0-2, 1-3, 2-4 (directed chains from 0) */
  ds_graph_add_edge(g, 0, 1, NULL);
  ds_graph_add_edge(g, 0, 2, NULL);
  ds_graph_add_edge(g, 1, 3, NULL);
  ds_graph_add_edge(g, 2, 4, NULL);

  visit_count = 0;
  ds_graph_bfs(g, 0, record_visit, NULL);
  assert(visit_count == 5);
  assert(visit_order[0] == 0); /* BFS starts at the root */

  visit_count = 0;
  ds_graph_dfs(g, 0, record_visit, NULL);
  assert(visit_count == 5);
  assert(visit_order[0] == 0);

  ds_graph_destroy(g, NULL, NULL);
  printf("bfs/dfs: ok\n");
}

static void test_dijkstra(void) {
  ds_graph_t *g = ds_graph_create();
  double dist[5];
  size_t prev[5];
  size_t i;
  ds_status_t s;
  for (i = 0; i < 5; ++i) {
    add_vertex(g, NULL);
  }
  /* weighted: 0->1 (4), 0->2 (1), 2->1 (2), 1->3 (1), 2->3 (5) */
  ds_graph_add_weighted_edge(g, 0, 1, 4.0, NULL);
  ds_graph_add_weighted_edge(g, 0, 2, 1.0, NULL);
  ds_graph_add_weighted_edge(g, 2, 1, 2.0, NULL);
  ds_graph_add_weighted_edge(g, 1, 3, 1.0, NULL);
  ds_graph_add_weighted_edge(g, 2, 3, 5.0, NULL);
  s = ds_graph_dijkstra(g, 0, dist, prev, 5);
  assert(s == DS_OK);
  /* shortest 0->2 = 1, 0->1 via 2 = 3, 0->3 via 2,1 = 4 */
  assert(dist[0] == 0.0);
  assert(dist[2] == 1.0);
  assert(dist[1] == 3.0);
  assert(dist[3] == 4.0);
  ds_graph_destroy(g, NULL, NULL);
  printf("dijkstra: ok\n");
}

static void test_bellman_ford(void) {
  ds_graph_t *g = ds_graph_create();
  double dist[3];
  size_t prev[3];
  bool neg = false;
  size_t i;
  ds_status_t s;
  for (i = 0; i < 3; ++i) {
    add_vertex(g, NULL);
  }
  /* positive graph first: 0->1 (2), 1->2 (3) */
  ds_graph_add_weighted_edge(g, 0, 1, 2.0, NULL);
  ds_graph_add_weighted_edge(g, 1, 2, 3.0, NULL);
  s = ds_graph_bellman_ford(g, 0, dist, prev, 3, &neg);
  assert(s == DS_OK);
  assert(!neg);
  assert(dist[2] == 5.0);
  ds_graph_destroy(g, NULL, NULL);

  /* negative cycle: 0->1 (1), 1->2 (-1), 2->0 (-1) sums to -1 around loop */
  g = ds_graph_create();
  for (i = 0; i < 3; ++i) {
    add_vertex(g, NULL);
  }
  ds_graph_add_weighted_edge(g, 0, 1, 1.0, NULL);
  ds_graph_add_weighted_edge(g, 1, 2, -1.0, NULL);
  ds_graph_add_weighted_edge(g, 2, 0, -1.0, NULL);
  neg = false;
  s = ds_graph_bellman_ford(g, 0, dist, prev, 3, &neg);
  assert(neg); /* negative cycle detected */
  ds_graph_destroy(g, NULL, NULL);
  printf("bellman-ford (incl. negative cycle): ok\n");
}

static void test_scc(void) {
  ds_graph_t *g = ds_graph_create();
  size_t comp[6];
  size_t count = 0;
  size_t i;
  ds_status_t s;
  for (i = 0; i < 5; ++i) {
    add_vertex(g, NULL);
  }
  /* two SCCs: {0,1,2} as a cycle, {3,4} as a cycle, edge 2->3 between */
  ds_graph_add_edge(g, 0, 1, NULL);
  ds_graph_add_edge(g, 1, 2, NULL);
  ds_graph_add_edge(g, 2, 0, NULL);
  ds_graph_add_edge(g, 2, 3, NULL);
  ds_graph_add_edge(g, 3, 4, NULL);
  ds_graph_add_edge(g, 4, 3, NULL);
  s = ds_graph_strongly_connected_components(g, comp, 6, &count);
  assert(s == DS_OK);
  assert(count == 2); /* exactly two strongly connected components */
  /* 0,1,2 share a component; 3,4 share another; the two differ */
  assert(comp[0] == comp[1] && comp[1] == comp[2]);
  assert(comp[3] == comp[4]);
  assert(comp[0] != comp[3]);
  ds_graph_destroy(g, NULL, NULL);
  printf("strongly connected components: ok\n");
}

static void test_counts_and_errors(void) {
  ds_graph_t *g = ds_graph_create();
  size_t a = add_vertex(g, NULL);
  size_t b = add_vertex(g, NULL);
  ds_status_t s;
  assert(ds_graph_vertex_count(g) == 2);
  ds_graph_add_edge(g, a, b, NULL);
  assert(ds_graph_edge_count(g, a) == 1);
  /* out-of-range edge should not succeed */
  s = ds_graph_add_edge(g, 99, 0, NULL);
  assert(s != DS_OK);
  ds_graph_destroy(g, NULL, NULL);
  printf("counts and error paths: ok\n");
}

int main(void) {
  test_topological();
  test_bfs_dfs();
  test_dijkstra();
  test_bellman_ford();
  test_scc();
  test_counts_and_errors();
  printf("ok\n");
  return 0;
}
