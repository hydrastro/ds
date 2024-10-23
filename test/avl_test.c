#include "../lib/avl.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  avl_node_t node;
  int key;
} my_data_t;

int compare_avl_nodes(avl_node_t *a, avl_node_t *b) {
  my_data_t *data_a = (my_data_t *)a;
  my_data_t *data_b = (my_data_t *)b;
  return data_a->key - data_b->key;
}

void print_node(avl_node_t *node) {
  my_data_t *data = (my_data_t *)node;
  printf("%d ", data->key);
}

my_data_t *create_data(int key) {
  my_data_t *new_data = (my_data_t *)malloc(sizeof(my_data_t));
  new_data->key = key;
  return new_data;
}

void destroy(avl_node_t *node) { free(node); }

void draw_avl_tree_recursive(avl_t *tree, avl_node_t *node, char *prefix) {
  if (node == tree->nil) {
    return;
  }
  printf("%s", prefix);
  char new_prefix[256];

  snprintf(new_prefix, sizeof(new_prefix), "%s    ", prefix);
  my_data_t *int_nodex = (my_data_t *)node;
  printf("├────── %d \n", int_nodex->key);
  fflush(stdout);

  draw_avl_tree_recursive(tree, node->left, new_prefix);
  draw_avl_tree_recursive(tree, node->right, new_prefix);
}

int main() {
  avl_t *tree = avl_create();

  for (int i = 0; i < 1000; i++) {
    avl_insert(tree, (avl_node_t *)create_data(i), compare_avl_nodes);
  }

  printf("Inorder traversal of AVL tree:\n");
  avl_inorder_walk_tree(tree, print_node);
  printf("\n");

  my_data_t search_data;
  search_data.key = 4;
  avl_node_t *found_node =
      avl_search(tree, (avl_node_t *)&search_data, compare_avl_nodes);

  if (found_node != tree->nil) {
    printf("Node with key %d found in the AVL tree.\n",
           ((my_data_t *)found_node)->key);

  } else {
    printf("Node with key %d not found in the AVL tree.\n", search_data.key);
  }

  printf("\n");
  draw_avl_tree_recursive(tree, tree->root, "");
  printf("\n");

  avl_delete_node(tree, found_node);
  printf("Inorder traversal of AVL tree:\n");
  avl_inorder_walk_tree(tree, print_node);
  printf("\n");
  found_node = avl_search(tree, (avl_node_t *)&search_data, compare_avl_nodes);

  if (found_node != tree->nil) {
    printf("Node with key %d found in the AVL tree.\n",
           ((my_data_t *)found_node)->key);

  } else {
    printf("Node with key %d not found in the AVL tree.\n", search_data.key);
  }
  printf("\n");
  draw_avl_tree_recursive(tree, tree->root, "");
  printf("\n");
  search_data.key = 7;
  found_node = avl_search(tree, (avl_node_t *)&search_data, compare_avl_nodes);
  avl_delete_node(tree, found_node);
  printf("Inorder traversal of AVL tree:\n");
  avl_inorder_walk_tree(tree, print_node);
  printf("\n");

  printf("\n");
  draw_avl_tree_recursive(tree, tree->root, "");
  printf("\n");

  avl_destroy_tree(tree, destroy);

  return 0;
}
