/*
 * Games decided two ways. A take-away game (take one or two tokens; whoever
 * takes the last one wins) is played on the tape of an alternating Turing
 * machine: its existential states are the first player's moves, its
 * universal states the second player's, and the machine accepts exactly
 * when the first player can force a win. Then a subtraction game (take
 * 1, 3 or 4) is solved directly as a game graph by retrograde analysis,
 * with a winning move for every winning position.
 */
#include "ds.h"
#include "ds_algo.h"
#include <stdio.h>
#include <string.h>

#define TOKENS 12
#define POSITIONS 22

int main(void) {
  enum { E, E2, U, U2, WIN, LOSE }; /* the machine's states */
  ds_tm_t tm;
  size_t n;
  unsigned char tape[TOKENS];
  unsigned char kind[2 * POSITIONS];
  size_t offset[2 * POSITIONS + 1];
  size_t succ[6 * POSITIONS];
  size_t rank[2 * POSITIONS];
  size_t choice[2 * POSITIONS];
  size_t edges = 0;
  size_t p;
  static const size_t take[3] = {1, 3, 4};
  if (ds_tm_init(&tm, 6, 2, 1, 0, E, NULL) != DS_OK) {
    return 1;
  }
  (void)ds_tm_set_kind(&tm, U, DS_TM_UNIVERSAL);
  (void)ds_tm_set_kind(&tm, U2, DS_TM_UNIVERSAL);
  (void)ds_tm_set_kind(&tm, WIN, DS_TM_ACCEPTING);
  (void)ds_tm_set_kind(&tm, LOSE, DS_TM_REJECTING);
  /* the first player erases one token or two, then it is the other's turn */
  (void)ds_tm_add1(&tm, E, 1, U, 0, DS_TM_RIGHT);
  (void)ds_tm_add1(&tm, E, 1, E2, 0, DS_TM_RIGHT);
  (void)ds_tm_add1(&tm, E2, 1, U, 0, DS_TM_RIGHT);
  (void)ds_tm_add1(&tm, E, 0, LOSE, 0, DS_TM_STAY); /* nothing left: lost */
  (void)ds_tm_add1(&tm, U, 1, E, 0, DS_TM_RIGHT);
  (void)ds_tm_add1(&tm, U, 1, U2, 0, DS_TM_RIGHT);
  (void)ds_tm_add1(&tm, U2, 1, E, 0, DS_TM_RIGHT);
  (void)ds_tm_add1(&tm, U, 0, WIN, 0, DS_TM_STAY);
  memset(tape, 1, sizeof(tape));
  printf("take 1 or 2, last token wins: first player");
  for (n = 1; n <= TOKENS; n++) {
    ds_tm_outcome_t out;
    ds_atm_stats_t st;
    if (ds_atm_accepts(&tm, tape, n, NULL, &out, &st) != DS_OK) {
      return 1;
    }
    printf(" %lu:%s", (unsigned long)n, out == DS_TM_ACCEPT ? "wins" : "loses");
    if (n == TOKENS) {
      printf("\n  (%lu tokens: %lu configurations, %lu transitions)\n",
             (unsigned long)n, (unsigned long)st.configs,
             (unsigned long)st.transitions);
    }
  }
  ds_tm_destroy(&tm);

  /* take 1, 3 or 4; position p with player 0 to move is node p, with
   * player 1 to move node POSITIONS + p; player 0 wins by taking the last */
  for (p = 0; p < 2 * POSITIONS; p++) {
    size_t left = p % POSITIONS;
    size_t t;
    offset[p] = edges;
    kind[p] = p < POSITIONS ? DS_GAME_OR : DS_GAME_AND;
    if (left == 0) { /* the player to move has nothing: the other took last */
      kind[p] = p < POSITIONS ? DS_GAME_FALSE : DS_GAME_TRUE;
      continue;
    }
    for (t = 0; t < 3; t++) {
      if (take[t] <= left) {
        succ[edges++] = (p < POSITIONS ? POSITIONS : 0) + left - take[t];
      }
    }
  }
  offset[2 * POSITIONS] = edges;
  if (ds_game_attractor(2 * POSITIONS, kind, offset, succ, rank, choice,
                        NULL) != DS_OK) {
    return 1;
  }
  printf("take 1, 3 or 4: the player to move loses from");
  for (p = 0; p < POSITIONS; p++) {
    if (rank[p] == DS_GAME_LOSE) {
      printf(" %lu", (unsigned long)p);
    }
  }
  printf(" (0 and 2 modulo 7)\n  winning moves:");
  for (p = 1; p < POSITIONS; p++) {
    if (rank[p] != DS_GAME_LOSE) {
      printf(" %lu-%lu", (unsigned long)p,
             (unsigned long)(p - (choice[p] - POSITIONS)));
    }
  }
  printf("\n");
  return 0;
}
