/*
 * A strict Fibonacci heap drives Dijkstra's algorithm on a weighted grid,
 * through the generic priority queue interface, and agrees with a binary
 * heap. Every bound of the strict heap is worst case: the measures it
 * reports stay within the paper's invariants after every operation.
 * Melding two heaps of pending jobs closes the example.
 */
#include "ds.h"
#include <stdio.h>
#include <string.h>

#define SIDE 60
#define N (SIDE * SIDE)

typedef struct {
  long dist;
  size_t vertex;
} entry_t;

static int by_dist(const void *a, const void *b, void *user) {
  const entry_t *x = (const entry_t *)a;
  const entry_t *y = (const entry_t *)b;
  (void)user;
  return x->dist < y->dist ? -1 : x->dist > y->dist ? 1 : 0;
}

static long weight(size_t a, size_t b) { /* deterministic, 1..19 */
  return (long)((a * 7919u + b * 104729u) % 19u) + 1;
}

/* shortest distances from vertex 0 with the given queue backend */
static long dijkstra(const ds_pq_ops_t *ops, long *dist, size_t *peak) {
  ds_pq_t pq;
  ds_pq_handle_t handle[N];
  unsigned char done[N];
  size_t v;
  entry_t e;
  *peak = 0;
  if (ds_pq_init(&pq, ops, sizeof(entry_t), by_dist, NULL, NULL) != DS_OK) {
    return -1;
  }
  memset(done, 0, sizeof(done));
  for (v = 0; v < N; v++) {
    dist[v] = -1;
  }
  dist[0] = 0;
  e.dist = 0;
  e.vertex = 0;
  (void)ds_pq_push(&pq, &e, &handle[0]);
  while (ds_pq_pop(&pq, &e) == DS_OK) {
    size_t x = e.vertex % SIDE, y = e.vertex / SIDE, k;
    size_t nb[4];
    size_t count = 0;
    done[e.vertex] = 1;
    if (x > 0) {
      nb[count++] = e.vertex - 1;
    }
    if (x + 1 < SIDE) {
      nb[count++] = e.vertex + 1;
    }
    if (y > 0) {
      nb[count++] = e.vertex - SIDE;
    }
    if (y + 1 < SIDE) {
      nb[count++] = e.vertex + SIDE;
    }
    for (k = 0; k < count; k++) {
      size_t u = nb[k];
      long d = e.dist + weight(e.vertex, u);
      entry_t f;
      if (done[u] || (dist[u] >= 0 && dist[u] <= d)) {
        continue;
      }
      f.dist = d;
      f.vertex = u;
      if (dist[u] < 0) {
        (void)ds_pq_push(&pq, &f, &handle[u]);
      } else {
        (void)ds_pq_update(&pq, handle[u], &f); /* decrease-key */
      }
      dist[u] = d;
    }
    if (ds_pq_size(&pq) > *peak) {
      *peak = ds_pq_size(&pq);
    }
  }
  ds_pq_destroy(&pq);
  return dist[N - 1];
}

static int by_long(const void *a, const void *b, void *user) {
  long x = *(const long *)a, y = *(const long *)b;
  (void)user;
  return x < y ? -1 : x > y ? 1 : 0;
}

int main(void) {
  static long d1[N], d2[N];
  size_t peak1, peak2, v, mismatches = 0;
  long far1 = dijkstra(ds_pq_strict_fibonacci_ops(), d1, &peak1);
  long far2 = dijkstra(ds_pq_binary_ops(), d2, &peak2);
  ds_sf_heap_t a, b;
  long job;
  size_t ar, loss, rootdeg, maxdeg;
  for (v = 0; v < N; v++) {
    mismatches += d1[v] != d2[v];
  }
  printf("grid %dx%d: corner-to-corner distance %ld (binary heap: %ld), "
         "%lu mismatches, at most %lu queued\n",
         SIDE, SIDE, far1, far2, (unsigned long)mismatches,
         (unsigned long)peak1);

  /* two job queues meld in O(1); handles stay valid */
  (void)ds_sf_heap_init(&a, sizeof(long), by_long, NULL, NULL);
  (void)ds_sf_heap_init(&b, sizeof(long), by_long, NULL, NULL);
  for (job = 0; job < 1000; job++) {
    long p = (job * 37) % 1000;
    (void)ds_sf_heap_push(job % 2 ? &a : &b, &p, NULL);
  }
  ds_sf_heap_meld(&a, &b);
  ds_sf_heap_measures(&a, &ar, &loss, &rootdeg, &maxdeg);
  printf("melded queue: %lu jobs, first %ld; %lu active roots, total loss "
         "%lu, root degree %lu (all bounded by 2 lg n + 9), invariants %s\n",
         (unsigned long)ds_sf_heap_size(&a), *(const long *)ds_sf_heap_top(&a),
         (unsigned long)ar, (unsigned long)loss, (unsigned long)rootdeg,
         ds_sf_heap_check(&a) ? "hold" : "BROKEN");
  ds_sf_heap_destroy(&a);
  ds_sf_heap_destroy(&b);
  return mismatches == 0 ? 0 : 1;
}
