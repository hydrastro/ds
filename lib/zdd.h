#ifndef DS_ZDD_H
#define DS_ZDD_H

/*
 * Zero-suppressed decision diagrams (ZDDs, Minato 1993): families of sets
 * of the manager's variables, hash-consed so that every family has
 * exactly one node (equality is node equality). A node is dropped when its
 * high edge leads to the empty family, instead of when both edges agree as
 * in lib/bdd, so a family costs nodes in proportion to its sets rather
 * than to the number of variables: the representation for sparse
 * combinatorial families (paths, matchings, covers, itemsets).
 *
 * DS_ZDD_EMPTY is the empty family and DS_ZDD_BASE the family holding only
 * the empty set. The variable order is pluggable: order[level] names the
 * variable at each level (NULL: variable i at level i).
 *
 * Family algebra, every operation iterative (an explicit stack, no
 * recursion on variables or nodes) with a computed cache:
 *
 *   union, intersect, diff, symdiff     of the families as sets of sets
 *   subset0(F, v), subset1(F, v)        the sets without v; those with v,
 *                                       v taken out
 *   change(F, v)                        v toggled in every set
 *   join(F, G)    { a u b : a in F, b in G }
 *   meet(F, G)    { a n b : a in F, b in G }
 *   nonsup(F, G)  the sets of F containing no set of G
 *   nonsub(F, G)  the sets of F contained in no set of G
 *   minimal(F), maximal(F)   the sets with no proper subset / superset in F
 *
 * plus counting (as a double: exact up to 2^53 sets), membership of a
 * set, one member, iteration over all members and node counts.
 *
 * Nodes are never freed individually; ds_zdd_destroy releases everything.
 * Memory comes from the context.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_ZDD_EMPTY ((size_t)0) /* {} */
#define DS_ZDD_BASE ((size_t)1)  /* {{}} */

typedef struct ds_zdd {
  size_t vars;
  size_t *level_of;     /* per variable */
  size_t *var_at_level; /* per level */
  size_t *var;          /* per node (terminals: vars) */
  size_t *low;
  size_t *high;
  size_t nodes;
  size_t capacity;
  size_t *unique; /* hash of nodes */
  size_t unique_size;
  size_t *cache; /* 4 words per entry: f, g, operation, result */
  size_t cache_size;
  size_t *stack;
  size_t stack_capacity;
  unsigned char *mark; /* per variable, scratch for set arguments */
  ds_context_t *context;
} ds_zdd_t;

/* order: `vars` distinct variables, or NULL (DS_ERR_INVALID otherwise) */
ds_status_t ds_zdd_init(ds_zdd_t *m, size_t vars, const size_t *order,
                        ds_context_t *context);
void ds_zdd_destroy(ds_zdd_t *m);

/* {{v}} */
ds_status_t ds_zdd_single(ds_zdd_t *m, size_t v, size_t *out);
/* the family holding the one set of these k variables (repeats count
 * once); DS_ERR_RANGE for a variable out of range */
ds_status_t ds_zdd_set(ds_zdd_t *m, const size_t *set, size_t k, size_t *out);

ds_status_t ds_zdd_union(ds_zdd_t *m, size_t f, size_t g, size_t *out);
ds_status_t ds_zdd_intersect(ds_zdd_t *m, size_t f, size_t g, size_t *out);
ds_status_t ds_zdd_diff(ds_zdd_t *m, size_t f, size_t g, size_t *out);
ds_status_t ds_zdd_symdiff(ds_zdd_t *m, size_t f, size_t g, size_t *out);
ds_status_t ds_zdd_subset0(ds_zdd_t *m, size_t f, size_t v, size_t *out);
ds_status_t ds_zdd_subset1(ds_zdd_t *m, size_t f, size_t v, size_t *out);
ds_status_t ds_zdd_change(ds_zdd_t *m, size_t f, size_t v, size_t *out);
ds_status_t ds_zdd_join(ds_zdd_t *m, size_t f, size_t g, size_t *out);
ds_status_t ds_zdd_meet(ds_zdd_t *m, size_t f, size_t g, size_t *out);
ds_status_t ds_zdd_nonsup(ds_zdd_t *m, size_t f, size_t g, size_t *out);
ds_status_t ds_zdd_nonsub(ds_zdd_t *m, size_t f, size_t g, size_t *out);
ds_status_t ds_zdd_minimal(ds_zdd_t *m, size_t f, size_t *out);
ds_status_t ds_zdd_maximal(ds_zdd_t *m, size_t f, size_t *out);

/* the number of sets in f */
ds_status_t ds_zdd_count(ds_zdd_t *m, size_t f, double *out);
/* whether the set of these k variables is in f */
bool ds_zdd_contains(ds_zdd_t *m, size_t f, const size_t *set, size_t k);
/* one set of f (the one taking low edges first) into out[0 .. *k), at
 * most cap variables; DS_NOT_FOUND for the empty family, DS_ERR_RANGE
 * when cap is short */
ds_status_t ds_zdd_any(const ds_zdd_t *m, size_t f, size_t *out, size_t cap,
                       size_t *k);
/* calls visit for every set of f (k variables, in level order) until it
 * returns false (DS_STOP); the set array is reused between calls */
typedef bool (*ds_zdd_visit_fn)(const size_t *set, size_t k, void *user);
ds_status_t ds_zdd_foreach(ds_zdd_t *m, size_t f, ds_zdd_visit_fn visit,
                           void *user);
/* nodes reachable from f, terminals included */
size_t ds_zdd_node_count(ds_zdd_t *m, size_t f);

#ifdef __cplusplus
}
#endif

#endif /* DS_ZDD_H */
