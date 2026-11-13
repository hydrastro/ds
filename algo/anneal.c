#include "anneal.h"
#include <math.h>
#include <string.h>

void ds_anneal_config_default(ds_anneal_config_t *config) {
  if (config == NULL) {
    return;
  }
  config->schedule = DS_ANNEAL_GEOMETRIC;
  config->acceptance = DS_ANNEAL_METROPOLIS;
  config->t0 = 0.0;
  config->initial_acceptance = 0.8;
  config->estimate_samples = 100;
  config->t_min = 1e-9;
  config->factor = 0.95;
  config->steps_per_temperature = 100;
  config->max_iterations = 100000;
  config->reheat_after = 0;
  config->reheat_factor = 2.0;
  config->target_energy = -HUGE_VAL;
  config->temperature = NULL;
  config->accept = NULL;
  config->resync_interval = 0;
}

typedef struct an_run {
  const ds_anneal_problem_t *p;
  const ds_anneal_config_t *c;
  ds_rng_t *rng;
  void *state;
  unsigned char *scratch; /* candidate state or move */
} an_run_t;

/* propose from the current state; the delta of taking it */
static double an_propose(an_run_t *r, double energy) {
  if (r->p->propose != NULL) {
    return r->p->propose(r->state, r->scratch, r->rng, r->p->user);
  }
  r->p->neighbor(r->state, r->scratch, r->rng, r->p->user);
  return r->p->energy(r->scratch, r->p->user) - energy;
}

static void an_take(an_run_t *r) {
  if (r->p->propose != NULL) {
    r->p->apply(r->state, r->scratch, r->p->user);
  } else if (r->p->copy == NULL) {
    memcpy(r->state, r->scratch, r->p->state_size);
  } else {
    /* owned states: exchange them, so no resource is shared or lost */
    unsigned char *a = (unsigned char *)r->state;
    size_t i;
    for (i = 0; i < r->p->state_size; i++) {
      unsigned char t = a[i];
      a[i] = r->scratch[i];
      r->scratch[i] = t;
    }
  }
}

static void an_keep_best(const ds_anneal_problem_t *p, void *best,
                         const void *state) {
  if (p->copy != NULL) {
    p->copy(best, state, p->user);
  } else {
    memcpy(best, state, p->state_size);
  }
}

/* move mode: the exact energy instead of the accumulated one */
static double an_resync(const ds_anneal_problem_t *p, const void *state,
                        double energy, ds_anneal_result_t *res) {
  double exact = p->energy(state, p->user);
  double drift = fabs(exact - energy);
  if (drift > res->max_drift) {
    res->max_drift = drift;
  }
  return exact;
}

static double an_accept_probability(const ds_anneal_config_t *c, double delta,
                                    double e_new, double t, void *user) {
  switch (c->acceptance) {
  case DS_ANNEAL_METROPOLIS:
    if (!(delta > 0.0)) {
      return 1.0;
    }
    return t > 0.0 ? exp(-delta / t) : 0.0;
  case DS_ANNEAL_BARKER:
    if (!(t > 0.0)) {
      return delta < 0.0 ? 1.0 : 0.0;
    }
    if (delta / t > 700.0) {
      return 0.0;
    }
    return 1.0 / (1.0 + exp(delta / t));
  case DS_ANNEAL_THRESHOLD:
    return delta < t ? 1.0 : 0.0;
  case DS_ANNEAL_GREAT_DELUGE:
    return e_new < t || delta < 0.0 ? 1.0 : 0.0;
  case DS_ANNEAL_ACCEPT_CUSTOM:
    return c->accept(delta, e_new, t, user);
  }
  return 0.0;
}

static double an_next_temperature(const ds_anneal_config_t *c, size_t stage,
                                  double t0, double t, size_t done,
                                  void *user) {
  switch (c->schedule) {
  case DS_ANNEAL_GEOMETRIC:
    return t * c->factor;
  case DS_ANNEAL_LINEAR: {
    double frac =
        c->max_iterations == 0 ? 1.0 : (double)done / (double)c->max_iterations;
    return frac >= 1.0 ? 0.0 : t0 * (1.0 - frac);
  }
  case DS_ANNEAL_LOGARITHMIC:
    return t0 / log(exp(1.0) + (double)stage);
  case DS_ANNEAL_LUNDY_MEES:
    return t / (1.0 + c->factor * t);
  case DS_ANNEAL_SCHEDULE_CUSTOM:
    return c->temperature(stage, t0, t, user);
  }
  return t;
}

/* t0 with exp(-mean uphill delta / t0) = chi0 (Johnson et al.); for the
 * threshold rules, the mean |delta| (or the current energy for the
 * deluge) */
static double an_estimate_t0(an_run_t *r, double energy) {
  const ds_anneal_config_t *c = r->c;
  double up = 0.0;
  double absd = 0.0;
  size_t ups = 0;
  size_t k;
  size_t samples = c->estimate_samples == 0 ? 1 : c->estimate_samples;
  if (c->acceptance == DS_ANNEAL_GREAT_DELUGE) {
    return energy;
  }
  for (k = 0; k < samples; k++) {
    double d = an_propose(r, energy);
    if (d > 0.0) {
      up += d;
      ups++;
    }
    absd += fabs(d);
  }
  if (c->acceptance == DS_ANNEAL_THRESHOLD) {
    return absd / (double)samples;
  }
  if (ups == 0) {
    return 1.0;
  }
  up /= (double)ups;
  if (!(c->initial_acceptance > 0.0) || !(c->initial_acceptance < 1.0)) {
    return up;
  }
  return -up / log(c->initial_acceptance);
}

ds_status_t ds_anneal_run(const ds_anneal_problem_t *problem,
                          const ds_anneal_config_t *config, ds_rng_t *rng,
                          void *state, void *best, ds_anneal_result_t *result,
                          ds_context_t *context) {
  an_run_t r;
  ds_anneal_result_t res;
  double energy;
  double t;
  size_t stage = 0;
  size_t stall = 0;
  size_t in_stage = 0;
  size_t steps;
  bool improved_stage = false;
  if (problem == NULL || config == NULL || rng == NULL || state == NULL ||
      result == NULL || problem->energy == NULL) {
    return DS_ERR_NULL;
  }
  if (problem->state_size == 0 ||
      (problem->propose == NULL && problem->neighbor == NULL) ||
      (problem->clone == NULL) != (problem->copy == NULL) ||
      (problem->clone == NULL) != (problem->release == NULL) ||
      (problem->propose != NULL &&
       (problem->apply == NULL || problem->move_size == 0)) ||
      (config->schedule == DS_ANNEAL_SCHEDULE_CUSTOM &&
       config->temperature == NULL) ||
      (config->acceptance == DS_ANNEAL_ACCEPT_CUSTOM &&
       config->accept == NULL)) {
    return DS_ERR_INVALID;
  }
  r.p = problem;
  r.c = config;
  r.rng = rng;
  r.state = state;
  r.scratch = (unsigned char *)ds_context_alloc(
      context,
      problem->propose != NULL ? problem->move_size : problem->state_size);
  if (r.scratch == NULL) {
    return DS_ERR_ALLOC;
  }
  if (problem->propose == NULL && problem->clone != NULL) {
    /* the candidate buffer is a state of its own */
    ds_status_t st = problem->clone(r.scratch, state, problem->user);
    if (st != DS_OK) {
      ds_context_free(context, r.scratch);
      return st;
    }
  }
  energy = problem->energy(state, problem->user);
  memset(&res, 0, sizeof(res));
  res.best_energy = energy;
  res.stop = DS_ANNEAL_STOP_ITERATIONS;
  if (best != NULL) {
    an_keep_best(problem, best, state);
  }
  t = config->t0 > 0.0 ? config->t0 : an_estimate_t0(&r, energy);
  res.t0 = t;
  steps =
      config->steps_per_temperature == 0 ? 1 : config->steps_per_temperature;
  while (res.iterations < config->max_iterations) {
    double delta;
    double prob;
    if (!(res.best_energy > config->target_energy)) {
      res.stop = DS_ANNEAL_STOP_TARGET;
      break;
    }
    if (config->acceptance != DS_ANNEAL_GREAT_DELUGE && t < config->t_min) {
      res.stop = DS_ANNEAL_STOP_COLD;
      break;
    }
    if (problem->stop != NULL &&
        problem->stop(res.iterations, t, energy, res.best_energy,
                      problem->user)) {
      res.stop = DS_ANNEAL_STOP_CALLBACK;
      break;
    }
    delta = an_propose(&r, energy);
    res.iterations++;
    prob =
        an_accept_probability(config, delta, energy + delta, t, problem->user);
    if (prob >= 1.0 || (prob > 0.0 && ds_rng_double(rng) < prob)) {
      an_take(&r);
      energy += delta;
      res.accepted++;
      if (problem->propose != NULL && config->resync_interval > 0 &&
          res.accepted % config->resync_interval == 0) {
        energy = an_resync(problem, state, energy, &res);
      }
      if (energy < res.best_energy) {
        res.best_energy = energy;
        res.improvements++;
        improved_stage = true;
        if (best != NULL) {
          an_keep_best(problem, best, state);
        }
      }
    }
    if (++in_stage >= steps) {
      in_stage = 0;
      stage++;
      stall = improved_stage ? 0 : stall + 1;
      improved_stage = false;
      if (config->reheat_after > 0 && stall >= config->reheat_after) {
        t *= config->reheat_factor;
        stall = 0;
        res.reheats++;
      } else {
        t = an_next_temperature(config, stage, res.t0, t, res.iterations,
                                problem->user);
      }
    }
  }
  if (res.stop == DS_ANNEAL_STOP_ITERATIONS &&
      !(res.best_energy > config->target_energy)) {
    res.stop = DS_ANNEAL_STOP_TARGET; /* reached on the last proposal */
  }
  /* the final energies exactly; move mode accumulated deltas */
  if (problem->propose != NULL) {
    energy = an_resync(problem, state, energy, &res);
    if (best != NULL) {
      res.best_energy = an_resync(problem, best, res.best_energy, &res);
    }
  } else {
    energy = problem->energy(state, problem->user);
    if (problem->release != NULL) {
      problem->release(r.scratch, problem->user);
    }
  }
  res.energy = energy;
  res.temperature = t;
  *result = res;
  ds_context_free(context, r.scratch);
  return DS_OK;
}
