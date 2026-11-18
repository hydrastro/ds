#include "executor.h"

static void ex_sequential_run(void *self, size_t tasks, ds_task_fn task,
                              void *arg) {
  size_t i;
  (void)self;
  for (i = 0; i < tasks; i++) {
    task(i, arg);
  }
}

static const ds_executor_t ex_sequential = {ex_sequential_run, NULL, 1};

const ds_executor_t *ds_executor_sequential(void) { return &ex_sequential; }

void ds_executor_chunk(size_t n, size_t tasks, size_t index, size_t *lo,
                       size_t *hi) {
  size_t q;
  size_t r;
  if (tasks == 0) {
    tasks = 1;
  }
  q = n / tasks;
  r = n % tasks;
  *lo = q * index + (index < r ? index : r);
  *hi = *lo + q + (index < r ? 1 : 0);
}

size_t ds_executor_tasks(const ds_executor_t *ex, size_t n) {
  size_t w = ex != NULL && ex->workers > 0 ? ex->workers : 1;
  size_t t = w > 1 ? (w > (size_t)-1 / 4 ? (size_t)-1 : 4 * w) : 1;
  if (t > n) {
    t = n;
  }
  return t > 0 ? t : 1;
}

void ds_executor_run(const ds_executor_t *ex, size_t tasks, ds_task_fn task,
                     void *arg) {
  if (ex == NULL || ex->run == NULL) {
    ex_sequential_run(NULL, tasks, task, arg);
  } else {
    ex->run(ex->self, tasks, task, arg);
  }
}
