#include "../lib/bst.h"
#include <stdio.h>
#include <stdlib.h>

void destroy_bst_node(bst_node_t *node) { free(node); }

int main() {
  bst_t *tree = bst_create();

  bst_insert(tree, 10);
  bst_insert(tree, 5);
  bst_insert(tree, 20);
  bst_insert(tree, 15);
  bst_insert(tree, 25);

  bst_node_t *min_node = bst_minimum(tree);
  printf("min: %d\n", min_node->key);

  bst_node_t *max_node = bst_maximum(tree);
  printf("max: %d\n", max_node->key);

  bst_node_t *search_node = bst_search(tree, 15);
  if (search_node != tree->nil) {
    printf("found: %d\n", search_node->key);
  } else {
    printf("not found\n");
  }

  bst_delete(tree, 10);
  printf("deleted 10\n");

  bst_destroy(tree, destroy_bst_node);
  printf("destroyed\n");

  return 0;
}
