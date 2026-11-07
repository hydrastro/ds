#ifndef DS_ALGO_LCA_H
#define DS_ALGO_LCA_H

/*
 * Lowest common ancestors in rooted forests given as parent arrays
 * (parent[v] = DS_LCA_NONE for a root). Nodes in different trees have no
 * common ancestor (DS_LCA_NONE).
 *
 *   ds_lca_lifting_t  binary lifting (jump pointers): O(n log n) to build,
 *                     O(log n) per query; also k-th ancestors and
 *                     distances
 *   ds_lca_euler_t    Euler tour plus a sparse table of depths: O(n log n)
 *                     to build, O(1) per query (LCA as range minimum)
 *   ds_lca_offline    Tarjan's offline algorithm: a batch of queries
 *                     answered in one depth-first pass with union-find,
 *                     O((n + q) alpha(n))
 *
 * Traversals are iterative. Memory comes from the ds_context_t.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_LCA_NONE (~(size_t)0)

typedef struct ds_lca_lifting {
  size_t n;
  size_t levels;
  size_t *up; /* levels x n */
  size_t *depth;
  size_t *root;
  ds_context_t *context;
} ds_lca_lifting_t;

ds_status_t ds_lca_lifting_build(ds_lca_lifting_t *l, const size_t *parent,
                                 size_t n, ds_context_t *context);
void ds_lca_lifting_destroy(ds_lca_lifting_t *l);
size_t ds_lca_lifting_query(const ds_lca_lifting_t *l, size_t u, size_t v);
/* The k-th ancestor of v (0: v itself), or DS_LCA_NONE above the root. */
size_t ds_lca_lifting_ancestor(const ds_lca_lifting_t *l, size_t v, size_t k);
/* Edges between u and v, or DS_LCA_NONE in different trees. */
size_t ds_lca_lifting_distance(const ds_lca_lifting_t *l, size_t u, size_t v);

typedef struct ds_lca_euler {
  size_t n;
  size_t len;    /* Euler tour length */
  size_t *tour;  /* nodes */
  size_t *depth; /* per node */
  size_t *first; /* first tour position per node */
  size_t *root;
  size_t *table; /* sparse table of tour positions, levels x len */
  size_t *lg;
  size_t levels;
  ds_context_t *context;
} ds_lca_euler_t;

ds_status_t ds_lca_euler_build(ds_lca_euler_t *e, const size_t *parent,
                               size_t n, ds_context_t *context);
void ds_lca_euler_destroy(ds_lca_euler_t *e);
size_t ds_lca_euler_query(const ds_lca_euler_t *e, size_t u, size_t v);

/* answers[i] = LCA(qu[i], qv[i]). */
ds_status_t ds_lca_offline(const size_t *parent, size_t n, const size_t *qu,
                           const size_t *qv, size_t q, size_t *answers,
                           ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_LCA_H */
