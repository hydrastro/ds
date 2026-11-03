#ifndef DS_ALGO_KWAY_MERGE_H
#define DS_ALGO_KWAY_MERGE_H

/*
 * Multiway merging.
 *
 * ds_tournament_t is a tournament tree over k players whose values are
 * stored by value: a loser tree (each internal node keeps the loser of
 * its match, the overall winner sits apart; replaying a changed player
 * costs one comparison per level) or a winner tree (each internal node
 * keeps the winner of its subtree). Players can be retired (an exhausted
 * input). Ties go to the lower player index, which makes every merge
 * below stable.
 *
 * ds_kway_merge() merges k sorted inputs read through a callback into an
 * output callback, with a pluggable selection policy:
 *   DS_MERGE_LOSER_TREE   tournament loser tree, ~log2 k compares/element
 *   DS_MERGE_WINNER_TREE  tournament winner tree, ~2 log2 k compares
 *   DS_MERGE_HEAP         binary heap of input indexes
 *   DS_MERGE_IDEAL        ideal merge: inputs kept sorted by their head,
 *                         the first is consumed and reinserted by linear
 *                         search, Theta(M k) worst case, very fast when
 *                         heads rarely overtake each other
 *   DS_MERGE_LINEAR       scan every head, k - 1 compares per element
 * ds_kway_merge_arrays() is the same over k in-memory arrays (multiway
 * merge, k-way merge).
 *
 * ds_optimal_merge_plan() computes the optimal merge pattern for runs of
 * given lengths merged at most `ways` at a time (Huffman's construction
 * with zero-length dummy runs), minimising the total number of elements
 * moved; ds_optimal_merge_arrays() executes it.
 *
 * Working memory comes from the ds_context_t, or from a caller buffer
 * (config.buffer, at least ds_kway_merge_buffer_size() bytes) so the
 * stream merge can run without allocating.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include "elem.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_TOURNAMENT_NONE (~(size_t)0)

typedef enum ds_tournament_kind {
  DS_TOURNAMENT_LOSER = 0,
  DS_TOURNAMENT_WINNER = 1
} ds_tournament_kind_t;

typedef struct ds_tournament_config {
  size_t players;
  size_t elem_size;
  ds_compare_func_t compare;
  void *user;
  ds_tournament_kind_t kind;
} ds_tournament_config_t;

typedef struct ds_tournament {
  ds_tournament_config_t config;
  unsigned char *values; /* players * elem_size */
  unsigned char *alive;  /* per player */
  size_t *tree;          /* 2 * players entries */
  void *block;           /* owned allocation (NULL with a caller buffer) */
  ds_context_t *context;
} ds_tournament_t;

size_t ds_tournament_buffer_size(size_t players, size_t elem_size);
ds_status_t ds_tournament_init(ds_tournament_t *t,
                               const ds_tournament_config_t *config,
                               ds_context_t *context);
ds_status_t ds_tournament_init_buffer(ds_tournament_t *t,
                                      const ds_tournament_config_t *config,
                                      void *buffer, size_t buffer_size);
void ds_tournament_destroy(ds_tournament_t *t);
/* Storage of player i's value; write it, then build or replay. */
void *ds_tournament_slot(ds_tournament_t *t, size_t player);
void ds_tournament_set_alive(ds_tournament_t *t, size_t player, bool alive);
bool ds_tournament_is_alive(const ds_tournament_t *t, size_t player);
/* Play every match: O(players). */
void ds_tournament_build(ds_tournament_t *t);
/* Winning player (smallest value, ties to the lower index), or
 * DS_TOURNAMENT_NONE when every player is retired. */
size_t ds_tournament_winner(const ds_tournament_t *t);
/* Re-play the matches on player i's path after its value or alive flag
 * changed: O(log players) for a winner tree, and for a loser tree when i
 * is the current winner (the merge pattern); a loser tree rebuilds,
 * O(players), for any other player. */
void ds_tournament_replay(ds_tournament_t *t, size_t player);

typedef enum ds_merge_policy {
  DS_MERGE_LOSER_TREE = 0,
  DS_MERGE_WINNER_TREE = 1,
  DS_MERGE_HEAP = 2,
  DS_MERGE_IDEAL = 3,
  DS_MERGE_LINEAR = 4
} ds_merge_policy_t;

/* Copy the next element of input `source` to out: DS_OK, DS_NOT_FOUND when
 * that input is exhausted, or an error (returned by the merge). */
typedef ds_status_t (*ds_merge_next_func_t)(size_t source, void *out_elem,
                                            void *user);
/* Receive the next merged element (from input `source`); anything but
 * DS_OK stops the merge and is returned. */
typedef ds_status_t (*ds_merge_emit_func_t)(const void *elem, size_t source,
                                            void *user);

typedef struct ds_kway_merge_config {
  size_t ways; /* number of inputs */
  size_t elem_size;
  ds_compare_func_t compare;
  void *user; /* passed to compare, next and emit */
  ds_merge_policy_t policy;
  void *buffer; /* optional working memory (NULL: use the context) */
  size_t buffer_size;
} ds_kway_merge_config_t;

size_t ds_kway_merge_buffer_size(size_t ways, size_t elem_size,
                                 ds_merge_policy_t policy);
/* Number of elements merged in *out_count (may be NULL). */
ds_status_t ds_kway_merge(const ds_kway_merge_config_t *config,
                          ds_merge_next_func_t next, ds_merge_emit_func_t emit,
                          size_t *out_count, ds_context_t *context);
/* Merge arrays[i] (counts[i] elements each) into out (sum of counts). */
ds_status_t ds_kway_merge_arrays(const void *const *arrays,
                                 const size_t *counts, size_t ways,
                                 size_t elem_size, ds_compare_func_t compare,
                                 void *user, ds_merge_policy_t policy,
                                 void *out, ds_context_t *context);

/* Optimal merge pattern for `runs` runs of the given lengths merged at
 * most `ways` (>= 2) at a time. Merge j (0-based) creates run id
 * runs + j; out_parent[id] is the merge (run id) that consumes run id,
 * DS_TOURNAMENT_NONE for the final run. out_parent needs 2 * runs entries
 * (dummy runs are not listed). The number of merges goes to *out_merges
 * and the total number of element moves (the cost) to *out_cost. */
ds_status_t ds_optimal_merge_plan(const size_t *lengths, size_t runs,
                                  size_t ways, size_t *out_parent,
                                  size_t *out_merges, double *out_cost,
                                  ds_context_t *context);
/* Merge the arrays into out following the optimal pattern. */
ds_status_t ds_optimal_merge_arrays(const void *const *arrays,
                                    const size_t *counts, size_t runs,
                                    size_t ways, size_t elem_size,
                                    ds_compare_func_t compare, void *user,
                                    void *out, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_KWAY_MERGE_H */
