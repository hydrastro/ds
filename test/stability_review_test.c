#include "../ds.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct int_avl_node {
  ds_avl_node_t node;
  int key;
} int_avl_node_t;

typedef struct int_btree_node {
  ds_btree_node_t node;
  int key;
} int_btree_node_t;

typedef struct int_heap_node {
  ds_heap_node_t node;
  int key;
} int_heap_node_t;

typedef struct int_list_node {
  ds_list_node_t node;
  int key;
} int_list_node_t;

typedef struct int_dlist_node {
  ds_dlist_node_t node;
  int key;
} int_dlist_node_t;

typedef struct int_queue_node {
  ds_queue_node_t node;
  int key;
} int_queue_node_t;

static int avl_compare(ds_avl_node_t *a, ds_avl_node_t *b) {
  return ((int_avl_node_t *)a)->key - ((int_avl_node_t *)b)->key;
}

static void avl_destroy(ds_avl_node_t *node) { free(node); }

static int avl_validate_rec(ds_avl_t *tree, ds_avl_node_t *node, int *prev,
                            int *height) {
  int left_height;
  int right_height;
  int balance;

  if (node == tree->nil) {
    *height = 0;
    return 1;
  }
  if (!avl_validate_rec(tree, node->left, prev, &left_height)) {
    return 0;
  }
  if (*prev != -1000000 && ((int_avl_node_t *)node)->key <= *prev) {
    return 0;
  }
  *prev = ((int_avl_node_t *)node)->key;
  if (!avl_validate_rec(tree, node->right, prev, &right_height)) {
    return 0;
  }
  balance = left_height - right_height;
  if (balance < -1 || balance > 1) {
    return 0;
  }
  if (node->height != 1 + (left_height > right_height ? left_height : right_height)) {
    return 0;
  }
  *height = node->height;
  return 1;
}

static int avl_validate(ds_avl_t *tree) {
  int prev;
  int height;
  prev = -1000000;
  height = 0;
  return avl_validate_rec(tree, tree->root, &prev, &height);
}

static int postorder_values[3];
static size_t postorder_count;
static void postorder_collect(ds_avl_node_t *node) {
  assert(postorder_count < 3U);
  postorder_values[postorder_count++] = ((int_avl_node_t *)node)->key;
}

static int btree_compare(ds_btree_node_t *a, ds_btree_node_t *b) {
  return ((int_btree_node_t *)a)->key - ((int_btree_node_t *)b)->key;
}

static void btree_destroy(ds_btree_node_t *node) { free(node); }
static void btree_walk_noop(ds_btree_node_t *node) { (void)node; }

static int heap_compare(ds_heap_node_t *a, ds_heap_node_t *b) {
  return ((int_heap_node_t *)a)->key - ((int_heap_node_t *)b)->key;
}

static void heap_node_destroy(ds_heap_node_t *node) { free(node); }

static void test_format_bounds(void) {
  ds_error_t error;
  ds_diagnostic_t diagnostic;
  char buffer[8];
  int n;

  ds_error_set(&error, DS_ERR_INVALID, "ds.test/really_long_error_id",
               "review", "this message is intentionally long", "long/file.c",
               1234UL, "long_function_name");
  memset(buffer, 'x', sizeof(buffer));
  n = ds_error_format(&error, buffer, sizeof(buffer));
  assert(n > 0);
  assert(buffer[sizeof(buffer) - 1U] == '\0');

  diagnostic.id = "ds.test/really_long_diagnostic_id";
  diagnostic.level = DS_DIAG_WARNING;
  diagnostic.module = "review";
  diagnostic.message = "this diagnostic is intentionally long";
  diagnostic.file = "long/file.c";
  diagnostic.line = 1234UL;
  diagnostic.function = "long_function_name";
  diagnostic.args = NULL;
  diagnostic.arg_count = 0U;
  memset(buffer, 'x', sizeof(buffer));
  n = ds_diagnostic_format(&diagnostic, buffer, sizeof(buffer));
  assert(n > 0);
  assert(buffer[sizeof(buffer) - 1U] == '\0');
}

static void test_list_sizes(void) {
  ds_list_t *list;
  ds_dlist_t *dlist;
  int_list_node_t a;
  int_dlist_node_t b;

  list = list_create();
  list_append(list, &a.node);
  assert(list->size == 1U);
  list_delete(list);
  assert(list->size == 0U);
  list_destroy(list, NULL);

  dlist = dlist_create();
  dlist_append(dlist, &b.node);
  assert(dlist->size == 1U);
  dlist_delete(dlist);
  assert(dlist->size == 0U);
  assert(dlist_is_empty(dlist));
  dlist_destroy(dlist, NULL);
}

static void test_queue_foreign_delete(void) {
  ds_queue_t *queue;
  int_queue_node_t a;
  int_queue_node_t foreign;

  queue = queue_create();
  queue_enqueue(queue, &a.node);
  assert(queue->size == 1U);
  queue_delete_node(queue, &foreign.node);
  assert(queue->size == 1U);
  queue_destroy(queue, NULL);
}

static void test_heap_indices(void) {
  ds_heap_t *heap;
  size_t i;
  int_heap_node_t *node;
  int values[32];

  heap = heap_create(0U);
  for (i = 0U; i < 32U; i++) {
    values[i] = (int)((i * 17U + 11U) % 97U);
    node = (int_heap_node_t *)malloc(sizeof(*node));
    assert(node != NULL);
    node->key = values[i];
    heap_insert(heap, &node->node, heap_compare);
  }
  while (!heap_is_empty(heap)) {
    node = (int_heap_node_t *)heap_extract_root(heap, heap_compare);
    free(node);
    for (i = 0U; i < heap->size; i++) {
      assert(heap->data[i]->index == i);
    }
  }
  heap_destroy(heap, heap_node_destroy);
}

static void test_avl_regressions(void) {
  ds_avl_t *tree;
  int values[] = {50, 25, 75, 10, 40, 30, 45, 5, 15, 35, 42, 43, 44};
  size_t i;
  int_avl_node_t *node;

  tree = avl_create();
  for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++) {
    node = (int_avl_node_t *)malloc(sizeof(*node));
    assert(node != NULL);
    node->key = values[i];
    avl_insert(tree, &node->node, avl_compare);
    assert(avl_validate(tree));
  }
  avl_destroy_tree(tree, avl_destroy);

  tree = avl_create();
  for (i = 0U; i < 3U; i++) {
    node = (int_avl_node_t *)malloc(sizeof(*node));
    assert(node != NULL);
    node->key = (int)i + 1;
    avl_insert(tree, &node->node, avl_compare);
  }
  postorder_count = 0U;
  avl_postorder_walk_tree(tree, postorder_collect);
  assert(postorder_count == 3U);
  assert(postorder_values[2] == ((int_avl_node_t *)tree->root)->key);
  avl_destroy_tree(tree, avl_destroy);
}

static void test_btree_size(void) {
  ds_btree_t *tree;
  int_btree_node_t *nodes[5];
  size_t i;

  tree = btree_create(2);
  for (i = 0U; i < 5U; i++) {
    nodes[i] = (int_btree_node_t *)malloc(sizeof(*nodes[i]));
    assert(nodes[i] != NULL);
    nodes[i]->key = (int)i;
    btree_insert(tree, &nodes[i]->node, btree_compare);
  }
  assert(tree->size == 5U);
  btree_delete_node(tree, &nodes[2]->node);
  free(nodes[2]);
  assert(tree->size == 4U);
  btree_inorder_walk_tree(tree, btree_walk_noop);
  btree_preorder_walk_tree(tree, btree_walk_noop);
  btree_postorder_walk_tree(tree, btree_walk_noop);
  btree_destroy_tree(tree, btree_destroy);
}

static void test_string_map_update(void) {
  ds_string_map_t *map;
  void *value;
  size_t i;

  map = ds_string_map_create();
  assert(map != NULL);
  for (i = 0U; i < 1000U; i++) {
    assert(ds_string_map_put(map, "same-key", (void *)(size_t)i) == DS_OK);
  }
  assert(ds_string_map_get(map, "same-key", &value) == DS_OK);
  assert((size_t)value == 999U);
  ds_string_map_destroy(map);
}

int main(void) {
  test_format_bounds();
  test_list_sizes();
  test_queue_foreign_delete();
  test_heap_indices();
  test_avl_regressions();
  test_btree_size();
  test_string_map_update();
  return 0;
}
