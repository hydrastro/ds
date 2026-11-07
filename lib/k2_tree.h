#ifndef DS_K2_TREE_H
#define DS_K2_TREE_H

/*
 * ds_k2_tree_t, the k^2-tree of Brisaboa, Ladra and Navarro: a compact
 * static representation of a sparse n x n binary matrix (the adjacency
 * matrix of a graph). The matrix, padded to k^h x k^h, is split into k^2
 * submatrices, each recursively, and every node is a bit telling whether
 * its submatrix has a one; the bits of the internal levels form T and those
 * of the last level L, read in breadth-first order. A node's children start
 * at rank1(T, node) * k^2, so the structure is just two bit vectors plus a
 * rank directory, using a few bits per one for clustered matrices.
 *
 * Queries: a single cell, the ones of a row (successors), of a column
 * (predecessors), and of any rectangle, each by one descent pruned at the
 * zero bits. Build from a list of cells in O(m h). Memory comes from the
 * ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* false stops */
typedef bool (*ds_k2_visit_func_t)(size_t row, size_t col, void *user);

typedef struct ds_k2_tree {
  size_t n; /* matrix side */
  size_t k;
  size_t height;    /* levels */
  size_t side;      /* k^height */
  unsigned long *t; /* T bits, 32 per word */
  size_t t_bits;
  size_t *t_rank;   /* ones before each word */
  unsigned long *l; /* L bits */
  size_t l_bits;
  size_t ones;
  ds_context_t *context;
} ds_k2_tree_t;

/* k >= 2 (0: 2); rows/cols: m cells (duplicates allowed), each < n. */
ds_status_t ds_k2_tree_build(ds_k2_tree_t *t, size_t n, size_t k,
                             const size_t *rows, const size_t *cols, size_t m,
                             ds_context_t *context);
void ds_k2_tree_destroy(ds_k2_tree_t *t);
size_t ds_k2_tree_ones(const ds_k2_tree_t *t);
/* T and L sizes in bits */
size_t ds_k2_tree_bits(const ds_k2_tree_t *t);
bool ds_k2_tree_get(const ds_k2_tree_t *t, size_t row, size_t col);
/* ones in rows r0..r1 and columns c0..c1 (inclusive), row-major per
 * submatrix order */
ds_status_t ds_k2_tree_range(const ds_k2_tree_t *t, size_t r0, size_t r1,
                             size_t c0, size_t c1, ds_k2_visit_func_t visit,
                             void *user);
ds_status_t ds_k2_tree_successors(const ds_k2_tree_t *t, size_t row,
                                  ds_k2_visit_func_t visit, void *user);
ds_status_t ds_k2_tree_predecessors(const ds_k2_tree_t *t, size_t col,
                                    ds_k2_visit_func_t visit, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_K2_TREE_H */
