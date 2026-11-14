#ifndef DS_ALGO_PARALLEL_SIM_H
#define DS_ALGO_PARALLEL_SIM_H

/*
 * Classic PRAM techniques, run in parallel. Every round reads the
 * previous round's arrays and writes fresh ones, as the synchronous
 * parallel machine does, and is cut into tasks handed to an executor
 * (lib/executor): the caller's threads, a pool, OpenMP, or by default one
 * after another. Tasks never write the same memory (the EREW discipline,
 * with sums taken per task and then combined), so every executor gives the
 * same result and the same number of rounds. The `_on` functions take the
 * executor; the others run sequentially.
 *
 *   ds_pram_list_rank        pointer jumping (Wyllie): every node's
 *                            distance to the end of its list in
 *                            ceil(log2 n) rounds
 *   ds_pram_list_scan        recursive doubling: the suffix combination
 *                            of values along each list for any
 *                            associative operator (need not commute)
 *   ds_pram_find_roots       shortcutting: every node of a forest learns
 *                            its root by parent = parent[parent]
 *   ds_pram_list_contract_rank  randomized list contraction (splice out an
 *                            independent set chosen by coin flips, recurse,
 *                            then expand back): ranks with O(n) expected
 *                            work in O(log n) expected rounds; the
 *                            surviving nodes are compacted every round by
 *                            per-task counts and their prefix sums, and
 *                            the coins are hashes of (seed, round, node),
 *                            so no task waits on another
 *   ds_pram_expr_eval        tree contraction (rake, Abrahamson et al.):
 *                            evaluates a binary expression tree of + and *
 *                            in O(log n) rounds by raking odd-numbered
 *                            leaves and composing linear functions
 *
 * Pointer jumping, recursive doubling and shortcutting do O(n log n) work
 * in O(log n) rounds; list contraction and tree contraction (whose leaf
 * list is compacted in parallel the same way) are work-efficient. The
 * operator of a scan may be called from several tasks at once, on
 * disjoint outputs. Validation and the tree's leaf order are sequential.
 *
 * Lists are successor arrays (next[i], DS_PRAM_NONE at a tail); forests
 * are parent arrays (a root is its own parent or DS_PRAM_NONE).
 */

#include "../lib/context.h"
#include "../lib/executor.h"
#include "../lib/status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_PRAM_NONE (~(size_t)0)

typedef void (*ds_pram_op_func_t)(const void *a, const void *b, void *out,
                                  void *user);

/* rank[i]: nodes after i in its list. *out_rounds may be NULL. */
ds_status_t ds_pram_list_rank(const size_t *next, size_t n, size_t *rank,
                              size_t *out_rounds, ds_context_t *context);
/* out[i] = values[i] op values[next[i]] op ... op values[tail]. */
ds_status_t ds_pram_list_scan(const size_t *next, size_t n, const void *values,
                              size_t elem_size, ds_pram_op_func_t op,
                              void *user, void *out, size_t *out_rounds,
                              ds_context_t *context);
ds_status_t ds_pram_find_roots(const size_t *parent, size_t n, size_t *root,
                               size_t *out_rounds, ds_context_t *context);
ds_status_t ds_pram_list_contract_rank(const size_t *next, size_t n,
                                       size_t *rank, unsigned long seed,
                                       size_t *out_rounds,
                                       ds_context_t *context);

typedef enum ds_pram_expr_op {
  DS_PRAM_LEAF = 0,
  DS_PRAM_ADD,
  DS_PRAM_MUL
} ds_pram_expr_op_t;

/* A full binary tree: internal nodes have both children and an op, leaves
 * carry value[i]. */
ds_status_t ds_pram_expr_eval(const size_t *left, const size_t *right,
                              const ds_pram_expr_op_t *op, const double *value,
                              size_t n, size_t root, double *out_value,
                              size_t *out_rounds, ds_context_t *context);

/* the same, each round run on ex (NULL: sequentially) */
ds_status_t ds_pram_list_rank_on(const ds_executor_t *ex, const size_t *next,
                                 size_t n, size_t *rank, size_t *out_rounds,
                                 ds_context_t *context);
ds_status_t ds_pram_list_scan_on(const ds_executor_t *ex, const size_t *next,
                                 size_t n, const void *values, size_t elem_size,
                                 ds_pram_op_func_t op, void *user, void *out,
                                 size_t *out_rounds, ds_context_t *context);
ds_status_t ds_pram_find_roots_on(const ds_executor_t *ex, const size_t *parent,
                                  size_t n, size_t *root, size_t *out_rounds,
                                  ds_context_t *context);
ds_status_t ds_pram_list_contract_rank_on(const ds_executor_t *ex,
                                          const size_t *next, size_t n,
                                          size_t *rank, unsigned long seed,
                                          size_t *out_rounds,
                                          ds_context_t *context);
ds_status_t ds_pram_expr_eval_on(const ds_executor_t *ex, const size_t *left,
                                 const size_t *right,
                                 const ds_pram_expr_op_t *op,
                                 const double *value, size_t n, size_t root,
                                 double *out_value, size_t *out_rounds,
                                 ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_PARALLEL_SIM_H */
