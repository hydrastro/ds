#include "../lib/bplus_tree.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct int_node {
  ds_bplus_tree_node_t node;
  int value;
} int_node_t;

static int last_seen;
static int ordered;
static int visited;
static int range_count;

static int compare_nodes(ds_bplus_tree_node_t *a, ds_bplus_tree_node_t *b) {
  return CAST(a, int_node_t)->value - CAST(b, int_node_t)->value;
}

static void check_order(ds_bplus_tree_node_t *node) {
  int v = CAST(node, int_node_t)->value;
  if (visited > 0 && v <= last_seen) {
    ordered = 0;
  }
  last_seen = v;
  visited += 1;
}

static void count_range(ds_bplus_tree_node_t *node) {
  (void)node;
  range_count += 1;
}

static ds_bplus_tree_node_t *clone_node(ds_bplus_tree_node_t *node) {
  int_node_t *copy = (int_node_t *)malloc(sizeof(int_node_t));
  copy->value = CAST(node, int_node_t)->value;
  return &copy->node;
}

static void destroy_node(ds_bplus_tree_node_t *node) { free(node); }

static void run_suite(bool bstar, const char *label) {
  int n = 400;
  int_node_t *nodes;
  int_node_t probe;
  int_node_t low;
  int_node_t high;
  ds_bplus_tree_t *tree;
  int i;

  tree = ds_bplus_tree_create_alloc(5, bstar, compare_nodes, malloc, free);
  assert(ds_bplus_tree_is_empty(tree));
  nodes = (int_node_t *)malloc(sizeof(int_node_t) * (size_t)n);

  /* insert in a scrambled order */
  for (i = 0; i < n; ++i) {
    nodes[i].value = (i * 137 + 7) % 1000;
    /* skip duplicates from the modulo */
    if (!ds_bplus_tree_insert(tree, &nodes[i].node)) {
      nodes[i].value = -1; /* mark as not inserted */
    }
  }
  printf("[%s] size: %lu\n", label, (unsigned long)ds_bplus_tree_size(tree));

  /* ordered leaf scan */
  ordered = 1;
  visited = 0;
  ds_bplus_tree_walk_forward(tree, check_order);
  printf("[%s] ordered scan: %s, visited: %d\n", label, ordered ? "yes" : "no",
         visited);
  assert(ordered == 1);
  assert((size_t)visited == ds_bplus_tree_size(tree));

  /* search hits and misses */
  probe.value = (5 * 137 + 7) % 1000;
  assert(ds_bplus_tree_search(tree, &probe.node) != tree->nil);
  probe.value = 100000;
  assert(ds_bplus_tree_search(tree, &probe.node) == tree->nil);

  /* min/max */
  printf("[%s] min: %d max: %d\n", label,
         CAST(ds_bplus_tree_minimum(tree), int_node_t)->value,
         CAST(ds_bplus_tree_maximum(tree), int_node_t)->value);

  /* range scan [200, 400] */
  low.value = 200;
  high.value = 400;
  range_count = 0;
  ds_bplus_tree_range_walk(tree, &low.node, &high.node, count_range);
  printf("[%s] range [200,400] count: %d\n", label, range_count);
  assert(range_count > 0);

  /* delete ~half, re-verify ordering and tree invariants */
  for (i = 0; i < n; i += 2) {
    if (nodes[i].value >= 0) {
      ds_bplus_tree_remove(tree, &nodes[i].node, NULL);
    }
  }
  ordered = 1;
  visited = 0;
  ds_bplus_tree_walk_forward(tree, check_order);
  printf("[%s] after deletions ordered: %s size: %lu\n", label,
         ordered ? "yes" : "no", (unsigned long)ds_bplus_tree_size(tree));
  assert(ordered == 1);
  assert((size_t)visited == ds_bplus_tree_size(tree));

  /* deleted keys must be gone, survivors must remain */
  for (i = 0; i < n; ++i) {
    if (nodes[i].value < 0) {
      continue;
    }
    probe.value = nodes[i].value;
    if (i % 2 == 0) {
      assert(ds_bplus_tree_search(tree, &probe.node) == tree->nil);
    } else {
      assert(ds_bplus_tree_search(tree, &probe.node) != tree->nil);
    }
  }

  ds_bplus_tree_destroy(tree, NULL);
  free(nodes);
}

int main(void) {
  ds_bplus_tree_t *tree;
  ds_bplus_tree_t *clone;
  int_node_t small[10];
  int i;

  run_suite(false, "B+");
  run_suite(true, "B*");

  /* clone test on a small B+ tree */
  tree = ds_bplus_tree_create(4, compare_nodes);
  for (i = 0; i < 10; ++i) {
    small[i].value = i;
    ds_bplus_tree_insert(tree, &small[i].node);
  }
  clone = ds_bplus_tree_clone(tree, clone_node);
  ordered = 1;
  visited = 0;
  ds_bplus_tree_walk_forward(clone, check_order);
  assert(ordered == 1 && visited == 10);
  printf("clone visited: %d\n", visited);
  ds_bplus_tree_destroy(tree, NULL);
  ds_bplus_tree_destroy(clone, destroy_node);

  printf("ok\n");
  return 0;
}
