#ifndef DS_RRB_VECTOR_H
#define DS_RRB_VECTOR_H

/*
 * RRB vectors (Bagwell and Rompf, "RRB-Trees: Efficient Immutable
 * Vectors", 2012; Stucki, Rompf, Ureche and Bagwell, "RRB vector: a
 * practical general purpose immutable sequence", ICFP 2015): persistent
 * vectors that also concatenate, split and insert in O(log n).
 *
 * The tree has branching M = 2^bits (32 by default). Leaves hold up to M
 * elements; an inner node holds up to M children and, when it is relaxed,
 * a table of their cumulative sizes. A node whose children all hold
 * exactly M^h elements except the last is balanced and needs no table:
 * indexing it is pure radix arithmetic. Indexing a relaxed node starts
 * at the radix guess, which never overshoots, and steps forward through
 * the table.
 *
 *   get, set                    O(log_M n)
 *   push_back, pop_back         O(log_M n) (path copying, no tail)
 *   concat                      O(M log_M n): the right spine of one tree
 *                               and the left spine of the other are merged
 *                               level by level; the merged children are
 *                               redistributed only where needed to keep at
 *                               most E = 2 nodes more than the optimum per
 *                               level (the concatenation plan of Stucki
 *                               et al.), so untouched nodes are shared
 *   slice, split, insert, remove, push_front
 *                               O(M log_M n), by cutting along paths
 *                               (relaxed nodes appear at the edges) and
 *                               concatenating
 *
 * Versions are handles over reference-counted nodes; every update builds
 * a new version and leaves its inputs intact. An output handle may be an
 * input, which then moves to the new version (the old one is released);
 * any other output handle is overwritten, so it must not hold a version.
 * On failure nothing changes.
 *
 * Element policies are those of lib/persistent_vector: plain bytes, owned
 * resources (copy_elem and destroy_elem: every leaf owns its copies, a
 * leaf built from others clones the elements it takes, and pop_back and
 * remove hand out a clone), or copy only. destroy_elem without copy_elem
 * is rejected. Memory comes from the context; no locking.
 */

#include "context.h"
#include "schema.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_rrb_config {
  size_t elem_size;
  unsigned bits; /* 2..8: branching 2^bits (config_init sets 5) */
  /* dst is uninitialised storage; return false on failure */
  bool (*copy_elem)(void *dst, const void *src, void *user);
  void (*destroy_elem)(void *elem, void *user);
  void *user;
  ds_context_t *context;
} ds_rrb_config_t;

typedef struct ds_rrb_node {
  size_t refs;
  size_t count;  /* elements (leaf) or children */
  size_t *sizes; /* cumulative child sizes when relaxed, else NULL */
  /* followed by M child pointers or M elements (and M sizes for inner
   * nodes) */
} ds_rrb_node_t;

typedef struct ds_rrb {
  ds_rrb_node_t *root; /* NULL when empty */
  size_t size;
  unsigned height; /* 0: the root is a leaf */
  const ds_rrb_config_t *config;
} ds_rrb_t;

typedef struct ds_rrb_iter {
  const ds_rrb_t *vec;
  size_t index;
  size_t leaf_base;
  size_t leaf_end;
  const unsigned char *leaf; /* elements of the current leaf */
} ds_rrb_iter_t;

void ds_rrb_config_init(ds_rrb_config_t *config, size_t elem_size);
/* config_init from an element schema (size, copy, destroy, user) */
ds_status_t ds_rrb_config_from_schema(ds_rrb_config_t *config,
                                      const ds_schema_t *schema);
/* an empty version; the config must outlive every version built from it */
ds_status_t ds_rrb_init(ds_rrb_t *vec, const ds_rrb_config_t *config);
/* another handle on the same version */
void ds_rrb_copy(const ds_rrb_t *vec, ds_rrb_t *out);
void ds_rrb_release(ds_rrb_t *vec);
size_t ds_rrb_size(const ds_rrb_t *vec);
/* pointer into the version's storage, valid while it lives */
const void *ds_rrb_get(const ds_rrb_t *vec, size_t index);
ds_status_t ds_rrb_set(const ds_rrb_t *vec, size_t index, const void *elem,
                       ds_rrb_t *out);
ds_status_t ds_rrb_push_back(const ds_rrb_t *vec, const void *elem,
                             ds_rrb_t *out);
ds_status_t ds_rrb_push_front(const ds_rrb_t *vec, const void *elem,
                              ds_rrb_t *out);
/* out_elem (optional) receives the element (a clone for owned ones);
 * DS_EMPTY on an empty vector */
ds_status_t ds_rrb_pop_back(const ds_rrb_t *vec, void *out_elem, ds_rrb_t *out);
/* a and b must share the config */
ds_status_t ds_rrb_concat(const ds_rrb_t *a, const ds_rrb_t *b, ds_rrb_t *out);
/* elements [from, to); DS_ERR_RANGE unless from <= to <= size */
ds_status_t ds_rrb_slice(const ds_rrb_t *vec, size_t from, size_t to,
                         ds_rrb_t *out);
/* [0, at) and [at, size) */
ds_status_t ds_rrb_split(const ds_rrb_t *vec, size_t at, ds_rrb_t *left,
                         ds_rrb_t *right);
/* elem before position at (at == size appends) */
ds_status_t ds_rrb_insert(const ds_rrb_t *vec, size_t at, const void *elem,
                          ds_rrb_t *out);
ds_status_t ds_rrb_remove(const ds_rrb_t *vec, size_t at, void *out_elem,
                          ds_rrb_t *out);
void ds_rrb_iter_init(ds_rrb_iter_t *it, const ds_rrb_t *vec, size_t start);
/* NULL at the end */
const void *ds_rrb_iter_next(ds_rrb_iter_t *it);
/* sizes tables, balance flags, counts and heights consistent (O(nodes),
 * for tests and debugging) */
bool ds_rrb_check(const ds_rrb_t *vec);

#ifdef __cplusplus
}
#endif

#endif /* DS_RRB_VECTOR_H */
