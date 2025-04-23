#include "../lib/btree.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct my_node {
  ds_btree_node_t node;
  int data;
} my_node_t;

int compare_int(ds_btree_node_t *a, ds_btree_node_t *b) {
  return CAST(a, my_node_t)->data - CAST(b, my_node_t)->data;
}

void destroy_int(ds_btree_node_t *data) { free(data); }

void print_btree_node(ds_btree_internal_node_t *node, int level,
                      void (*print_data)(ds_btree_node_t *)) {
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

void print_int(ds_btree_node_t *data) {
  my_node_t *node = CAST(data, my_node_t);
  printf("%d,", node->data);
}

ds_btree_node_t *clone_node(ds_btree_node_t *node) {
  my_node_t *new_node = (my_node_t *)malloc(sizeof(my_node_t));
  printf(" node %p\n", new_node);
  new_node->data = (CAST(node, my_node_t))->data;
  return &new_node->node;
}

void test_btree_operations() {
  ds_btree_t *tree = btree_create(2);
  int values[] = {10, 20, 5, 6, 15, 30, 25, 35};
  for (int i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    my_node_t *data = (my_node_t *)malloc(sizeof(my_node_t));
    data->data = values[i];
    btree_insert(tree, &data->node, compare_int);
  }

  printf("after inserts:\n");
  print_btree_node(tree->root, 0, print_int);
  printf("\nIn-order traversal:\n");
  btree_inorder_walk(tree, tree->root, print_int);
  printf("\n");

  printf("Pre-order traversal:\n");
  btree_preorder_walk(tree, tree->root, print_int);
  printf("\n");

  printf("Post-order traversal:\n");
  btree_postorder_walk(tree, tree->root, print_int);
  printf("\n");

  my_node_t *wkey = (my_node_t *)malloc(sizeof(my_node_t));
  for (int i = 0; i < 35; i++) {
    wkey->data = i;
    ds_btree_node_t *result = btree_search(tree, &wkey->node, compare_int);
    if (result != tree->nil) {
      printf("found: %d check: %d\n", CAST(result, my_node_t)->data, i);
    } else {
      printf("%d not found\n", i);
    }
  }

  print_btree_node(tree->root, 0, print_int);

  int delete_values[] = {10, 20, 5, 6, 15, 35};
  for (int i = 0; i < sizeof(delete_values) / sizeof(delete_values[0]); i++) {
    printf("deleting %d\n", delete_values[i]);
    fflush(stdout);
    wkey->data = delete_values[i];
    printf("searching...\n");
    fflush(stdout);
    ds_btree_node_t *result = btree_search(tree, &wkey->node, compare_int);
    printf("search OK\n");
    fflush(stdout);
    if (result == tree->nil) {
      printf("result is nil\n");
    } else {
      printf("result defined:");
      print_int(result);
      printf("\n");
      btree_delete_node(tree, result);
      printf("deleted %d\n", delete_values[i]);
      fflush(stdout);
      print_btree_node(tree->root, 0, print_int);

      destroy_int(result);
      fflush(stdout);
    }
  }

  ds_btree_t *new_tree = btree_clone(tree, clone_node);

  printf("after deletions:\n");
  print_btree_node(tree->root, 0, print_int);
  printf("destroying whole tree\n");
  btree_destroy_tree(tree, destroy_int);
  printf("destroyed whole tree\n");
  free(wkey);

  printf("clone:\n");
  print_btree_node(new_tree->root, 0, print_int);
  btree_destroy_tree(new_tree, destroy_int);
}

void *thread_function(void *arg) {
  ds_btree_t *tree = (ds_btree_t *)arg;

  for (int i = 0; i < 100; i++) {
    my_node_t *data = (my_node_t *)malloc(sizeof(my_node_t));
    data->data = rand() % 1000;
    printf("thread insert..\n");
    btree_insert(tree, &data->node, compare_int);
    printf("thread inserted\n");
  }

  return NULL;
}

void test_thread_safety() {
  ds_btree_t *tree = btree_create(2);

  pthread_t threads[5];
  for (int i = 0; i < 5; i++) {
    pthread_create(&threads[i], NULL, thread_function, tree);
  }

  for (int i = 0; i < 5; i++) {
    pthread_join(threads[i], NULL);
  }

  printf("after inserts:\n");
  print_btree_node(tree->root, 0, print_int);

  btree_destroy_tree(tree, destroy_int);
}

int main() {
  test_btree_operations();
  test_thread_safety();

  return 0;
}
