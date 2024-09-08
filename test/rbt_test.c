#include "../lib/rbt.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAL(type) ((type *)malloc(sizeof(type)))
#define IN(tree, nd, cmp) (rbt_insert(tree, (&(nd)->node), (cmp)))
#define SEARCH(tree, node, cmp, type)                                          \
  (CAST(rbt_search((tree), (tree)->root, (node), (cmp)), type))
#define IFNIL(tree, result) if ((tree->nil) == &result->node)
#define FF (fflush(stdout))
#define WALK(tree, print_node) (rbt_inorder_walk_tree(tree, print_node))
#define PN(node, print, type) (print(CAST(node, type)))
#define NEXT(tree, nd, type) (CAST(rbt_successor(tree, &nd->node), type))
#define PREV(tree, nd, type) (CAST(rbt_predecessor(tree, &nd->node), type))
#define ISINT(x) (x >= '0' && x <= '9')
#define TFIRST(tree, nd) (CAST(rbt_minimum(tree,&nd->node))
#define TLAST(tree, nd) (CAST(rbt_maximum(tree,&nd->node))

typedef struct my_node {
  rbt_node_t node;
  int data;
} my_node_t;

int compare_nodes(void *node1, void *node2) {
  return (CAST(node1, my_node_t))->data - (CAST(node2, my_node_t))->data;
}

void print_node(void *node) { printf("%d ", (CAST(node, my_node_t))->data); }

void destroy_node(void *node) {
  my_node_t *my_node = CAST(node, my_node_t);
  free(my_node);
}

int main(void) {
  int i;
  my_node_t *node, *result_node, *search_data;
  rbt_t *tree;
  srand(time(NULL));

  tree = rbt_create();
  for (i = 0; i < 20; i++) {
    node = (my_node_t *)malloc(sizeof(my_node_t));
    node->data = rand() % 100;
    rbt_insert(tree, &node->node, compare_nodes);
  }
  rbt_inorder_walk_tree(tree, print_node);

  printf("\n");

  search_data = (my_node_t *)malloc(sizeof(my_node_t));
  search_data->data = 10;
  ;
  result_node = (my_node_t *)rbt_search(tree, tree->root, &search_data->node,
                                        compare_nodes);
  if (tree->nil == &result_node->node) {
    printf("not found");
  } else {
    print_node(result_node);
  }
  rbt_destroy(tree, tree->root, destroy_node);

  return 0;
}
