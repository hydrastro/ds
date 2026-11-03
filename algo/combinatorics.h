#ifndef DS_ALGO_COMBINATORICS_H
#define DS_ALGO_COMBINATORICS_H

/*
 * Combinatorial generation, ranking and counting.
 *
 * Counting functions return DS_ERR_OVERFLOW instead of wrapping when the
 * result does not fit in an unsigned long.
 *
 *   combinations   next k-subset of {0..n-1} in lexicographic order; rank
 *                  and unrank (combinatorial number system); binomial
 *   permutations   rank and unrank in lexicographic order (Lehmer code);
 *                  Steinhaus-Johnson-Trotter generation by adjacent
 *                  transpositions (each permutation differs from the
 *                  previous by one swap of neighbours)
 *   derangements   count D(n) and a uniformly random derangement
 *   partitions     integer partitions p(n) (Euler's pentagonal recurrence)
 *                  and their generation; set partitions as restricted
 *                  growth strings and Bell numbers; Catalan numbers
 *   puzzles        n queens (bitmask backtracking, all solutions counted or
 *                  visited), knight's tour (Warnsdorff's rule with
 *                  backtracking), towers of Hanoi move generation, Collatz
 *                  trajectories
 */

#include "../lib/status.h"
#include "elem.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

ds_status_t ds_binomial(unsigned long n, unsigned long k, unsigned long *out);
/* c holds k increasing indices in [0, n); advance to the next k-subset.
 * Returns false (and leaves c unchanged) after the last one. */
bool ds_next_combination(size_t *c, size_t k, size_t n);
ds_status_t ds_combination_rank(const size_t *c, size_t k, size_t n,
                                unsigned long *out_rank);
ds_status_t ds_combination_unrank(unsigned long rank, size_t k, size_t n,
                                  size_t *out_c);

ds_status_t ds_factorial(unsigned long n, unsigned long *out);
/* Lexicographic rank of a permutation of 0..n-1 (n <= 20 on 64-bit). */
ds_status_t ds_permutation_rank(const size_t *perm, size_t n,
                                unsigned long *out_rank);
ds_status_t ds_permutation_unrank(unsigned long rank, size_t n,
                                  size_t *out_perm);

/* Steinhaus-Johnson-Trotter state; arrays of n entries supplied by the
 * caller (no allocation). */
typedef struct ds_sjt {
  size_t *perm;
  size_t *pos;
  signed char *dir;
  size_t n;
} ds_sjt_t;

void ds_sjt_init(ds_sjt_t *s, size_t n, size_t *perm, size_t *pos,
                 signed char *dir);
/* Advance by one adjacent swap; the swapped positions go to *out_i and
 * *out_i + 1. Returns false after the last permutation. */
bool ds_sjt_next(ds_sjt_t *s, size_t *out_i);

ds_status_t ds_derangement_count(unsigned long n, unsigned long *out);
/* Uniform random derangement of 0..n-1 (n != 1) by rejection, expected
 * e tries. */
ds_status_t ds_random_derangement(size_t *out, size_t n,
                                  ds_random_func_t random, void *state);

ds_status_t ds_catalan(unsigned long n, unsigned long *out);
ds_status_t ds_partition_count(unsigned long n, unsigned long *out);
ds_status_t ds_bell(unsigned long n, unsigned long *out);
/* parts holds a partition of n in non-increasing order (*count parts);
 * start with parts = {n}, count 1. Advances in reverse lexicographic
 * order; false after {1, 1, ..., 1}. parts needs n entries. */
bool ds_next_partition(size_t *parts, size_t *count);
/* Restricted growth string a[0..n-1] (a[0] = 0, a[i] <= 1 + max(a[..i])),
 * one per set partition; start from all zeros. */
bool ds_next_set_partition(size_t *a, size_t n);

/* Visit callback for n-queens solutions: cols[r] is the column of the
 * queen on row r. Return DS_OK to continue. */
typedef ds_status_t (*ds_queens_visit_func_t)(const size_t *cols, size_t n,
                                              void *user);
/* n <= bits of unsigned long. cols: n entries of scratch/output. */
ds_status_t ds_n_queens(size_t n, size_t *cols, ds_queens_visit_func_t visit,
                        void *user, unsigned long *out_count);

/* Knight's tour on a rows x cols board from (r, c): out_order[r * cols + c]
 * gets the move number (0-based). closed asks for a tour ending a knight's
 * move from the start. scratch: 10 * rows * cols entries. DS_NOT_FOUND if
 * none exists (boards without tours are rejected up front; otherwise
 * Warnsdorff-ordered backtracking, fast in practice but exponential in the
 * worst case). */
ds_status_t ds_knights_tour(size_t rows, size_t cols, size_t start_r,
                            size_t start_c, bool closed, size_t *out_order,
                            size_t *scratch);

typedef void (*ds_hanoi_move_func_t)(unsigned int disk, unsigned int from,
                                     unsigned int to, void *user);
/* Move `disks` disks from peg 0 to peg 2 (2^disks - 1 moves). */
void ds_hanoi(unsigned int disks, ds_hanoi_move_func_t move, void *user);

/* Steps of the Collatz trajectory of n to 1 (DS_ERR_OVERFLOW if a value
 * leaves unsigned long); the values to out (capacity entries, may be
 * NULL). */
ds_status_t ds_collatz(unsigned long n, unsigned long *out_steps,
                       unsigned long *out, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_COMBINATORICS_H */
