#ifndef DS_ALGO_MULTIFLOW_H
#define DS_ALGO_MULTIFLOW_H

/* Directed multi-commodity flow through a reusable LP/MILP formulation.
 * Each commodity retains its own source/sink and conservation equations;
 * all commodities share each arc's capacity. Parallel arcs and loops work.
 * An integral commodity has integer arc flows AND integer delivered amount.
 * Integer flows may split among paths: this is not unsplittable routing.
 *
 * Models own a dense LP, independent of the input arrays. With K commodities,
 * M arcs and V vertices there are K*(M+1) columns and K*V+M+2*K rows.
 * Memory and construction time are O(rows*columns). A coefficient limit can
 * reject an oversized model before allocation. All allocations use context.
 * The model exposes its LP and integer mask for other solvers; treat its fields
 * as read-only until destroy. The default uses floating-point simplex/MILP,
 * with tolerance-based validation, not exact rational certificates.
 */
#include <dsa/algorithms/lp.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_multiflow_arc {
  size_t from, to;
  double capacity; /* finite, nonnegative */
  double cost;     /* finite; negative costs allow bounded circulations */
} ds_multiflow_arc_t;

typedef struct ds_multiflow_demand {
  size_t source, sink;     /* distinct, in 0..vertex_count-1 */
  double minimum, maximum; /* finite, 0 <= minimum <= maximum */
  double weight;           /* finite, nonnegative throughput weight */
  bool integral;
} ds_multiflow_demand_t;

typedef enum ds_multiflow_goal {
  DS_MULTIFLOW_MAX_THROUGHPUT = 0,
  DS_MULTIFLOW_MIN_COST
} ds_multiflow_goal_t;

typedef struct ds_multiflow_spec {
  size_t vertex_count, arc_count, commodity_count;
  const ds_multiflow_arc_t *arcs;
  const ds_multiflow_demand_t *demands;
  /* Optional K*M commodity-major costs, overriding arc.cost. */
  const double *costs;
  ds_multiflow_goal_t goal;
  size_t max_coefficients; /* 0: unlimited; init sets 1,000,000 */
} ds_multiflow_spec_t;

typedef struct ds_multiflow_model {
  ds_lp_owned_t program;
  unsigned char *integer;
  size_t vertex_count, arc_count, commodity_count;
  bool has_integer;
} ds_multiflow_model_t;

/* Column k*M+e is flow of commodity k on arc e; column K*M+k is its
 * delivered amount. Rows: K*V conservation, M capacity, then (min,max)
 * for each commodity. Setting minimum=maximum requires full delivery.
 * Build output must be uninitialized; it is unchanged on failure. */
void ds_multiflow_spec_init(ds_multiflow_spec_t *spec);
ds_status_t ds_multiflow_build(const ds_multiflow_spec_t *spec,
                               ds_multiflow_model_t *out,
                               ds_context_t *context);
void ds_multiflow_destroy(ds_multiflow_model_t *model);

/* A callback receives a mask iff any variable is integral; NULL here means
 * an entirely CONTINUOUS problem (unlike ds_milp_solve's NULL mask).
 * On OPTIMAL, fill every x entry. On LIMIT/RELAXATION_UNBOUNDED either fill
 * every entry with an incumbent or leave all x entries untouched. Other
 * results leave x untouched. Return DS_OK after setting result, or an error.
 * Scratch comes from context. Callback/user state is borrowed and may advance
 * even if validation fails; do not mutate the model/output arrays in callback.
 */
typedef ds_status_t (*ds_multiflow_solver_fn)(
    const ds_lp_t *lp, const unsigned char *integer, ds_milp_method_t method,
    size_t budget, double *x, double *objective, ds_lp_result_t *result,
    void *user, ds_context_t *context);
typedef struct ds_multiflow_policy {
  ds_multiflow_solver_fn solve; /* NULL: built-in LP/MILP */
  void *user;
  ds_milp_method_t method;
  size_t node_budget; /* init: 100,000; 0: unlimited */
  double tolerance;   /* init: 1e-6; finite, 0 < tolerance < 0.5 */
} ds_multiflow_policy_t;
typedef struct ds_multiflow_result {
  ds_lp_result_t result;
  bool has_solution;
  double objective; /* recomputed from validated solution; 0 if absent */
} ds_multiflow_result_t;

void ds_multiflow_policy_init(ds_multiflow_policy_t *policy);
/* flows: K*M, delivered: K; either may be NULL. Arrays must be disjoint from
 * one another, out and the model. Candidate values are staged, rounded for
 * integral variables and checked against all rows before publication. Tiny
 * negative values within tolerance are clamped to zero. Row tolerance is
 * relative to 1+|rhs|+sum|a*x|; integrality/nonnegativity use absolute
 * tolerance. Errors preserve all outputs. DS_OK with has_solution=false changes
 * only out; it does not mean the optimization problem was solved. A LIMIT may
 * have an incumbent but never certifies optimality. Validation failure is
 * DS_ERR_STATE.
 */
ds_status_t ds_multiflow_solve(const ds_multiflow_model_t *model,
                               const ds_multiflow_policy_t *policy,
                               double *flows, double *delivered,
                               ds_multiflow_result_t *out);

#ifdef __cplusplus
}
#endif
#endif /* DS_ALGO_MULTIFLOW_H */
