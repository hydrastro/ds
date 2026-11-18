/* Competing shipments share road capacity. Model construction is separate
 * from solver choice, and each returned plan is validated before publication.
 */
#include <dsaa.h>
#include <stdio.h>

static ds_status_t choose_solver(const ds_lp_t *lp,
                                 const unsigned char *integer,
                                 ds_milp_method_t method, size_t budget,
                                 double *x, double *objective,
                                 ds_lp_result_t *result, void *user,
                                 ds_context_t *context) {
  const char *name = (const char *)user;
  printf("%s: %lu rows, %lu variables\n", name, (unsigned long)lp->rows,
         (unsigned long)lp->cols);
  if (integer != NULL)
    return ds_milp_solve(lp, integer, method, budget, x, objective, result,
                         context);
  return ds_lp_solve(lp, x, NULL, objective, result, context);
}

static ds_status_t describe(const ds_multiflow_spec_t *spec,
                            const ds_multiflow_policy_t *policy,
                            const char *name) {
  ds_multiflow_model_t model;
  ds_multiflow_result_t result;
  double flows[8], delivered[2];
  ds_status_t st = ds_multiflow_build(spec, &model, NULL);
  if (st != DS_OK)
    return st;
  st = ds_multiflow_solve(&model, policy, flows, delivered, &result);
  if (st == DS_OK) {
    printf("%s: %s", name,
           result.result == DS_LP_OPTIMAL      ? "optimal"
           : result.result == DS_LP_LIMIT      ? "budget reached"
           : result.result == DS_LP_INFEASIBLE ? "infeasible"
                                               : "other result");
    if (result.has_solution)
      printf(", delivered %.3f / %.3f, shared road %.3f, objective %.3f\n",
             delivered[0], delivered[1], flows[1] + flows[5], result.objective);
    else
      puts(", no plan published");
  }
  ds_multiflow_destroy(&model);
  return st;
}

int main(void) {
  ds_multiflow_arc_t arcs[] = {
      {0, 1, 4.0, 1.0}, {1, 2, 2.5, 2.0}, {2, 3, 4.0, 1.0}, {0, 3, 1.0, 10.0}};
  ds_multiflow_demand_t demands[] = {{0, 3, 0.0, 3.0, 2.0, false},
                                     {1, 2, 0.0, 3.0, 1.0, false}};
  ds_multiflow_spec_t spec;
  ds_multiflow_policy_t policy;
  ds_status_t st;
  ds_multiflow_spec_init(&spec);
  ds_multiflow_policy_init(&policy);
  spec.vertex_count = 4;
  spec.arc_count = 4;
  spec.commodity_count = 2;
  spec.arcs = arcs;
  spec.demands = demands;
  st = describe(&spec, &policy, "Fractional loads");
  if (st != DS_OK)
    goto fail;
  demands[0].integral = true;
  demands[1].integral = true;
  policy.node_budget = 1;
  st = describe(&spec, &policy, "One-node budget");
  if (st != DS_OK)
    goto fail;
  policy.node_budget = 10000;
  st = describe(&spec, &policy, "Whole loads");
  if (st != DS_OK)
    goto fail;
  demands[0].minimum = demands[0].maximum = 1.0;
  demands[1].minimum = demands[1].maximum = 1.0;
  spec.goal = DS_MULTIFLOW_MIN_COST;
  policy.method = DS_MILP_CUTTING_PLANE;
  policy.solve = choose_solver;
  policy.user = (void *)"Selected solver";
  st = describe(&spec, &policy, "Required deliveries at minimum cost");
  if (st != DS_OK)
    goto fail;
  demands[0].minimum = demands[0].maximum = 3.0;
  demands[1].minimum = demands[1].maximum = 3.0;
  st = describe(&spec, &policy, "Overbooked road");
  if (st != DS_OK)
    goto fail;
  return 0;
fail:
  fprintf(stderr, "Routing failed: %s\n", ds_status_name(st));
  return 1;
}
