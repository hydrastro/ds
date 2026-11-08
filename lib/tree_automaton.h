#ifndef DS_TREE_AUTOMATON_H
#define DS_TREE_AUTOMATON_H

/*
 * Finite tree automata over a ranked alphabet: symbol f has arity
 * rank[f]. Trees are given in prefix (Polish) order, which the ranks make
 * unambiguous: f(a, g(b)) is {f, a, g, b} with ranks 2, 0, 1, 0.
 *
 * ds_fta_t  bottom-up automaton, transitions f(q1, ..., qk) -> q. It is
 *           nondeterministic in general (NFTA); deterministic (DFTA) when
 *           no two transitions share a left-hand side, complete when every
 *           left-hand side has one. Runs compute, bottom-up, the set of
 *           states each subtree can reach (bitsets, an explicit stack).
 *   ds_fta_determinize  subset construction for tree automata (the
 *                       result is a complete DFTA, the empty set being
 *                       the sink), with a cap on the number of states
 *   ds_fta_intersection / ds_fta_union   product and disjoint union
 *   ds_fta_complement   of a complete DFTA (flip the final states)
 *   ds_fta_is_empty     reachable-state fixpoint, with an optional
 *                       witness tree
 *
 * ds_tdta_t top-down automaton: from state q at a node labelled f,
 *           transitions (q, f) -> (q1, ..., qk) send qi to the i-th
 *           child; a leaf a needs a transition (q, a) -> (). Deterministic
 *           ones (at most one transition per (q, f), strictly weaker than
 *           bottom-up automata) run directly in one left-to-right pass;
 *           nondeterministic ones are run as the equivalent bottom-up
 *           automaton (ds_tdta_to_fta).
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

typedef struct ds_fta {
  size_t states;
  size_t symbols;
  size_t *rank; /* per symbol */
  bool *final;  /* per state */
  size_t *sym;  /* per transition */
  size_t *target;
  size_t *args; /* offset into the argument pool */
  size_t count;
  size_t capacity;
  size_t *pool;
  size_t pool_used;
  size_t pool_capacity;
  ds_context_t *context;
} ds_fta_t;

ds_status_t ds_fta_init(ds_fta_t *fta, size_t states, size_t symbols,
                        const size_t *rank, ds_context_t *context);
void ds_fta_destroy(ds_fta_t *fta);
ds_status_t ds_fta_set_final(ds_fta_t *fta, size_t state, bool final);
/* args holds rank[symbol] states (may be NULL for constants) */
ds_status_t ds_fta_add(ds_fta_t *fta, size_t symbol, const size_t *args,
                       size_t target);
bool ds_fta_is_deterministic(const ds_fta_t *fta);
bool ds_fta_is_complete(const ds_fta_t *fta);
/* DS_ERR_INVALID if the prefix sequence is not exactly one tree */
ds_status_t ds_fta_accepts(const ds_fta_t *fta, const size_t *tree,
                           size_t length, bool *out_accepted);
/* reached[state] (size fta->states) = whether the whole tree can reach it */
ds_status_t ds_fta_run(const ds_fta_t *fta, const size_t *tree, size_t length,
                       bool *reached);
ds_status_t ds_fta_determinize(const ds_fta_t *fta, size_t max_states,
                               ds_fta_t *out, ds_context_t *context);
ds_status_t ds_fta_intersection(const ds_fta_t *a, const ds_fta_t *b,
                                ds_fta_t *out, ds_context_t *context);
ds_status_t ds_fta_union(const ds_fta_t *a, const ds_fta_t *b, ds_fta_t *out,
                         ds_context_t *context);
/* DS_ERR_STATE unless fta is a complete DFTA; complements in place */
ds_status_t ds_fta_complement(ds_fta_t *fta);
/* *out_empty; when not empty and witness != NULL, a smallest-height
 * accepted tree in prefix order (capacity symbols, *witness_length its
 * full length, DS_ERR_RANGE if it did not fit) */
ds_status_t ds_fta_is_empty(const ds_fta_t *fta, bool *out_empty,
                            size_t *witness, size_t capacity,
                            size_t *witness_length);

typedef struct ds_tdta {
  size_t states;
  size_t symbols;
  size_t *rank;
  size_t initial;
  size_t *from; /* per transition */
  size_t *sym;
  size_t *args; /* offset into the pool: rank[sym] child states */
  size_t count;
  size_t capacity;
  size_t *pool;
  size_t pool_used;
  size_t pool_capacity;
  ds_context_t *context;
} ds_tdta_t;

ds_status_t ds_tdta_init(ds_tdta_t *tdta, size_t states, size_t symbols,
                         const size_t *rank, size_t initial,
                         ds_context_t *context);
void ds_tdta_destroy(ds_tdta_t *tdta);
ds_status_t ds_tdta_add(ds_tdta_t *tdta, size_t from, size_t symbol,
                        const size_t *children);
bool ds_tdta_is_deterministic(const ds_tdta_t *tdta);
ds_status_t ds_tdta_accepts(const ds_tdta_t *tdta, const size_t *tree,
                            size_t length, bool *out_accepted);
ds_status_t ds_tdta_to_fta(const ds_tdta_t *tdta, ds_fta_t *out,
                           ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_TREE_AUTOMATON_H */
