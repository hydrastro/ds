/*
 * Melding priority queues: when nodes move and when items are copied.
 *
 * A native meld moves the absorbed queue's nodes, so its handles stay
 * valid and now address items in the receiving queue. That requires the
 * same order (compare function and user data) and memory either context
 * can release (ds_context_compatible). Otherwise ds_pq_meld copies the
 * items one by one in the receiving queue's order, and the absorbed
 * queue's handles become invalid.
 */
#include "ds.h"
#include <stdio.h>

static int ascending(const void *a, const void *b, void *user) {
  int x = *(const int *)a;
  int y = *(const int *)b;
  (void)user;
  return (x > y) - (x < y);
}

static int descending(const void *a, const void *b, void *user) {
  return ascending(b, a, user);
}

static void drain(const char *label, ds_pq_t *q) {
  int x;
  printf("%s:", label);
  while (ds_pq_pop(q, &x) == DS_OK) {
    printf(" %d", x);
  }
  printf("\n");
}

int main(void) {
  ds_context_t a;
  ds_context_t b;
  ds_pq_t p;
  ds_pq_t q;
  ds_pq_handle_t h;
  int i;
  int x;

  ds_context_init(&a);
  ds_context_init(&b); /* another context over the same heap */
  printf("contexts compatible: %s\n",
         ds_context_compatible(&a, &b) ? "yes" : "no");

  /* same order, compatible memory: a native meld, handles survive */
  if (ds_pq_init(&p, ds_pq_pairing_ops(), sizeof(int), ascending, NULL, &a) !=
          DS_OK ||
      ds_pq_init(&q, ds_pq_pairing_ops(), sizeof(int), ascending, NULL, &b) !=
          DS_OK) {
    return 1;
  }
  for (i = 0; i < 5; i++) {
    x = 10 + i;
    (void)ds_pq_push(&p, &x, NULL);
    x = 20 + i;
    (void)ds_pq_push(&q, &x, i == 4 ? &h : NULL);
  }
  if (ds_pq_meld(&p, &q) != DS_OK) {
    return 1;
  }
  x = 1; /* the handle from q still addresses 24, now inside p */
  (void)ds_pq_update(&p, h, &x);
  drain("native meld, 24 decreased to 1 through its old handle", &p);
  ds_pq_destroy(&p);
  ds_pq_destroy(&q);

  /* different orders: the items are copied in the receiver's order */
  if (ds_pq_init(&p, ds_pq_pairing_ops(), sizeof(int), ascending, NULL, &a) !=
          DS_OK ||
      ds_pq_init(&q, ds_pq_pairing_ops(), sizeof(int), descending, NULL, &a) !=
          DS_OK) {
    return 1;
  }
  for (i = 0; i < 5; i++) {
    x = 10 + i;
    (void)ds_pq_push(&p, &x, NULL);
    x = 20 + i;
    (void)ds_pq_push(&q, &x, NULL);
  }
  if (ds_pq_meld(&p, &q) != DS_OK) {
    return 1;
  }
  drain("copying meld into the ascending queue", &p);
  ds_pq_destroy(&p);
  ds_pq_destroy(&q);
  return 0;
}
