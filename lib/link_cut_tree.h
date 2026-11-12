#ifndef DS_LINK_CUT_TREE_H
#define DS_LINK_CUT_TREE_H

/*
 * Link-cut trees (Sleator & Tarjan): a dynamic forest on vertices
 * 0 .. n-1 with O(log n) amortized link, cut, reroot (evert),
 * connectivity, LCA and path operations.
 *
 * Each vertex holds an element by value; paths are aggregated by a user
 * monoid exactly like the implicit treap (summarize, combine, plus the
 * reversed aggregate so non-commutative monoids survive rerooting), and
 * whole paths can take a lazy action (tag_apply_elem, tag_apply_summary
 * with the path length, tag_compose). Paths are preferred-path splay
 * trees; splaying pushes pending reversals/tags down from the top with an
 * explicit stack, so nothing recurses.
 *
 * Errors: linking two connected vertices or cutting a non-edge is
 * DS_ERR_STATE; path operations on disconnected vertices DS_ERR_STATE.
 * Memory comes from the ds_context_t.
 *
 * Laws: as for lib/implicit_treap. combine is associative and never called
 * with out aliasing an input; the reversed aggregate is maintained with
 * the same combine in mirrored order, so rerooting keeps non-commutative
 * path aggregates exact; path tags are uniform per-element actions
 * (tag_apply_summary must agree with applying the tag to every element,
 * for forward and reversed aggregates, and tag_compose must be
 * sequential application). A path aggregate is ordered from the first
 * vertex named in the query to the second.
 *
 * Roots: path_query, path_apply and path_length reroot the tree at their
 * first vertex, exactly as ds_lct_reroot does, so afterwards
 * ds_lct_find_root reports that vertex and ds_lct_lca answers for the tree
 * rooted there. link(u, v) and cut(u, v) reroot u's tree at u as part of
 * the operation (after a link, the joined tree keeps v's root). Callers
 * that rely on a particular root reroot at it again before root-relative
 * queries (find_root, lca); connectivity does not depend on the root.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_LCT_NONE (~(size_t)0)

typedef struct ds_lct_config {
  size_t elem_size;    /* may be 0 */
  size_t summary_size; /* 0: no path aggregates */
  void (*summarize)(void *out, const void *elem, void *user);
  void (*combine)(void *out, const void *left, const void *right, void *user);
  size_t tag_size; /* 0: no path actions */
  void (*tag_apply_elem)(void *elem, const void *tag, void *user);
  void (*tag_apply_summary)(void *summary, const void *tag, size_t count,
                            void *user);
  void (*tag_compose)(void *tag, const void *newer, void *user);
  void *user;
  ds_context_t *context;
} ds_lct_config_t;

typedef struct ds_lct {
  size_t n;
  ds_lct_config_t config;
  size_t *left;
  size_t *right;
  size_t *parent;
  size_t *size;
  unsigned char *rev;
  unsigned char *tagged;
  unsigned char *elems;
  unsigned char *sums;
  unsigned char *rsums;
  unsigned char *tags;
  unsigned char *scratch; /* three summaries */
  size_t *stack;
} ds_lct_t;

void ds_lct_config_init(ds_lct_config_t *config);
/* elems (optional): n initial elements */
ds_status_t ds_lct_init(ds_lct_t *t, size_t n, const void *elems,
                        const ds_lct_config_t *config);
void ds_lct_destroy(ds_lct_t *t);
ds_status_t ds_lct_link(ds_lct_t *t, size_t u, size_t v);
ds_status_t ds_lct_cut(ds_lct_t *t, size_t u, size_t v);
bool ds_lct_connected(ds_lct_t *t, size_t u, size_t v);
/* make r the root of its tree */
ds_status_t ds_lct_reroot(ds_lct_t *t, size_t r);
/* root of v's tree (the current root, after any reroot) */
size_t ds_lct_find_root(ds_lct_t *t, size_t v);
/* lowest common ancestor of u and v for the tree rooted at its current
 * root; DS_LCT_NONE when disconnected */
size_t ds_lct_lca(ds_lct_t *t, size_t u, size_t v);
/* aggregate of the u..v path (in that order) */
ds_status_t ds_lct_path_query(ds_lct_t *t, size_t u, size_t v,
                              void *out_summary);
/* apply a tag to every vertex of the u..v path */
ds_status_t ds_lct_path_apply(ds_lct_t *t, size_t u, size_t v, const void *tag);
/* vertices on the u..v path */
size_t ds_lct_path_length(ds_lct_t *t, size_t u, size_t v);
ds_status_t ds_lct_get(ds_lct_t *t, size_t v, void *out_elem);
ds_status_t ds_lct_set(ds_lct_t *t, size_t v, const void *elem);

#ifdef __cplusplus
}
#endif

#endif /* DS_LINK_CUT_TREE_H */
