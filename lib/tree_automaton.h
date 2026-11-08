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

/*
 * ds_tree_transducer_t: deterministic bottom-up tree transducers. A rule
 *   f(q1(x1), ..., qk(xk)) -> q(t)
 * reads input symbol f whose children reached states q1..qk with outputs
 * x1..xk, moves to state q and outputs the tree t: a prefix sequence over
 * the ranked output alphabet in which DS_TT_VAR(i) stands for the output
 * of child i (variables may be copied, reordered or dropped). A run
 * computes state and output bottom-up with an explicit stack; the input is
 * accepted when the root's state is final, and the transduction is the
 * root's output. Copying can make outputs exponentially large, so runs
 * take an output budget (DS_ERR_RANGE when exceeded).
 */
#define DS_TT_VAR(i) ((~(size_t)0) - (size_t)(i))

typedef struct ds_tree_transducer {
  size_t states;
  size_t in_symbols;
  size_t *in_rank;
  size_t out_symbols;
  size_t *out_rank;
  bool *final;
  size_t *sym; /* per rule */
  size_t *target;
  size_t *args; /* offset into pool: rank states */
  size_t *rhs;  /* offset into pool */
  size_t *rhs_len;
  size_t count;
  size_t capacity;
  size_t *pool;
  size_t pool_used;
  size_t pool_capacity;
  ds_context_t *context;
} ds_tree_transducer_t;

ds_status_t ds_tt_init(ds_tree_transducer_t *tt, size_t states,
                       size_t in_symbols, const size_t *in_rank,
                       size_t out_symbols, const size_t *out_rank,
                       ds_context_t *context);
void ds_tt_destroy(ds_tree_transducer_t *tt);
ds_status_t ds_tt_set_final(ds_tree_transducer_t *tt, size_t state, bool final);
/* DS_ERR_INVALID if rhs is not one output tree (variables count as
 * leaves, DS_TT_VAR(i) needs i < rank of symbol) or the left-hand side is
 * already used (the transducer is deterministic) */
ds_status_t ds_tt_add(ds_tree_transducer_t *tt, size_t symbol,
                      const size_t *args, size_t target, const size_t *rhs,
                      size_t rhs_len);
/* run on a prefix-order input tree; *accepted, and when accepted the
 * output tree (prefix order) is copied to out (*out_len its length,
 * DS_ERR_RANGE if it does not fit in capacity). max_work bounds the total
 * output symbols built over the run. A node without an applicable rule
 * rejects. */
ds_status_t ds_tt_run(const ds_tree_transducer_t *tt, const size_t *tree,
                      size_t length, size_t max_work, bool *accepted,
                      size_t *out, size_t capacity, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* DS_TREE_AUTOMATON_H */
