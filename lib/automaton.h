#ifndef DS_AUTOMATON_H
#define DS_AUTOMATON_H

/*
 * Finite automata and transducers over an alphabet {0, ..., alphabet - 1}
 * (bytes when alphabet is 256).
 *
 *   ds_dfa_t    deterministic finite automaton (finite state machine) with
 *               a dense transition table; missing transitions
 *               (DS_AUTOMATON_NONE) reject, so partial DFAs are fine
 *   ds_nfa_t    nondeterministic finite automaton with epsilon moves,
 *               simulated by state sets
 *   ds_nfa_to_dfa       subset construction (with a state cap)
 *   ds_dfa_minimize     Hopcroft's partition refinement, O(k n log n);
 *                       the result is the minimal DFA with states renumbered
 *                       in breadth-first order from the start, so two
 *                       languages are equal iff their minimal DFAs are equal
 *                       (ds_dfa_equivalent)
 *   ds_dfa_product      product automaton with any boolean combination of
 *                       acceptance (union, intersection, difference, ...)
 *   ds_nfa_union        union of automata by a new start state
 *   ds_mealy_t          Mealy machine: one output symbol per transition
 *   ds_moore_t          Moore machine: one output symbol per state; with
 *                       conversions both ways
 *   ds_fst_t            finite state transducer (deterministic, with an
 *                       output string per transition and per final state)
 *
 * Memory comes from the ds_context_t given at init; structs are public.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_AUTOMATON_NONE (~(size_t)0)
#define DS_NFA_EPSILON (~(size_t)0)

/* Truth tables for ds_dfa_product: bit (accept_a * 2 + accept_b). */
#define DS_AUTOMATON_UNION 0xEu
#define DS_AUTOMATON_INTERSECTION 0x8u
#define DS_AUTOMATON_DIFFERENCE 0x4u
#define DS_AUTOMATON_SYMMETRIC_DIFFERENCE 0x6u

typedef struct ds_dfa {
  size_t states;
  size_t alphabet;
  size_t start;
  size_t *delta; /* states * alphabet */
  unsigned char *accepting;
  size_t capacity;
  ds_context_t *context;
} ds_dfa_t;

ds_status_t ds_dfa_init(ds_dfa_t *dfa, size_t alphabet, ds_context_t *context);
void ds_dfa_destroy(ds_dfa_t *dfa);
/* New state (no transitions); its id in *out_state. The first state added
 * becomes the start. */
ds_status_t ds_dfa_add_state(ds_dfa_t *dfa, bool accepting, size_t *out_state);
ds_status_t ds_dfa_set_transition(ds_dfa_t *dfa, size_t from, size_t symbol,
                                  size_t to);
ds_status_t ds_dfa_set_start(ds_dfa_t *dfa, size_t state);
/* State reached (DS_AUTOMATON_NONE when a transition is missing). */
size_t ds_dfa_run(const ds_dfa_t *dfa, const size_t *symbols, size_t n);
bool ds_dfa_accepts(const ds_dfa_t *dfa, const size_t *symbols, size_t n);
bool ds_dfa_accepts_bytes(const ds_dfa_t *dfa, const unsigned char *s,
                          size_t n);

typedef struct ds_nfa {
  size_t states;
  size_t alphabet;
  size_t start;
  unsigned char *accepting;
  size_t state_capacity;
  size_t *from; /* edges */
  size_t *symbol;
  size_t *to;
  size_t edges;
  size_t edge_capacity;
  ds_context_t *context;
} ds_nfa_t;

ds_status_t ds_nfa_init(ds_nfa_t *nfa, size_t alphabet, ds_context_t *context);
void ds_nfa_destroy(ds_nfa_t *nfa);
ds_status_t ds_nfa_add_state(ds_nfa_t *nfa, bool accepting, size_t *out_state);
/* symbol DS_NFA_EPSILON for an epsilon move. */
ds_status_t ds_nfa_add_transition(ds_nfa_t *nfa, size_t from, size_t symbol,
                                  size_t to);
ds_status_t ds_nfa_set_start(ds_nfa_t *nfa, size_t state);
ds_status_t ds_nfa_accepts(const ds_nfa_t *nfa, const size_t *symbols, size_t n,
                           bool *out_accepts);
/* out is initialised here. DS_ERR_OVERFLOW when more than max_states DFA
 * states would be needed (0: no cap). */
ds_status_t ds_nfa_to_dfa(const ds_nfa_t *nfa, ds_dfa_t *out, size_t max_states,
                          ds_context_t *context);
/* out: a new NFA accepting L(a) | L(b) (same alphabet). */
ds_status_t ds_nfa_union(const ds_nfa_t *a, const ds_nfa_t *b, ds_nfa_t *out,
                         ds_context_t *context);

/* out is initialised here. The minimal partial DFA (no dead state; a DFA
 * accepting nothing has one non-accepting state). */
ds_status_t ds_dfa_minimize(const ds_dfa_t *dfa, ds_dfa_t *out,
                            ds_context_t *context);
ds_status_t ds_dfa_equivalent(const ds_dfa_t *a, const ds_dfa_t *b,
                              bool *out_equal, ds_context_t *context);
/* Reachable part of the product; table as above. */
ds_status_t ds_dfa_product(const ds_dfa_t *a, const ds_dfa_t *b, unsigned table,
                           ds_dfa_t *out, ds_context_t *context);

/* ---- transducers ---------------------------------------------------- */

typedef struct ds_mealy {
  ds_dfa_t dfa;   /* acceptance unused */
  size_t *output; /* per transition, parallel to dfa.delta */
  size_t output_capacity;
} ds_mealy_t;

ds_status_t ds_mealy_init(ds_mealy_t *m, size_t alphabet,
                          ds_context_t *context);
void ds_mealy_destroy(ds_mealy_t *m);
ds_status_t ds_mealy_add_state(ds_mealy_t *m, size_t *out_state);
ds_status_t ds_mealy_set_transition(ds_mealy_t *m, size_t from, size_t symbol,
                                    size_t to, size_t output);
/* out receives n symbols; DS_NOT_FOUND on a missing transition (out holds
 * the outputs produced so far, their count in *out_count). */
ds_status_t ds_mealy_run(const ds_mealy_t *m, const size_t *input, size_t n,
                         size_t *out, size_t *out_count);

typedef struct ds_moore {
  ds_dfa_t dfa;
  size_t *output; /* per state */
  size_t output_capacity;
} ds_moore_t;

ds_status_t ds_moore_init(ds_moore_t *m, size_t alphabet,
                          ds_context_t *context);
void ds_moore_destroy(ds_moore_t *m);
ds_status_t ds_moore_add_state(ds_moore_t *m, size_t output, size_t *out_state);
ds_status_t ds_moore_set_transition(ds_moore_t *m, size_t from, size_t symbol,
                                    size_t to);
/* out receives n + 1 symbols (the start state's output first). */
ds_status_t ds_moore_run(const ds_moore_t *m, const size_t *input, size_t n,
                         size_t *out, size_t *out_count);
/* Mealy equivalent: output of a transition = output of its target (the
 * Moore start output is dropped). out is initialised here. */
ds_status_t ds_moore_to_mealy(const ds_moore_t *m, ds_mealy_t *out,
                              ds_context_t *context);
/* Moore equivalent: states (q, last output); the start state outputs
 * initial_output. out is initialised here. */
ds_status_t ds_mealy_to_moore(const ds_mealy_t *m, size_t initial_output,
                              ds_moore_t *out, ds_context_t *context);

typedef struct ds_fst {
  ds_dfa_t dfa;        /* accepting = final */
  size_t *out_start;   /* per transition: offset into pool */
  size_t *out_len;     /* per transition */
  size_t *final_start; /* per state */
  size_t *final_len;
  size_t *pool;
  size_t pool_len;
  size_t pool_capacity;
  size_t capacity; /* transitions allocated */
  size_t state_capacity;
} ds_fst_t;

ds_status_t ds_fst_init(ds_fst_t *t, size_t alphabet, ds_context_t *context);
void ds_fst_destroy(ds_fst_t *t);
/* Final states may emit final_output (final_len symbols) at the end. */
ds_status_t ds_fst_add_state(ds_fst_t *t, bool final,
                             const size_t *final_output, size_t final_len,
                             size_t *out_state);
ds_status_t ds_fst_set_transition(ds_fst_t *t, size_t from, size_t symbol,
                                  size_t to, const size_t *output,
                                  size_t output_len);
/* Transduce input: DS_NOT_FOUND if rejected; the output length goes to
 * *out_count (also with DS_ERR_OVERFLOW when capacity is too small). */
ds_status_t ds_fst_run(const ds_fst_t *t, const size_t *input, size_t n,
                       size_t *out, size_t capacity, size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* DS_AUTOMATON_H */
