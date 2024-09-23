#include "../lib/bst.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct my_data_struct {
  bst_node_t node;
  int value;
} my_data_t;

void walk(bst_t *tree, bst_node_t *node) {
  if (node == tree->nil) {
    return;
  }
  walk(tree, node->left);
  printf(".%d.", CAST(node, my_data_t)->value);
  walk(tree, node->right);
}

int compare(bst_node_t *node1, bst_node_t *node2) {
  return CAST(node1, my_data_t)->value - CAST(node2, my_data_t)->value;
}

void destroy_bst_node(bst_node_t *node) {
  my_data_t *data = (my_data_t *)node;
  printf("Destroying node with value: %d\n", data->value);
  free(data);
}

int main() {
  bst_t *tree = bst_create();

  for (int i = 0; i < 100; i++) {
    my_data_t *data = (my_data_t *)malloc(sizeof(my_data_t));
    data->value = i;
    bst_insert(tree, &data->node, compare);
  }

  bst_node_t *kek = bst_minimum(tree, tree->root);
  walk(tree, kek);
  printf("\n");

  my_data_t search_data;
  search_data.value = 5;
  bst_node_t *found_node = bst_search(tree, &(search_data.node), compare);

  if (found_node != tree->nil) {
    my_data_t *found_data = (my_data_t *)found_node;
    printf("Found node with value: %d\n", found_data->value);
  } else {
    printf("Node not found!\n");
  }
  kek = bst_minimum(tree, tree->root);
  walk(tree, kek);

  if (found_node != tree->nil) {
    printf("deleting node\n");
    fflush(stdout);
    bst_delete_node(tree, found_node);
    printf("deleting node\n");
    fflush(stdout);

    found_node = bst_search(tree, &search_data.node, compare);
    if (found_node == tree->nil) {
      printf("Node successfully deleted!\n");
    } else {
      printf("Node deletion failed!\n");
    }
  }
  kek = bst_minimum(tree, tree->root);
  walk(tree, kek);
  printf("\n");

  printf("destroying tree...\n");
  bst_destroy_tree(tree, destroy_bst_node);

  return 0;
}
