#ifndef DS_MODEL_CHECK_H
#define DS_MODEL_CHECK_H

/*
 * Explicit-state CTL model checking over Kripke structures.
 *
 * ds_kripke_t  a finite set of states, a transition relation (CSR, both
 *              directions), atomic propositions labelling the states and a
 *              set of initial states. CTL assumes every state has a
 *              successor; ds_kripke_make_total adds self-loops to deadlocks.
 * ds_ctl_t     an arena of CTL formulas (a DAG: children always precede
 *              their parents), built with the constructors or parsed from
 *              text such as "AG (req -> AF ack)" or "E[!err U done]".
 *
 * ds_ctl_check labels every state with every needed subformula, bottom-up
 * and without recursion, in O(|f| (n + m)): EX by predecessor scans,
 * E[f U g] by backward search, EG f as a greatest fixpoint (successor
 * counting). The A operators and EF, AF, AG, weak until (W) reduce to
 * these. With fairness constraints (sets of states to be visited
 * infinitely often) the path quantifiers range over fair paths only
 * (Emerson-Lei): EG is computed from the nontrivial strongly connected
 * components of the f-subgraph that meet every fairness set (iterative
 * Tarjan), and EX, EU are restricted to states with a fair path.
 * Witnesses: a shortest path for E[f U g] and a lasso for EG f.
 *
 * Costs with fairness: the fair states (those with a fair path) are
 * computed once per check, and each EG (also inside AF, AU, EW) runs
 * Tarjan's algorithm on its subgraph and tests every nontrivial component
 * against every fairness set: O(n + m + n k) for k fairness sets instead
 * of the O(n + m) of the plain greatest fixpoint, so a check costs
 * O(|f| (n + m + n k)).
 *
 * Fairness and witnesses: ds_ctl_witness_until and ds_ctl_witness_globally
 * ignore fairness (they follow the plain semantics); the _fair versions
 * match ds_ctl_check with the same fairness sets. A fair until witness
 * ends in a b-state from which a fair path continues; a fair EG lasso
 * runs through a-states into a fair component, and its loop, inside that
 * component, visits every fairness set before closing.
 *
 * Memory comes from the ds_context_t given at init.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_kripke {
  size_t states;
  size_t props;
  bool *label; /* states * props */
  bool *initial;
  size_t *edge_from; /* edge list until finalised */
  size_t *edge_to;
  size_t edges;
  size_t edge_capacity;
  size_t *succ_off; /* CSR, rebuilt lazily */
  size_t *succ;
  size_t *pred_off;
  size_t *pred;
  bool built;
  ds_context_t *context;
} ds_kripke_t;

ds_status_t ds_kripke_init(ds_kripke_t *k, size_t states, size_t props,
                           ds_context_t *context);
void ds_kripke_destroy(ds_kripke_t *k);
ds_status_t ds_kripke_add_edge(ds_kripke_t *k, size_t from, size_t to);
ds_status_t ds_kripke_set_label(ds_kripke_t *k, size_t state, size_t prop,
                                bool value);
ds_status_t ds_kripke_set_initial(ds_kripke_t *k, size_t state, bool value);
bool ds_kripke_is_total(ds_kripke_t *k);
/* add a self-loop to every state without successors */
ds_status_t ds_kripke_make_total(ds_kripke_t *k);
/* successors of a state (valid until the next edge is added) */
ds_status_t ds_kripke_successors(ds_kripke_t *k, size_t state,
                                 const size_t **out, size_t *count);

typedef enum ds_ctl_op {
  DS_CTL_TRUE,
  DS_CTL_FALSE,
  DS_CTL_ATOM,
  DS_CTL_NOT,
  DS_CTL_AND,
  DS_CTL_OR,
  DS_CTL_IMPLIES,
  DS_CTL_IFF,
  DS_CTL_EX,
  DS_CTL_AX,
  DS_CTL_EF,
  DS_CTL_AF,
  DS_CTL_EG,
  DS_CTL_AG,
  DS_CTL_EU, /* E[a U b] */
  DS_CTL_AU,
  DS_CTL_EW, /* E[a W b] = E[a U b] | EG a */
  DS_CTL_AW
} ds_ctl_op_t;

typedef struct ds_ctl {
  unsigned char *op;
  size_t *a; /* atom index for ATOM */
  size_t *b;
  size_t count;
  size_t capacity;
  ds_context_t *context;
} ds_ctl_t;

ds_status_t ds_ctl_init(ds_ctl_t *ctl, ds_context_t *context);
void ds_ctl_destroy(ds_ctl_t *ctl);
ds_status_t ds_ctl_const(ds_ctl_t *ctl, bool value, size_t *out);
ds_status_t ds_ctl_atom(ds_ctl_t *ctl, size_t prop, size_t *out);
ds_status_t ds_ctl_unary(ds_ctl_t *ctl, ds_ctl_op_t op, size_t a, size_t *out);
ds_status_t ds_ctl_binary(ds_ctl_t *ctl, ds_ctl_op_t op, size_t a, size_t b,
                          size_t *out);
/* Syntax: true false name ! & | -> <-> ( ) EX AX EF AF EG AG
 * E[f U g] A[f U g] E[f W g] A[f W g]; names are looked up in
 * names[0..count) (atom = index). -> is right associative; nesting is
 * limited to 256 levels (DS_ERR_RANGE). DS_ERR_INVALID on syntax errors
 * and unknown names, with *error_at (optional) the offset. */
ds_status_t ds_ctl_parse(ds_ctl_t *ctl, const char *text,
                         const char *const *names, size_t count, size_t *out,
                         size_t *error_at);

/* sat[s] for every state; fair (optional) holds fair_count state sets */
ds_status_t ds_ctl_check(ds_kripke_t *k, const ds_ctl_t *ctl, size_t formula,
                         const bool *const *fair, size_t fair_count, bool *sat);
/* the formula holds in every initial state (DS_ERR_STATE if none) */
ds_status_t ds_ctl_holds(ds_kripke_t *k, const ds_ctl_t *ctl, size_t formula,
                         const bool *const *fair, size_t fair_count, bool *out);
/* shortest path from start through a-states to a b-state (a, b as state
 * sets, e.g. from ds_ctl_check); DS_ERR_STATE if E[a U b] fails there */
ds_status_t ds_ctl_witness_until(ds_kripke_t *k, const bool *a, const bool *b,
                                 size_t start, size_t *path, size_t capacity,
                                 size_t *length);
/* a lasso of a-states from start: path[0..length) where the last state
 * steps back to path[*loop_at]; DS_ERR_STATE if EG a fails there */
ds_status_t ds_ctl_witness_globally(ds_kripke_t *k, const bool *a, size_t start,
                                    size_t *path, size_t capacity,
                                    size_t *length, size_t *loop_at);
/* the same over fair paths (fair_count 0: the plain versions); *length is
 * set even when capacity is short (DS_ERR_RANGE) */
ds_status_t ds_ctl_witness_until_fair(ds_kripke_t *k, const bool *a,
                                      const bool *b, const bool *const *fair,
                                      size_t fair_count, size_t start,
                                      size_t *path, size_t capacity,
                                      size_t *length);
ds_status_t ds_ctl_witness_globally_fair(ds_kripke_t *k, const bool *a,
                                         const bool *const *fair,
                                         size_t fair_count, size_t start,
                                         size_t *path, size_t capacity,
                                         size_t *length, size_t *loop_at);

#ifdef __cplusplus
}
#endif

#endif /* DS_MODEL_CHECK_H */
