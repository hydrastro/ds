#ifndef DS_ALGO_LOGIC_GRAPH_H
#define DS_ALGO_LOGIC_GRAPH_H

/* Boolean circuits and least-fixed-point Horn inference over integer facts.
 * All work storage uses the context. Outputs are published only on success;
 * output arrays may overlap input arrays, but distinct outputs must not
 * overlap. Inputs and graph views remain stable for the whole call. No
 * recursion.
 */
#include <dsa/algorithms/graph_view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_logic_op {
  DS_LOGIC_INPUT,
  DS_LOGIC_FALSE,
  DS_LOGIC_TRUE,
  DS_LOGIC_NOT,
  DS_LOGIC_AND,
  DS_LOGIC_OR,
  DS_LOGIC_XOR,
  DS_LOGIC_NAND,
  DS_LOGIC_NOR,
  DS_LOGIC_MUX,
  DS_LOGIC_CUSTOM
} ds_logic_op_t;

typedef struct ds_logic_gate {
  ds_logic_op_t op;
  unsigned char input; /* INPUT: zero is false, nonzero true */
} ds_logic_gate_t;

/* Called in dependency order. Inputs follow the graph view's edge order.
 * Return DS_OK and a Boolean byte (0 or 1), or an error. Callback side effects
 * are not rolled back if a later gate fails. */
typedef ds_status_t (*ds_logic_gate_fn)(size_t gate,
                                        const unsigned char *inputs,
                                        size_t count, unsigned char *out,
                                        void *user);
typedef struct ds_logic_policy {
  ds_context_t *context;
  ds_logic_gate_fn custom;
  void *user;
} ds_logic_policy_t;

/* Each vertex is a gate; outgoing edges name its inputs (weights ignored).
 * Arbitrary numbering and shared subcircuits are supported. INPUT/constants
 * have arity 0, NOT arity 1, MUX arity 3: selector, true input, false input.
 * AND/OR/XOR and their negations permit any arity; empty AND is true and empty
 * OR/XOR false. CUSTOM accepts any arity and requires policy->custom.
 * DS_ERR_STATE for cycles, DS_ERR_RANGE for bad targets, DS_ERR_INVALID for
 * bad opcode/arity/custom output. O(V+E) plus callback time and graph-view
 * access costs, O(V + maximum arity) auxiliary storage. out has V bytes.
 * policy may be NULL for defaults. */
ds_status_t ds_logic_circuit_eval(const ds_gview_t *dependencies,
                                  const ds_logic_gate_t *gates,
                                  const ds_logic_policy_t *policy,
                                  unsigned char *out);

#define DS_HORN_GIVEN ((size_t) - 1)
typedef struct ds_horn_rules {
  size_t fact_count;
  size_t rule_count;
  const size_t *offsets;  /* rule_count+1; starts at zero, nondecreasing */
  const size_t *premises; /* offsets[rule_count] fact IDs */
  const size_t *heads;    /* rule_count fact IDs */
} ds_horn_rules_t;

/* A rule fires when every premise is known. Empty rules fire immediately;
 * repeated premises are legal. Cycles without initial support derive nothing.
 * initial/out: fact_count bytes; NULL initial means no given facts. Optional
 * out_reason records the first deriving rule, DS_HORN_GIVEN for given or
 * unreachable facts (distinguish using out). Rule 0 is a valid reason.
 * O(facts + rules + premise occurrences) time and auxiliary space. */
ds_status_t ds_horn_closure(const ds_horn_rules_t *rules,
                            const unsigned char *initial, unsigned char *out,
                            size_t *out_reason, ds_context_t *context);

typedef struct ds_path_rule {
  size_t head;
  size_t left;
  size_t right;
} ds_path_rule_t;
/* DADS path-system problem: terminals are initially admissible; (x,y,z)
 * admits x when y and z are admitted. sources/terminals have vertex_count
 * bytes (NULL means empty). out_any is true if any source is admissible;
 * out_admissible optionally receives the complete closure. Uses the Horn
 * engine; O(vertices+rules) time/space. */
ds_status_t ds_path_system(size_t vertex_count, const ds_path_rule_t *rules,
                           size_t rule_count, const unsigned char *sources,
                           const unsigned char *terminals, bool *out_any,
                           unsigned char *out_admissible,
                           ds_context_t *context);

#ifdef __cplusplus
}
#endif
#endif /* DS_ALGO_LOGIC_GRAPH_H */
