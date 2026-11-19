#include <dsa/algorithms/multiflow.h>
#include <dsa/core/checked.h>
#include <float.h>
#include <math.h>
#include <string.h>

static bool mf_finite(double v) { return v >= -DBL_MAX && v <= DBL_MAX; }

void ds_multiflow_spec_init(ds_multiflow_spec_t *s) {
  if (s != NULL) {
    memset(s, 0, sizeof(*s));
    s->max_coefficients = 1000000;
  }
}
void ds_multiflow_policy_init(ds_multiflow_policy_t *p) {
  if (p != NULL) {
    memset(p, 0, sizeof(*p));
    p->node_budget = 100000;
    p->tolerance = 1e-6;
  }
}
void ds_multiflow_destroy(ds_multiflow_model_t *m) {
  if (m != NULL) {
    ds_context_free(m->program.context, m->integer);
    ds_lp_owned_destroy(&m->program);
    memset(m, 0, sizeof(*m));
  }
}
ds_status_t ds_multiflow_build(const ds_multiflow_spec_t *s,
                               ds_multiflow_model_t *out,
                               ds_context_t *context) {
  ds_multiflow_model_t m;
  ds_lp_owned_t *p = &m.program;
  size_t flows, cols, rows, conservation, bounds, cells, k, e, row;
  if (s == NULL || out == NULL || (s->arc_count > 0 && s->arcs == NULL) ||
      (s->commodity_count > 0 && s->demands == NULL))
    return DS_ERR_NULL;
  if (s->goal != DS_MULTIFLOW_MAX_THROUGHPUT &&
      s->goal != DS_MULTIFLOW_MIN_COST)
    return DS_ERR_INVALID;
  if (!ds_size_mul(s->commodity_count, s->arc_count, &flows) ||
      !ds_size_add(flows, s->commodity_count, &cols) ||
      !ds_size_mul(s->commodity_count, s->vertex_count, &conservation) ||
      !ds_size_mul(s->commodity_count, 2, &bounds) ||
      !ds_size_add(conservation, s->arc_count, &rows) ||
      !ds_size_add(rows, bounds, &rows) || !ds_size_mul(rows, cols, &cells) ||
      cells > (size_t)-1 / sizeof(double) ||
      rows > (size_t)-1 / sizeof(double) ||
      rows > (size_t)-1 / sizeof(ds_lp_sense_t) ||
      cols > (size_t)-1 / sizeof(double))
    return DS_ERR_OVERFLOW;
  if (s->max_coefficients != 0 && cells > s->max_coefficients)
    return DS_ERR_RANGE;
  for (e = 0; e < s->arc_count; e++) {
    const ds_multiflow_arc_t *a = &s->arcs[e];
    if (a->from >= s->vertex_count || a->to >= s->vertex_count)
      return DS_ERR_RANGE;
    if (!mf_finite(a->capacity) || a->capacity < 0.0 || !mf_finite(a->cost))
      return DS_ERR_INVALID;
  }
  for (k = 0; k < s->commodity_count; k++) {
    const ds_multiflow_demand_t *d = &s->demands[k];
    if (d->source >= s->vertex_count || d->sink >= s->vertex_count)
      return DS_ERR_RANGE;
    if (d->source == d->sink || !mf_finite(d->minimum) ||
        !mf_finite(d->maximum) || !mf_finite(d->weight) || d->minimum < 0.0 ||
        d->maximum < d->minimum || d->weight < 0.0)
      return DS_ERR_INVALID;
  }
  if (s->costs != NULL)
    for (e = 0; e < flows; e++)
      if (!mf_finite(s->costs[e]))
        return DS_ERR_INVALID;
  memset(&m, 0, sizeof(m));
  p->context = context != NULL ? context : ds_default_context();
  p->a = (double *)ds_context_calloc(context, cells > 0 ? cells : 1,
                                     sizeof(double));
  p->b =
      (double *)ds_context_calloc(context, rows > 0 ? rows : 1, sizeof(double));
  p->c =
      (double *)ds_context_calloc(context, cols > 0 ? cols : 1, sizeof(double));
  p->sense = (ds_lp_sense_t *)ds_context_alloc(
      context, (rows > 0 ? rows : 1) * sizeof(ds_lp_sense_t));
  m.integer =
      (unsigned char *)ds_context_calloc(context, cols > 0 ? cols : 1, 1);
  if (p->a == NULL || p->b == NULL || p->c == NULL || p->sense == NULL ||
      m.integer == NULL) {
    ds_multiflow_destroy(&m);
    return DS_ERR_ALLOC;
  }
  m.vertex_count = s->vertex_count;
  m.arc_count = s->arc_count;
  m.commodity_count = s->commodity_count;
  p->lp.rows = rows;
  p->lp.cols = cols;
  p->lp.a = p->a;
  p->lp.b = p->b;
  p->lp.c = p->c;
  p->lp.sense = p->sense;
  p->lp.maximize = s->goal == DS_MULTIFLOW_MAX_THROUGHPUT;
  for (row = 0; row < conservation; row++)
    p->sense[row] = DS_LP_EQ;
  for (e = 0; e < s->arc_count; e++) {
    row = conservation + e;
    p->sense[row] = DS_LP_LE;
    p->b[row] = s->arcs[e].capacity;
  }
  for (k = 0; k < s->commodity_count; k++) {
    const ds_multiflow_demand_t *d = &s->demands[k];
    size_t delivery = flows + k;
    m.integer[delivery] = d->integral ? 1 : 0;
    m.has_integer = m.has_integer || d->integral;
    p->c[delivery] = p->lp.maximize ? d->weight : 0.0;
    row = k * s->vertex_count;
    p->a[(row + d->source) * cols + delivery] = -1.0;
    p->a[(row + d->sink) * cols + delivery] = 1.0;
    for (e = 0; e < s->arc_count; e++) {
      size_t col = k * s->arc_count + e;
      const ds_multiflow_arc_t *a = &s->arcs[e];
      p->a[(row + a->from) * cols + col] += 1.0;
      p->a[(row + a->to) * cols + col] -= 1.0;
      p->a[(conservation + e) * cols + col] = 1.0;
      if (!p->lp.maximize)
        p->c[col] = s->costs != NULL ? s->costs[col] : a->cost;
      m.integer[col] = d->integral ? 1 : 0;
    }
    row = conservation + s->arc_count + 2 * k;
    p->sense[row] = DS_LP_GE;
    p->b[row] = d->minimum;
    p->a[row * cols + delivery] = 1.0;
    p->sense[row + 1] = DS_LP_LE;
    p->b[row + 1] = d->maximum;
    p->a[(row + 1) * cols + delivery] = 1.0;
  }
  *out = m;
  return DS_OK;
}

static bool mf_validate(const ds_multiflow_model_t *m, double *x, double tol,
                        double *objective) {
  const ds_lp_t *lp = &m->program.lp;
  size_t i, j;
  double obj = 0.0;
  for (j = 0; j < lp->cols; j++) {
    if (!mf_finite(x[j]) || x[j] < -tol)
      return false;
    if (m->integer[j]) {
      double rounded = floor(x[j] + 0.5);
      if (fabs(rounded - x[j]) > tol)
        return false;
      x[j] = rounded;
    }
    if (x[j] < 0.0)
      x[j] = 0.0;
    obj += lp->c[j] * x[j];
  }
  if (!mf_finite(obj))
    return false;
  for (i = 0; i < lp->rows; i++) {
    double lhs = 0.0, scale = 1.0 + fabs(lp->b[i]), error;
    for (j = 0; j < lp->cols; j++) {
      double term = lp->a[i * lp->cols + j] * x[j];
      lhs += term;
      scale += fabs(term);
    }
    error = lhs - lp->b[i];
    if (!mf_finite(lhs) || !mf_finite(scale) || !mf_finite(error) ||
        (lp->sense[i] == DS_LP_LE && error > tol * scale) ||
        (lp->sense[i] == DS_LP_GE && error < -tol * scale) ||
        (lp->sense[i] == DS_LP_EQ && fabs(error) > tol * scale))
      return false;
  }
  *objective = obj;
  return true;
}

ds_status_t ds_multiflow_solve(const ds_multiflow_model_t *m,
                               const ds_multiflow_policy_t *policy,
                               double *flows, double *delivered,
                               ds_multiflow_result_t *out) {
  ds_multiflow_policy_t defaults;
  ds_multiflow_result_t result;
  const ds_multiflow_policy_t *p = policy;
  const ds_lp_t *lp;
  ds_context_t *context;
  double *x, objective = 0.0;
  size_t j, flow_count;
  bool untouched = true;
  ds_status_t st;
  if (m == NULL || out == NULL)
    return DS_ERR_NULL;
  if (p == NULL) {
    ds_multiflow_policy_init(&defaults);
    p = &defaults;
  }
  if (!(p->tolerance > 0.0 && p->tolerance < 0.5) ||
      (p->method != DS_MILP_BRANCH_AND_BOUND &&
       p->method != DS_MILP_CUTTING_PLANE))
    return DS_ERR_INVALID;
  lp = &m->program.lp;
  context = m->program.context;
  if (m->integer == NULL || lp->c == NULL)
    return DS_ERR_STATE;
  x = (double *)ds_context_alloc(context, (lp->cols > 0 ? lp->cols : 1) *
                                              sizeof(double));
  if (x == NULL)
    return DS_ERR_ALLOC;
  for (j = 0; j < lp->cols; j++)
    x[j] = -DBL_MAX;
  result.result = DS_LP_LIMIT;
  result.has_solution = false;
  result.objective = 0.0;
  if (p->solve != NULL)
    st = p->solve(lp, m->has_integer ? m->integer : NULL, p->method,
                  p->node_budget, x, &objective, &result.result, p->user,
                  context);
  else if (m->has_integer)
    st = ds_milp_solve(lp, m->integer, p->method, p->node_budget, x, &objective,
                       &result.result, context);
  else
    st = ds_lp_solve(lp, x, NULL, &objective, &result.result, context);
  if (st != DS_OK) {
    if (st > DS_OK)
      st = DS_ERR_CALLBACK;
    goto done;
  }
  for (j = 0; j < lp->cols; j++)
    if (x[j] > -DBL_MAX || !mf_finite(x[j]))
      untouched = false;
  if (result.result == DS_LP_OPTIMAL ||
      ((result.result == DS_LP_LIMIT ||
        result.result == DS_LP_RELAXATION_UNBOUNDED) &&
       !untouched)) {
    if (!mf_validate(m, x, p->tolerance, &result.objective)) {
      st = DS_ERR_STATE;
      goto done;
    }
    result.has_solution = true;
  } else if (result.result != DS_LP_LIMIT &&
             result.result != DS_LP_INFEASIBLE &&
             result.result != DS_LP_UNBOUNDED &&
             result.result != DS_LP_RELAXATION_UNBOUNDED) {
    st = DS_ERR_STATE;
    goto done;
  }
  if (result.has_solution) {
    flow_count = m->commodity_count * m->arc_count;
    if (flows != NULL && flow_count > 0)
      memcpy(flows, x, flow_count * sizeof(double));
    if (delivered != NULL && m->commodity_count > 0)
      memcpy(delivered, x + flow_count, m->commodity_count * sizeof(double));
  }
  *out = result;
done:
  ds_context_free(context, x);
  return st;
}
