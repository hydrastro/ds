#ifndef DS_BDD_H
#define DS_BDD_H

/*
 * Reduced ordered binary decision diagrams (ROBDDs): a manager holds a
 * shared, hash-consed node store, so every boolean function over the
 * manager's variables has exactly one node (equality is node equality).
 *
 * The variable order is pluggable: order[level] names the variable tested
 * at each level (NULL: variable i at level i). Operations are iterative
 * (explicit stacks, no recursion on the number of variables or nodes):
 * if-then-else with a computed cache, apply for any of the 16 binary
 * operators (truth table), negation, restriction (cofactor), existential
 * and universal quantification, evaluation, satisfying-assignment count
 * and a satisfying assignment, and node counts.
 *
 * Nodes are never freed individually; ds_bdd_destroy releases everything.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_BDD_FALSE ((size_t)0)
#define DS_BDD_TRUE ((size_t)1)

/* Truth tables for ds_bdd_apply: bit (f * 2 + g). */
#define DS_BDD_AND 0x8u
#define DS_BDD_OR 0xEu
#define DS_BDD_XOR 0x6u
#define DS_BDD_NAND 0x7u
#define DS_BDD_NOR 0x1u
#define DS_BDD_IMPLIES 0xBu /* f -> g */
#define DS_BDD_EQUIV 0x9u

typedef struct ds_bdd {
  size_t vars;
  size_t *level_of;     /* per variable */
  size_t *var_at_level; /* per level */
  size_t *var;          /* per node (terminals: vars) */
  size_t *low;
  size_t *high;
  size_t nodes;
  size_t capacity;
  size_t *unique; /* hash of nodes */
  size_t unique_size;
  size_t *cache; /* 4 words per entry: f, g, h, result */
  size_t cache_size;
  size_t *stack;
  size_t stack_capacity;
  ds_context_t *context;
} ds_bdd_t;

/* order: `vars` distinct variables, or NULL. */
ds_status_t ds_bdd_init(ds_bdd_t *m, size_t vars, const size_t *order,
                        ds_context_t *context);
void ds_bdd_destroy(ds_bdd_t *m);
/* The function "variable v" (or its negation). */
ds_status_t ds_bdd_var(ds_bdd_t *m, size_t v, size_t *out);
ds_status_t ds_bdd_nvar(ds_bdd_t *m, size_t v, size_t *out);
ds_status_t ds_bdd_ite(ds_bdd_t *m, size_t f, size_t g, size_t h, size_t *out);
ds_status_t ds_bdd_not(ds_bdd_t *m, size_t f, size_t *out);
ds_status_t ds_bdd_apply(ds_bdd_t *m, unsigned table, size_t f, size_t g,
                         size_t *out);
ds_status_t ds_bdd_and(ds_bdd_t *m, size_t f, size_t g, size_t *out);
ds_status_t ds_bdd_or(ds_bdd_t *m, size_t f, size_t g, size_t *out);
ds_status_t ds_bdd_xor(ds_bdd_t *m, size_t f, size_t g, size_t *out);
/* f with variable v fixed to value. */
ds_status_t ds_bdd_restrict(ds_bdd_t *m, size_t f, size_t v, bool value,
                            size_t *out);
ds_status_t ds_bdd_exists(ds_bdd_t *m, size_t f, size_t v, size_t *out);
ds_status_t ds_bdd_forall(ds_bdd_t *m, size_t f, size_t v, size_t *out);
/* assignment[v] in {0, 1} for every variable. */
bool ds_bdd_eval(const ds_bdd_t *m, size_t f, const unsigned char *assignment);
/* Number of satisfying assignments of all `vars` variables. */
double ds_bdd_sat_count(ds_bdd_t *m, size_t f);
/* One satisfying assignment (2 = either value): DS_NOT_FOUND for
 * false. */
ds_status_t ds_bdd_any_sat(const ds_bdd_t *m, size_t f,
                           unsigned char *assignment);
/* Nodes reachable from f, terminals included. */
size_t ds_bdd_node_count(ds_bdd_t *m, size_t f);

#ifdef __cplusplus
}
#endif

#endif /* DS_BDD_H */
