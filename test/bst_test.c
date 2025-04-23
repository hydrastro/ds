#include "../lib/bst.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct my_data_struct {
  ds_bst_node_t node;
  int value;
} my_data_t;

void walk(ds_bst_t *tree, ds_bst_node_t *node) {
  if (node == tree->nil) {
    return;
  }
  walk(tree, node->left);
  printf(".%d.", CAST(node, my_data_t)->value);
  walk(tree, node->right);
}

int compare(ds_bst_node_t *node1, ds_bst_node_t *node2) {
  return CAST(node1, my_data_t)->value - CAST(node2, my_data_t)->value;
}

void destroy_bst_node(ds_bst_node_t *node) {
  my_data_t *data = (my_data_t *)node;
  printf("Destroying node with value: %d\n", data->value);
  free(data);
}

ds_bst_node_t *clone_node(ds_bst_node_t *node) {
  my_data_t *new_node = (my_data_t *)malloc(sizeof(my_data_t));
  new_node->value = (CAST(node, my_data_t))->value;
  return &new_node->node;
}

int main() {
  ds_bst_t *tree = bst_create();

  for (int i = 0; i < 10; i++) {
    my_data_t *data = (my_data_t *)malloc(sizeof(my_data_t));
    data->value = i;
    bst_insert(tree, &data->node, compare);
  }

  ds_bst_node_t *kek = bst_minimum(tree, tree->root);
  walk(tree, kek);
  printf("\n");

  my_data_t search_data;
  search_data.value = 5;
  ds_bst_node_t *found_node = bst_search(tree, &(search_data.node), compare);

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
    destroy_bst_node(found_node);

    printf("Node successfully deleted!\n");
  }
  kek = bst_minimum(tree, tree->root);
  walk(tree, kek);
  printf("\n");

  ds_bst_t *new_tree = bst_clone(tree, clone_node);

  printf("destroying tree...\n");
  bst_destroy_tree(tree, destroy_bst_node);

  kek = bst_minimum(new_tree, new_tree->root);
  walk(new_tree, kek);
  printf("\n");

  printf("destroying tree...\n");
  bst_destroy_tree(new_tree, destroy_bst_node);

  return 0;
}
