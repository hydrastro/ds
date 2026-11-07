#include "../lib/ring_list.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct int_node {
  ds_ring_list_node_t node;
  int value;
} int_node_t;

static int sum;

static int compare_nodes(ds_ring_list_node_t *a, ds_ring_list_node_t *b) {
  return CAST(a, int_node_t)->value - CAST(b, int_node_t)->value;
}

static void accumulate(ds_ring_list_node_t *node) {
  sum += CAST(node, int_node_t)->value;
}

static ds_ring_list_node_t *clone_node(ds_ring_list_node_t *node) {
  int_node_t *copy = (int_node_t *)malloc(sizeof(int_node_t));
  copy->value = CAST(node, int_node_t)->value;
  return &copy->node;
}

static void destroy_node(ds_ring_list_node_t *node) { free(node); }

int main(void) {
  int_node_t nodes[5];
  int_node_t probe;
  ds_ring_list_node_t *found;
  ds_ring_list_t *list = ds_ring_list_create();
  ds_ring_list_t *clone;
  int i;

  assert(ds_ring_list_is_empty(list));
  for (i = 0; i < 5; ++i) {
    nodes[i].value = i + 1;
    ds_ring_list_push_back(list, &nodes[i].node);
  }
  printf("size: %lu\n", (unsigned long)list->size);
  assert(list->size == 5);
  printf("front: %d back: %d\n",
         CAST(ds_ring_list_front(list), int_node_t)->value,
         CAST(ds_ring_list_back(list), int_node_t)->value);
  assert(CAST(ds_ring_list_front(list), int_node_t)->value == 1);
  assert(CAST(ds_ring_list_back(list), int_node_t)->value == 5);

  sum = 0;
  ds_ring_list_walk_forward(list, list->nil, accumulate);
  printf("sum: %d\n", sum);
  assert(sum == 15);

  /* rotate: head advances */
  ds_ring_list_rotate(list);
  printf("front after rotate: %d\n",
         CAST(ds_ring_list_front(list), int_node_t)->value);
  assert(CAST(ds_ring_list_front(list), int_node_t)->value == 2);

  probe.value = 3;
  found = ds_ring_list_search(list, &probe.node, compare_nodes);
  assert(found != list->nil);
  probe.value = 42;
  assert(ds_ring_list_search(list, &probe.node, compare_nodes) == list->nil);

  clone = ds_ring_list_clone(list, clone_node);
  assert(clone->size == 5);
  sum = 0;
  ds_ring_list_walk_forward(clone, clone->nil, accumulate);
  assert(sum == 15);

  /* delete middle node */
  ds_ring_list_delete_node(list, &nodes[2].node);
  assert(list->size == 4);
  sum = 0;
  ds_ring_list_walk_forward(list, list->nil, accumulate);
  printf("sum after delete: %d\n", sum);
  assert(sum == 12);

  /* pop until empty */
  while (!ds_ring_list_is_empty(list)) {
    ds_ring_list_node_t *n = ds_ring_list_pop_front(list);
    (void)n;
  }
  assert(ds_ring_list_pop_front(list) == list->nil);

  ds_ring_list_destroy(list, NULL);
  ds_ring_list_destroy(clone, destroy_node);
  printf("ok\n");
  return 0;
}
