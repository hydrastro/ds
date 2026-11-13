#ifndef DS_LOUDS_H
#define DS_LOUDS_H

/*
 * LOUDS (level-order unary degree sequence, Jacobson 1989): a static
 * ordered tree of n nodes in 2n + 1 bits plus the rank/select directory
 * of a lib/rank_select backend chosen by the caller.
 *
 * Nodes are numbered 0 .. n-1 in level order (breadth first, children
 * left to right; the root is 0). The bits are "10" for a virtual root
 * above the real one, then for every node in level order one 1 per child
 * and a 0. A node's children therefore have consecutive numbers, and with
 * s(v) = select0(v):
 *
 *   degree(v)      s(v + 1) - s(v) - 1
 *   child(v, j)    s(v) - v + j              (j < degree)
 *   parent(v)      select1(v) - v - 1        (none for the root)
 *   child_rank(v)  select1(v) - s(parent) - 1
 *   next sibling   v + 1 when bit select1(v) + 1 is set
 *
 * so each navigation step is one or two selects (and an access), with the
 * backend's costs: ds_rs_plain() for speed, ds_rs_rrr() for space;
 * Elias-Fano suits trees whose bit vector is sparse, which a LOUDS never
 * is. Byte-labelled edges (the label of each node's incoming edge, sorted
 * among siblings) turn it into a static trie: ds_louds_child_label finds
 * a child by binary search over the sibling range.
 *
 * Building takes the degrees in level order, or a parent array in any
 * numbering (children kept in increasing input order), and checks that
 * the input describes one tree. Memory comes from the context.
 */

#include "context.h"
#include "rank_select.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_LOUDS_NONE (~(size_t)0)

typedef struct ds_louds {
  ds_rs_t bits; /* 2n + 1 bits */
  size_t n;
  ds_context_t *context;
} ds_louds_t;

/* degree[v] for the nodes in level order. DS_ERR_INVALID unless they
 * describe one tree of n nodes (every node reached before its own degree
 * is read, n - 1 children in all). ops NULL: ds_rs_plain() */
ds_status_t ds_louds_from_degrees(ds_louds_t *t, const size_t *degree, size_t n,
                                  const ds_rs_ops_t *ops,
                                  ds_context_t *context);
/* parent[v] for nodes in any numbering, DS_LOUDS_NONE for the root;
 * siblings keep increasing input order. out_number (optional, n entries)
 * receives each input node's level-order number. DS_ERR_INVALID unless
 * the parents form one tree (one root, in range, no cycle). */
ds_status_t ds_louds_from_parents(ds_louds_t *t, const size_t *parent, size_t n,
                                  size_t *out_number, const ds_rs_ops_t *ops,
                                  ds_context_t *context);
void ds_louds_destroy(ds_louds_t *t);

size_t ds_louds_size(const ds_louds_t *t);
/* the calls below take nodes v < n */
size_t ds_louds_degree(const ds_louds_t *t, size_t v);
bool ds_louds_is_leaf(const ds_louds_t *t, size_t v);
/* the j-th child (0-based), or DS_LOUDS_NONE */
size_t ds_louds_child(const ds_louds_t *t, size_t v, size_t j);
size_t ds_louds_first_child(const ds_louds_t *t, size_t v);
size_t ds_louds_last_child(const ds_louds_t *t, size_t v);
size_t ds_louds_parent(const ds_louds_t *t, size_t v);
size_t ds_louds_next_sibling(const ds_louds_t *t, size_t v);
size_t ds_louds_prev_sibling(const ds_louds_t *t, size_t v);
/* v's index among its siblings (0 for the root) */
size_t ds_louds_child_rank(const ds_louds_t *t, size_t v);
/* edges from the root, O(depth) parent steps */
size_t ds_louds_depth(const ds_louds_t *t, size_t v);
/* labels[c] is the label of the edge into node c, increasing among
 * siblings: the child of v by that label, or DS_LOUDS_NONE */
size_t ds_louds_child_label(const ds_louds_t *t, size_t v,
                            const unsigned char *labels, unsigned char label);
size_t ds_louds_bytes(const ds_louds_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DS_LOUDS_H */
