#ifndef DS_TURING_MACHINE_H
#define DS_TURING_MACHINE_H

/*
 * Turing machines: deterministic or nondeterministic, with one or more
 * two-way infinite tapes over the symbols {0, ..., symbols - 1}.
 *
 * A transition reads one symbol per tape in some state and writes one
 * symbol per tape, moves every head (left, right or stay) and changes
 * state. States can be marked accepting, rejecting or halting; a machine
 * also halts when no transition applies. Transitions are kept sorted by
 * (state, symbols read) and found by binary search.
 *
 *   ds_tm_run       run a deterministic machine on a configuration
 *                   (state, tapes, heads) for at most max_steps steps
 *   ds_tm_accepts   nondeterministic acceptance by breadth-first search
 *                   over configurations (bounded by steps and by the
 *                   number of live configurations)
 *   ds_tm_transduce Turing transducer: the non-blank contents of the last
 *                   tape when the machine halts
 *   ds_tm_parse     the compact "1RB1LB_1LA1RZ" notation (one group per
 *                   state, three characters per symbol: write, move L/R/S,
 *                   next state A..Y or Z/H for halt, "---" undefined)
 *   ds_tm_busy_beaver         the record machines for 1..5 states and two
 *                             symbols (longest running, S(n) = 1, 6, 21,
 *                             107, 47176870; they leave 1, 4, 5, 13, 4098
 *                             ones, Sigma(n) except for n = 3 where it is 6)
 *   ds_tm_busy_beaver_search  exhaustive search over n-state 2-symbol
 *                             machines, extending a partial machine only
 *                             when the run reaches a missing transition
 *                             (explicit stack); reports Sigma and S among
 *                             machines that halt within the step limit
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

#define DS_TM_MAX_TAPES 8

typedef enum ds_tm_move { DS_TM_LEFT, DS_TM_RIGHT, DS_TM_STAY } ds_tm_move_t;

typedef enum ds_tm_state_kind {
  DS_TM_NORMAL,
  DS_TM_ACCEPTING,
  DS_TM_REJECTING,
  DS_TM_HALTING
} ds_tm_state_kind_t;

typedef enum ds_tm_outcome {
  DS_TM_ACCEPT,
  DS_TM_REJECT,
  DS_TM_HALT,   /* halting state, or no applicable transition */
  DS_TM_LIMIT,  /* step (or configuration) limit reached */
  DS_TM_RUNNING /* not halted yet (after ds_tm_step) */
} ds_tm_outcome_t;

typedef struct ds_tm {
  size_t states;
  size_t tapes;
  unsigned symbols;
  unsigned char blank;
  size_t start;
  unsigned char *kind; /* ds_tm_state_kind_t per state */
  /* transitions: parallel arrays, tuples of `tapes` entries each */
  size_t *from;
  size_t *to;
  size_t *key; /* the read tuple, base `symbols` */
  unsigned char *read;
  unsigned char *write;
  unsigned char *move;
  size_t count;
  size_t capacity;
  bool sorted;
  ds_context_t *context;
} ds_tm_t;

typedef struct ds_tm_tape {
  unsigned char *cells;
  size_t capacity;
  long base; /* position of cells[0] */
  long head;
} ds_tm_tape_t;

typedef struct ds_tm_config {
  size_t state;
  size_t tapes;
  unsigned char blank;
  ds_tm_tape_t tape[DS_TM_MAX_TAPES];
  unsigned long steps;
  ds_tm_outcome_t outcome;
  ds_context_t *context;
} ds_tm_config_t;

ds_status_t ds_tm_init(ds_tm_t *tm, size_t states, unsigned symbols,
                       size_t tapes, unsigned char blank, size_t start,
                       ds_context_t *context);
void ds_tm_destroy(ds_tm_t *tm);
ds_status_t ds_tm_set_kind(ds_tm_t *tm, size_t state, ds_tm_state_kind_t kind);
/* read, write and move hold `tapes` entries */
ds_status_t ds_tm_add(ds_tm_t *tm, size_t from, const unsigned char *read,
                      size_t to, const unsigned char *write,
                      const ds_tm_move_t *move);
/* single-tape shorthand */
ds_status_t ds_tm_add1(ds_tm_t *tm, size_t from, unsigned char read, size_t to,
                       unsigned char write, ds_tm_move_t move);
bool ds_tm_is_deterministic(ds_tm_t *tm);
/* parse the compact notation; the machine gets n + 1 states, the last
 * one halting, blank 0, start 0, one tape */
ds_status_t ds_tm_parse(ds_tm_t *tm, const char *text, ds_context_t *context);
/* n in 1..5 */
ds_status_t ds_tm_busy_beaver(ds_tm_t *tm, size_t n, ds_context_t *context);

/* input goes on tape 0 at positions 0..length-1, every head at 0 */
ds_status_t ds_tm_config_init(ds_tm_config_t *cfg, const ds_tm_t *tm,
                              const unsigned char *input, size_t length,
                              ds_context_t *context);
ds_status_t ds_tm_config_copy(ds_tm_config_t *dst, const ds_tm_config_t *src);
void ds_tm_config_destroy(ds_tm_config_t *cfg);
unsigned char ds_tm_tape_get(const ds_tm_config_t *cfg, size_t tape, long pos);
/* smallest window [lo, hi] holding every non-blank cell; false if none */
bool ds_tm_tape_extent(const ds_tm_config_t *cfg, size_t tape, long *lo,
                       long *hi);
size_t ds_tm_tape_count(const ds_tm_config_t *cfg, size_t tape,
                        unsigned char symbol);

/* one step of a deterministic machine (the first matching transition);
 * cfg->outcome becomes RUNNING or a halting outcome */
ds_status_t ds_tm_step(ds_tm_t *tm, ds_tm_config_t *cfg);
ds_status_t ds_tm_run(ds_tm_t *tm, ds_tm_config_t *cfg, unsigned long max_steps,
                      ds_tm_outcome_t *out);
/* ACCEPT if some branch accepts within max_steps, REJECT if every branch
 * halts without accepting, LIMIT otherwise (also when more than
 * max_configs configurations are live) */
ds_status_t ds_tm_accepts(ds_tm_t *tm, const unsigned char *input,
                          size_t length, unsigned long max_steps,
                          size_t max_configs, ds_tm_outcome_t *out);
/* run deterministically and copy the last tape's non-blank window to out
 * (capacity symbols; *out_length is the full length, DS_ERR_RANGE if it
 * did not fit) */
ds_status_t ds_tm_transduce(ds_tm_t *tm, const unsigned char *input,
                            size_t length, unsigned long max_steps,
                            unsigned char *out, size_t capacity,
                            size_t *out_length, ds_tm_outcome_t *outcome);
/* max ones (Sigma) and steps (S) over n-state 2-symbol machines that halt
 * within max_steps; *undecided counts machines stopped by the limit */
ds_status_t ds_tm_busy_beaver_search(size_t n, unsigned long max_steps,
                                     unsigned long *sigma, unsigned long *steps,
                                     unsigned long *undecided,
                                     ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_TURING_MACHINE_H */
