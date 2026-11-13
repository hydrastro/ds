#ifndef DS_DECREMENTAL_MSF_H
#define DS_DECREMENTAL_MSF_H

/*
 * Deletions-only minimum spanning forest (Holm, de Lichtenberg and
 * Thorup, J. ACM 2001, section 4): built once from a weighted multigraph
 * on n vertices, it keeps a minimum spanning forest F while edges are
 * deleted, each deletion reporting the edge (if any) that replaced it in
 * F. O(log^2 n) amortized per deletion (expected, through the treaps of
 * the Euler-tour trees) after an O(m log m + n log n) start.
 *
 * Weights are doubles; equal weights are ordered by edge index, so the
 * forest is unique. The edge levels and forests F_0, F_1, ... are those of
 * the connectivity structure (lib/dynamic_connectivity): every edge starts
 * at level 0 and only moves up, F_i spans the edges of level at least i
 * and a tree of F_i has at most n / 2^i vertices. What changes is the
 * search for a replacement: after a tree edge of level l is cut, for i
 * from l down to 0, the smaller of the two trees of F_i has its level-i
 * tree edges promoted, and its level-i non-tree edges are taken lightest
 * first: one leaving the tree replaces the deleted edge, one inside it is
 * promoted. The lightest replacement lies on the highest level that has
 * one, so it is the first found, and the forest stays minimum because
 * every cycle keeps a non-tree edge that is both its heaviest and of its
 * lowest level.
 *
 * Each vertex keeps its level-i tree edges in a list and its level-i
 * non-tree edges in a pairing heap ordered by weight, threaded through
 * the edge records, with heads in a map (lib/map_iface). The Euler-tour
 * tree of F_i aggregates, per tree, how many vertices have level-i tree
 * edges and the lightest level-i non-tree edge at any vertex, so the next
 * edge to examine is found in O(log n).
 *
 * Failure: a deletion that runs out of memory while promoting or recording
 * a replacement leaves the structure broken (ds_dmsf_broken): updates
 * then return DS_ERR_STATE until ds_dmsf_rebuild, which starts over from
 * the remaining edges. Memory comes from the context.
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

#define DS_DMSF_NONE ((size_t) - 1)

typedef struct ds_dmsf_edge {
  size_t end[2];
  double weight;
  /* per end: the tree list (next, prev) or the non-tree heap (child, next,
   * prev: the parent for a first child) */
  size_t next[2];
  size_t prev[2];
  size_t child[2];
  unsigned level;
  unsigned char tree; /* in F_0 .. F_level */
  unsigned char live;
} ds_dmsf_edge_t;

typedef struct ds_dmsf {
  size_t n;
  size_t m;
  unsigned levels;
  ds_ett_t *forest; /* levels Euler-tour forests */
  unsigned char *forest_ready;
  ds_dmsf_edge_t *edge; /* m records, by index */
  ds_map_t heads;       /* (vertex, 2 level + kind) -> first edge */
  bool map_ready;
  bool broken;
  size_t edges;      /* live */
  size_t tree_edges; /* in F */
  double weight;     /* of F, kept as a running sum */
  unsigned long promotions;
  unsigned long replacements;
  ds_context_t *context;
} ds_dmsf_t;

/* m edges (u[i], v[i]) of weight w[i] on n vertices; edge i keeps index
 * i. Self-loops are allowed (never in F), NaN weights are not
 * (DS_ERR_INVALID). */
ds_status_t ds_dmsf_init(ds_dmsf_t *g, size_t n, const size_t *u,
                         const size_t *v, const double *w, size_t m,
                         ds_context_t *context);
void ds_dmsf_destroy(ds_dmsf_t *g);
/* delete edge e; *out_replacement (optional) is the edge that took its
 * place in F, or DS_DMSF_NONE (also when e was not in F). DS_NOT_FOUND
 * for an edge already deleted. */
ds_status_t ds_dmsf_delete(ds_dmsf_t *g, size_t e, size_t *out_replacement);

bool ds_dmsf_connected(const ds_dmsf_t *g, size_t u, size_t v);
size_t ds_dmsf_component_size(const ds_dmsf_t *g, size_t v);
bool ds_dmsf_live(const ds_dmsf_t *g, size_t e);
bool ds_dmsf_in_forest(const ds_dmsf_t *g, size_t e);
/* the edge's level (for inspection) */
unsigned ds_dmsf_level(const ds_dmsf_t *g, size_t e);
size_t ds_dmsf_edges(const ds_dmsf_t *g);
size_t ds_dmsf_forest_edges(const ds_dmsf_t *g);
/* the forest's weight (a running sum: rounding may accumulate) */
double ds_dmsf_weight(const ds_dmsf_t *g);

bool ds_dmsf_broken(const ds_dmsf_t *g);
/* the structure from scratch over the live edges, all at level 0 */
ds_status_t ds_dmsf_rebuild(ds_dmsf_t *g);
/* the levels, the forests, the lists, heaps and aggregates, invariant (i)
 * (a non-tree edge's ends are connected at its level), the tree sizes,
 * and that F is the minimum spanning forest of the live edges (against
 * Kruskal); O(m log m + n log^2 n) */
bool ds_dmsf_check(ds_dmsf_t *g);

#ifdef __cplusplus
}
#endif

#endif /* DS_DECREMENTAL_MSF_H */
