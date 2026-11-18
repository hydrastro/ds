/* Hyperedges model shared projects; ordinary BFS runs on their incidence view.
 */
#include "ds.h"
#include <stdio.h>

static ds_status_t connections(ds_hypergraph_t *graph, const char *label) {
  ds_gview_t view;
  size_t hops[16], i;
  ds_status_t st = ds_gview_from_hypergraph(graph, &view);
  if (st != DS_OK)
    return st;
  if (view.vertex_count > 16)
    return DS_ERR_RANGE;
  st = ds_gview_bfs(&view, 0, hops, NULL, NULL);
  if (st != DS_OK)
    return st;
  printf("%s:", label);
  for (i = 0; i < ds_hypergraph_vertex_count(graph); i++) {
    if (hops[i] == DS_GVIEW_NONE)
      printf(" %lu:unconnected", (unsigned long)i);
    else
      printf(" %lu:%lu", (unsigned long)i, (unsigned long)(hops[i] / 2));
  }
  puts("");
  return DS_OK;
}
int main(void) {
  ds_hypergraph_config_t config;
  ds_hypergraph_t graph;
  ds_hyperedge_handle_t first, second, third;
  const size_t a[4] = {2, 0, 1, 2}, b[2] = {2, 3}, c[1] = {4},
               linked[2] = {3, 4};
  const char *name = "compiler";
  const size_t *members;
  size_t count;
  ds_status_t st;
  ds_hypergraph_config_init(&config);
  config.vertex_count = 5;
  config.value_size = sizeof(name);
  config.edge_map_ops = ds_map_swiss_ops();
  st = ds_hypergraph_init(&graph, &config);
  if (st != DS_OK)
    goto done;
  st = ds_hypergraph_insert(&graph, a, 4, &name, &first);
  name = "catalog";
  if (st == DS_OK)
    st = ds_hypergraph_insert(&graph, b, 2, &name, &second);
  name = "archive";
  if (st == DS_OK)
    st = ds_hypergraph_insert(&graph, c, 1, &name, &third);
  if (st == DS_OK)
    st = ds_hypergraph_members(&graph, first, &members, &count);
  if (st == DS_OK)
    printf("Compiler project: %lu distinct members\n", (unsigned long)count);
  if (st == DS_OK)
    st = connections(&graph, "Initial project hops");
  if (st == DS_OK)
    st = ds_hypergraph_set_members(&graph, third, linked, 2);
  if (st == DS_OK)
    st = connections(&graph, "Archive joins the network");
  if (st == DS_OK)
    st = ds_hypergraph_remove(&graph, second);
  if (st == DS_OK)
    st = connections(&graph, "Catalog project retired");
  if (st == DS_OK)
    st = ds_hypergraph_resize_vertices(&graph, 6);
  if (st == DS_OK)
    printf("Expanded vertex universe to %lu\n",
           (unsigned long)ds_hypergraph_vertex_count(&graph));
  ds_hypergraph_destroy(&graph);
done:
  if (st != DS_OK)
    fprintf(stderr, "shared projects: %s\n", ds_status_name(st));
  return st == DS_OK ? 0 : 1;
}
