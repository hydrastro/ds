/*
 * Sequence containers: stack (LIFO), queue (FIFO), and deque (double-ended).
 *
 * All three share the intrusive pattern: embed the container's node in your
 * struct, hand its address in, and CAST() back out. They differ only in where
 * elements enter and leave. Pop/dequeue return the node (or the container's nil
 * sentinel when empty); the elements here are stack objects, so we free
 * nothing.
 */
#include "ds.h"
#include <stdio.h>

typedef struct si {
  ds_stack_node_t node;
  int v;
} si_t;

typedef struct qi {
  ds_queue_node_t node;
  int v;
} qi_t;

typedef struct di {
  ds_deque_node_t node;
  int v;
} di_t;

int main(void) {
  ds_stack_t *stack = ds_stack_create();
  ds_queue_t *queue = ds_queue_create();
  ds_deque_t *deque = ds_deque_create();
  si_t s[3];
  qi_t q[3];
  di_t d[3];
  int i;

  for (i = 0; i < 3; ++i) {
    s[i].v = i;
    q[i].v = i;
    d[i].v = i;
  }

  /* stack: last in, first out -> pops 2,1,0 */
  ds_stack_push(stack, &s[0].node);
  ds_stack_push(stack, &s[1].node);
  ds_stack_push(stack, &s[2].node);
  printf("stack (LIFO):");
  while (!ds_stack_is_empty(stack)) {
    printf(" %d", CAST(ds_stack_pop(stack), si_t)->v);
  }
  printf("\n");

  /* queue: first in, first out -> dequeues 0,1,2 */
  ds_queue_enqueue(queue, &q[0].node);
  ds_queue_enqueue(queue, &q[1].node);
  ds_queue_enqueue(queue, &q[2].node);
  printf("queue (FIFO):");
  while (queue->head != queue->nil) {
    printf(" %d", CAST(ds_queue_dequeue(queue), qi_t)->v);
  }
  printf("\n");

  /* deque: push to both ends, then drain from the front -> 1,0,2 */
  ds_deque_push_back(deque, &d[0].node);
  ds_deque_push_front(deque, &d[1].node);
  ds_deque_push_back(deque, &d[2].node);
  printf("deque (front->back):");
  for (;;) {
    ds_deque_node_t *n = ds_deque_pop_front(deque);
    if (n == deque->nil) {
      break;
    }
    printf(" %d", CAST(n, di_t)->v);
  }
  printf("\n");

  ds_stack_destroy(stack, NULL);
  ds_queue_destroy(queue, NULL);
  ds_deque_destroy(deque, NULL);
  return 0;
}
