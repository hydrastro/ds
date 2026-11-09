#ifndef DS_ADJ_MATRIX_H
#define DS_ADJ_MATRIX_H

/*
 * Adjacency-matrix graph representation: an owning dense graph on a fixed
 * vertex set 0 .. n-1, for dense graphs and for algorithms that ask "is
 * there an edge u -> v" far more often than they enumerate.
 *
 * Each row is an n-entry weight array plus a sparse set of the present
 * targets (Briggs and Torczon): row v keeps its targets packed in
 * list[v][0 .. degree(v)) and pos[v][t] gives t's index there, so
 *
 *   set, remove, has, weight      O(1)
 *   degree, i-th edge of a row    O(1)  (rows enumerate in O(degree),
 *                                        not O(n))
 *   clear                         O(n)
 *
 * at 3 n^2 words of memory (weights, positions, packed targets). Removal
 * moves the row's last target into the hole, so the edge order of a row
 * changes when one of its edges is removed.
 *
 * Undirected matrices store both directions of every edge (a self loop
 * once); set and remove keep them in step. algo/graph_view's
 * ds_gview_from_adj_matrix() runs every graph-view algorithm on a matrix,
 * and ds_adj_matrix_to_dense() exports the weights for matrix algorithms
 * such as Floyd-Warshall. Memory comes from the ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_adj_matrix {
  size_t n;
  size_t edges; /* stored directed entries (an undirected edge counts 2,
                   a self loop 1) */
  bool directed;
  double *weight; /* n * n */
  size_t *pos;    /* n * n: index of target t in row v's list */
  size_t *list;   /* n * n: row v's targets, degree[v] of them */
  size_t *degree; /* n */
  ds_context_t *context;
} ds_adj_matrix_t;

/* DS_ERR_OVERFLOW when 3 n^2 words do not fit in size_t */
ds_status_t ds_adj_matrix_init(ds_adj_matrix_t *m, size_t n, bool directed,
                               ds_context_t *context);
void ds_adj_matrix_destroy(ds_adj_matrix_t *m);

/* add u -> v (and v -> u when undirected) with weight w: DS_OK when the
 * edge is new, DS_EXISTS when its weight was replaced; DS_ERR_RANGE for a
 * vertex out of range */
ds_status_t ds_adj_matrix_set(ds_adj_matrix_t *m, size_t u, size_t v, double w);
/* DS_OK, or DS_NOT_FOUND when there is no such edge */
ds_status_t ds_adj_matrix_remove(ds_adj_matrix_t *m, size_t u, size_t v);
bool ds_adj_matrix_has(const ds_adj_matrix_t *m, size_t u, size_t v);
/* DS_OK and the weight, or DS_NOT_FOUND */
ds_status_t ds_adj_matrix_weight(const ds_adj_matrix_t *m, size_t u, size_t v,
                                 double *out_weight);
size_t ds_adj_matrix_vertex_count(const ds_adj_matrix_t *m);
/* stored directed entries, see the struct */
size_t ds_adj_matrix_entry_count(const ds_adj_matrix_t *m);
size_t ds_adj_matrix_degree(const ds_adj_matrix_t *m, size_t v);
/* the index-th edge of row v (index < degree(v)); out_weight optional */
void ds_adj_matrix_edge(const ds_adj_matrix_t *m, size_t v, size_t index,
                        size_t *out_target, double *out_weight);
/* remove every edge in O(n) */
void ds_adj_matrix_clear(ds_adj_matrix_t *m);
/* out (n * n doubles, row major): the weight of each edge, absent for
 * missing ones and diagonal for v -> v when there is no self loop */
void ds_adj_matrix_to_dense(const ds_adj_matrix_t *m, double absent,
                            double diagonal, double *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_ADJ_MATRIX_H */
