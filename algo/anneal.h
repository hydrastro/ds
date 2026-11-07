#ifndef DS_ANNEAL_H
#define DS_ANNEAL_H

/*
 * Simulated annealing (Kirkpatrick, Gelatt & Vecchi) and its
 * threshold-style relatives, over a user state stored by value.
 *
 * Two ways to describe the search space:
 *
 *   neighbour mode  neighbor() writes a candidate state; energy() scores
 *                   it (each step copies a state of state_size bytes)
 *   move mode       propose() describes a move of move_size bytes and
 *                   returns its energy delta, apply() performs it; nothing
 *                   is copied unless the best state is tracked, and the
 *                   energy is only evaluated once, at the start
 *
 * The temperature follows a schedule (geometric T0 a^k, linear, the
 * logarithmic T0 / ln(e + k) of Geman & Geman, Lundy-Mees
 * T / (1 + b T), or a callback), updated every steps_per_temperature
 * proposals. Moves are accepted by the Metropolis rule, Barker's rule,
 * threshold accepting (Dueck & Scheuer: delta < T), the great deluge
 * (new energy < T, the "water level") or a callback. With t0 <= 0 the
 * initial temperature is estimated from random moves so that an uphill
 * move is accepted with probability initial_acceptance. Optional
 * reheating after a stall, a stop callback and a target energy.
 */

#include "context.h"
#include "random.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_anneal_schedule {
  DS_ANNEAL_GEOMETRIC,
  DS_ANNEAL_LINEAR,
  DS_ANNEAL_LOGARITHMIC,
  DS_ANNEAL_LUNDY_MEES,
  DS_ANNEAL_SCHEDULE_CUSTOM
} ds_anneal_schedule_t;

typedef enum ds_anneal_acceptance {
  DS_ANNEAL_METROPOLIS,
  DS_ANNEAL_BARKER,
  DS_ANNEAL_THRESHOLD,
  DS_ANNEAL_GREAT_DELUGE,
  DS_ANNEAL_ACCEPT_CUSTOM
} ds_anneal_acceptance_t;

typedef struct ds_anneal_problem {
  size_t state_size;
  double (*energy)(const void *state, void *user);
  /* neighbour mode */
  void (*neighbor)(const void *state, void *out, ds_rng_t *rng, void *user);
  /* move mode (used when propose is set) */
  size_t move_size;
  double (*propose)(const void *state, void *move, ds_rng_t *rng, void *user);
  void (*apply)(void *state, const void *move, void *user);
  /* optional: return true to stop */
  bool (*stop)(size_t iteration, double temperature, double energy, double best,
               void *user);
  void *user;
} ds_anneal_problem_t;

typedef struct ds_anneal_config {
  ds_anneal_schedule_t schedule;
  ds_anneal_acceptance_t acceptance;
  double t0;                 /* <= 0: estimate */
  double initial_acceptance; /* for the estimate, in (0, 1) */
  size_t estimate_samples;
  double t_min;  /* stop below this temperature */
  double factor; /* geometric a, Lundy-Mees b; unused otherwise */
  size_t steps_per_temperature;
  size_t max_iterations; /* proposals; also the linear schedule's span */
  size_t reheat_after;   /* temperature stages without a new best, 0 off */
  double reheat_factor;  /* T *= reheat_factor on a reheat */
  double target_energy;  /* stop once best <= target */
  double (*temperature)(size_t stage, double t0, double previous, void *user);
  /* probability of accepting an energy change delta to energy e_new */
  double (*accept)(double delta, double e_new, double temperature, void *user);
} ds_anneal_config_t;

typedef struct ds_anneal_result {
  double energy; /* of the final current state */
  double best_energy;
  double t0;          /* the initial temperature used */
  double temperature; /* at the end */
  size_t iterations;
  size_t accepted;
  size_t improvements; /* times the best was improved */
  size_t reheats;
} ds_anneal_result_t;

/* geometric 0.95, Metropolis, t0 estimated for 0.8 acceptance from 100
 * samples, 100 steps per temperature, 100000 iterations, t_min 1e-9,
 * no reheating, target -HUGE_VAL */
void ds_anneal_config_default(ds_anneal_config_t *config);

/* state: the initial state in, the final current state out; best
 * (optional) receives the best state seen. */
ds_status_t ds_anneal_run(const ds_anneal_problem_t *problem,
                          const ds_anneal_config_t *config, ds_rng_t *rng,
                          void *state, void *best, ds_anneal_result_t *result,
                          ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ANNEAL_H */
