/* The same event scheduler uses either heap through the priority-queue
 * capability. Handles survive priority changes and unrelated insertions. */
#include <dsa.h>
#include <stdio.h>

typedef struct event {
  int time;
  int id;
} event_t;

static int earlier(const void *a, const void *b, void *user) {
  const event_t *x = (const event_t *)a;
  const event_t *y = (const event_t *)b;
  (void)user;
  if (x->time != y->time)
    return (x->time > y->time) - (x->time < y->time);
  return (x->id > y->id) - (x->id < y->id);
}

static ds_status_t schedule(const ds_pq_ops_t *backend) {
  int times[12] = {10, 20, 2, 14, 8, 7, 30, 6, 5, 9, 3, 15};
  ds_pq_t queue;
  ds_pq_handle_t handles[12];
  ds_status_t st;
  event_t event;
  int i;
  st = ds_pq_init(&queue, backend, sizeof(event), earlier, NULL, NULL);
  if (st != DS_OK)
    return st;
  st = ds_pq_reserve(&queue, 12);
  for (i = 0; i < 12 && st == DS_OK; i++) {
    event.time = times[i];
    event.id = i;
    st = ds_pq_push(&queue, &event, &handles[i]);
  }
  /* Expedite event 0, postpone event 2, cancel event 5. */
  event.time = 1;
  event.id = 0;
  if (st == DS_OK)
    st = ds_pq_update(&queue, handles[0], &event);
  event.time = 25;
  event.id = 2;
  if (st == DS_OK)
    st = ds_pq_update(&queue, handles[2], &event);
  if (st == DS_OK)
    st = ds_pq_remove(&queue, handles[5], NULL);
  printf("%s:\n", backend->name);
  while (st == DS_OK && !ds_pq_is_empty(&queue)) {
    st = ds_pq_pop(&queue, &event);
    if (st == DS_OK)
      printf("  time %2d: event %d\n", event.time, event.id);
  }
  ds_pq_destroy(&queue);
  return st;
}

int main(void) {
  ds_status_t st = schedule(ds_pq_min_max_ops());
  if (st == DS_OK)
    st = schedule(ds_pq_shadow_ops());
  if (st != DS_OK)
    fprintf(stderr, "scheduler failed: %d\n", (int)st);
  return st == DS_OK ? 0 : 1;
}
