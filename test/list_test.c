#include "../lib/list.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct my_node {
  ds_list_node_t node;
  int data;
} my_node_t;

int compare_nodes(ds_list_node_t *node1, ds_list_node_t *node2) {
  my_node_t *node_a = CAST(node1, my_node_t);
  my_node_t *node_b = CAST(node2, my_node_t);
  return node_b->data - node_a->data;
}

void print_node(ds_list_node_t *data) {
  my_node_t *node = CAST(data, my_node_t);
  printf("%d ", node->data);
}

void destroy_node(ds_list_node_t *data) {
  my_node_t *node = CAST(data, my_node_t);
  free(node);
}

ds_list_node_t *clone_node(ds_list_node_t *node) {
  my_node_t *new_node = (my_node_t *)malloc(sizeof(my_node_t));
  new_node->data = (CAST(node, my_node_t))->data;
  return &new_node->node;
}

int main(void) {
  int i;
  my_node_t *node, *found_node, *search_value;
  ds_list_t *list;
  srand((unsigned int)time((long int *)NULL));
  list = list_create();
  search_value = (my_node_t *)malloc(sizeof(my_node_t));
  search_value->data = 5;

  for (i = 0; i < 10; i++) {
    node = (my_node_t *)malloc(sizeof(my_node_t));
    node->data = rand() % 100;
    list_append(list, &node->node);
  }
  list_walk_forward(list, list->head, print_node);
  printf("\n");
  list_walk_backwards(list, list->head, print_node);
  printf("\n");

  found_node =
      (my_node_t *)list_search(list, &search_value->node, compare_nodes);
  if ((ds_list_node_t *)found_node != list->nil) {
    print_node(&found_node->node);
    printf("found\n");
  } else {
    printf("not found\n");
  }

  my_node_t *new_node = (my_node_t *)malloc(sizeof(my_node_t));
  new_node->data = 999;
  list_prepend(list, &new_node->node);

  printf("prepend: ");
  list_walk_forward(list, list->head, print_node);
  printf("\n");

  if (&found_node->node != list->nil) {
    list_delete_node(list, &found_node->node);
    destroy_node(&found_node->node);
    printf("delete node: ");
    list_walk_forward(list, list->head, print_node);
    printf("\n");
  }
  free(search_value);

  ds_list_t *new_list = list_clone(list, clone_node);
  list_destroy(list, destroy_node);
  printf("\n");
  list_walk_forward(new_list, new_list->head, print_node);
  list_destroy(new_list, destroy_node);

  return 0;
}
