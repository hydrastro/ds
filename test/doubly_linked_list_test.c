#include "../lib/doubly_linked_list.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct my_node {
  doubly_linked_list_node_t node;
  int data;
} my_node_t;

int compare_nodes(doubly_linked_list_node_t *node1,
                  doubly_linked_list_node_t *node2) {
  my_node_t *node_a = CAST(node1, my_node_t);
  my_node_t *node_b = CAST(node2, my_node_t);
  return node_b->data - node_a->data;
}

void print_node(doubly_linked_list_node_t *node) {
  my_node_t *my_node = CAST(node, my_node_t);
  printf("%d ", my_node->data);
}

void destroy_node(doubly_linked_list_node_t *node) {
  my_node_t *my_node = CAST(node, my_node_t);
  free(my_node);
}

int main(void) {
  int i;
  my_node_t *node, *found_node, *search_value;
  doubly_linked_list_t *list;
  srand(time((long int *)NULL));
  list = doubly_linked_list_create();
  search_value = (my_node_t *)malloc(sizeof(my_node_t));
  search_value->data = 5;

  for (i = 0; i < 10; i++) {
    node = (my_node_t *)malloc(sizeof(my_node_t));
    node->data = rand() % 100;
    doubly_linked_list_append(list, &node->node);
  }

  doubly_linked_list_walk_forward(list, list->head, print_node);
  printf("\n");
  doubly_linked_list_walk_backwards(list, list->head, print_node);

  found_node = (my_node_t *)doubly_linked_list_search(list, &search_value->node,
                                                      compare_nodes);
  if ((doubly_linked_list_node_t *)found_node != list->nil) {
    printf("found %d\n", found_node->data);
  } else {
    printf("not found\n", search_value);
  }

  my_node_t *new_node = (my_node_t *)malloc(sizeof(my_node_t));
  new_node->data = 999;
  doubly_linked_list_prepend(list, &new_node->node);

  printf("prepend: ");
  doubly_linked_list_walk_forward(list, list->head, print_node);

  if (&found_node->node != list->nil) {
    doubly_linked_list_delete_node(list, &found_node->node);
    printf("delete node: ");
    doubly_linked_list_walk_forward(list, list->head, print_node);
  }

  doubly_linked_list_destroy(list, destroy_node);

  return 0;
}
