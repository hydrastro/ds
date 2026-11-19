/* Reusable Boolean gates and least-fixed-point prerequisite inference. */
#include <dsa.h>
#include <stdio.h>

static ds_status_t threshold(size_t gate, const unsigned char *inputs,
                             size_t count, unsigned char *out, void *user) {
  const size_t *minimum = (const size_t *)user;
  size_t i, yes = 0;
  (void)gate;
  for (i = 0; i < count; i++) {
    yes += inputs[i] != 0;
  }
  *out = yes >= *minimum;
  return DS_OK;
}
int main(void) {
  const size_t offsets[6] = {0, 2, 4, 5, 5, 5};
  const size_t inputs[5] = {1, 2, 3, 4, 4};
  const ds_logic_gate_t gates[5] = {{DS_LOGIC_AND, 0},
                                    {DS_LOGIC_CUSTOM, 0},
                                    {DS_LOGIC_NOT, 0},
                                    {DS_LOGIC_INPUT, 1},
                                    {DS_LOGIC_INPUT, 0}};
  ds_gview_csr_t csr;
  ds_gview_t view;
  ds_logic_policy_t policy;
  size_t minimum = 1, i;
  unsigned char evaluated[5];
  const size_t rule_offsets[7] = {0, 0, 3, 5, 6, 7, 8};
  const size_t premises[8] = {0, 1, 2, 3, 3, 6, 5, 4};
  const size_t heads[6] = {2, 3, 4, 5, 6, 7};
  const unsigned char given[8] = {1, 1, 0, 0, 0, 0, 0, 0};
  unsigned char closure[8];
  size_t reason[8];
  ds_horn_rules_t rules;
  const ds_path_rule_t paths[5] = {
      {3, 0, 1}, {4, 3, 2}, {7, 4, 4}, {5, 6, 6}, {6, 5, 5}};
  const unsigned char terminals[8] = {1, 1, 1, 0, 0, 0, 0, 0};
  const unsigned char sources[8] = {0, 0, 0, 0, 0, 0, 0, 1};
  bool ready;
  ds_status_t st;
  csr.vertex_count = 5;
  csr.offsets = offsets;
  csr.targets = inputs;
  csr.weights = NULL;
  ds_gview_from_csr(&csr, &view);
  policy.context = NULL;
  policy.custom = threshold;
  policy.user = &minimum;
  st = ds_logic_circuit_eval(&view, gates, &policy, evaluated);
  if (st != DS_OK) {
    goto done;
  }
  printf("Deployment gate: %s\n", evaluated[0] ? "ready" : "waiting");
  rules.fact_count = 8;
  rules.rule_count = 6;
  rules.offsets = rule_offsets;
  rules.premises = premises;
  rules.heads = heads;
  st = ds_horn_closure(&rules, given, closure, reason, NULL);
  if (st != DS_OK) {
    goto done;
  }
  puts("Derived prerequisites:");
  for (i = 0; i < 8; i++) {
    if (!closure[i]) {
      printf("  %lu: waiting\n", (unsigned long)i);
    } else if (reason[i] == DS_HORN_GIVEN) {
      printf("  %lu: supplied\n", (unsigned long)i);
    } else {
      printf("  %lu: rule %lu\n", (unsigned long)i, (unsigned long)reason[i]);
    }
  }
  st = ds_path_system(8, paths, 5, sources, terminals, &ready, closure, NULL);
  if (st == DS_OK) {
    printf("Path-system release target: %s\n",
           ready ? "admissible" : "waiting");
  }
done:
  if (st != DS_OK) {
    fprintf(stderr, "deployment rules: %s\n", ds_status_name(st));
  }
  return st == DS_OK ? 0 : 1;
}
