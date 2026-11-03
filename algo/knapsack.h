#ifndef DS_ALGO_KNAPSACK_H
#define DS_ALGO_KNAPSACK_H

/*
 * Knapsack, packing and chain-ordering optimisation.
 *
 *   ds_knapsack_01()          0/1 knapsack, exact by dynamic programming
 *                             over integer capacities, O(n W) time and
 *                             O(W) values plus n W bits for the choice
 *   ds_knapsack_unbounded()   any number of copies of each item, O(n W)
 *   ds_knapsack_fractional()  items may be split: greedy by value density,
 *                             optimal, O(n log n)
 *   ds_bin_packing()          bin packing heuristics behind a policy: next
 *                             fit, first fit, best fit, worst fit and their
 *                             decreasing (sorted) variants; FFD uses at
 *                             most 11/9 OPT + 6/9 bins
 *   ds_cutting_stock()        one-dimensional cutting stock: pieces (with
 *                             demands) cut from stock lengths, via the bin
 *                             packing policies (a heuristic, not column
 *                             generation)
 *   ds_strip_packing()        rectangles into a strip of fixed width by
 *                             level algorithms: next-fit or first-fit
 *                             decreasing height (NFDH, FFDH)
 *   ds_matrix_chain()         optimal parenthesisation of a matrix chain,
 *                             O(n^3), with the split table for the order
 *
 * Working memory comes from the ds_context_t (NULL for the default).
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_bin_policy {
  DS_BIN_NEXT_FIT = 0,
  DS_BIN_FIRST_FIT = 1,
  DS_BIN_BEST_FIT = 2,
  DS_BIN_WORST_FIT = 3,
  DS_BIN_FIRST_FIT_DECREASING = 4,
  DS_BIN_BEST_FIT_DECREASING = 5
} ds_bin_policy_t;

typedef enum ds_strip_policy {
  DS_STRIP_NFDH = 0,
  DS_STRIP_FFDH = 1
} ds_strip_policy_t;

/* out_take[i] = 1 for the chosen items (may be NULL). */
ds_status_t ds_knapsack_01(const size_t *weights, const double *values,
                           size_t n, size_t capacity, unsigned char *out_take,
                           double *out_value, ds_context_t *context);
/* out_counts[i] = copies of item i (may be NULL). */
ds_status_t ds_knapsack_unbounded(const size_t *weights, const double *values,
                                  size_t n, size_t capacity, size_t *out_counts,
                                  double *out_value, ds_context_t *context);
/* out_fraction[i] in [0, 1] (may be NULL). */
ds_status_t ds_knapsack_fractional(const double *weights, const double *values,
                                   size_t n, double capacity,
                                   double *out_fraction, double *out_value,
                                   ds_context_t *context);

/* out_bin[i] = bin of item i; DS_ERR_RANGE if an item exceeds capacity. */
ds_status_t ds_bin_packing(const double *sizes, size_t n, double capacity,
                           ds_bin_policy_t policy, size_t *out_bin,
                           size_t *out_bins, ds_context_t *context);

/* Pieces of piece_count lengths, demand[i] copies each, cut from stock of
 * `stock` length. out_stock gets, per copy (in piece order, demands
 * expanded), the stock bar it is cut from (sum of demands entries). */
ds_status_t ds_cutting_stock(const double *lengths, const size_t *demand,
                             size_t piece_count, double stock,
                             ds_bin_policy_t policy, size_t *out_stock,
                             size_t *out_bars, ds_context_t *context);

/* Bottom-left corners of the placed rectangles and the strip height used.
 * DS_ERR_RANGE if a rectangle is wider than the strip. */
ds_status_t ds_strip_packing(const double *widths, const double *heights,
                             size_t n, double strip_width,
                             ds_strip_policy_t policy, double *out_x,
                             double *out_y, double *out_height,
                             ds_context_t *context);

/* dims has n + 1 entries (matrix i is dims[i] x dims[i + 1]). out_split
 * (n x n, may be NULL): out_split[i * n + j] = k means the product of
 * matrices i..j is split after k. */
ds_status_t ds_matrix_chain(const size_t *dims, size_t n, double *out_cost,
                            size_t *out_split, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_KNAPSACK_H */
