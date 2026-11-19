#include <dsa/algorithms/logic_graph.h>
#include <dsa/core/checked.h>
#include <string.h>

static bool lg_arity(ds_logic_op_t op, size_t count) {
  switch (op) {
  case DS_LOGIC_INPUT:
  case DS_LOGIC_FALSE:
  case DS_LOGIC_TRUE:
    return count == 0;
  case DS_LOGIC_NOT:
    return count == 1;
  case DS_LOGIC_MUX:
    return count == 3;
  case DS_LOGIC_AND:
  case DS_LOGIC_OR:
  case DS_LOGIC_XOR:
  case DS_LOGIC_NAND:
  case DS_LOGIC_NOR:
  case DS_LOGIC_CUSTOM:
    return true;
  default:
    return false;
  }
}

ds_status_t ds_logic_circuit_eval(const ds_gview_t *g,
                                  const ds_logic_gate_t *gates,
                                  const ds_logic_policy_t *policy,
                                  unsigned char *out) {
  ds_context_t *ctx = policy == NULL ? NULL : policy->context;
  size_t n, i, j, max_degree = 0, bytes;
  size_t *order = NULL;
  unsigned char *values = NULL, *inputs = NULL;
  ds_status_t st;
  if (g == NULL || g->degree == NULL || g->edge == NULL) {
    return DS_ERR_NULL;
  }
  n = g->vertex_count;
  if (n == 0) {
    return DS_OK;
  }
  if (gates == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!ds_size_mul(n, sizeof(size_t), &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  for (i = 0; i < n; i++) {
    size_t count = g->degree(g->graph, i);
    if (!lg_arity(gates[i].op, count) ||
        (gates[i].op == DS_LOGIC_CUSTOM &&
         (policy == NULL || policy->custom == NULL))) {
      return DS_ERR_INVALID;
    }
    if (count > max_degree) {
      max_degree = count;
    }
    for (j = 0; j < count; j++) {
      size_t target;
      double weight;
      g->edge(g->graph, i, j, &target, &weight);
      if (target >= n) {
        return DS_ERR_RANGE;
      }
    }
  }
  order = (size_t *)ds_context_alloc(ctx, bytes);
  values = (unsigned char *)ds_context_alloc(ctx, n);
  inputs =
      (unsigned char *)ds_context_alloc(ctx, max_degree > 0 ? max_degree : 1);
  if (order == NULL || values == NULL || inputs == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  st = ds_gview_topological_sort(g, order, ctx);
  if (st != DS_OK) {
    goto done;
  }
  /* Edges point to inputs, so reverse the graph's topological order. */
  for (i = n; i > 0; i--) {
    size_t v = order[i - 1], count = g->degree(g->graph, v);
    unsigned char value = 0;
    for (j = 0; j < count; j++) {
      size_t target;
      double weight;
      g->edge(g->graph, v, j, &target, &weight);
      inputs[j] = values[target];
    }
    switch (gates[v].op) {
    case DS_LOGIC_INPUT:
      value = gates[v].input != 0;
      break;
    case DS_LOGIC_FALSE:
      value = 0;
      break;
    case DS_LOGIC_TRUE:
      value = 1;
      break;
    case DS_LOGIC_NOT:
      value = inputs[0] == 0;
      break;
    case DS_LOGIC_AND:
    case DS_LOGIC_NAND:
      value = 1;
      for (j = 0; j < count; j++) {
        value = (unsigned char)(value & inputs[j]);
      }
      if (gates[v].op == DS_LOGIC_NAND) {
        value = value == 0;
      }
      break;
    case DS_LOGIC_OR:
    case DS_LOGIC_NOR:
      for (j = 0; j < count; j++) {
        value = (unsigned char)(value | inputs[j]);
      }
      if (gates[v].op == DS_LOGIC_NOR) {
        value = value == 0;
      }
      break;
    case DS_LOGIC_XOR:
      for (j = 0; j < count; j++) {
        value = (unsigned char)(value ^ inputs[j]);
      }
      break;
    case DS_LOGIC_MUX:
      value = inputs[0] ? inputs[1] : inputs[2];
      break;
    case DS_LOGIC_CUSTOM:
      if (policy == NULL || policy->custom == NULL) {
        st = DS_ERR_INVALID;
        goto done;
      }
      st = policy->custom(v, inputs, count, &value, policy->user);
      if (st != DS_OK) {
        goto done;
      }
      if (value > 1) {
        st = DS_ERR_INVALID;
        goto done;
      }
      break;
    default:
      st = DS_ERR_INVALID;
      goto done;
    }
    values[v] = value;
  }
  memmove(out, values, n);
  st = DS_OK;
done:
  ds_context_free(ctx, inputs);
  ds_context_free(ctx, values);
  ds_context_free(ctx, order);
  return st;
}

typedef struct lg_occurrence {
  size_t rule;
  size_t next;
} lg_occurrence_t;

ds_status_t ds_horn_closure(const ds_horn_rules_t *r,
                            const unsigned char *initial, unsigned char *out,
                            size_t *out_reason, ds_context_t *ctx) {
  size_t n, m, p, i, j, used = 0, read = 0;
  size_t word_bytes, rule_bytes, occurrence_bytes;
  size_t *first = NULL, *remaining = NULL, *queue = NULL, *reason = NULL;
  unsigned char *known = NULL;
  lg_occurrence_t *occ = NULL;
  ds_status_t st = DS_ERR_ALLOC;
  if (r == NULL) {
    return DS_ERR_NULL;
  }
  n = r->fact_count;
  m = r->rule_count;
  if ((n > 0 && out == NULL) ||
      (m > 0 && (r->offsets == NULL || r->heads == NULL))) {
    return DS_ERR_NULL;
  }
  if (m > 0 && r->offsets[0] != 0) {
    return DS_ERR_INVALID;
  }
  p = m == 0 ? 0 : r->offsets[m];
  if (p > 0 && r->premises == NULL) {
    return DS_ERR_NULL;
  }
  if (!ds_size_mul(n, sizeof(size_t), &word_bytes) ||
      !ds_size_mul(m, sizeof(size_t), &rule_bytes) ||
      !ds_size_mul(p, sizeof(lg_occurrence_t), &occurrence_bytes)) {
    return DS_ERR_OVERFLOW;
  }
  for (i = 0; i < m; i++) {
    if (r->offsets[i] > r->offsets[i + 1]) {
      return DS_ERR_INVALID;
    }
    if (r->heads[i] >= n) {
      return DS_ERR_RANGE;
    }
  }
  for (i = 0; i < p; i++) {
    if (r->premises[i] >= n) {
      return DS_ERR_RANGE;
    }
  }
  if (n == 0) {
    return DS_OK;
  }
  first = (size_t *)ds_context_alloc(ctx, word_bytes);
  queue = (size_t *)ds_context_alloc(ctx, word_bytes);
  known = (unsigned char *)ds_context_alloc(ctx, n);
  if (m > 0) {
    remaining = (size_t *)ds_context_alloc(ctx, rule_bytes);
  }
  if (p > 0) {
    occ = (lg_occurrence_t *)ds_context_alloc(ctx, occurrence_bytes);
  }
  if (out_reason != NULL) {
    reason = (size_t *)ds_context_alloc(ctx, word_bytes);
  }
  if (first == NULL || queue == NULL || known == NULL ||
      (m > 0 && remaining == NULL) || (p > 0 && occ == NULL) ||
      (out_reason != NULL && reason == NULL)) {
    goto done;
  }
  for (i = 0; i < n; i++) {
    first[i] = DS_HORN_GIVEN;
    known[i] = initial != NULL && initial[i] != 0;
    if (reason != NULL) {
      reason[i] = DS_HORN_GIVEN;
    }
    if (known[i]) {
      queue[used++] = i;
    }
  }
  for (i = 0; i < m; i++) {
    size_t head = r->heads[i];
    remaining[i] = r->offsets[i + 1] - r->offsets[i];
    for (j = r->offsets[i]; j < r->offsets[i + 1]; j++) {
      size_t fact = r->premises[j];
      occ[j].rule = i;
      occ[j].next = first[fact];
      first[fact] = j;
    }
    if (remaining[i] == 0 && !known[head]) {
      known[head] = 1;
      if (reason != NULL) {
        reason[head] = i;
      }
      queue[used++] = head;
    }
  }
  while (read < used) {
    size_t fact = queue[read++];
    for (j = first[fact]; j != DS_HORN_GIVEN; j = occ[j].next) {
      size_t rule = occ[j].rule, head = r->heads[rule];
      remaining[rule]--;
      if (remaining[rule] == 0 && !known[head]) {
        known[head] = 1;
        if (reason != NULL) {
          reason[head] = rule;
        }
        queue[used++] = head;
      }
    }
  }
  memmove(out, known, n);
  if (reason != NULL) {
    memmove(out_reason, reason, word_bytes);
  }
  st = DS_OK;
done:
  ds_context_free(ctx, reason);
  ds_context_free(ctx, occ);
  ds_context_free(ctx, known);
  ds_context_free(ctx, queue);
  ds_context_free(ctx, remaining);
  ds_context_free(ctx, first);
  return st;
}

ds_status_t ds_path_system(size_t n, const ds_path_rule_t *rules, size_t m,
                           const unsigned char *sources,
                           const unsigned char *terminals, bool *out_any,
                           unsigned char *out_admissible, ds_context_t *ctx) {
  ds_horn_rules_t horn;
  size_t i, slots, bytes;
  size_t *block;
  unsigned char *known;
  ds_status_t st;
  bool any = false;
  if (out_any == NULL || (m > 0 && rules == NULL)) {
    return DS_ERR_NULL;
  }
  if (!ds_size_mul(m, 4, &slots) || !ds_size_add(slots, 1, &slots) ||
      !ds_size_mul(slots, sizeof(size_t), &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  block = (size_t *)ds_context_alloc(ctx, bytes);
  known = (unsigned char *)ds_context_alloc(ctx, n > 0 ? n : 1);
  if (block == NULL || known == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  horn.fact_count = n;
  horn.rule_count = m;
  horn.offsets = block;
  horn.heads = block + m + 1;
  horn.premises = block + 2 * m + 1;
  for (i = 0; i < m; i++) {
    block[i] = 2 * i;
    block[m + 1 + i] = rules[i].head;
    block[2 * m + 1 + 2 * i] = rules[i].left;
    block[2 * m + 2 + 2 * i] = rules[i].right;
  }
  block[m] = 2 * m;
  st = ds_horn_closure(&horn, terminals, known, NULL, ctx);
  if (st != DS_OK) {
    goto done;
  }
  for (i = 0; i < n; i++) {
    if (known[i] && sources != NULL && sources[i]) {
      any = true;
    }
  }
  if (out_admissible != NULL && n > 0) {
    memmove(out_admissible, known, n);
  }
  *out_any = any;
done:
  ds_context_free(ctx, known);
  ds_context_free(ctx, block);
  return st;
}
