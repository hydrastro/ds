/*
 * The PRAM techniques of dsa/algorithms/parallel_sim.h on a custom executor.
 * C89 has no threads, so this executor stands in for any scheduler: it runs
 * each parallel step's tasks in a different scrambled order and counts what it
 * was given. Because tasks of one step never write the same memory, the
 * results and round counts match the sequential runs exactly; on a thread
 * pool, `run` would hand the tasks to the workers and wait for them.
 */
#include <dsa.h>
#include <dsaa.h>
#include <stdio.h>
#include <stdlib.h>

#define N 50000

typedef struct scrambler {
  unsigned long seed;
  unsigned long steps;
  unsigned long tasks;
} scrambler_t;

static unsigned long step(unsigned long *seed) {
  *seed = (*seed * 1103515245ul + 12345ul) & 0xFFFFFFFFul;
  return *seed >> 8;
}

/* runs the tasks in a random order: a scheduler that promises nothing */
static void scrambled_run(void *self, size_t tasks, ds_task_fn task,
                          void *arg) {
  scrambler_t *s = (scrambler_t *)self;
  size_t order[256], i;
  for (i = 0; i < tasks && i < 256; i++) {
    order[i] = i;
  }
  for (i = tasks < 256 ? tasks : 256; i > 1; i--) {
    size_t j = (size_t)(step(&s->seed) % i), t = order[i - 1];
    order[i - 1] = order[j];
    order[j] = t;
  }
  for (i = 0; i < tasks; i++) {
    task(i < 256 ? order[i] : i, arg);
  }
  s->steps++;
  s->tasks += tasks;
}

static size_t next_of[N], rank_a[N], rank_b[N], perm[N];

int main(void) {
  scrambler_t s;
  ds_executor_t ex;
  size_t i, rounds_a, rounds_b, mismatches = 0;
  unsigned long seed = 99;
  s.seed = 1;
  s.steps = s.tasks = 0;
  ex.run = scrambled_run;
  ex.self = &s;
  ex.workers = 8; /* steps are cut into about 32 tasks */
  /* one list threaded through the array in a random order */
  for (i = 0; i < N; i++) {
    perm[i] = i;
  }
  for (i = N; i > 1; i--) {
    size_t j = (size_t)(step(&seed) % i), t = perm[i - 1];
    perm[i - 1] = perm[j];
    perm[j] = t;
  }
  for (i = 0; i < N; i++) {
    next_of[perm[i]] = i + 1 < N ? perm[i + 1] : DS_PRAM_NONE;
  }
  if (ds_pram_list_rank_on(&ex, next_of, N, rank_a, &rounds_a, NULL) !=
          DS_OK ||
      ds_pram_list_rank(next_of, N, rank_b, &rounds_b, NULL) != DS_OK) {
    return 1;
  }
  for (i = 0; i < N; i++) {
    if (rank_a[i] != rank_b[i] || rank_a[perm[i]] != N - 1 - i) {
      mismatches++;
    }
  }
  printf("pointer jumping: %lu rounds (sequential: %lu), %lu mismatches\n",
         (unsigned long)rounds_a, (unsigned long)rounds_b,
         (unsigned long)mismatches);
  if (ds_pram_list_contract_rank_on(&ex, next_of, N, rank_a, 7, &rounds_a,
                                    NULL) != DS_OK) {
    return 1;
  }
  for (i = 0; i < N; i++) {
    if (rank_a[i] != rank_b[i]) {
      mismatches++;
    }
  }
  printf("list contraction: %lu rounds, %lu mismatches\n",
         (unsigned long)rounds_a, (unsigned long)mismatches);
  printf("the executor ran %lu parallel steps of %lu tasks in all\n", s.steps,
         s.tasks);
  return mismatches == 0 ? 0 : 1;
}
