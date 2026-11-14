#ifndef DS_DYNAMIC_MSF_H
#define DS_DYNAMIC_MSF_H

/*
 * Fully dynamic minimum spanning forest (Holm, de Lichtenberg and Thorup,
 * J. ACM 2001, section 5): a weighted multigraph on n vertices under edge
 * insertions and deletions, keeping a minimum spanning forest F, in
 * O(log^4 n) amortized time per update (expected, through the treaps of
 * the deletions-only structures).
 *
 * F lives in a link-cut tree in which every edge of F is a node of its
 * own, so the heaviest edge on a path is found in O(log n). An insertion
 * that closes a cycle swaps out the heaviest edge of the cycle if the new
 * edge is lighter. Deletions are where the reduction comes in: the edges
 * are covered by deletions-only structures A_0, A_1, ... (lib
 * /decremental_msf), A_i holding at most about 2^i edges, such that every
 * non-tree edge of the graph is a non-tree edge of exactly one A_i and a
 * tree edge only of later ones. Deleting an edge deletes it from every A_i
 * containing it; the lightest edge the A_i report as replacements and
 * that reconnects F replaces it (it is the lightest replacement overall).
 * The reported edges then go into the smallest A_j that can take them
 * together with the non-tree edges of A_0 .. A_j, which is rebuilt and
 * the ones below it emptied, as in a binary counter.
 *
 * An A_j does not hold F itself but F compressed: the paths of F joining
 * the ends of its non-tree edges, cut at their branching points, become
 * super edges weighing as much as the heaviest edge on them, so A_j has
 * O(2^j) edges and vertices. The compression is built in O(log n) per
 * end with the link-cut tree: from a root, each new end walks up to the
 * nearest vertex already on the compressed tree (a search for the deepest
 * marked vertex on its path), splitting the super edge it meets. Every
 * edge of F carries, per A_i, the super edge covering it, assigned to
 * whole paths at once by lazy tags, so the super edges a deletion
 * destroys are found in O(log n) each. Swapping an edge out of F leaves
 * the super edges over it valid: they still stand for paths of the graph
 * no heavier than their weight.
 *
 * Weights are doubles; equal weights are ordered by edge id, so the
 * forest is unique. Edge ids are handed out by insert and reused after
 * deletion. Space: O((n + m) log m) for the per-structure tags, plus the
 * structures themselves.
 *
 * Failure: an insertion that cannot get its edge record changes nothing;
 * an update that runs out of memory while rebuilding a structure (or a
 * deletion that cannot record its replacements) leaves the structure
 * broken (ds_dyn_msf_broken): updates then return DS_ERR_STATE until
 * ds_dyn_msf_rebuild, which builds everything again from the live edges.
 * Memory comes from the context.
 */

#include "context.h"
#include "decremental_msf.h"
#include "map_iface.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_DYN_MSF_NONE ((size_t) - 1)

typedef struct ds_dyn_msf_edge {
  size_t end[2];
  double weight;
  unsigned char live;
  unsigned char tree; /* in F */
} ds_dyn_msf_edge_t;

/* the link-cut tree's node: vertices 0 .. n-1, then one per edge id */
typedef struct ds_dyn_msf_node {
  size_t ch[2];
  size_t parent; /* in the splay tree, or the path parent */
  size_t best;   /* the heaviest edge node in the splay subtree */
  size_t mark;   /* a build's generation */
  size_t max_mark;
  size_t lazy_mark;
  unsigned char rev;
  unsigned char lazy_tags;
} ds_dyn_msf_node_t;

/* one deletions-only structure A_i over compressed F */
typedef struct ds_dyn_msf_slot {
  ds_dmsf_t msf;
  bool ready;
  size_t edges;   /* local edges */
  size_t *global; /* local edge -> graph edge id, or super id */
  unsigned char *is_super;
  size_t base; /* super ids base .. base + supers - 1 */
  size_t supers;
  size_t *super_local; /* super id - base -> local edge (or NONE) */
  ds_map_t local;      /* graph edge id -> local edge */
  bool map_ready;
} ds_dyn_msf_slot_t;

typedef struct ds_dyn_msf {
  size_t n;
  ds_dyn_msf_edge_t *edge;
  size_t edge_capacity;
  size_t edge_slots; /* ids handed out, live or free */
  size_t free_edge;  /* free ids, linked through end[0] */
  size_t edges;
  size_t tree_edges;
  double weight;           /* of F, a running sum */
  ds_dyn_msf_node_t *node; /* n + edge_capacity */
  size_t *tag;  /* node * slot_capacity + slot: the super id over a node */
  size_t *lazy; /* pending tag assignments, the same layout */
  ds_dyn_msf_slot_t *slot;
  size_t slot_capacity;
  size_t slots; /* in use: A_0 .. A_{slots-1} */
  size_t next_super;
  size_t generation;
  size_t *path;  /* splay scratch, one per node */
  size_t *stamp; /* per vertex: build scratch */
  size_t *virt;
  size_t *local_id;
  bool broken;
  unsigned long rebuilds; /* structures built, for inspection */
  ds_context_t *context;
} ds_dyn_msf_t;

ds_status_t ds_dyn_msf_init(ds_dyn_msf_t *g, size_t n, ds_context_t *context);
void ds_dyn_msf_destroy(ds_dyn_msf_t *g);
/* add edge (u, v) of weight w (u == v allowed, never in F; NaN is
 * DS_ERR_INVALID); its id to *out_id */
ds_status_t ds_dyn_msf_insert(ds_dyn_msf_t *g, size_t u, size_t v, double w,
                              size_t *out_id);
/* DS_NOT_FOUND for an id not in use */
ds_status_t ds_dyn_msf_delete(ds_dyn_msf_t *g, size_t id);

bool ds_dyn_msf_connected(ds_dyn_msf_t *g, size_t u, size_t v);
bool ds_dyn_msf_live(const ds_dyn_msf_t *g, size_t id);
bool ds_dyn_msf_in_forest(const ds_dyn_msf_t *g, size_t id);
/* the heaviest edge of F on the u-v path: DS_NOT_FOUND when u and v are
 * not connected (or equal) */
ds_status_t ds_dyn_msf_path_max(ds_dyn_msf_t *g, size_t u, size_t v,
                                size_t *out_id);
size_t ds_dyn_msf_edges(const ds_dyn_msf_t *g);
size_t ds_dyn_msf_forest_edges(const ds_dyn_msf_t *g);
/* the forest's weight (a running sum: rounding may accumulate) */
double ds_dyn_msf_weight(const ds_dyn_msf_t *g);

bool ds_dyn_msf_broken(const ds_dyn_msf_t *g);
/* F from scratch by Kruskal over the live edges, every edge in one A_j */
ds_status_t ds_dyn_msf_rebuild(ds_dyn_msf_t *g);
/* F against Kruskal over the live edges, the link-cut tree's links, the
 * reduction's invariant (each non-tree edge a non-tree edge of exactly one
 * A_i, a tree edge only of later ones) and every A_i's own check */
bool ds_dyn_msf_check(ds_dyn_msf_t *g);

#ifdef __cplusplus
}
#endif

#endif /* DS_DYNAMIC_MSF_H */
