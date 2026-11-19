#include <dsa/algorithms/karnaugh.h>
#include <limits.h>
#include <string.h>

typedef struct km_candidate {
  ds_kmap_cube_t cube;
  unsigned long cover[2]; /* only low 32 bits of each word are used */
  size_t literals;
} km_candidate_t;
typedef struct km_search {
  km_candidate_t *candidates;
  size_t candidate_count, cells, budget, nodes;
  unsigned long required[2];
  size_t selected[64], best[64], best_count, best_literals;
  unsigned long best_cost;
  bool stopped;
} km_search_t;

static size_t km_cells(const ds_kmap_t *m) {
  return (size_t)1 << m->variables;
}
static bool km_member(const unsigned long *bits, size_t index) {
  return (bits[index / 32] & (1UL << (index % 32))) != 0;
}
static void km_set(unsigned long *bits, size_t index) {
  bits[index / 32] |= 1UL << (index % 32);
}
static bool km_complete(const km_search_t *s, const unsigned long *covered) {
  return (covered[0] & s->required[0]) == s->required[0] &&
         (covered[1] & s->required[1]) == s->required[1];
}
static size_t km_literals(unsigned int mask) {
  size_t n = 0;
  while (mask != 0) {
    mask &= mask - 1;
    n++;
  }
  return n;
}
ds_status_t ds_kmap_init(ds_kmap_t *m, size_t variables, size_t row_variables,
                         const size_t *order, const unsigned char *values) {
  ds_kmap_t stage;
  unsigned int seen = 0;
  size_t i, bit;
  if (m == NULL || values == NULL) {
    return DS_ERR_NULL;
  }
  if (variables > DS_KMAP_MAX_VARIABLES || row_variables > variables) {
    return DS_ERR_RANGE;
  }
  memset(&stage, 0, sizeof(stage));
  stage.variables = variables;
  stage.row_variables = row_variables;
  for (i = 0; i < variables; i++) {
    bit = order != NULL ? order[i] : i;
    if (bit >= variables || (seen & (1u << bit)) != 0) {
      return DS_ERR_INVALID;
    }
    seen |= 1u << bit;
    stage.order[i] = bit;
  }
  for (i = 0; i < km_cells(&stage); i++) {
    if (values[i] > DS_KMAP_DONT_CARE) {
      return DS_ERR_INVALID;
    }
    stage.values[i] = values[i];
  }
  *m = stage;
  return DS_OK;
}
ds_status_t ds_kmap_assignment(const ds_kmap_t *m, size_t row, size_t column,
                               size_t *out) {
  size_t i, bits, assignment = 0;
  if (m == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (row >= ((size_t)1 << m->row_variables) ||
      column >= ((size_t)1 << (m->variables - m->row_variables))) {
    return DS_ERR_RANGE;
  }
  bits = (row ^ (row >> 1)) | ((column ^ (column >> 1)) << m->row_variables);
  for (i = 0; i < m->variables; i++) {
    if ((bits & ((size_t)1 << i)) != 0) {
      assignment |= (size_t)1 << m->order[i];
    }
  }
  *out = assignment;
  return DS_OK;
}
static size_t km_ungray(size_t gray) {
  size_t result = 0;
  for (; gray != 0; gray >>= 1) {
    result ^= gray;
  }
  return result;
}
ds_status_t ds_kmap_coordinates(const ds_kmap_t *m, size_t assignment,
                                size_t *out_row, size_t *out_column) {
  size_t i, bits = 0;
  if (m == NULL || out_row == NULL || out_column == NULL) {
    return DS_ERR_NULL;
  }
  if (assignment >= km_cells(m)) {
    return DS_ERR_RANGE;
  }
  for (i = 0; i < m->variables; i++) {
    if ((assignment & ((size_t)1 << m->order[i])) != 0) {
      bits |= (size_t)1 << i;
    }
  }
  *out_row = km_ungray(bits & (((size_t)1 << m->row_variables) - 1));
  *out_column = km_ungray(bits >> m->row_variables);
  return DS_OK;
}
void ds_kmap_policy_init(ds_kmap_policy_t *p) {
  if (p != NULL) {
    memset(p, 0, sizeof(*p));
    p->node_budget = 100000;
  }
}
static bool km_valid(const ds_kmap_t *m, bool target, unsigned int mask,
                     unsigned int value, unsigned long *covered) {
  size_t i;
  covered[0] = covered[1] = 0;
  for (i = 0; i < km_cells(m); i++) {
    if ((i & mask) != value) {
      continue;
    }
    if (m->values[i] == DS_KMAP_DONT_CARE) {
      continue;
    }
    if ((m->values[i] != 0) != target) {
      return false;
    }
    km_set(covered, i);
  }
  return covered[0] != 0 || covered[1] != 0;
}
static bool km_better(const km_search_t *s, unsigned long cost, size_t count,
                      size_t literals) {
  return cost < s->best_cost ||
         (cost == s->best_cost &&
          (count < s->best_count ||
           (count == s->best_count && literals < s->best_literals)));
}
static void km_greedy(km_search_t *s) {
  unsigned long covered[2] = {0, 0};
  s->best_count = s->best_literals = 0;
  s->best_cost = 0;
  while (!km_complete(s, covered)) {
    size_t i, j, best = 0, best_gain = 0;
    for (i = 0; i < s->candidate_count; i++) {
      const km_candidate_t *c = &s->candidates[i];
      size_t gain = 0;
      for (j = 0; j < s->cells; j++) {
        if (km_member(c->cover, j) && !km_member(covered, j)) {
          gain++;
        }
      }
      if (gain != 0 &&
          (best_gain == 0 ||
           c->cube.cost * best_gain < s->candidates[best].cube.cost * gain ||
           (c->cube.cost * best_gain == s->candidates[best].cube.cost * gain &&
            (gain > best_gain ||
             (gain == best_gain &&
              c->literals < s->candidates[best].literals))))) {
        best = i;
        best_gain = gain;
      }
    }
    /* Every required cell belongs to at least its minterm and a prime cube. */
    s->best[s->best_count++] = best;
    s->best_cost += s->candidates[best].cube.cost;
    s->best_literals += s->candidates[best].literals;
    covered[0] |= s->candidates[best].cover[0];
    covered[1] |= s->candidates[best].cover[1];
  }
}
static void km_search(km_search_t *s, const unsigned long *covered,
                      size_t depth, unsigned long cost, size_t literals) {
  size_t point = 0, fewest = (size_t)-1, i, j;
  if (s->budget != 0 && s->nodes == s->budget) {
    s->stopped = true;
    return;
  }
  if (s->nodes < (size_t)-1) {
    s->nodes++;
  }
  if (km_complete(s, covered)) {
    if (km_better(s, cost, depth, literals)) {
      memcpy(s->best, s->selected, depth * sizeof(size_t));
      s->best_count = depth;
      s->best_cost = cost;
      s->best_literals = literals;
    }
    return;
  }
  if (cost > s->best_cost ||
      (cost == s->best_cost && depth >= s->best_count)) {
    return;
  }
  for (j = 0; j < s->cells; j++) {
    size_t choices = 0;
    if (!km_member(s->required, j) || km_member(covered, j)) {
      continue;
    }
    for (i = 0; i < s->candidate_count; i++) {
      if (km_member(s->candidates[i].cover, j) &&
          s->candidates[i].cube.cost <= s->best_cost - cost) {
        choices++;
      }
    }
    if (choices < fewest) {
      point = j;
      fewest = choices;
    }
  }
  if (fewest == 0) {
    return;
  }
  for (i = 0; i < s->candidate_count && !s->stopped; i++) {
    const km_candidate_t *c = &s->candidates[i];
    unsigned long next[2];
    if (cost > s->best_cost) {
      return;
    }
    if (!km_member(c->cover, point) || c->cube.cost > s->best_cost - cost) {
      continue;
    }
    next[0] = covered[0] | c->cover[0];
    next[1] = covered[1] | c->cover[1];
    s->selected[depth] = i;
    km_search(s, next, depth + 1, cost + c->cube.cost, literals + c->literals);
  }
}
ds_status_t ds_kmap_minimize(const ds_kmap_t *m, bool target,
                             const ds_kmap_policy_t *policy,
                             ds_kmap_cover_t *out, ds_context_t *context) {
  ds_kmap_policy_t defaults;
  ds_kmap_cover_t result;
  const ds_kmap_policy_t *p = policy;
  km_search_t *s;
  unsigned int mask, value;
  unsigned long covered[2] = {0, 0};
  size_t capacity = 1, i;
  ds_status_t st = DS_OK;
  if (m == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (m->variables > DS_KMAP_MAX_VARIABLES) {
    return DS_ERR_RANGE;
  }
  if (p == NULL) {
    ds_kmap_policy_init(&defaults);
    p = &defaults;
  }
  for (i = 0; i < m->variables; i++) {
    capacity *= 3;
  }
  s = (km_search_t *)ds_context_calloc(context, 1, sizeof(*s));
  if (s == NULL) {
    return DS_ERR_ALLOC;
  }
  s->candidates = (km_candidate_t *)ds_context_alloc(
      context, capacity * sizeof(km_candidate_t));
  if (s->candidates == NULL) {
    ds_context_free(context, s);
    return DS_ERR_ALLOC;
  }
  s->cells = km_cells(m);
  s->budget = p->node_budget;
  for (i = 0; i < s->cells; i++) {
    if (m->values[i] != DS_KMAP_DONT_CARE && (m->values[i] != 0) == target) {
      km_set(s->required, i);
    }
  }
  for (mask = 0; mask < s->cells; mask++) {
    value = mask;
    for (;;) {
      km_candidate_t c;
      bool prime = true;
      c.cube.mask = mask;
      c.cube.value = value;
      c.cube.cost = 1;
      c.literals = km_literals(mask);
      if (km_valid(m, target, mask, value, c.cover)) {
        if (p->cost == NULL) {
          for (i = 0; i < m->variables && prime; i++) {
            unsigned int bit = 1u << i;
            unsigned long ignored[2];
            if ((mask & bit) != 0 &&
                km_valid(m, target, mask & ~bit, value & ~bit, ignored)) {
              prime = false;
            }
          }
        } else {
          st = p->cost(&c.cube, &c.cube.cost, p->user);
          if (st != DS_OK) {
            if (st > DS_OK) {
              st = DS_ERR_CALLBACK;
            }
            goto done;
          }
          if (c.cube.cost > ULONG_MAX / s->cells) {
            st = DS_ERR_OVERFLOW;
            goto done;
          }
        }
        if (prime) {
          s->candidates[s->candidate_count++] = c;
        }
      }
      if (value == 0) {
        break;
      }
      value = (value - 1) & mask;
    }
  }
  km_greedy(s);
  km_search(s, covered, 0, 0, 0);
  memset(&result, 0, sizeof(result));
  if (s->best_count > 0) {
    result.cubes = (ds_kmap_cube_t *)ds_context_alloc(
        context, s->best_count * sizeof(ds_kmap_cube_t));
    if (result.cubes == NULL) {
      st = DS_ERR_ALLOC;
      goto done;
    }
    for (i = 0; i < s->best_count; i++) {
      result.cubes[i] = s->candidates[s->best[i]].cube;
    }
  }
  result.context = context;
  result.variables = m->variables;
  result.target = target;
  result.count = s->best_count;
  result.cost = s->best_cost;
  result.literals = s->best_literals;
  result.search_nodes = s->nodes;
  result.optimal = !s->stopped;
  *out = result;
  st = s->stopped ? DS_STOP : DS_OK;
done:
  ds_context_free(context, s->candidates);
  ds_context_free(context, s);
  return st;
}
ds_status_t ds_kmap_evaluate(const ds_kmap_cover_t *c, size_t assignment,
                             bool *out) {
  size_t i;
  bool found = false;
  if (c == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (assignment >= ((size_t)1 << c->variables)) {
    return DS_ERR_RANGE;
  }
  for (i = 0; i < c->count && !found; i++) {
    found = (assignment & c->cubes[i].mask) == c->cubes[i].value;
  }
  *out = found ? c->target : !c->target;
  return DS_OK;
}
void ds_kmap_cover_destroy(ds_kmap_cover_t *c) {
  if (c != NULL) {
    ds_context_free(c->context, c->cubes);
    memset(c, 0, sizeof(*c));
  }
}
