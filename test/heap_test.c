#include "../lib/heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct int_node {
  int value;
} int_node_t;

int compare_int_nodes(void *a, void *b) {
  int_node_t *node_a = CAST(a, int_node_t);
  int_node_t *node_b = CAST(b, int_node_t);
  return node_a->value - node_b->value;
}

int_node_t *create_int_node(int value) {
  int_node_t *node = (int_node_t *)malloc(sizeof(int_node_t));
  node->value = value;
  return node;
}

void destroy_int_node(void *node) { free(node); }

int main(void) {
  unsigned long seed = 0ul;
  srand(time(seed));
  int i;
  heap_t *heap = heap_create(10);

  for (i = 0; i < 20; i++) {
    heap_insert(heap, (void *)create_int_node(rand() % 100), compare_int_nodes);
  }

  int_node_t *root = (int_node_t *)heap_peek_root(heap);
  printf("root: %d\n", root->value);

  while (!heap_is_empty(heap)) {
    int_node_t *min_node =
        (int_node_t *)heap_extract_root(heap, compare_int_nodes);
    printf("extract: %d\n", min_node->value);
    destroy_int_node((void *)min_node);
  }

  heap_destroy(heap, NULL);

  return 0;
}
