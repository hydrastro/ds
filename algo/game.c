#include "game.h"
#include <limits.h>
#include <string.h>

typedef struct gm {
  const ds_game_t *g;
  void *state;
  unsigned char *buf; /* depth levels of max_moves moves */
  size_t level_bytes;
  size_t nodes;
  ds_game_algorithm_t alg;
} gm_t;

static long gm_eval(gm_t *s, bool max_view) {
  long v = s->g->evaluate(s->state, s->g->user);
  if (max_view) {
    return v;
  }
  /* negamax: score for the side to move */
  if (s->g->maximizing(s->state, s->g->user)) {
    return v;
  }
  return v == LONG_MIN ? LONG_MAX : -v;
}

static long gm_neg(long v) { return v == LONG_MIN ? LONG_MAX : -v; }

/* Generates the moves of this level; 0 when terminal or none. */
static size_t gm_moves(gm_t *s, size_t level, size_t depth) {
  unsigned char *m = s->buf + level * s->level_bytes;
  size_t n;
  if (depth == 0 ||
      (s->g->terminal != NULL && s->g->terminal(s->state, s->g->user))) {
    return 0;
  }
  n = s->g->moves(s->state, m, s->g->max_moves, s->g->user);
  if (n > s->g->max_moves) {
    n = s->g->max_moves;
  }
  if (n > 1 && s->g->order != NULL && s->alg != DS_GAME_MINIMAX) {
    s->g->order(s->state, m, n, s->g->user);
  }
  return n;
}

static long gm_minimax(gm_t *s, size_t level, size_t depth, long alpha,
                       long beta) {
  unsigned char *m = s->buf + level * s->level_bytes;
  size_t n;
  size_t i;
  long best;
  long v;
  bool maxp;
  s->nodes++;
  n = gm_moves(s, level, depth);
  if (n == 0) {
    return gm_eval(s, true);
  }
  maxp = s->g->maximizing(s->state, s->g->user);
  best = maxp ? LONG_MIN : LONG_MAX;
  for (i = 0; i < n; i++) {
    s->g->apply(s->state, m + i * s->g->move_size, s->g->user);
    v = gm_minimax(s, level + 1, depth - 1, alpha, beta);
    s->g->undo(s->state, m + i * s->g->move_size, s->g->user);
    if (maxp) {
      if (v > best) {
        best = v;
      }
      if (best > alpha) {
        alpha = best;
      }
    } else {
      if (v < best) {
        best = v;
      }
      if (best < beta) {
        beta = best;
      }
    }
    if (s->alg == DS_GAME_ALPHA_BETA && alpha >= beta) {
      break;
    }
  }
  return best;
}

static long gm_negamax(gm_t *s, size_t level, size_t depth, long alpha,
                       long beta);

/* Negamax value of the current (child) position for the player `me` who
 * moved into it: negated only when the turn passed to the opponent, so
 * games where a player may move twice in a row work too. */
static long gm_child(gm_t *s, size_t level, size_t depth, long alpha, long beta,
                     bool me) {
  if (s->g->maximizing(s->state, s->g->user) == me) {
    return gm_negamax(s, level, depth, alpha, beta);
  }
  return gm_neg(gm_negamax(s, level, depth, gm_neg(beta), gm_neg(alpha)));
}

static long gm_negamax(gm_t *s, size_t level, size_t depth, long alpha,
                       long beta) {
  unsigned char *m = s->buf + level * s->level_bytes;
  size_t n;
  size_t i;
  long best = LONG_MIN;
  long v;
  bool me;
  s->nodes++;
  n = gm_moves(s, level, depth);
  if (n == 0) {
    return gm_eval(s, false);
  }
  me = s->g->maximizing(s->state, s->g->user);
  for (i = 0; i < n; i++) {
    s->g->apply(s->state, m + i * s->g->move_size, s->g->user);
    v = gm_child(s, level + 1, depth - 1, alpha, beta, me);
    s->g->undo(s->state, m + i * s->g->move_size, s->g->user);
    if (v > best) {
      best = v;
    }
    if (best > alpha) {
      alpha = best;
    }
    if (alpha >= beta) {
      break;
    }
  }
  return best;
}

ds_status_t ds_game_search(const ds_game_t *game, void *state, size_t depth,
                           ds_game_algorithm_t algorithm, void *out_best_move,
                           long *out_value, size_t *out_nodes,
                           ds_context_t *context) {
  gm_t s;
  unsigned char *root;
  size_t n;
  size_t i;
  size_t best_i = 0;
  long best;
  long v;
  long alpha = LONG_MIN;
  long beta = LONG_MAX;
  bool maxp;
  if (game == NULL || state == NULL || out_value == NULL ||
      game->moves == NULL || game->apply == NULL || game->undo == NULL ||
      game->evaluate == NULL || game->maximizing == NULL) {
    return DS_ERR_NULL;
  }
  if (game->move_size == 0 || game->max_moves == 0 || (int)algorithm < 0 ||
      algorithm > DS_GAME_NEGAMAX) {
    return DS_ERR_INVALID;
  }
  if (game->max_moves > ((size_t)-1) / game->move_size ||
      depth + 1 > ((size_t)-1) / (game->max_moves * game->move_size)) {
    return DS_ERR_OVERFLOW;
  }
  s.g = game;
  s.state = state;
  s.level_bytes = game->max_moves * game->move_size;
  s.nodes = 1;
  s.alg = algorithm;
  s.buf =
      (unsigned char *)ds_context_alloc(context, (depth + 1) * s.level_bytes);
  if (s.buf == NULL) {
    return DS_ERR_ALLOC;
  }
  root = s.buf;
  n = gm_moves(&s, 0, depth);
  if (n == 0) {
    *out_value = game->evaluate(state, game->user);
    if (out_nodes != NULL) {
      *out_nodes = 1;
    }
    ds_context_free(context, s.buf);
    return DS_NOT_FOUND;
  }
  maxp = game->maximizing(state, game->user);
  best = maxp ? LONG_MIN : LONG_MAX;
  for (i = 0; i < n; i++) {
    game->apply(state, root + i * game->move_size, game->user);
    if (algorithm == DS_GAME_NEGAMAX) {
      /* score for the root player, turned into the max player's view */
      v = gm_child(&s, 1, depth - 1, maxp ? alpha : gm_neg(beta),
                   maxp ? beta : gm_neg(alpha), maxp);
      if (!maxp) {
        v = gm_neg(v);
      }
    } else {
      v = gm_minimax(&s, 1, depth - 1, alpha, beta);
    }
    game->undo(state, root + i * game->move_size, game->user);
    if (i == 0 || (maxp ? v > best : v < best)) {
      best = v;
      best_i = i;
    }
    if (algorithm != DS_GAME_MINIMAX) {
      if (maxp && best > alpha) {
        alpha = best;
      } else if (!maxp && best < beta) {
        beta = best;
      }
    }
  }
  if (out_best_move != NULL) {
    memcpy(out_best_move, root + best_i * game->move_size, game->move_size);
  }
  *out_value = best;
  if (out_nodes != NULL) {
    *out_nodes = s.nodes;
  }
  ds_context_free(context, s.buf);
  return DS_OK;
}
