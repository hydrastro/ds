#ifndef DS_DFUDS_H
#define DS_DFUDS_H

/*
 * Depth-first unary degree sequences (Benoit, Demaine, Munro, Raman, Raman
 * and Rao, 2005): a static ordered tree of n nodes in 2n parentheses,
 * navigated with the parentheses core of lib/bp_tree.
 *
 * Nodes are visited in preorder and each writes its degree in unary: d
 * opens and a close. A leading open balances the sequence. Node numbers
 * are preorder numbers; node r's description starts after the r-th close
 * (counting from 1), so numbers and positions convert with one rank or
 * select of zeros. Compared with the balanced-parentheses form:
 *
 *   degree(v)          one select: the length of v's run of opens
 *   child(v, k)        one select and one find_close (BP counts minima)
 *   parent, child_rank one find_open and one rank
 *   siblings           one find_close
 *   subtree size       one forward search
 *   lca                parent(rmq(u, v - 1) + 1) (Jansson, Sadakane and
 *                      Sung)
 *
 * Depth, height and level ancestors are not supported in this form; use
 * lib/bp_tree for them.
 *
 * Trees come from a parent array (siblings in input order; the preorder
 * numbers of the input nodes are reported) or from a parentheses
 * sequence, checked to describe one tree. Memory comes from the context.
 */

#include "bp_tree.h"
#include "context.h"
#include "rank_select.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_dfuds {
  ds_parens_t p;
  size_t nodes;
} ds_dfuds_t;

/* parent[i] is the parent of input node i (DS_BP_NONE for the root);
 * out_number (optional) receives each input node's preorder number */
ds_status_t ds_dfuds_from_parents(ds_dfuds_t *t, const size_t *parent, size_t n,
                                  size_t *out_number, const ds_rs_ops_t *rs_ops,
                                  ds_context_t *context);
/* a DFUDS sequence of 2n bits (or 0 bits) */
ds_status_t ds_dfuds_from_parens(ds_dfuds_t *t, const unsigned long *words,
                                 size_t bits, const ds_rs_ops_t *rs_ops,
                                 ds_context_t *context);
void ds_dfuds_destroy(ds_dfuds_t *t);

size_t ds_dfuds_nodes(const ds_dfuds_t *t);
/* where node v's description starts, and the node starting there */
size_t ds_dfuds_position(const ds_dfuds_t *t, size_t v);
size_t ds_dfuds_node_at(const ds_dfuds_t *t, size_t position);
/* DS_BP_NONE when there is no such node */
size_t ds_dfuds_parent(const ds_dfuds_t *t, size_t v);
size_t ds_dfuds_degree(const ds_dfuds_t *t, size_t v);
/* the k-th child (0-based) */
size_t ds_dfuds_child(const ds_dfuds_t *t, size_t v, size_t k);
size_t ds_dfuds_first_child(const ds_dfuds_t *t, size_t v);
size_t ds_dfuds_last_child(const ds_dfuds_t *t, size_t v);
size_t ds_dfuds_next_sibling(const ds_dfuds_t *t, size_t v);
size_t ds_dfuds_prev_sibling(const ds_dfuds_t *t, size_t v);
/* how many siblings precede v */
size_t ds_dfuds_child_rank(const ds_dfuds_t *t, size_t v);
bool ds_dfuds_is_leaf(const ds_dfuds_t *t, size_t v);
size_t ds_dfuds_subtree_size(const ds_dfuds_t *t, size_t v);
bool ds_dfuds_is_ancestor(const ds_dfuds_t *t, size_t u, size_t v);
size_t ds_dfuds_lca(const ds_dfuds_t *t, size_t u, size_t v);
size_t ds_dfuds_bytes(const ds_dfuds_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DS_DFUDS_H */
