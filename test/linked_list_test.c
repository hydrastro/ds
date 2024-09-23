#include "../lib/linked_list.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct my_node {
  linked_list_node_t node;
  int data;
} my_node_t;

int compare_nodes(linked_list_node_t *node1, linked_list_node_t *node2) {
  my_node_t *node_a = CAST(node1, my_node_t);
  my_node_t *node_b = CAST(node2, my_node_t);
  return node_b->data - node_a->data;
}

void print_node(linked_list_node_t *data) {
  my_node_t *node = CAST(data, my_node_t);
  printf("%d ", node->data);
}

void destroy_node(linked_list_node_t *data) {
  my_node_t *node = CAST(data, my_node_t);
  free(node);
}

int main(void) {
  int i;
  my_node_t *node, *found_node, *search_value;
  linked_list_t *list;
  srand((unsigned int)time((long int *)NULL));
  list = linked_list_create();
  search_value = (my_node_t *)malloc(sizeof(my_node_t));
  search_value->data = 5;

  for (i = 0; i < 10; i++) {
    node = (my_node_t *)malloc(sizeof(my_node_t));
    node->data = rand() % 100;
    linked_list_append(list, &node->node);
  }
  linked_list_walk_forward(list, list->head, print_node);
  printf("\n");
  linked_list_walk_backwards(list, list->head, print_node);
  printf("\n");

  found_node =
      (my_node_t *)linked_list_search(list, &search_value->node, compare_nodes);
  if ((linked_list_node_t *)found_node != list->nil) {
    print_node(&found_node->node);
    printf("found\n");
  } else {
    printf("not found\n");
  }

  my_node_t *new_node = (my_node_t *)malloc(sizeof(my_node_t));
  new_node->data = 999;
  linked_list_prepend(list, &new_node->node);

  printf("prepend: ");
  linked_list_walk_forward(list, list->head, print_node);
  printf("\n");

  if (&found_node->node != list->nil) {
    linked_list_delete_node(list, &found_node->node);
    printf("delete node: ");
    linked_list_walk_forward(list, list->head, print_node);
    printf("\n");
  }

  linked_list_destroy(list, destroy_node);

  return 0;
}
