#include "graph.h"
#include <stdlib.h>
#include <string.h>

static ds_status_t graph_reserve_vertices(ds_graph_t *graph, size_t capacity);
static ds_status_t graph_reserve_edges(ds_graph_t *graph, size_t vertex,
                                       size_t capacity);

static ds_status_t graph_reserve_vertices(ds_graph_t *graph, size_t capacity) {
  ds_graph_vertex_t *vertices;
  size_t i;
  if (capacity <= graph->vertex_capacity) {
    return DS_OK;
  }
  vertices = (ds_graph_vertex_t *)realloc(graph->vertices,
                                          capacity * sizeof(*vertices));
  if (vertices == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = graph->vertex_capacity; i < capacity; i++) {
    vertices[i].data = NULL;
    vertices[i].edges = NULL;
    vertices[i].edge_count = 0U;
    vertices[i].edge_capacity = 0U;
  }
  graph->vertices = vertices;
  graph->vertex_capacity = capacity;
  return DS_OK;
}

static ds_status_t graph_reserve_edges(ds_graph_t *graph, size_t vertex,
                                       size_t capacity) {
  ds_graph_edge_t *edges;
  if (capacity <= graph->vertices[vertex].edge_capacity) {
    return DS_OK;
  }
  edges = (ds_graph_edge_t *)realloc(graph->vertices[vertex].edges,
                                     capacity * sizeof(*edges));
  if (edges == NULL) {
    return DS_ERR_ALLOC;
  }
  graph->vertices[vertex].edges = edges;
  graph->vertices[vertex].edge_capacity = capacity;
  return DS_OK;
}

ds_graph_t *FUNC(ds_graph_create)(void) { return FUNC(ds_graph_create_ctx)(NULL); }

ds_graph_t *FUNC(ds_graph_create_ctx)(ds_context_t *context) {
  ds_graph_t *graph;
  graph = (ds_graph_t *)malloc(sizeof(*graph));
  if (graph == NULL) {
    return NULL;
  }
  graph->vertices = NULL;
  graph->vertex_count = 0U;
  graph->vertex_capacity = 0U;
  graph->context = context != NULL ? context : ds_default_context();
  return graph;
}

void FUNC(ds_graph_destroy)(ds_graph_t *graph,
                            void (*destroy_vertex)(void *),
                            void (*destroy_edge)(void *)) {
  size_t i;
  size_t j;
  if (graph == NULL) {
    return;
  }
  for (i = 0U; i < graph->vertex_count; i++) {
    if (destroy_vertex != NULL) {
      destroy_vertex(graph->vertices[i].data);
    }
    if (destroy_edge != NULL) {
      for (j = 0U; j < graph->vertices[i].edge_count; j++) {
        destroy_edge(graph->vertices[i].edges[j].data);
      }
    }
    free(graph->vertices[i].edges);
  }
  free(graph->vertices);
  free(graph);
}

ds_status_t FUNC(ds_graph_add_vertex)(ds_graph_t *graph, void *data,
                                       size_t *out_index) {
  ds_status_t status;
  size_t index;
  if (graph == NULL) {
    return DS_ERR_NULL;
  }
  if (graph->vertex_count == graph->vertex_capacity) {
    status = graph_reserve_vertices(
        graph, graph->vertex_capacity == 0U ? 8U : graph->vertex_capacity * 2U);
    if (status != DS_OK) {
      return status;
    }
  }
  index = graph->vertex_count;
  graph->vertices[index].data = data;
  graph->vertices[index].edge_count = 0U;
  graph->vertex_count++;
  if (out_index != NULL) {
    *out_index = index;
  }
  return DS_OK;
}

ds_status_t FUNC(ds_graph_add_edge)(ds_graph_t *graph, size_t from, size_t to,
                                     void *data) {
  ds_status_t status;
  size_t index;
  if (graph == NULL) {
    return DS_ERR_NULL;
  }
  if (from >= graph->vertex_count || to >= graph->vertex_count) {
    return DS_ERR_RANGE;
  }
  if (graph->vertices[from].edge_count == graph->vertices[from].edge_capacity) {
    status = graph_reserve_edges(
        graph, from, graph->vertices[from].edge_capacity == 0U
                         ? 4U
                         : graph->vertices[from].edge_capacity * 2U);
    if (status != DS_OK) {
      return status;
    }
  }
  index = graph->vertices[from].edge_count;
  graph->vertices[from].edges[index].to = to;
  graph->vertices[from].edges[index].data = data;
  graph->vertices[from].edge_count++;
  return DS_OK;
}

ds_status_t FUNC(ds_graph_get_vertex)(ds_graph_t *graph, size_t index,
                                       void **out_data) {
  if (out_data != NULL) {
    *out_data = NULL;
  }
  if (graph == NULL || out_data == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= graph->vertex_count) {
    return DS_ERR_RANGE;
  }
  *out_data = graph->vertices[index].data;
  return DS_OK;
}

size_t FUNC(ds_graph_vertex_count)(ds_graph_t *graph) {
  if (graph == NULL) {
    return 0U;
  }
  return graph->vertex_count;
}

size_t FUNC(ds_graph_edge_count)(ds_graph_t *graph, size_t vertex) {
  if (graph == NULL || vertex >= graph->vertex_count) {
    return 0U;
  }
  return graph->vertices[vertex].edge_count;
}
