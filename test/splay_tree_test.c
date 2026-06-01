#include "../lib/splay_tree.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct int_node {
  ds_splay_tree_node_t node;
  int value;
} int_node_t;

static int last_seen;
static int ordered;
static int visited;

static int compare_nodes(ds_splay_tree_node_t *a, ds_splay_tree_node_t *b) {
  return CAST(a, int_node_t)->value - CAST(b, int_node_t)->value;
}

static void check_inorder(ds_splay_tree_node_t *node) {
  int v = CAST(node, int_node_t)->value;
  if (visited > 0 && v < last_seen) {
    ordered = 0;
  }
  last_seen = v;
  visited += 1;
}

static ds_splay_tree_node_t *clone_node(ds_splay_tree_node_t *node) {
  int_node_t *copy = (int_node_t *)malloc(sizeof(int_node_t));
  copy->value = CAST(node, int_node_t)->value;
  return &copy->node;
}

static void destroy_node(ds_splay_tree_node_t *node) {
  free(node);
}

int main(void) {
  int input[] = {50, 20, 70, 10, 30, 60, 80, 5, 25, 35};
  int n = (int)(sizeof(input) / sizeof(input[0]));
  int_node_t *nodes;
  int_node_t probe;
  ds_splay_tree_node_t *found;
  ds_splay_tree_t *tree = splay_tree_create(compare_nodes);
  ds_splay_tree_t *clone;
  int i;

  assert(splay_tree_is_empty(tree));
  nodes = (int_node_t *)malloc(sizeof(int_node_t) * (size_t)n);
  for (i = 0; i < n; ++i) {
    nodes[i].value = input[i];
    splay_tree_insert(tree, &nodes[i].node);
  }
  printf("size: %lu\n", (unsigned long)tree->size);
  assert(tree->size == 10);

  ordered = 1;
  visited = 0;
  splay_tree_inorder_walk_tree(tree, check_inorder);
  printf("inorder sorted: %s, visited: %d\n", ordered ? "yes" : "no", visited);
  assert(ordered == 1 && visited == 10);

  probe.value = 30;
  found = splay_tree_search(tree, &probe.node);
  assert(found != tree->nil);
  /* searched node should now be the root (splay property) */
  assert(CAST(tree->root, int_node_t)->value == 30);
  printf("after search(30) root is: %d\n", CAST(tree->root, int_node_t)->value);

  probe.value = 999;
  assert(splay_tree_search(tree, &probe.node) == tree->nil);

  clone = splay_tree_clone(tree, clone_node);
  ordered = 1;
  visited = 0;
  splay_tree_inorder_walk_tree(clone, check_inorder);
  assert(ordered == 1 && visited == 10);

  probe.value = 70;
  found = splay_tree_remove(tree, &probe.node);
  assert(found != tree->nil);
  assert(tree->size == 9);
  probe.value = 70;
  assert(splay_tree_search(tree, &probe.node) == tree->nil);
  printf("after remove(70) size: %lu\n", (unsigned long)tree->size);

  ordered = 1;
  visited = 0;
  splay_tree_inorder_walk_tree(tree, check_inorder);
  assert(ordered == 1 && visited == 9);

  splay_tree_destroy(tree, NULL);
  free(nodes);
  splay_tree_destroy(clone, destroy_node);
  printf("ok\n");
  return 0;
}
