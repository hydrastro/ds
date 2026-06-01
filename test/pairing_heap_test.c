#include "../lib/pairing_heap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct int_node {
  ds_pairing_heap_node_t node;
  int value;
} int_node_t;

static int compare_nodes(ds_pairing_heap_node_t *a, ds_pairing_heap_node_t *b) {
  return CAST(a, int_node_t)->value - CAST(b, int_node_t)->value;
}

int main(void) {
  int input[] = {50, 20, 70, 10, 30, 60, 80, 5, 25, 35};
  int n = (int)(sizeof(input) / sizeof(input[0]));
  int_node_t *nodes;
  ds_pairing_heap_t *heap = ds_pairing_heap_create(compare_nodes);
  ds_pairing_heap_t *other = ds_pairing_heap_create(compare_nodes);
  int previous;
  int count;
  int i;

  assert(ds_pairing_heap_is_empty(heap));
  nodes = (int_node_t *)malloc(sizeof(int_node_t) * (size_t)(n + 3));
  for (i = 0; i < n; ++i) {
    ds_pairing_heap_node_init(&nodes[i].node);
    nodes[i].value = input[i];
    ds_pairing_heap_insert(heap, &nodes[i].node);
  }
  printf("size: %lu\n", (unsigned long)ds_pairing_heap_size(heap));
  assert(ds_pairing_heap_size(heap) == 10);
  printf("min: %d\n", CAST(ds_pairing_heap_peek(heap), int_node_t)->value);
  assert(CAST(ds_pairing_heap_peek(heap), int_node_t)->value == 5);

  /* decrease-key: make node[2] (70) the new min */
  nodes[2].value = 1;
  ds_pairing_heap_decrease_key(heap, &nodes[2].node);
  printf("min after decrease: %d\n",
         CAST(ds_pairing_heap_peek(heap), int_node_t)->value);
  assert(CAST(ds_pairing_heap_peek(heap), int_node_t)->value == 1);

  /* merge another heap in */
  for (i = 0; i < 3; ++i) {
    ds_pairing_heap_node_init(&nodes[n + i].node);
    nodes[n + i].value = 2 + i;
    ds_pairing_heap_insert(other, &nodes[n + i].node);
  }
  ds_pairing_heap_merge(heap, other);
  assert(ds_pairing_heap_size(heap) == 13);
  assert(ds_pairing_heap_is_empty(other));

  /* extract all -> must come out in nondecreasing order */
  previous = -1;
  count = 0;
  while (!ds_pairing_heap_is_empty(heap)) {
    ds_pairing_heap_node_t *m = ds_pairing_heap_extract(heap);
    int v = CAST(m, int_node_t)->value;
    assert(v >= previous);
    previous = v;
    count += 1;
  }
  printf("extracted in order, count: %d\n", count);
  assert(count == 13);
  assert(ds_pairing_heap_extract(heap) == heap->nil);

  ds_pairing_heap_destroy(heap, NULL);
  ds_pairing_heap_destroy(other, NULL);
  free(nodes);
  printf("ok\n");
  return 0;
}
