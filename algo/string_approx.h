#ifndef DS_ALGO_STRING_APPROX_H
#define DS_ALGO_STRING_APPROX_H

/*
 * Approximate string matching, alignment and string similarity over byte
 * strings with explicit lengths.
 *
 *   ds_edit_script            cheapest edit script (match, substitute,
 *                             insert, delete operations) between two
 *                             strings under pluggable operation costs
 *   ds_align                  global (Needleman-Wunsch) or local
 *                             (Smith-Waterman) alignment with a pluggable
 *                             substitution score and affine gaps (Gotoh);
 *                             the operations are optional (linear memory
 *                             without them)
 *   ds_approx_search          string matching with errors: every end
 *                             position where the pattern matches with at
 *                             most k edits; Sellers' dynamic programming
 *                             with Ukkonen's cut-off, or Myers' bit-parallel
 *                             algorithm for patterns up to a machine word
 *   ds_longest_common_substring
 *   ds_shortest_common_supersequence
 *   ds_shortest_common_superstring  exact (subset dynamic programming over
 *                             overlaps) up to a limit, greedy merging above
 *   ds_heaviest_common_subsequence  common subsequence of maximum total
 *                             weight, weights from a callback
 *   ds_lcs_dominant_matches   the k-dominant matches of the LCS table
 *   ds_jaro, ds_jaro_winkler  Jaro and Jaro-Winkler similarity
 *   ds_ratcliff_obershelp     Ratcliff/Obershelp gestalt similarity
 *
 * Working memory comes from the ds_context_t (O(n m) bytes where a
 * traceback is needed, O(m) words otherwise).
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_edit_op {
  DS_EDIT_MATCH = 0,
  DS_EDIT_SUBSTITUTE = 1,
  DS_EDIT_INSERT = 2, /* consume a character of b */
  DS_EDIT_DELETE = 3  /* consume a character of a */
} ds_edit_op_t;

/* Operation costs; a NULL callback costs 1 (substitution of equal
 * characters always costs 0). Costs must be >= 0. */
typedef struct ds_edit_costs {
  double (*substitute)(unsigned char a, unsigned char b, void *user);
  double (*insert)(unsigned char b, void *user);
  double (*remove)(unsigned char a, void *user);
  void *user;
} ds_edit_costs_t;

/* costs NULL: unit costs. out_ops (capacity n + m, may be NULL) receives
 * the script from left to right; its length goes to *out_op_count. */
ds_status_t ds_edit_script(const unsigned char *a, size_t n,
                           const unsigned char *b, size_t m,
                           const ds_edit_costs_t *costs, unsigned char *out_ops,
                           size_t *out_op_count, double *out_cost,
                           ds_context_t *context);

typedef enum ds_align_mode {
  DS_ALIGN_GLOBAL = 0,
  DS_ALIGN_LOCAL = 1
} ds_align_mode_t;

/* Scores are maximised. A gap of length L scores -(gap_open + (L - 1) *
 * gap_extend). score NULL: match / mismatch. */
typedef struct ds_align_scoring {
  double (*score)(unsigned char a, unsigned char b, void *user);
  double match;
  double mismatch;
  double gap_open;
  double gap_extend;
  void *user;
} ds_align_scoring_t;

typedef struct ds_alignment {
  double score;
  size_t a_begin; /* aligned part of a: [a_begin, a_end) */
  size_t a_end;
  size_t b_begin;
  size_t b_end;
  size_t op_count;
} ds_alignment_t;

/* out_ops (capacity n + m) may be NULL; the begin positions of a local
 * alignment are only known with a traceback (they equal the ends
 * otherwise). */
ds_status_t ds_align(const unsigned char *a, size_t n, const unsigned char *b,
                     size_t m, const ds_align_scoring_t *scoring,
                     ds_align_mode_t mode, unsigned char *out_ops,
                     ds_alignment_t *out, ds_context_t *context);

typedef enum ds_approx_algorithm {
  DS_APPROX_AUTO = 0,
  DS_APPROX_SELLERS = 1,
  DS_APPROX_MYERS = 2
} ds_approx_algorithm_t;

/* Called with the end (exclusive) of a text window matching the pattern
 * with `errors` <= k edits (the best for that end); return false to
 * stop. */
typedef bool (*ds_approx_match_func_t)(size_t end, size_t errors, void *user);

/* Number of reported ends in *out_count (may be NULL). Myers needs
 * m <= bits of unsigned long (DS_APPROX_AUTO picks it then). */
ds_status_t ds_approx_search(const unsigned char *text, size_t n,
                             const unsigned char *pattern, size_t m, size_t k,
                             ds_approx_algorithm_t algorithm,
                             ds_approx_match_func_t on_match, void *user,
                             size_t *out_count, ds_context_t *context);

ds_status_t ds_longest_common_substring(const unsigned char *a, size_t n,
                                        const unsigned char *b, size_t m,
                                        size_t *out_a_pos, size_t *out_b_pos,
                                        size_t *out_length,
                                        ds_context_t *context);
/* out (capacity n + m) may be NULL; length in *out_length. */
ds_status_t ds_shortest_common_supersequence(const unsigned char *a, size_t n,
                                             const unsigned char *b, size_t m,
                                             unsigned char *out,
                                             size_t *out_length,
                                             ds_context_t *context);
/* Strings strs[i] of lens[i] bytes. Exact when the strings left after
 * removing duplicates and substrings number at most exact_limit (<= 16),
 * greedy merging otherwise. The result's length goes to *out_length;
 * DS_ERR_OVERFLOW if it does not fit in capacity (out may be NULL). */
ds_status_t ds_shortest_common_superstring(const unsigned char *const *strs,
                                           const size_t *lens, size_t count,
                                           size_t exact_limit,
                                           unsigned char *out, size_t capacity,
                                           size_t *out_length,
                                           ds_context_t *context);
/* weight(i, j) for matching a[i] == b[j] (NULL: 1, the LCS). Matched
 * index pairs (capacity min(n, m)) are optional. */
typedef double (*ds_match_weight_func_t)(size_t i, size_t j, void *user);
ds_status_t ds_heaviest_common_subsequence(
    const unsigned char *a, size_t n, const unsigned char *b, size_t m,
    ds_match_weight_func_t weight, void *user, size_t *out_a_index,
    size_t *out_b_index, size_t *out_count, double *out_weight,
    ds_context_t *context);
/* (i, j) is k-dominant when a[i] == b[j], the LCS of a[0..i] and b[0..j]
 * is k, and no other match with value k lies above and to the left of it.
 * Visits them row by row; the count in *out_count. */
typedef bool (*ds_dominant_match_func_t)(size_t i, size_t j, size_t k,
                                         void *user);
ds_status_t ds_lcs_dominant_matches(const unsigned char *a, size_t n,
                                    const unsigned char *b, size_t m,
                                    ds_dominant_match_func_t visit, void *user,
                                    size_t *out_count, ds_context_t *context);

ds_status_t ds_jaro(const unsigned char *a, size_t n, const unsigned char *b,
                    size_t m, double *out_similarity, ds_context_t *context);
/* prefix_scale < 0 and boost_threshold < 0 select the usual 0.1 and 0.7;
 * the common prefix counts up to 4 characters. */
ds_status_t ds_jaro_winkler(const unsigned char *a, size_t n,
                            const unsigned char *b, size_t m,
                            double prefix_scale, double boost_threshold,
                            double *out_similarity, ds_context_t *context);
/* 2 K / (n + m), K the characters matched by recursive longest common
 * substrings; K in *out_matches (may be NULL). */
ds_status_t ds_ratcliff_obershelp(const unsigned char *a, size_t n,
                                  const unsigned char *b, size_t m,
                                  double *out_similarity, size_t *out_matches,
                                  ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_STRING_APPROX_H */
