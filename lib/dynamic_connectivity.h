#ifndef DS_DYNAMIC_CONNECTIVITY_H
#define DS_DYNAMIC_CONNECTIVITY_H

/*
 * Fully dynamic connectivity (Holm, de Lichtenberg and Thorup,
 * "Poly-logarithmic deterministic fully-dynamic algorithms for
 * connectivity, minimum spanning tree, 2-edge, and biconnectivity",
 * J. ACM 2001): an undirected simple graph on n vertices under edge
 * insertions and deletions, answering whether two vertices are connected.
 *
 * Every edge has a level from 0 to L - 1, L = floor(log2 n) + 1, and only
 * ever moves up. F_i is a spanning forest of the edges of level at least
 * i, kept as Euler-tour trees (lib/euler_tour_tree, one per level), with
 * F_0 containing F_1 containing ...; F_0 is a spanning forest of the
 * whole graph. A tree of F_i never has more than n / 2^i vertices.
 *
 *   insert     a tree edge of F_0 if it joins two components, otherwise a
 *              non-tree edge of level 0
 *   delete     a non-tree edge just leaves. A tree edge of level l is cut
 *              from F_0 .. F_l; then, from level l down to 0, the smaller
 *              of the two trees of F_i has its level-i tree edges promoted
 *              to level i + 1, and its level-i non-tree edges are examined
 *              one by one: an edge leaving the tree replaces the deleted
 *              one in F_0 .. F_i and the search stops; one inside it is
 *              promoted. Every promotion is paid by the edge's level,
 *              which only grows: O(log^2 n) amortized per update.
 *   query      connectivity and component sizes from F_0: O(log n)
 *              expected
 *
 * The vertices holding level-i edges are found inside a tree of F_i in
 * O(log n) each by ds_ett_component_find over per-vertex flags (has
 * level-i tree edges, has level-i non-tree edges), which the Euler-tour
 * trees aggregate into counts; a flag changes only when a list becomes
 * empty or non-empty. The edges themselves sit in per-vertex, per-level lists
 * threaded through the edge records, whose heads live in a map (lib
 * /map_iface); edges are found by their ends through another map.
 *
 * Space: O(n log n) for the L Euler-tour forests plus O(m) for the edges.
 *
 * Failure: insert is all or nothing. A delete that runs out of memory
 * while recording a replacement edge or promoting a non-tree edge leaves
 * the structure broken (ds_dc_broken): queries are then meaningless and
 * updates return DS_ERR_STATE until ds_dc_rebuild, which rebuilds every
 * level from the current edges in O(m log n) and can be retried. A
 * promotion of a tree edge that cannot get memory is simply skipped
 * (only the amortized bound suffers). Memory comes from the context.
 */

#include "context.h"
#include "euler_tour_tree.h"
#include "map_iface.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_dc_edge {
  size_t end[2];
  size_t next[2]; /* in the list at end[k] */
  size_t prev[2];
  unsigned level;
  unsigned char tree; /* in F_0 .. F_level */
  unsigned char live;
} ds_dc_edge_t;

typedef struct ds_dynconn {
  size_t n;
  unsigned levels;
  ds_ett_t *forest; /* levels Euler-tour forests */
  unsigned char *forest_ready;
  ds_dc_edge_t *edge;
  size_t edge_capacity;
  size_t edge_slots; /* slots handed out, live or free */
  size_t free_edge;  /* free slots, linked through next[0] */
  size_t edges;
  size_t tree_edges;
  ds_map_t by_ends; /* (min, max) -> edge */
  ds_map_t heads;   /* (vertex, 2 level + kind) -> first edge */
  bool maps_ready;
  bool broken;
  unsigned long promotions;
  unsigned long replacements;
  ds_context_t *context;
} ds_dynconn_t;

ds_status_t ds_dc_init(ds_dynconn_t *g, size_t n, ds_context_t *context);
void ds_dc_destroy(ds_dynconn_t *g);
/* DS_EXISTS when the edge is there already, DS_ERR_INVALID for a loop,
 * DS_ERR_RANGE for a vertex out of range */
ds_status_t ds_dc_insert(ds_dynconn_t *g, size_t u, size_t v);
/* DS_NOT_FOUND when the edge is not there */
ds_status_t ds_dc_delete(ds_dynconn_t *g, size_t u, size_t v);
bool ds_dc_connected(const ds_dynconn_t *g, size_t u, size_t v);
bool ds_dc_has_edge(const ds_dynconn_t *g, size_t u, size_t v);
/* whether the edge is in the spanning forest F_0 */
bool ds_dc_is_tree_edge(const ds_dynconn_t *g, size_t u, size_t v);
size_t ds_dc_component_size(const ds_dynconn_t *g, size_t v);
size_t ds_dc_components(const ds_dynconn_t *g);
size_t ds_dc_edges(const ds_dynconn_t *g);
bool ds_dc_broken(const ds_dynconn_t *g);
/* every edge back to level 0 and every forest rebuilt; the way out of a
 * broken state (DS_ERR_ALLOC leaves it broken and can be retried) */
ds_status_t ds_dc_rebuild(ds_dynconn_t *g);
/* the invariants: forests nested and spanning their levels' edges, tree
 * sizes bounded by n / 2^i, lists and counts consistent. O((n + m) L log n)
 * (for tests and debugging) */
bool ds_dc_check(ds_dynconn_t *g);

#ifdef __cplusplus
}
#endif

#endif /* DS_DYNAMIC_CONNECTIVITY_H */
