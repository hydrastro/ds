#include "../lib/deque.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct my_deque_node {
  ds_deque_node_t node;
  int data;
} my_deque_node_t;

void print_deque_node(ds_deque_node_t *deque_node) {
  my_deque_node_t *node = CAST(deque_node, my_deque_node_t);
  printf("%d ", node->data);
}

void destroy_deque_node(ds_deque_node_t *deque_node) {
  my_deque_node_t *node = CAST(deque_node, my_deque_node_t);
  free(node);
}

ds_deque_node_t *clone_node(ds_deque_node_t *node) {
  my_deque_node_t *new_node =
      (my_deque_node_t *)malloc(sizeof(my_deque_node_t));
  new_node->data = (CAST(node, my_deque_node_t))->data;
  return &new_node->node;
}

int main(void) {
  int i;
  ds_deque_t *deque;
  my_deque_node_t *node;
  srand((unsigned int)time((long int *)NULL));

  deque = deque_create();

  for (i = 0; i < 10; i++) {
    node = (my_deque_node_t *)malloc(sizeof(my_deque_node_t));
    node->data = rand() % 100;
    deque_push_back(deque, &node->node);
  }

  ds_deque_node_t *current_node;
  current_node = deque->head;
  while (current_node != deque->nil) {
    print_deque_node(current_node);
    current_node = current_node->next;
  }
  printf("\n");

  printf("front pop:\n");
  while (!deque_is_empty(deque)) {
    node = CAST(deque_pop_front(deque), my_deque_node_t);
    current_node = deque->head;
    while (current_node != deque->nil) {
      print_deque_node(current_node);
      current_node = current_node->next;
    }
    printf("\n");
    free(node);
  }

  printf("front push:\n");
  for (i = 0; i < 5; i++) {
    node = (my_deque_node_t *)malloc(sizeof(my_deque_node_t));
    node->data = rand() % 100;
    deque_push_front(deque, &node->node);
  }
  current_node = deque->head;
  while (current_node != deque->nil) {
    print_deque_node(current_node);
    current_node = current_node->next;
  }
  printf("\n");

  my_deque_node_t *front_node = CAST(deque_peek_front(deque), my_deque_node_t);
  my_deque_node_t *back_node = CAST(deque_peek_back(deque), my_deque_node_t);

  if (front_node) {
    printf("front: %d\n", front_node->data);
  }

  if (back_node) {
    printf("back: %d\n", back_node->data);
  }

  while (current_node != deque->nil) {
    print_deque_node(current_node);
    current_node = current_node->next;
  }

  ds_deque_t *new_deque = deque_clone(deque, clone_node);
  deque_destroy(deque, destroy_deque_node);
  deque_destroy(new_deque, destroy_deque_node);

  return 0;
}
