#ifndef DS_PUSHDOWN_H
#define DS_PUSHDOWN_H

/*
 * Pushdown automata and pushdown transducers.
 *
 * Input symbols are 0..input_symbols-1 and stack symbols
 * 0..stack_symbols-1. A transition (from, a, X) -> (to, push, output)
 * reads input symbol a (or nothing: DS_PDA_EPSILON), pops X (or looks
 * at nothing: DS_PDA_EPSILON), pushes a string (push[0] ends on top) and,
 * for transducers, emits an output string. Acceptance is by final state,
 * by empty stack, or both at once, after the whole input is read.
 *
 *   ds_pda_accepts        exact membership for nondeterministic automata,
 *                         epsilon cycles and unbounded stacks included: a
 *                         worklist computes the summaries "from position
 *                         i in state p with X on top, the automaton can
 *                         read up to j and reach q having popped X" (the
 *                         [pXq] triples of the PDA-to-grammar
 *                         construction), Earley-style, so only reachable
 *                         summaries are built; polynomial in the input
 *   ds_pda_is_deterministic  DPDA condition: per (state, top) either one
 *                         epsilon move or at most one move per symbol
 *   ds_pda_run            direct simulation of a deterministic automaton
 *                         (and transducer: the output of the run), with a
 *                         step limit against epsilon loops
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

#define DS_PDA_EPSILON (~(size_t)0)

typedef enum ds_pda_acceptance {
  DS_PDA_FINAL_STATE,
  DS_PDA_EMPTY_STACK,
  DS_PDA_FINAL_AND_EMPTY
} ds_pda_acceptance_t;

typedef enum ds_pda_outcome {
  DS_PDA_ACCEPT,
  DS_PDA_REJECT,
  DS_PDA_LIMIT
} ds_pda_outcome_t;

typedef struct ds_pda_transition {
  size_t from;
  size_t input; /* or DS_PDA_EPSILON */
  size_t pop;   /* or DS_PDA_EPSILON */
  size_t to;
  size_t push_offset; /* into push_pool */
  size_t push_length;
  size_t output_offset; /* into output_pool */
  size_t output_length;
} ds_pda_transition_t;

typedef struct ds_pda {
  size_t states;
  size_t input_symbols;
  size_t stack_symbols;
  size_t start;
  size_t start_stack; /* initial stack content, or DS_PDA_EPSILON */
  ds_pda_acceptance_t acceptance;
  bool *accepting;
  ds_pda_transition_t *transitions;
  size_t count;
  size_t capacity;
  size_t *push_pool;
  size_t push_used;
  size_t push_capacity;
  size_t *output_pool;
  size_t output_used;
  size_t output_capacity;
  ds_context_t *context;
} ds_pda_t;

ds_status_t ds_pda_init(ds_pda_t *pda, size_t states, size_t input_symbols,
                        size_t stack_symbols, size_t start, size_t start_stack,
                        ds_pda_acceptance_t acceptance, ds_context_t *context);
void ds_pda_destroy(ds_pda_t *pda);
ds_status_t ds_pda_set_accepting(ds_pda_t *pda, size_t state, bool accepting);
/* push[0] ends on top of the stack; output may be NULL when its length is
 * 0 (output symbols are not checked against any alphabet) */
ds_status_t ds_pda_add(ds_pda_t *pda, size_t from, size_t input, size_t pop,
                       size_t to, const size_t *push, size_t push_length,
                       const size_t *output, size_t output_length);
bool ds_pda_is_deterministic(const ds_pda_t *pda);
ds_status_t ds_pda_accepts(const ds_pda_t *pda, const size_t *input,
                           size_t length, bool *out_accepted);
/* DS_ERR_STATE if not deterministic. The output of the run is copied to
 * out (capacity symbols); *out_length is its full length (DS_ERR_RANGE if
 * it did not fit). max_steps bounds the moves taken. */
ds_status_t ds_pda_run(const ds_pda_t *pda, const size_t *input, size_t length,
                       unsigned long max_steps, ds_pda_outcome_t *outcome,
                       size_t *out, size_t capacity, size_t *out_length);

#ifdef __cplusplus
}
#endif

#endif /* DS_PUSHDOWN_H */
