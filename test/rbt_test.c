#include "rbt.h"
#include<stdio.h>
#include<stdlib.h>
#include<time.h>

typedef struct my_node {
  rbt_node_t node;
  int data;
} my_node_t;

int compare_nodes(void *node1, void *node2) {
  return ((my_node_t *)node1)->data -
         ((my_node_t *)node2)->data;
}

void print_node(void *node) {
  printf("%d", ((my_node_t *)node)->data);
}

void interleaving(void *node) {
  printf(" ");
}

void destroy_node(void *node) {
  free(node);
}

void main(void) {
  int i;
  my_node_t *node;
  rbt_t *tree;
  srand(time(NULL));
  tree = rbt_create();
  for(i = 0; i < 20; i++) {
    node = (my_node_t *) malloc(sizeof(my_node_t));
    node->data = rand() % 100;
    rbt_insert(tree, &node->node, compare_nodes);
  }
  rbt_inorder_walk_tree(tree, print_node, interleaving);
  rbt_destroy(tree, tree->root, destroy_node);
}
