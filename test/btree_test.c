#include "btree.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int compare_int(void *a, void *b) { return (*(int *)a - *(int *)b); }

void destroy_int(void *data) {}

void print_btree_node(btree_node_t *node, int level,
                      void (*print_data)(void *)) {
  if (node == NULL)
    return;

  printf("level %d: ", level);
  for (int i = 0; i < node->num_keys; i++) {
    print_data(node->data[i]);
    printf(" ");
  }
  printf("\n");

  if (!node->is_leaf) {
    for (int i = 0; i <= node->num_keys; i++) {
      print_btree_node(node->children[i], level + 1, print_data);
    }
  }
}

void print_int(void *data) { printf("%d", *(int *)data); }

void test_btree_operations() {
  btree_t *tree = btree_create(2);
  int values[] = {10, 20, 5, 6, 15, 30, 25, 35};
  for (int i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    int *data = (int *)malloc(sizeof(int));
    *data = values[i];
    btree_insert(tree, data, compare_int);
  }

  printf("after inserts:\n");
  print_btree_node(tree->root, 0, print_int);

  for (int i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    int key = values[i];
    int *result = (int *)btree_search(tree, &key, compare_int);
    if (result) {
      printf("found: %d\n", *result);
    } else {
      printf("%d not found\n", key);
    }
  }

  int delete_values[] = {10, 20, 5};
  for (int i = 0; i < sizeof(delete_values) / sizeof(delete_values[0]); i++) {
    int key = delete_values[i];
    btree_delete(tree, &key, destroy_int, compare_int);
    printf("deleted %d\n", key);
    print_btree_node(tree->root, 0, print_int);
  }

  printf("deletions::\n");
  print_btree_node(tree->root, 0, print_int);
  btree_destroy(tree, destroy_int);
}

void *thread_function(void *arg) {
  btree_t *tree = (btree_t *)arg;

  for (int i = 0; i < 100; i++) {
    int *data = (int *)malloc(sizeof(int));
    *data = rand() % 1000;
    printf("thread insert..\n");
    btree_insert(tree, data, compare_int);
    printf("thread inserted\n");
  }

  return NULL;
}

void test_thread_safety() {
  btree_t *tree = btree_create(2);

  pthread_t threads[5];
  for (int i = 0; i < 5; i++) {
    pthread_create(&threads[i], NULL, thread_function, tree);
  }

  for (int i = 0; i < 5; i++) {
    pthread_join(threads[i], NULL);
  }

  printf("after inserts:\n");
  print_btree_node(tree->root, 0, print_int);

  btree_destroy(tree, destroy_int);
}

int main() {
  test_btree_operations();
  test_thread_safety();

  return 0;
}
