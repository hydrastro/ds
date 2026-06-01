#include "../lib/skip_list.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct int_node {
  ds_skip_list_node_t node;
  int value;
} int_node_t;

static int_node_t *create_int_node(int value) {
  int_node_t *new_node = (int_node_t *)malloc(sizeof(int_node_t));
  skip_list_node_init(&new_node->node);
  new_node->value = value;
  return new_node;
}

static int compare_nodes(ds_skip_list_node_t *a, ds_skip_list_node_t *b) {
  int av = CAST(a, int_node_t)->value;
  int bv = CAST(b, int_node_t)->value;
  if (av < bv) {
    return -1;
  }
  if (av > bv) {
    return 1;
  }
  return 0;
}

static void destroy_node(ds_skip_list_node_t *node) {
  free(node);
}

static ds_skip_list_node_t *clone_node(ds_skip_list_node_t *node) {
  return &create_int_node(CAST(node, int_node_t)->value)->node;
}

static int expect_sorted_and_count(ds_skip_list_t *list) {
  ds_skip_list_node_t *current = skip_list_first(list);
  int previous = -2147483647 - 1;
  int count = 0;
  while (current != list->nil) {
    int value = CAST(current, int_node_t)->value;
    if (value < previous) {
      printf("FAIL: order violated %d after %d\n", value, previous);
      return -1;
    }
    previous = value;
    count += 1;
    current = skip_list_next(list, current);
  }
  return count;
}

int main(void) {
  int input[] = {5, 1, 9, 3, 7, 2, 8, 4, 6, 0};
  int n = (int)(sizeof(input) / sizeof(input[0]));
  int i;
  int count;
  int_node_t probe;
  ds_skip_list_node_t *found;
  ds_skip_list_t *list = skip_list_create(compare_nodes);
  ds_skip_list_t *clone;

  for (i = 0; i < n; ++i) {
    skip_list_insert(list, &create_int_node(input[i])->node);
  }
  printf("size after insert: %lu\n", (unsigned long)list->size);

  count = expect_sorted_and_count(list);
  printf("in-order count: %d\n", count);

  probe.value = 7;
  found = skip_list_search(list, &probe.node);
  printf("search 7: %s\n", found != list->nil ? "found" : "missing");

  probe.value = 42;
  found = skip_list_search(list, &probe.node);
  printf("search 42: %s\n", found != list->nil ? "found" : "missing");

  clone = skip_list_clone(list, clone_node);
  printf("clone size: %lu\n", (unsigned long)clone->size);
  printf("clone in-order count: %d\n", expect_sorted_and_count(clone));

  probe.value = 3;
  found = skip_list_remove(list, &probe.node);
  if (found != list->nil) {
    destroy_node(found);
  }
  probe.value = 0;
  found = skip_list_remove(list, &probe.node);
  if (found != list->nil) {
    destroy_node(found);
  }
  probe.value = 9;
  found = skip_list_remove(list, &probe.node);
  if (found != list->nil) {
    destroy_node(found);
  }
  printf("size after 3 removals: %lu\n", (unsigned long)list->size);
  printf("post-removal in-order count: %d\n", expect_sorted_and_count(list));

  printf("empty? %s\n", skip_list_is_empty(list) ? "yes" : "no");

  skip_list_destroy(list, destroy_node);
  skip_list_destroy(clone, destroy_node);

  return 0;
}
