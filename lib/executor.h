#ifndef DS_EXECUTOR_H
#define DS_EXECUTOR_H

/*
 * Executors: how a parallel step runs. The library stays free of threads
 * (C89 has none); an algorithm that has parallel work describes each step
 * as `tasks` independent tasks and hands them to an executor, which may
 * run them on the caller's threads, a pool, OpenMP, or one after another.
 * Tasks of one step never write the same memory and only read what the
 * step before wrote, so every executor gives the same result.
 *
 * `run` must call task(0, arg) .. task(tasks - 1, arg), each exactly once,
 * in any order and at any concurrency, and return once all have finished
 * (and their writes are visible to the caller: a join or a barrier).
 * `workers` says how many tasks a step should be cut into to keep the
 * executor busy (algorithms make a few per worker); 0 counts as 1.
 *
 * ds_executor_sequential() runs the tasks in order on the calling thread.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ds_task_fn)(size_t index, void *arg);

typedef struct ds_executor {
  void (*run)(void *self, size_t tasks, ds_task_fn task, void *arg);
  void *self;
  size_t workers;
} ds_executor_t;

const ds_executor_t *ds_executor_sequential(void);

/* index range [*lo, *hi) of task `index` when n items are cut into
 * `tasks` near-equal chunks */
void ds_executor_chunk(size_t n, size_t tasks, size_t index, size_t *lo,
                       size_t *hi);
/* how many tasks to cut n items into on ex (NULL: sequential), at least
 * 1, at most n (or 1 when n is 0) */
size_t ds_executor_tasks(const ds_executor_t *ex, size_t n);
/* runs the step on ex (NULL: sequentially) */
void ds_executor_run(const ds_executor_t *ex, size_t tasks, ds_task_fn task,
                     void *arg);

#ifdef __cplusplus
}
#endif

#endif /* DS_EXECUTOR_H */
