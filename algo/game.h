#ifndef DS_ALGO_GAME_H
#define DS_ALGO_GAME_H

/*
 * Game-tree search for two-player zero-sum games of perfect information,
 * driven entirely by callbacks, so any game fits:
 *
 *   DS_GAME_MINIMAX     plain minimax to a depth limit
 *   DS_GAME_ALPHA_BETA  minimax with alpha-beta pruning (same value, far
 *                       fewer nodes; better with a move-ordering callback)
 *   DS_GAME_NEGAMAX     the negamax formulation of alpha-beta (turns need
 *                       not alternate: a player may move twice)
 *
 * The game supplies: the moves of the current position (written by value
 * into a buffer), apply and undo, a static evaluation from the maximizing
 * player's point of view, whose turn it is, and optionally a terminal test
 * and a move-ordering hook. The position lives in the caller's state and
 * is restored when the search returns. Scores are long integers (no
 * floating-point ties); evaluations must not be LONG_MIN, so that every
 * score can be negated.
 *
 * Game graphs: ds_game_attractor solves a reachability game on a finite
 * graph given in full (retrograde analysis), in O(n + m): player 0 moves
 * at OR nodes, player 1 at AND nodes, and player 0 wins on reaching a TRUE
 * node. It is the least solution of win(v) = OR / AND of win over the
 * successors (false at an OR node without successors, true at an AND node
 * without them), so it also evaluates AND-OR graphs and decides the
 * configuration graphs of alternating machines (algo/alternating_tm).
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_game {
  size_t move_size;
  size_t max_moves; /* capacity of the move buffer per position */
  /* Moves of the current position into out (at most capacity). */
  size_t (*moves)(void *state, void *out, size_t capacity, void *user);
  void (*apply)(void *state, const void *move, void *user);
  void (*undo)(void *state, const void *move, void *user);
  /* Score for the maximizing player. */
  long (*evaluate)(const void *state, void *user);
  bool (*maximizing)(const void *state, void *user);
  /* NULL: a position without moves is terminal. */
  bool (*terminal)(const void *state, void *user);
  /* NULL: moves as generated. May reorder moves[0..count). */
  void (*order)(const void *state, void *moves, size_t count, void *user);
  void *user;
} ds_game_t;

typedef enum ds_game_algorithm {
  DS_GAME_MINIMAX = 0,
  DS_GAME_ALPHA_BETA,
  DS_GAME_NEGAMAX
} ds_game_algorithm_t;

/* Best move at the root (copied to out_best_move, may be NULL) and its
 * value; DS_NOT_FOUND when the root has no moves (*out_value is then the
 * evaluation). *out_nodes (may be NULL) counts the positions visited. */
ds_status_t ds_game_search(const ds_game_t *game, void *state, size_t depth,
                           ds_game_algorithm_t algorithm, void *out_best_move,
                           long *out_value, size_t *out_nodes,
                           ds_context_t *context);

/* ---- game graphs ---- */

typedef enum ds_game_node_kind {
  DS_GAME_OR = 0, /* player 0 moves: wins if some successor wins */
  DS_GAME_AND,    /* player 1 moves: wins if every successor wins */
  DS_GAME_TRUE,   /* won (successors ignored) */
  DS_GAME_FALSE   /* lost (successors ignored) */
} ds_game_node_kind_t;

#define DS_GAME_LOSE ((size_t) - 1)
#define DS_GAME_NO_CHOICE ((size_t) - 1)

/* n nodes, kind[v] a ds_game_node_kind_t, successors of v in
 * succ[offset[v] .. offset[v + 1]) (repeats allowed: a repeated successor
 * of an AND node counts once per edge, which does not change the result).
 * out_rank[v]: the round in which v is won: 0 for TRUE nodes and for AND
 * nodes without successors, 1 + the least rank of a won successor at an
 * OR node, 1 + the greatest successor rank at an AND node; DS_GAME_LOSE
 * for the nodes player 0 cannot win (cycles never win: the solution is
 * the least one). out_choice (may be NULL): at a won OR node a successor of
 * rank one less, so following the choices wins within out_rank[v] moves
 * whatever player 1 does; DS_GAME_NO_CHOICE elsewhere. DS_ERR_RANGE for a
 * successor >= n or an unknown kind; O(n + m) time and memory. */
ds_status_t ds_game_attractor(size_t n, const unsigned char *kind,
                              const size_t *offset, const size_t *succ,
                              size_t *out_rank, size_t *out_choice,
                              ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_GAME_H */
