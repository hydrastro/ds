/*
 * Binary heap used as a min-priority-queue.
 *
 * insert places an embedded node into the heap; extract_root repeatedly removes
 * the smallest element under the supplied comparison, so draining the heap
 * yields ascending order.
 */
#include "ds.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct job {
  ds_heap_node_t node;
  int cost;
} job_t;

static int compare_jobs(ds_heap_node_t *a, ds_heap_node_t *b) {
  return CAST(a, job_t)->cost - CAST(b, job_t)->cost;
}

static void free_job(ds_heap_node_t *node) { free(node); }

static job_t *make_job(int cost) {
  job_t *j = (job_t *)malloc(sizeof(job_t));
  j->cost = cost;
  return j;
}

int main(void) {
  ds_heap_t *heap = ds_heap_create(16);
  int costs[6];
  int i;

  costs[0] = 5;
  costs[1] = 1;
  costs[2] = 4;
  costs[3] = 2;
  costs[4] = 6;
  costs[5] = 3;

  for (i = 0; i < 6; ++i) {
    ds_heap_insert(heap, &make_job(costs[i])->node, compare_jobs);
  }

  /* peek shows the current minimum without removing it */
  printf("min cost: %d\n", CAST(ds_heap_peek_root(heap), job_t)->cost);

  /* draining the heap yields ascending costs */
  printf("drained order: ");
  while (!ds_heap_is_empty(heap)) {
    ds_heap_node_t *top =
        (ds_heap_node_t *)ds_heap_extract_root(heap, compare_jobs);
    job_t *j = CAST(top, job_t);
    printf("%d ", j->cost);
    free(j);
  }
  printf("\n");

  /* nothing left to own, but pass the callback anyway for good habits */
  ds_heap_destroy(heap, free_job);
  return 0;
}
