#ifndef DS_TOP_TREE_H
#define DS_TOP_TREE_H

/*
 * Top trees (Alstrup, Holm, de Lichtenberg and Thorup) for dynamic forests
 * with information on both paths and subtrees, in the self-adjusting form of
 * Tarjan and Werneck: amortized O(log n) per link, cut or query.
 *
 * Every tree is kept as a hierarchy of clusters. A path cluster is a path
 * with everything that hangs from its vertices; a point cluster is a
 * subtree hanging from one vertex. The caller defines what a cluster
 * summarises through five functions (ds_top_tree_ops_t):
 *
 *   vertex(out, value, point)  the path cluster of one vertex, with the
 *                              point cluster of all that hangs from it
 *                              (point NULL when nothing does)
 *   compress(out, near, far)   two path clusters joined end to end, near
 *                              being the part closer to the path's start
 *   to_point(out, path)        a hanging subtree as a point cluster, from
 *                              the path cluster of its top path
 *   rake(out, a, b)            two point clusters hanging from one vertex
 *                              (associative and commutative)
 *   reverse(path)              the same path read from the other end (NULL
 *                              when summaries do not depend on direction)
 *
 * so path aggregates (sums, maxima, ordered hashes), subtree aggregates and
 * mixtures of both (a component's diameter, a vertex's farthest leaf) all
 * fit, without needing inverses. Edge values are vertices of their own: an
 * edge u-v with a value becomes u-e-v.
 *
 * Representation: each preferred path is a splay tree of its vertices
 * (the compress tree), and the subtrees hanging from a vertex form a splay
 * tree of their own (the rake tree, whose leaves hold the hanging
 * compress trees); a vertex's summary combines its path neighbours, its
 * value and its rake tree. Exposing a path splices preferred paths as in
 * link-cut trees, splaying in both kinds of trees, and making a vertex the
 * root reverses its path lazily.
 *
 * Vertices are 0 .. n - 1, fixed at init; all memory (vertices, at most n
 * rake nodes, a splay stack) is taken at init, so no operation allocates
 * or fails for lack of memory afterwards. Nothing recurses.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_top_tree_ops {
  size_t value_size; /* per vertex (may be 0) */
  size_t path_size;  /* a path cluster summary */
  size_t point_size; /* a point cluster summary */
  void (*vertex)(void *out_path, const void *value, const void *point,
                 void *user);
  void (*compress)(void *out_path, const void *near, const void *far,
                   void *user);
  void (*to_point)(void *out_point, const void *path, void *user);
  void (*rake)(void *out_point, const void *a, const void *b, void *user);
  void (*reverse)(void *path, void *user);
  void *user;
} ds_top_tree_ops_t;

typedef struct ds_top_tree {
  size_t n;
  ds_top_tree_ops_t ops;
  unsigned char *vertices; /* n vertex nodes of vstride bytes */
  unsigned char *rakes;    /* n rake nodes of rstride bytes */
  size_t vstride;
  size_t rstride;
  size_t value_at; /* offsets in a vertex node */
  size_t sum_at;
  size_t point_at; /* offset in a rake node */
  void *free_rakes;
  void **stack;           /* splay scratch, n entries */
  unsigned char *scratch; /* summary scratch */
  ds_context_t *context;
} ds_top_tree_t;

/* n isolated vertices with the given values (n * value_size bytes; NULL
 * for all-zero values) */
ds_status_t ds_top_tree_init(ds_top_tree_t *t, size_t n,
                             const ds_top_tree_ops_t *ops, const void *values,
                             ds_context_t *context);
void ds_top_tree_destroy(ds_top_tree_t *t);

/* join two trees by the edge u-v; DS_ERR_INVALID if already connected */
ds_status_t ds_top_link(ds_top_tree_t *t, size_t u, size_t v);
/* remove the edge u-v; DS_NOT_FOUND if there is none */
ds_status_t ds_top_cut(ds_top_tree_t *t, size_t u, size_t v);
bool ds_top_connected(ds_top_tree_t *t, size_t u, size_t v);
bool ds_top_adjacent(ds_top_tree_t *t, size_t u, size_t v);

/* the path cluster with u-v as its spine: the path from u to v with all
 * that hangs from it (the whole tree); DS_NOT_FOUND if not connected */
ds_status_t ds_top_expose(ds_top_tree_t *t, size_t u, size_t v, void *out_path);
/* v's subtree when the tree is rooted at root, as the path cluster of v
 * alone with all that hangs below it; DS_NOT_FOUND if not connected */
ds_status_t ds_top_subtree(ds_top_tree_t *t, size_t v, size_t root,
                           void *out_path);

const void *ds_top_value(const ds_top_tree_t *t, size_t v);
void ds_top_set_value(ds_top_tree_t *t, size_t v, const void *value);

/* a search along the path u..v by its clusters: choose sees the cluster
 * of the path before a candidate vertex (NULL at the start) and the
 * candidate's own cluster (it with all that hangs from it), and returns a
 * negative value to look before the candidate, 0 to take it, positive to
 * look after it; it must point consistently (as for a binary search: the
 * k-th vertex, the first prefix to reach a bound). *out_vertex gets the
 * vertex taken; DS_NOT_FOUND when none is taken or u, v are not
 * connected. O(log n) amortized. */
typedef int (*ds_top_choice_fn)(const void *before, const void *here,
                                void *user);
ds_status_t ds_top_path_search(ds_top_tree_t *t, size_t u, size_t v,
                               ds_top_choice_fn choose, void *user,
                               size_t *out_vertex);

/* explicit rooting: make v the root of its tree; the root of v's tree */
void ds_top_evert(ds_top_tree_t *t, size_t v);
size_t ds_top_find_root(ds_top_tree_t *t, size_t v);

#ifdef __cplusplus
}
#endif

#endif /* DS_TOP_TREE_H */
