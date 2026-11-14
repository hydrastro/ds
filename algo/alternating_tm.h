#ifndef DS_ALGO_ALTERNATING_TM_H
#define DS_ALGO_ALTERNATING_TM_H

/*
 * Alternating Turing machines (Chandra, Kozen and Stockmeyer, 1981): the
 * machines of lib/turing_machine, whose normal states are existential and
 * whose DS_TM_UNIVERSAL states are universal. A configuration accepts when
 * its state is accepting, when it is existential and some successor
 * accepts, or when it is universal and every successor accepts; so a
 * universal configuration without an applicable transition accepts and an
 * existential one rejects, and rejecting and halting states reject.
 * Acceptance is the least such labelling: an input is accepted when a
 * finite accepting computation tree exists, and a branch running forever
 * accepts nothing.
 *
 * ds_atm_accepts explores the configurations reachable from the input,
 * breadth first, storing each one once in a map (lib/map_iface, any
 * backend) keyed by a canonical encoding: every tape is translated so that
 * its window (the non-blank cells and the head) starts at 0, so
 * configurations that differ only by where a tape lies are one. The
 * exploration stops at max_configs configurations (or max_bytes of
 * encodings) and does not enter configurations whose window on some tape
 * is wider than `space` cells.
 * Acceptance over the explored graph is then a reachability game solved
 * by ds_game_attractor (algo/game), twice if needed: with the
 * configurations left out taken as rejecting, and as accepting. Since
 * acceptance is monotone in them, an ACCEPT from the first or a REJECT
 * from the second is the answer for the machine itself, without any
 * bound; when they differ the outcome is DS_TM_LIMIT. Time and memory are
 * linear in the explored configurations and transitions (times the
 * length of an encoding).
 *
 * The bounds make the classical resource classes runnable: with space
 * s(n) the configuration graph is finite, and an alternating machine in
 * space s(n) decides what a deterministic one decides in time 2^O(s(n)).
 */

#include "../lib/context.h"
#include "../lib/map_iface.h"
#include "../lib/status.h"
#include "../lib/turing_machine.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_atm_options {
  size_t space;            /* the widest window per tape; 0: no bound */
  size_t max_configs;      /* configurations stored; 0: 1 << 20 */
  size_t max_bytes;        /* their encodings, in bytes; 0: 1 << 26 */
  const ds_map_ops_t *map; /* the configuration map; NULL: Robin Hood */
} ds_atm_options_t;

typedef struct ds_atm_stats {
  size_t configs;     /* configurations explored */
  size_t transitions; /* edges of the explored configuration graph */
  size_t beyond;      /* transitions leading past the limits */
  size_t depth;       /* on ACCEPT: the height of a shortest accepting
                         computation tree (its longest branch, in steps) */
} ds_atm_stats_t;

void ds_atm_options_init(ds_atm_options_t *options);

/* *out: DS_TM_ACCEPT, DS_TM_REJECT or DS_TM_LIMIT; options and stats may
 * be NULL. The input goes on tape 0 from position 0 with every head at 0,
 * as in ds_tm_config_init. Memory comes from the machine's context. */
ds_status_t ds_atm_accepts(ds_tm_t *tm, const unsigned char *input,
                           size_t length, const ds_atm_options_t *options,
                           ds_tm_outcome_t *out, ds_atm_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_ALTERNATING_TM_H */
