#ifndef DS_ALGO_LP_H
#define DS_ALGO_LP_H

/*
 * Linear and integer programming on dense problems.
 *
 * A linear program: maximise (or minimise) c^T x subject to rows
 * a_i^T x (<=, >=, =) b_i and x >= 0. The problem's arrays belong to the
 * caller (ds_lp_t only points at them).
 *
 *   ds_lp_solve       two-phase tableau simplex with Bland's rule (no
 *                     cycling): optimal x, objective, and the dual values
 *                     (shadow prices) of the rows; or infeasible /
 *                     unbounded
 *   ds_lp_dual_build  the dual linear program, written back in this
 *                     module's form (sign-constrained and free dual
 *                     variables mapped to non-negative ones), with the
 *                     map from its variables to the primal rows
 *   ds_milp_solve     integer and mixed integer linear programs (chosen
 *                     variables integral): depth-first branch and bound
 *                     on the most fractional variable, or Gomory fractional
 *                     cutting planes (pure integer programs with integral
 *                     data; others go straight to branch and bound)
 *                     followed by branch and bound when the cuts stall;
 *                     node and cut budgets
 *
 * Scratch memory comes from the ds_context_t.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_lp_sense { DS_LP_LE = 0, DS_LP_GE, DS_LP_EQ } ds_lp_sense_t;

typedef struct ds_lp {
  size_t rows;
  size_t cols;
  const double *a; /* rows x cols */
  const ds_lp_sense_t *sense;
  const double *b;
  const double *c;
  bool maximize;
} ds_lp_t;

typedef enum ds_lp_result {
  DS_LP_OPTIMAL = 0,
  DS_LP_INFEASIBLE,
  DS_LP_UNBOUNDED,
  DS_LP_LIMIT /* a node or cut budget ran out: best solution so far */
} ds_lp_result_t;

/* x: cols; dual (rows, may be NULL): d objective / d b_i. */
ds_status_t ds_lp_solve(const ds_lp_t *lp, double *x, double *dual,
                        double *out_objective, ds_lp_result_t *out_result,
                        ds_context_t *context);

typedef struct ds_lp_owned {
  ds_lp_t lp;
  double *a;
  ds_lp_sense_t *sense;
  double *b;
  double *c;
  size_t *row_of; /* dual variable -> primal row */
  double *sign;   /* dual value of that row = sign * variable */
  ds_context_t *context;
} ds_lp_owned_t;

/* dual->lp is the dual of lp; its optimum equals lp's (strong duality),
 * and the primal row duals are sign[j] * y_j summed per row. */
ds_status_t ds_lp_dual_build(const ds_lp_t *lp, ds_lp_owned_t *dual,
                             ds_context_t *context);
void ds_lp_owned_destroy(ds_lp_owned_t *owned);

typedef enum ds_milp_method {
  DS_MILP_BRANCH_AND_BOUND = 0,
  DS_MILP_CUTTING_PLANE
} ds_milp_method_t;

/* integer: cols flags (NULL: every variable integral). budget: nodes
 * (and cuts) to spend, 0 for unlimited. */
ds_status_t ds_milp_solve(const ds_lp_t *lp, const unsigned char *integer,
                          ds_milp_method_t method, size_t budget, double *x,
                          double *out_objective, ds_lp_result_t *out_result,
                          ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_LP_H */
