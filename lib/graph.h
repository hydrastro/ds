#ifndef DS_GRAPH_H
#define DS_GRAPH_H

#include "common.h"

#include "context.h"
#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_graph_edge {
  size_t to;
  void *data;
} ds_graph_edge_t;

typedef struct ds_graph_vertex {
  void *data;
  ds_graph_edge_t *edges;
  size_t edge_count;
  size_t edge_capacity;
} ds_graph_vertex_t;

typedef struct ds_graph {
  ds_graph_vertex_t *vertices;
  size_t vertex_count;
  size_t vertex_capacity;
  ds_context_t *context;
} ds_graph_t;

ds_graph_t *FUNC(ds_graph_create)(void);
ds_graph_t *FUNC(ds_graph_create_ctx)(ds_context_t *context);
void FUNC(ds_graph_destroy)(ds_graph_t *graph,
                            void (*destroy_vertex)(void *),
                            void (*destroy_edge)(void *));
ds_status_t FUNC(ds_graph_add_vertex)(ds_graph_t *graph, void *data,
                                       size_t *out_index);
ds_status_t FUNC(ds_graph_add_edge)(ds_graph_t *graph, size_t from, size_t to,
                                     void *data);
ds_status_t FUNC(ds_graph_get_vertex)(ds_graph_t *graph, size_t index,
                                       void **out_data);
size_t FUNC(ds_graph_vertex_count)(ds_graph_t *graph);
size_t FUNC(ds_graph_edge_count)(ds_graph_t *graph, size_t vertex);

#ifdef __cplusplus
}
#endif

#endif /* DS_GRAPH_H */
