#include "../lib/heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
typedef struct int_node {
  heap_node_t node;
  int value;
} int_node_t;

void draw_heap_tree_recursive(heap_t *heap, size_t index, int depth,
                              char *prefix) {
  if (index >= heap->size) {
    return;
  }

  heap_node_t *node = (heap_node_t *)heap->data[index];
  printf("%s", prefix);

  int_node_t *int_nodex = (int_node_t *)node;
  printf("├────── %d %d\n", int_nodex->value, (&int_nodex->node)->index);
  fflush(stdout);

  char new_prefix[256];
  snprintf(new_prefix, sizeof(new_prefix), "%s    ", prefix);

  draw_heap_tree_recursive(heap, 2 * index + 1, depth + 1, new_prefix);
  draw_heap_tree_recursive(heap, 2 * index + 2, depth + 1, new_prefix);
}

int compare_int_nodes(heap_node_t *a, heap_node_t *b) {
  return CAST(a, int_node_t)->value - CAST(b, int_node_t)->value;
}

int_node_t *create_int_node(int value) {
  int_node_t *node = (int_node_t *)malloc(sizeof(int_node_t));
  node->value = value;
  return node;
}

void destroy_int_node(void *node) { free(node); }

int main(void) {
  srand(time((long int *)NULL));
  int i;
  heap_t *heap = heap_create(10);

  for (i = 0; i < 20; i++) {
    heap_insert(heap, (void *)create_int_node(rand() % 100), compare_int_nodes);
  }

  int_node_t *root = (int_node_t *)heap_peek_root(heap);
  printf("root: %d\n", root->value);
  draw_heap_tree_recursive(heap, 0, 0, "");
  for (i = 0; i < heap->size; i++) {
    printf("%d %d\n", CAST(heap->data[i], int_node_t)->value,
           (heap->data[i])->index);
  }

  while (!heap_is_empty(heap)) {
    int_node_t *min_node =
        (int_node_t *)heap_extract_root(heap, compare_int_nodes);
    printf("extract: %d\n", min_node->value);
    destroy_int_node((void *)min_node);
  }

  heap_destroy(heap, NULL);

  return 0;
}
