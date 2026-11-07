#include "../lib/treap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct int_node {
  ds_treap_node_t node;
  int value;
} int_node_t;

static int last_seen;
static int ordered;
static int visited;
static int heap_ok;

static int compare_nodes(ds_treap_node_t *a, ds_treap_node_t *b) {
  return CAST(a, int_node_t)->value - CAST(b, int_node_t)->value;
}

static void check_inorder(ds_treap_node_t *node) {
  int v = CAST(node, int_node_t)->value;
  if (visited > 0 && v < last_seen) {
    ordered = 0;
  }
  last_seen = v;
  visited += 1;
}

static ds_treap_t *global_treap;

static void check_heap(ds_treap_node_t *node) {
  if (node->left != global_treap->nil &&
      node->left->priority > node->priority) {
    heap_ok = 0;
  }
  if (node->right != global_treap->nil &&
      node->right->priority > node->priority) {
    heap_ok = 0;
  }
}

static ds_treap_node_t *clone_node(ds_treap_node_t *node) {
  int_node_t *copy = (int_node_t *)malloc(sizeof(int_node_t));
  ds_treap_node_init(&copy->node);
  copy->value = CAST(node, int_node_t)->value;
  return &copy->node;
}

static void destroy_node(ds_treap_node_t *node) { free(node); }

int main(void) {
  int n = 64;
  int_node_t *nodes;
  int_node_t probe;
  ds_treap_t *treap = ds_treap_create(compare_nodes);
  ds_treap_t *clone;
  int i;

  assert(ds_treap_is_empty(treap));
  global_treap = treap;
  nodes = (int_node_t *)malloc(sizeof(int_node_t) * (size_t)n);
  for (i = 0; i < n; ++i) {
    ds_treap_node_init(&nodes[i].node);
    nodes[i].value = (i * 37 + 11) % 211;
    ds_treap_insert(treap, &nodes[i].node);
  }
  printf("size: %lu\n", (unsigned long)treap->size);

  ordered = 1;
  visited = 0;
  ds_treap_inorder_walk_tree(treap, check_inorder);
  printf("inorder sorted: %s\n", ordered ? "yes" : "no");
  assert(ordered == 1);

  heap_ok = 1;
  ds_treap_inorder_walk_tree(treap, check_heap);
  printf("heap property: %s\n", heap_ok ? "yes" : "no");
  assert(heap_ok == 1);

  probe.value = (5 * 37 + 11) % 211;
  assert(ds_treap_search(treap, &probe.node) != treap->nil);
  probe.value = 9999;
  assert(ds_treap_search(treap, &probe.node) == treap->nil);

  clone = ds_treap_clone(treap, clone_node);
  global_treap = clone;
  heap_ok = 1;
  ds_treap_inorder_walk_tree(clone, check_heap);
  assert(heap_ok == 1);
  global_treap = treap;

  /* remove a handful, re-check invariants */
  for (i = 0; i < n; i += 5) {
    probe.value = nodes[i].value;
    ds_treap_remove(treap, &probe.node);
  }
  ordered = 1;
  visited = 0;
  ds_treap_inorder_walk_tree(treap, check_inorder);
  heap_ok = 1;
  ds_treap_inorder_walk_tree(treap, check_heap);
  printf("after removals sorted: %s heap: %s size: %lu\n",
         ordered ? "yes" : "no", heap_ok ? "yes" : "no",
         (unsigned long)treap->size);
  assert(ordered == 1 && heap_ok == 1);

  ds_treap_destroy(treap, NULL);
  free(nodes);
  ds_treap_destroy(clone, destroy_node);
  printf("ok\n");
  return 0;
}
