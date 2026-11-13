#ifndef DS_BP_TREE_H
#define DS_BP_TREE_H

/*
 * Succinct ordered trees in balanced-parentheses form, navigated through
 * a range min-max tree (Navarro and Sadakane, "Fully functional static
 * and dynamic succinct trees", 2014).
 *
 * The parentheses core (ds_parens_t) is reusable on its own: any bit
 * sequence read as parentheses (1 = open, 0 = close), with
 *
 *   excess(i)            opens minus closes in [0, i]
 *   fwd_search(i, d)     the first j > i with excess(j) = excess(i) + d
 *   bwd_search(i, d)     the last j < i with excess(j) = excess(i) + d
 *                        (j = -1, the empty prefix with excess 0, counts)
 *   find_close, find_open, enclose
 *   range minimum and maximum of the excess over [i, j], the leftmost
 *   position of the minimum, how many times it occurs and where its k-th
 *   occurrence is
 *
 * A search scans the bits of its own block a byte at a time (constant
 * tables say whether a byte reaches the target), then climbs and
 * descends a complete binary tree over the blocks whose nodes keep the
 * least and greatest excess of their range and how often the least one
 * occurs: O(B / 8 + log(n / B)) for blocks of B = DS_PARENS_BLOCK bits.
 * rank and select come from any lib/rank_select backend over the same
 * bits. Space: the n bits, 4 words per tree node (about 2 nodes per
 * block: 0.5 bit per parenthesis on 64-bit targets) and the rank/select
 * directory.
 *
 * The tree (ds_bp_tree_t) writes a node as an open parenthesis, its
 * subtrees in order, and a close: 2n bits for n nodes. Nodes are
 * numbered in preorder (the root is 0), so a subtree is a contiguous
 * range of numbers. Every operation maps the number to its open
 * parenthesis (one select), answers with the searches above, and maps
 * back (one rank):
 *
 *   parent, first/last child, next/previous sibling, is_leaf
 *   degree, child(v, k), child_rank      counting minima of the excess
 *   depth, height, subtree size, is_ancestor
 *   lca                                  enclose(rmq(u, v) + 1)
 *   level_ancestor(v, d)                 one backward search
 *   postorder number
 *
 * Trees come from a parent array (siblings in input order; the level
 * numbers of the input nodes are reported) or from a parentheses
 * sequence; both are checked to be one tree (DS_ERR_INVALID otherwise).
 * Memory comes from the context.
 */

#include "context.h"
#include "rank_select.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_BP_NONE (~(size_t)0)
#define DS_PARENS_BLOCK 512 /* bits per leaf of the range min-max tree */

typedef struct ds_parens {
  unsigned long *words;
  size_t n; /* parentheses */
  size_t blocks;
  size_t leaves; /* a power of two >= blocks; node 1 is the root */
  long *mn;      /* per tree node: the least excess in its range */
  long *mx;      /* the greatest */
  long *end;     /* the excess at the end of the range */
  size_t *cnt;   /* how often the least excess occurs */
  ds_rs_t rs;
  ds_context_t *context;
} ds_parens_t;

/* bits as unsigned long words, bit i at (words[i / W] >> (i % W)) & 1;
 * copied. rs_ops NULL: ds_rs_plain(). Any sequence is accepted; the
 * searches simply report DS_BP_NONE when nothing matches. */
ds_status_t ds_parens_build(ds_parens_t *p, const unsigned long *words,
                            size_t n, const ds_rs_ops_t *rs_ops,
                            ds_context_t *context);
void ds_parens_destroy(ds_parens_t *p);
bool ds_parens_bit(const ds_parens_t *p, size_t i);
long ds_parens_excess(const ds_parens_t *p, size_t i);
size_t ds_parens_fwd_search(const ds_parens_t *p, size_t i, long d);
/* DS_BP_NONE when no such j; the empty prefix (j = -1) is reported as
 * *before = true with DS_BP_NONE returned (before may be NULL) */
size_t ds_parens_bwd_search(const ds_parens_t *p, size_t i, long d,
                            bool *before);
/* the matching close of the open at i, the matching open of the close at
 * i, and the open enclosing the pair at i; DS_BP_NONE when there is none */
size_t ds_parens_find_close(const ds_parens_t *p, size_t i);
size_t ds_parens_find_open(const ds_parens_t *p, size_t i);
size_t ds_parens_enclose(const ds_parens_t *p, size_t i);
/* over the excess of positions i..j (i <= j < n): the least value and
 * how often it occurs (count may be NULL), the greatest value, the
 * leftmost position of the least, and the position of its k-th
 * occurrence (0-based; DS_BP_NONE past the last) */
long ds_parens_range_min(const ds_parens_t *p, size_t i, size_t j,
                         size_t *count);
long ds_parens_range_max(const ds_parens_t *p, size_t i, size_t j);
size_t ds_parens_rmq(const ds_parens_t *p, size_t i, size_t j);
size_t ds_parens_min_select(const ds_parens_t *p, size_t i, size_t j, size_t k);
size_t ds_parens_bytes(const ds_parens_t *p);

typedef struct ds_bp_tree {
  ds_parens_t p;
  size_t nodes;
} ds_bp_tree_t;

/* parent[i] is the parent of input node i (DS_BP_NONE for the root);
 * out_number (optional) receives each input node's preorder number */
ds_status_t ds_bp_from_parents(ds_bp_tree_t *t, const size_t *parent, size_t n,
                               size_t *out_number, const ds_rs_ops_t *rs_ops,
                               ds_context_t *context);
/* a parentheses sequence of 2n bits describing one tree (or 0 bits) */
ds_status_t ds_bp_from_parens(ds_bp_tree_t *t, const unsigned long *words,
                              size_t bits, const ds_rs_ops_t *rs_ops,
                              ds_context_t *context);
void ds_bp_destroy(ds_bp_tree_t *t);

size_t ds_bp_nodes(const ds_bp_tree_t *t);
/* the open parenthesis of node v, and the node opened at a position */
size_t ds_bp_position(const ds_bp_tree_t *t, size_t v);
size_t ds_bp_node_at(const ds_bp_tree_t *t, size_t position);
/* DS_BP_NONE when there is no such node */
size_t ds_bp_parent(const ds_bp_tree_t *t, size_t v);
size_t ds_bp_first_child(const ds_bp_tree_t *t, size_t v);
size_t ds_bp_last_child(const ds_bp_tree_t *t, size_t v);
size_t ds_bp_next_sibling(const ds_bp_tree_t *t, size_t v);
size_t ds_bp_prev_sibling(const ds_bp_tree_t *t, size_t v);
/* the k-th child (0-based) */
size_t ds_bp_child(const ds_bp_tree_t *t, size_t v, size_t k);
bool ds_bp_is_leaf(const ds_bp_tree_t *t, size_t v);
size_t ds_bp_degree(const ds_bp_tree_t *t, size_t v);
/* how many siblings precede v */
size_t ds_bp_child_rank(const ds_bp_tree_t *t, size_t v);
/* the root has depth 0; a leaf height 0 */
size_t ds_bp_depth(const ds_bp_tree_t *t, size_t v);
size_t ds_bp_height(const ds_bp_tree_t *t, size_t v);
size_t ds_bp_subtree_size(const ds_bp_tree_t *t, size_t v);
bool ds_bp_is_ancestor(const ds_bp_tree_t *t, size_t u, size_t v);
size_t ds_bp_lca(const ds_bp_tree_t *t, size_t u, size_t v);
/* the ancestor d levels up (d = 0: v itself) */
size_t ds_bp_level_ancestor(const ds_bp_tree_t *t, size_t v, size_t d);
size_t ds_bp_postorder(const ds_bp_tree_t *t, size_t v);
size_t ds_bp_bytes(const ds_bp_tree_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DS_BP_TREE_H */
