#include "../lib/order_statistic_tree.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct item {
  ds_ost_node_t node;
  int key;
} item_t;

static int compare_items(ds_ost_node_t *a, ds_ost_node_t *b) {
  return CAST(a, item_t)->key - CAST(b, item_t)->key;
}

static int walk_prev;
static int walk_ordered;
static int walk_count;

static void check_order(ds_ost_node_t *node) {
  int v = CAST(node, item_t)->key;
  if (walk_count > 0 && v <= walk_prev) {
    walk_ordered = 0;
  }
  walk_prev = v;
  walk_count += 1;
}

static int cmp_int(const void *a, const void *b) {
  return *(const int *)a - *(const int *)b;
}

int main(void) {
  int n = 2000;
  item_t *items = (item_t *)malloc(sizeof(item_t) * (size_t)n);
  int *sorted = (int *)malloc(sizeof(int) * (size_t)n);
  ds_ost_t *tree = ds_ost_create(compare_items);
  int count = 0;
  int i;
  item_t probe;

  /* insert unique scrambled keys */
  for (i = 0; i < n; ++i) {
    int k = (int)(((unsigned)i * 2654435761U) >> 8) % 100000;
    items[count].key = k;
    if (ds_ost_insert(tree, &items[count].node)) {
      sorted[count] = k;
      count += 1;
    }
  }
  qsort(sorted, (size_t)count, sizeof(int), cmp_int);
  printf("inserted %d unique keys\n", count);
  assert(ds_ost_size(tree) == (size_t)count);

  /* in-order walk is sorted */
  walk_count = 0;
  walk_ordered = 1;
  ds_ost_inorder_walk(tree, check_order);
  assert(walk_ordered == 1);
  assert(walk_count == count);

  /* select(k) must equal sorted[k] */
  for (i = 0; i < count; i += 13) {
    ds_ost_node_t *s = ds_ost_select(tree, (size_t)i);
    assert(s != tree->nil);
    assert(CAST(s, item_t)->key == sorted[i]);
  }
  printf("select verified\n");

  /* rank(x) must equal index in sorted order */
  for (i = 0; i < count; i += 13) {
    probe.key = sorted[i];
    assert(ds_ost_rank(tree, &probe.node) == (size_t)i);
  }
  printf("rank verified\n");

  /* select out of range -> nil */
  assert(ds_ost_select(tree, (size_t)count) == tree->nil);

  /* min/max */
  assert(CAST(ds_ost_minimum(tree), item_t)->key == sorted[0]);
  assert(CAST(ds_ost_maximum(tree), item_t)->key == sorted[count - 1]);

  /* remove a quarter, re-verify select/rank consistency */
  for (i = 0; i < count; i += 4) {
    probe.key = sorted[i];
    ds_ost_remove(tree, &probe.node, NULL);
  }
  walk_count = 0;
  walk_ordered = 1;
  ds_ost_inorder_walk(tree, check_order);
  assert(walk_ordered == 1);
  assert((size_t)walk_count == ds_ost_size(tree));
  printf("after deletions size=%lu, still ordered\n",
         (unsigned long)ds_ost_size(tree));

  /* select(0) should be the smallest surviving element */
  {
    ds_ost_node_t *first = ds_ost_select(tree, 0);
    assert(first == ds_ost_minimum(tree));
  }

  ds_ost_destroy(tree, NULL);
  free(items);
  free(sorted);
  printf("ok\n");
  return 0;
}
