#include "../lib/avl.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  ds_avl_node_t node;
  int key;
} my_data_t;

int compare_avl_nodes(ds_avl_node_t *a, ds_avl_node_t *b) {
  my_data_t *data_a = (my_data_t *)a;
  my_data_t *data_b = (my_data_t *)b;
  return data_a->key - data_b->key;
}

void print_node(ds_avl_node_t *node) {
  my_data_t *data = (my_data_t *)node;
  printf("%d ", data->key);
}

my_data_t *create_data(int key) {
  my_data_t *new_data = (my_data_t *)malloc(sizeof(my_data_t));
  new_data->key = key;
  return new_data;
}

void destroy(ds_avl_node_t *node) { free(node); }

void draw_avl_tree_recursive(ds_avl_t *tree, ds_avl_node_t *node, char *prefix) {
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

ds_avl_node_t *clone_node(ds_avl_node_t *node) {
  my_data_t *new_node = (my_data_t *)malloc(sizeof(my_data_t));
  new_node->key = (CAST(node, my_data_t))->key;
  return &new_node->node;
}

int main() {
  ds_avl_t *tree = avl_create();

  for (int i = 0; i < 1000; i++) {
    avl_insert(tree, (ds_avl_node_t *)create_data(i), compare_avl_nodes);
  }

  printf("Inorder traversal of AVL tree:\n");
  avl_inorder_walk_tree(tree, print_node);
  printf("\n");

  my_data_t search_data;

  search_data.key = 4;
  ds_avl_node_t *found_node =
      avl_search(tree, (ds_avl_node_t *)&search_data, compare_avl_nodes);

  if (found_node != tree->nil) {
    printf("Node with key %d found in the AVL tree.\n",
           ((my_data_t *)found_node)->key);
    avl_destroy_node(tree, found_node, destroy);
    printf("Deleted entry\n");
  } else {
    printf("Node with key %d not found in the AVL tree.\n", search_data.key);
  }

  printf("\n");
  draw_avl_tree_recursive(tree, tree->root, "");
  printf("\n");

  search_data.key = 7;
  found_node = avl_search(tree, (ds_avl_node_t *)&search_data, compare_avl_nodes);
  if (found_node != tree->nil) {
    avl_destroy_node(tree, found_node, destroy);
    printf("Deleted entry 7\n");
  }

  printf("Inorder traversal of AVL tree:\n");
  avl_inorder_walk_tree(tree, print_node);
  printf("\n");

  printf("Cloning tree\n");
  ds_avl_t *new_tree = avl_clone(tree, clone_node);
  printf("Cloned tree\n");

  printf("Destroying tree\n");
  avl_destroy_tree(tree, destroy);
  printf("Destroyed\n");

  printf("\nTransversing tree:\n");
  draw_avl_tree_recursive(new_tree, new_tree->root, "");
  printf("\n");

  printf("Destroying tree\n");
  avl_destroy_tree(new_tree, destroy);
  printf("Destroyed\n");

  return 0;
}
