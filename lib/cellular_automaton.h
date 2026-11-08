#ifndef DS_CELLULAR_AUTOMATON_H
#define DS_CELLULAR_AUTOMATON_H

/*
 * Cellular automata on finite 1-D and 2-D grids, double buffered.
 *
 * ds_ca1_t  one dimension, k states, radius r. Rules:
 *   table        any rule: new = table[sum_i cell_i k^(2r - i)] over the
 *                neighbourhood read left to right (k^(2r+1) entries);
 *                ds_ca1_set_elementary fills it from a Wolfram code 0..255
 *   totalistic   new = digit s (base k) of a Wolfram totalistic code,
 *                s = neighbourhood sum; or an explicit table by sum
 *   callback     new = rule(neighbourhood, size, user)
 *
 * ds_ca2_t  two dimensions, Moore or von Neumann neighbourhood of any
 *   range. Rules:
 *   life-like    outer totalistic birth/survival sets ("B3/S23"), with
 *                Generations rules ("B2/S/C3": states 2..C-1 are dying
 *                cells that age back to 0 and do not count)
 *   callback     new = rule(ca, x, y, user), reading the current grid
 *
 * Boundaries: periodic (torus), fixed (a constant outside value) or
 * reflecting. Steps return how many cells changed (0: a still life).
 * Memory comes from the ds_context_t given at init.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_ca_boundary {
  DS_CA_PERIODIC,
  DS_CA_FIXED,
  DS_CA_REFLECT
} ds_ca_boundary_t;

typedef enum ds_ca_rule_kind {
  DS_CA_RULE_TABLE,
  DS_CA_RULE_TOTALISTIC,
  DS_CA_RULE_CALLBACK
} ds_ca_rule_kind_t;

typedef enum ds_ca_neighborhood {
  DS_CA_MOORE,
  DS_CA_VON_NEUMANN
} ds_ca_neighborhood_t;

typedef unsigned char (*ds_ca1_rule_fn)(const unsigned char *neighborhood,
                                        size_t size, void *user);

typedef struct ds_ca1 {
  size_t width;
  unsigned states;
  size_t radius;
  ds_ca_boundary_t boundary;
  unsigned char outside;
  unsigned char *cells;
  unsigned char *next;
  ds_ca_rule_kind_t kind;
  unsigned char *table; /* full table or by-sum table */
  size_t table_size;
  ds_ca1_rule_fn rule;
  void *user;
  unsigned char *window; /* 2r + 1 scratch */
  unsigned long generation;
  ds_context_t *context;
} ds_ca1_t;

ds_status_t ds_ca1_init(ds_ca1_t *ca, size_t width, unsigned states,
                        size_t radius, ds_ca_boundary_t boundary,
                        ds_context_t *context);
void ds_ca1_destroy(ds_ca1_t *ca);
void ds_ca1_set_outside(ds_ca1_t *ca, unsigned char value);
/* table of states^(2r+1) entries (DS_ERR_OVERFLOW above 2^24) */
ds_status_t ds_ca1_set_table(ds_ca1_t *ca, const unsigned char *table);
/* k = 2, r = 1 */
ds_status_t ds_ca1_set_elementary(ds_ca1_t *ca, unsigned rule);
/* Wolfram totalistic code: digits base k, least significant = sum 0 */
ds_status_t ds_ca1_set_totalistic_code(ds_ca1_t *ca, unsigned long code);
/* by_sum has (2r+1)(k-1)+1 entries */
ds_status_t ds_ca1_set_totalistic(ds_ca1_t *ca, const unsigned char *by_sum);
ds_status_t ds_ca1_set_callback(ds_ca1_t *ca, ds_ca1_rule_fn rule, void *user);
unsigned char ds_ca1_get(const ds_ca1_t *ca, size_t i);
ds_status_t ds_ca1_set(ds_ca1_t *ca, size_t i, unsigned char state);
void ds_ca1_clear(ds_ca1_t *ca);
/* returns the number of changed cells */
size_t ds_ca1_step(ds_ca1_t *ca);
void ds_ca1_run(ds_ca1_t *ca, unsigned long generations);

struct ds_ca2;
typedef unsigned char (*ds_ca2_rule_fn)(const struct ds_ca2 *ca, size_t x,
                                        size_t y, void *user);

typedef struct ds_ca2 {
  size_t width;
  size_t height;
  unsigned states;
  ds_ca_neighborhood_t neighborhood;
  size_t range;
  ds_ca_boundary_t boundary;
  unsigned char outside;
  unsigned char *cells; /* row-major */
  unsigned char *next;
  ds_ca_rule_kind_t kind; /* TOTALISTIC: life-like; CALLBACK */
  bool *birth;            /* by live-neighbour count */
  bool *survive;
  size_t max_neighbors;
  ds_ca2_rule_fn rule;
  void *user;
  unsigned long generation;
  ds_context_t *context;
} ds_ca2_t;

/* states >= 2; the default rule is Conway's B3/S23 (Generations with C =
 * states when states > 2) */
ds_status_t ds_ca2_init(ds_ca2_t *ca, size_t width, size_t height,
                        unsigned states, ds_ca_neighborhood_t neighborhood,
                        size_t range, ds_ca_boundary_t boundary,
                        ds_context_t *context);
void ds_ca2_destroy(ds_ca2_t *ca);
void ds_ca2_set_outside(ds_ca2_t *ca, unsigned char value);
/* birth / survive hold max_neighbors + 1 flags */
ds_status_t ds_ca2_set_life(ds_ca2_t *ca, const bool *birth,
                            const bool *survive);
/* "B3/S23", "b36/s23", "23/3" (S/B), "B2/S/C3", "/2/3" (S/B/C); a C (or
 * G) part must equal the grid's state count. DS_ERR_INVALID on bad text,
 * DS_ERR_RANGE for counts above max_neighbors. */
ds_status_t ds_ca2_parse_rule(ds_ca2_t *ca, const char *rule);
ds_status_t ds_ca2_set_callback(ds_ca2_t *ca, ds_ca2_rule_fn rule, void *user);
unsigned char ds_ca2_get(const ds_ca2_t *ca, size_t x, size_t y);
/* boundary-aware read at signed coordinates (for callbacks) */
unsigned char ds_ca2_at(const ds_ca2_t *ca, long x, long y);
ds_status_t ds_ca2_set(ds_ca2_t *ca, size_t x, size_t y, unsigned char state);
void ds_ca2_clear(ds_ca2_t *ca);
/* live (state 1) neighbours of (x, y) */
size_t ds_ca2_live_neighbors(const ds_ca2_t *ca, size_t x, size_t y);
/* cells in a nonzero state */
size_t ds_ca2_population(const ds_ca2_t *ca);
size_t ds_ca2_step(ds_ca2_t *ca);
void ds_ca2_run(ds_ca2_t *ca, unsigned long generations);

#ifdef __cplusplus
}
#endif

#endif /* DS_CELLULAR_AUTOMATON_H */
