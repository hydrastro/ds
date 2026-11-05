#ifndef DS_SUFFIX_TREE_H
#define DS_SUFFIX_TREE_H

/*
 * Suffix trees by Ukkonen's online algorithm (O(n) nodes and time for a
 * fixed alphabet), over one byte string or several (the multi, or
 * generalized, suffix tree: every string ends in its own terminator).
 * Children live in one hash table keyed by (node, symbol); ordered child
 * lists for traversals are built on demand.
 *
 * Queries: substring test, number of occurrences, every occurrence as
 * (string, offset), longest repeated substring, longest substring common
 * to all strings, and the suffix array (lexicographic depth-first walk).
 *
 * Also the Burrows-Wheeler transform (via the suffix tree), its inverse
 * (LF mapping) and the inverse suffix array.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_suffix_tree {
  size_t *text;  /* symbols: terminator i -> i, byte b -> strings + b */
  size_t length; /* symbols */
  size_t strings;
  size_t *string_start; /* strings + 1 entries */
  size_t *start;        /* per node: edge label start */
  size_t *end;          /* per node: inclusive end, ~0 for leaves */
  size_t *link;
  size_t *suffix; /* leaf: suffix start; internal: ~0 */
  size_t nodes;
  size_t node_capacity;
  size_t *hkey_node; /* child hash */
  size_t *hkey_sym;
  size_t *hval;
  size_t hash_size;
  size_t *child_off; /* ordered children, built on demand */
  size_t *child;
  ds_context_t *context;
} ds_suffix_tree_t;

typedef bool (*ds_suffix_tree_visit_func_t)(size_t string, size_t offset,
                                            void *user);

ds_status_t ds_suffix_tree_build(ds_suffix_tree_t *t, const unsigned char *s,
                                 size_t n, ds_context_t *context);
ds_status_t ds_suffix_tree_build_multi(ds_suffix_tree_t *t,
                                       const unsigned char *const *strs,
                                       const size_t *lens, size_t count,
                                       ds_context_t *context);
void ds_suffix_tree_destroy(ds_suffix_tree_t *t);
bool ds_suffix_tree_contains(const ds_suffix_tree_t *t, const unsigned char *p,
                             size_t m);
ds_status_t ds_suffix_tree_count(ds_suffix_tree_t *t, const unsigned char *p,
                                 size_t m, size_t *out_count);
/* Every occurrence of p (non-empty), in no particular order. */
ds_status_t ds_suffix_tree_locate(ds_suffix_tree_t *t, const unsigned char *p,
                                  size_t m, ds_suffix_tree_visit_func_t visit,
                                  void *user);
/* Longest substring occurring at least twice (anywhere); length 0 if
 * none. */
ds_status_t ds_suffix_tree_longest_repeat(ds_suffix_tree_t *t,
                                          size_t *out_string,
                                          size_t *out_offset,
                                          size_t *out_length);
/* Longest substring occurring in every string. */
ds_status_t ds_suffix_tree_longest_common(ds_suffix_tree_t *t,
                                          size_t *out_string,
                                          size_t *out_offset,
                                          size_t *out_length);
/* Single-string trees: the suffix array of the string (n entries). */
ds_status_t ds_suffix_tree_suffix_array(ds_suffix_tree_t *t, size_t *out_sa);

/* BWT of s: n bytes (the end-of-string marker removed) and the row where
 * it was in *out_primary. out must not alias s. */
ds_status_t ds_bwt(const unsigned char *s, size_t n, unsigned char *out,
                   size_t *out_primary, ds_context_t *context);
ds_status_t ds_inverse_bwt(const unsigned char *bwt, size_t n, size_t primary,
                           unsigned char *out, ds_context_t *context);
/* out[sa[i]] = i; DS_ERR_INVALID if sa is not a permutation. */
ds_status_t ds_inverse_suffix_array(const size_t *sa, size_t n, size_t *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_SUFFIX_TREE_H */
