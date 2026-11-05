#ifndef DS_SUFFIX_AUTOMATON_H
#define DS_SUFFIX_AUTOMATON_H

/*
 * Suffix automaton (directed acyclic word graph, DAWG) of a byte string:
 * the minimal DFA accepting its suffixes; every substring is a path from
 * the initial state. Built online (Blumer et al.) in O(n) states (at most
 * 2n - 1) and transitions (at most 3n - 4), with transitions kept in
 * per-state lists plus one hash table for O(1) lookups.
 *
 * Queries: substring test, number of occurrences (end-position set sizes,
 * prepared on demand), first occurrence, number of distinct substrings,
 * longest common substring with another string, and suffix test.
 *
 * ds_cdawg_t is the compact DAWG: the automaton with every non-branching,
 * non-final state contracted, so edges are labelled with substrings of
 * the text (start, length). It references the text, which must outlive it.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_suffix_automaton {
  size_t *len;      /* per state: longest string of the state */
  size_t *link;     /* suffix link */
  size_t *firstpos; /* end index of the first occurrence */
  size_t *edge_head;
  unsigned char *cloned;
  size_t states;
  size_t state_capacity;
  size_t *edge_to;
  size_t *edge_next;
  unsigned char *edge_symbol;
  size_t edges;
  size_t edge_capacity;
  size_t *hash; /* (state, symbol) -> edge */
  size_t hash_size;
  size_t last;
  size_t length;       /* characters added */
  size_t *occurrences; /* prepared on demand */
  ds_context_t *context;
} ds_suffix_automaton_t;

ds_status_t ds_suffix_automaton_init(ds_suffix_automaton_t *sa,
                                     ds_context_t *context);
void ds_suffix_automaton_destroy(ds_suffix_automaton_t *sa);
/* Append one character (online). */
ds_status_t ds_suffix_automaton_extend(ds_suffix_automaton_t *sa,
                                       unsigned char c);
ds_status_t ds_suffix_automaton_build(ds_suffix_automaton_t *sa,
                                      const unsigned char *s, size_t n,
                                      ds_context_t *context);
/* Target of the transition, or ~0. */
size_t ds_suffix_automaton_next(const ds_suffix_automaton_t *sa, size_t state,
                                unsigned char c);
bool ds_suffix_automaton_contains(const ds_suffix_automaton_t *sa,
                                  const unsigned char *p, size_t m);
bool ds_suffix_automaton_is_suffix(const ds_suffix_automaton_t *sa,
                                   const unsigned char *p, size_t m);
/* Occurrences of p (0 if absent). */
ds_status_t ds_suffix_automaton_count(ds_suffix_automaton_t *sa,
                                      const unsigned char *p, size_t m,
                                      size_t *out_count);
/* Start of the first occurrence: DS_NOT_FOUND if absent. */
ds_status_t ds_suffix_automaton_first(const ds_suffix_automaton_t *sa,
                                      const unsigned char *p, size_t m,
                                      size_t *out_position);
/* Distinct non-empty substrings (may wrap for huge inputs). */
size_t ds_suffix_automaton_distinct(const ds_suffix_automaton_t *sa);
/* Longest common substring of the indexed text and t: its start in t and
 * length. */
void ds_suffix_automaton_lcs(const ds_suffix_automaton_t *sa,
                             const unsigned char *t, size_t m,
                             size_t *out_t_pos, size_t *out_length);

typedef struct ds_cdawg {
  const unsigned char *text;
  size_t length;
  size_t nodes;
  size_t root;
  size_t *edge_head;    /* per node */
  unsigned char *final; /* per node */
  size_t *edge_to;
  size_t *edge_start;
  size_t *edge_len;
  size_t *edge_next;
  size_t edges;
  ds_context_t *context;
} ds_cdawg_t;

/* From an automaton built over text (n characters). */
ds_status_t ds_cdawg_build(ds_cdawg_t *cd, const ds_suffix_automaton_t *sa,
                           const unsigned char *text, size_t n,
                           ds_context_t *context);
void ds_cdawg_destroy(ds_cdawg_t *cd);
bool ds_cdawg_contains(const ds_cdawg_t *cd, const unsigned char *p, size_t m);

#ifdef __cplusplus
}
#endif

#endif /* DS_SUFFIX_AUTOMATON_H */
