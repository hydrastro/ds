#ifndef DS_HYPERGRAPH_H
#define DS_HYPERGRAPH_H

/* Undirected hypergraphs: a hyperedge is a set of vertex IDs, with a copied
 * fixed-size payload. Empty and parallel hyperedges are allowed. Membership
 * input is sorted/deduplicated. Vertex IDs are 0..vertex_count-1.
 *
 * Edge handles are graph-local, monotonically assigned unsigned longs; 0 is
 * invalid and removed handles are never reused in this initialized graph.
 * Reinitializing/destroying a graph ends every handle's lifetime. The caller
 * selects the map backend for handle lookup; edge and incidence sequences use
 * contiguous vectors. All memory uses the context. Map capability requirements
 * (including realloc support) apply. No internal synchronization.
 *
 * Insert and membership replacement reserve storage before publication;
 * failure preserves logical contents but can invalidate incidence-vector
 * pointers through reserve. Edge member/value pointers remain valid until that
 * edge is removed, members are replaced, or the graph is destroyed. Map growth
 * does not move the separately allocated edge records. Payload bytes have no
 * ownership callbacks: resources referenced by those bytes remain caller-owned.
 */
#include "map_iface.h"
#include "vec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long ds_hyperedge_handle_t;
typedef struct ds_hypergraph_config {
  size_t vertex_count;
  size_t value_size;
  const ds_map_ops_t *edge_map_ops; /* NULL: Robin Hood */
  ds_context_t *context;
} ds_hypergraph_config_t;
typedef struct ds_hypergraph {
  ds_map_t edges;
  ds_vec_t edge_ids;
  ds_vec_t *incident;
  size_t vertex_count;
  size_t value_size;
  size_t value_offset;
  unsigned long next_id; /* zero after exhaustion */
  ds_context_t *context;
} ds_hypergraph_t;

void ds_hypergraph_config_init(ds_hypergraph_config_t *config);
ds_status_t ds_hypergraph_init(ds_hypergraph_t *graph,
                               const ds_hypergraph_config_t *config);
void ds_hypergraph_destroy(ds_hypergraph_t *graph);
/* Shrinking requires every removed vertex to have degree zero. */
ds_status_t ds_hypergraph_resize_vertices(ds_hypergraph_t *graph, size_t count);
size_t ds_hypergraph_vertex_count(const ds_hypergraph_t *graph);
size_t ds_hypergraph_edge_count(const ds_hypergraph_t *graph);
ds_status_t ds_hypergraph_insert(ds_hypergraph_t *graph, const size_t *vertices,
                                 size_t count, const void *value,
                                 ds_hyperedge_handle_t *out_edge);
ds_status_t ds_hypergraph_set_members(ds_hypergraph_t *graph,
                                      ds_hyperedge_handle_t edge,
                                      const size_t *vertices, size_t count);
ds_status_t ds_hypergraph_remove(ds_hypergraph_t *graph,
                                 ds_hyperedge_handle_t edge);
ds_status_t ds_hypergraph_members(const ds_hypergraph_t *graph,
                                  ds_hyperedge_handle_t edge,
                                  const size_t **out_vertices,
                                  size_t *out_count);
/* NULL insertion/update value zero-initializes the copied value. */
void *ds_hypergraph_value(const ds_hypergraph_t *graph,
                          ds_hyperedge_handle_t edge);
ds_status_t ds_hypergraph_set_value(ds_hypergraph_t *graph,
                                    ds_hyperedge_handle_t edge,
                                    const void *value);
/* Dense edge order changes after removal. These bridge handles and the
 * edge vertices of an incidence graph view. */
ds_status_t ds_hypergraph_edge_at(const ds_hypergraph_t *graph, size_t index,
                                  ds_hyperedge_handle_t *out_edge);
ds_status_t ds_hypergraph_edge_index(const ds_hypergraph_t *graph,
                                     ds_hyperedge_handle_t edge,
                                     size_t *out_index);
/* Borrowed edge-ID sequence, invalidated by any mutation attempt. */
ds_status_t ds_hypergraph_incident(const ds_hypergraph_t *graph, size_t vertex,
                                   const ds_hyperedge_handle_t **out_edges,
                                   size_t *out_count);

#ifdef __cplusplus
}
#endif
#endif /* DS_HYPERGRAPH_H */
