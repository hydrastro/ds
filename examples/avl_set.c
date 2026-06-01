/*
 * AVL tree as an ordered set of integers.
 *
 * Like all the intrusive trees here, you supply a comparison function over the
 * embedded nodes and recover your record with CAST(). An in-order walk visits
 * keys in sorted order.
 */
#include "ds.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct entry {
  ds_avl_node_t node;
  int key;
} entry_t;

static int compare_entries(ds_avl_node_t *a, ds_avl_node_t *b) {
  return CAST(a, entry_t)->key - CAST(b, entry_t)->key;
}

static void print_key(ds_avl_node_t *node) {
  printf("%d ", CAST(node, entry_t)->key);
}

static void free_entry(ds_avl_node_t *node) {
  free(node);
}

static entry_t *make_entry(int key) {
  entry_t *e = (entry_t *)malloc(sizeof(entry_t));
  e->key = key;
  return e;
}

int main(void) {
  ds_avl_t *tree = ds_avl_create();
  int keys[7];
  int i;
  entry_t probe;

  keys[0] = 50;
  keys[1] = 30;
  keys[2] = 70;
  keys[3] = 20;
  keys[4] = 40;
  keys[5] = 60;
  keys[6] = 80;

  /* the tree owns the heap entries because we pass free_entry to destroy */
  for (i = 0; i < 7; ++i) {
    ds_avl_insert(tree, &make_entry(keys[i])->node, compare_entries);
  }

  printf("in-order (sorted): ");
  ds_avl_inorder_walk_tree(tree, print_key);
  printf("\n");

  /* membership query: search returns the node, or the tree's nil sentinel */
  probe.key = 40;
  if (ds_avl_search(tree, &probe.node, compare_entries) != tree->nil) {
    printf("40 is present\n");
  }
  probe.key = 41;
  if (ds_avl_search(tree, &probe.node, compare_entries) == tree->nil) {
    printf("41 is absent\n");
  }

  ds_avl_destroy_tree(tree, free_entry);
  return 0;
}
