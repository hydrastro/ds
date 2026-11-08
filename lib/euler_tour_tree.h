#ifndef DS_EULER_TOUR_TREE_H
#define DS_EULER_TOUR_TREE_H

/*
 * Euler-tour trees (Henzinger & King, 1995): a dynamic forest on n
 * vertices in which each tree is kept as its Euler tour (one occurrence
 * per vertex plus the two arcs of every edge) in a balanced sequence
 * tree, here a treap with parent pointers. Rerooting rotates a tour,
 * linking concatenates two tours around a new arc pair, cutting splits
 * a tour at the edge's arcs. Expected O(log n) per operation.
 *
 * Unlike link-cut trees (path aggregates), Euler-tour trees aggregate
 * whole components and subtrees: the summary of a vertex's component,
 * or of the side of an edge (v, p) containing v, with lazy actions
 * applied to the same sets. Element, summary and tag are user-defined
 * byte blobs with the callbacks of ds_lct_config_t (combine must be
 * associative; the order follows the tour, so commutative operations
 * are the usual choice).
 *
 * Edges are found through an associative map chosen by the caller
 * (lib/map_iface, default Robin Hood hashing). Memory comes from the
 * context.
 */

#include "context.h"
#include "map_iface.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_ett_config {
  size_t elem_size;    /* may be 0 */
  size_t summary_size; /* 0: no aggregates */
  void (*summarize)(void *out, const void *elem, void *user);
  void (*combine)(void *out, const void *left, const void *right, void *user);
  size_t tag_size; /* 0: no actions */
  void (*tag_apply_elem)(void *elem, const void *tag, void *user);
  void (*tag_apply_summary)(void *summary, const void *tag, size_t count,
                            void *user);
  void (*tag_compose)(void *tag, const void *newer, void *user);
  void *user;
  ds_context_t *context;
  const ds_map_ops_t *map_ops; /* NULL: Robin Hood map */
  unsigned long seed;          /* treap priorities */
} ds_ett_config_t;

typedef struct ds_ett_node {
  struct ds_ett_node *left;
  struct ds_ett_node *right;
  struct ds_ett_node *parent;
  unsigned long priority;
  size_t size;   /* tour entries in the subtree */
  size_t vcount; /* vertex entries in the subtree */
  size_t vertex; /* owning vertex, (size_t)-1 for arcs */
  int tagged;
} ds_ett_node_t;

typedef bool (*ds_ett_visit_func_t)(size_t vertex, const void *elem,
                                    void *user);

typedef struct ds_ett {
  size_t n;
  size_t edges;
  ds_ett_config_t config;
  size_t node_size;
  size_t elem_offset;
  size_t summary_offset;
  size_t tag_offset;
  unsigned char *vertices; /* n nodes */
  ds_map_t edge_map;       /* (min, max) -> arc pair */
  unsigned long rng;
  unsigned char *scratch; /* two summaries */
} ds_ett_t;

void ds_ett_config_init(ds_ett_config_t *config);
/* elems: n elements (NULL: zeroed) */
ds_status_t ds_ett_init(ds_ett_t *t, size_t n, const void *elems,
                        const ds_ett_config_t *config);
void ds_ett_destroy(ds_ett_t *t);
/* DS_ERR_STATE when u and v are already connected (or u == v) */
ds_status_t ds_ett_link(ds_ett_t *t, size_t u, size_t v);
/* DS_NOT_FOUND when the edge does not exist */
ds_status_t ds_ett_cut(ds_ett_t *t, size_t u, size_t v);
bool ds_ett_connected(const ds_ett_t *t, size_t u, size_t v);
bool ds_ett_has_edge(const ds_ett_t *t, size_t u, size_t v);
size_t ds_ett_edge_count(const ds_ett_t *t);
size_t ds_ett_component_count(const ds_ett_t *t);
size_t ds_ett_component_size(const ds_ett_t *t, size_t v);
ds_status_t ds_ett_component_query(ds_ett_t *t, size_t v, void *out_summary);
ds_status_t ds_ett_component_apply(ds_ett_t *t, size_t v, const void *tag);
/* the side of edge (v, p) containing v; DS_NOT_FOUND without the edge */
ds_status_t ds_ett_subtree_size(ds_ett_t *t, size_t v, size_t p,
                                size_t *out_size);
ds_status_t ds_ett_subtree_query(ds_ett_t *t, size_t v, size_t p,
                                 void *out_summary);
ds_status_t ds_ett_subtree_apply(ds_ett_t *t, size_t v, size_t p,
                                 const void *tag);
ds_status_t ds_ett_get(ds_ett_t *t, size_t v, void *out_elem);
ds_status_t ds_ett_set(ds_ett_t *t, size_t v, const void *elem);
/* the vertices of v's component in tour order (pending actions applied
 * first); stops when visit returns false */
ds_status_t ds_ett_visit_component(ds_ett_t *t, size_t v,
                                   ds_ett_visit_func_t visit, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_EULER_TOUR_TREE_H */
