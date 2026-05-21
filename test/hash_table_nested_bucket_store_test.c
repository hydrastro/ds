#include "../lib/hash_table.h"
#include "../lib/rbt.h"
#include "../lib/trie.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORD_COUNT 12

typedef enum rbt_bucket_key_kind {
  RBT_BUCKET_INT_KEY,
  RBT_BUCKET_SLICE_KEY
} rbt_bucket_key_kind_t;

typedef struct rbt_bucket_node {
  ds_rbt_node_t tree_node;
  int int_key;
  size_t slice_key;
  ds_hash_node_t *hash_node;
} rbt_bucket_node_t;

typedef struct hash_table_bucket_destroy_context {
  ds_hash_table_t *owner;
  void (*destroy)(ds_hash_node_t *);
} hash_table_bucket_destroy_context_t;

typedef struct hash_table_bucket_apply_context {
  void (*callback)(ds_hash_node_t *, void *);
  void *args;
} hash_table_bucket_apply_context_t;

typedef struct trie_nested_apply_context {
  ds_trie_t *trie;
  void (*callback)(ds_trie_t *, ds_trie_node_t *, va_list *);
  va_list *args;
} trie_nested_apply_context_t;

static const ds_hash_bucket_store_t rbt_int_bucket_store;
static const ds_hash_bucket_store_t rbt_slice_bucket_store;
static const ds_hash_bucket_store_t hash_table_int_bucket_store;
static const ds_hash_bucket_store_t hash_table_slice_bucket_store;

static size_t constant_hash(void *key) {
  (void)key;
  return 0u;
}

static int compare_int(void *a, void *b) {
  int int_a;
  int int_b;
  int_a = *(int *)a;
  int_b = *(int *)b;
  if (int_a < int_b) {
    return -1;
  }
  if (int_a > int_b) {
    return 1;
  }
  return 0;
}

static int compare_slice(void *a, void *b) {
  size_t slice_a;
  size_t slice_b;
  slice_a = (size_t)a;
  slice_b = (size_t)b;
  if (slice_a < slice_b) {
    return -1;
  }
  if (slice_a > slice_b) {
    return 1;
  }
  return 0;
}

static void destroy_int_node(ds_hash_node_t *node) {
  free(node->key);
  free(node->value);
}

static rbt_bucket_node_t *rbt_bucket_node_from_tree_node(ds_rbt_node_t *node) {
  return (rbt_bucket_node_t *)node;
}

static void rbt_bucket_set_key(rbt_bucket_node_t *node, void *key,
                               rbt_bucket_key_kind_t key_kind) {
  if (key_kind == RBT_BUCKET_INT_KEY) {
    node->int_key = *(int *)key;
    node->slice_key = 0u;
  } else {
    node->int_key = 0;
    node->slice_key = (size_t)key;
  }
}

static int rbt_int_compare_nodes(ds_rbt_node_t *a, ds_rbt_node_t *b) {
  rbt_bucket_node_t *node_a;
  rbt_bucket_node_t *node_b;
  node_a = rbt_bucket_node_from_tree_node(a);
  node_b = rbt_bucket_node_from_tree_node(b);
  if (node_a->int_key < node_b->int_key) {
    return -1;
  }
  if (node_a->int_key > node_b->int_key) {
    return 1;
  }
  return 0;
}

static int rbt_slice_compare_nodes(ds_rbt_node_t *a, ds_rbt_node_t *b) {
  rbt_bucket_node_t *node_a;
  rbt_bucket_node_t *node_b;
  node_a = rbt_bucket_node_from_tree_node(a);
  node_b = rbt_bucket_node_from_tree_node(b);
  if (node_a->slice_key < node_b->slice_key) {
    return -1;
  }
  if (node_a->slice_key > node_b->slice_key) {
    return 1;
  }
  return 0;
}

static ds_rbt_node_t *rbt_bucket_find_tree_node(
    void *bucket, void *key, rbt_bucket_key_kind_t key_kind,
    int (*compare_nodes)(ds_rbt_node_t *, ds_rbt_node_t *)) {
  ds_rbt_t *tree;
  ds_hash_node_t probe_hash_node;
  rbt_bucket_node_t probe_node;
  ds_rbt_node_t *found;
  tree = (ds_rbt_t *)bucket;
  probe_hash_node.key = key;
  probe_hash_node.value = NULL;
  probe_hash_node.next = NULL;
  probe_hash_node.list_next = NULL;
  probe_hash_node.list_prev = NULL;
  probe_node.tree_node.parent_color = RBT_BLACK;
  probe_node.tree_node.left = NULL;
  probe_node.tree_node.right = NULL;
  rbt_bucket_set_key(&probe_node, key, key_kind);
  probe_node.hash_node = &probe_hash_node;
  found = rbt_search(tree, tree->root, &probe_node.tree_node, compare_nodes);
  if (found == tree->nil) {
    return NULL;
  }
  return found;
}

static ds_hash_node_t *rbt_bucket_search_with_compare(
    void *bucket, void *key, rbt_bucket_key_kind_t key_kind,
    int (*compare_nodes)(ds_rbt_node_t *, ds_rbt_node_t *)) {
  ds_rbt_node_t *found;
  found = rbt_bucket_find_tree_node(bucket, key, key_kind, compare_nodes);
  if (found == NULL) {
    return NULL;
  }
  return rbt_bucket_node_from_tree_node(found)->hash_node;
}

static void rbt_bucket_insert_with_compare(
    ds_hash_table_t *table, void **bucket, ds_hash_node_t *node,
    rbt_bucket_key_kind_t key_kind,
    int (*compare_nodes)(ds_rbt_node_t *, ds_rbt_node_t *)) {
  ds_rbt_t *tree;
  rbt_bucket_node_t *bucket_node;
  tree = (ds_rbt_t *)*bucket;
  ds_rbt_node_t *existing;
  existing = rbt_bucket_find_tree_node(*bucket, node->key, key_kind,
                                       compare_nodes);
  if (existing != NULL) {
    bucket_node = rbt_bucket_node_from_tree_node(existing);
    rbt_bucket_set_key(bucket_node, node->key, key_kind);
    bucket_node->hash_node = node;
    return;
  }
  bucket_node = (rbt_bucket_node_t *)table->allocator(sizeof(*bucket_node));
  bucket_node->tree_node.parent_color = RBT_BLACK;
  bucket_node->tree_node.left = tree->nil;
  bucket_node->tree_node.right = tree->nil;
  rbt_bucket_set_key(bucket_node, node->key, key_kind);
  bucket_node->hash_node = node;
  rbt_insert(tree, &bucket_node->tree_node, compare_nodes);
}

static ds_hash_node_t *rbt_bucket_remove_with_compare(
    ds_hash_table_t *table, void **bucket, void *key,
    rbt_bucket_key_kind_t key_kind,
    int (*compare_nodes)(ds_rbt_node_t *, ds_rbt_node_t *)) {
  ds_rbt_t *tree;
  ds_hash_node_t *hash_node;
  ds_rbt_node_t *found;
  rbt_bucket_node_t *bucket_node;
  tree = (ds_rbt_t *)*bucket;
  found = rbt_bucket_find_tree_node(*bucket, key, key_kind, compare_nodes);
  if (found == NULL) {
    return NULL;
  }
  bucket_node = rbt_bucket_node_from_tree_node(found);
  hash_node = bucket_node->hash_node;
  rbt_delete_node(tree, found);
  table->deallocator(bucket_node);
  return hash_node;
}

static void rbt_bucket_destroy_recursive(ds_hash_table_t *table,
                                         ds_rbt_t *tree,
                                         ds_rbt_node_t *node,
                                         void (*destroy)(ds_hash_node_t *)) {
  rbt_bucket_node_t *bucket_node;
  if (node == tree->nil) {
    return;
  }
  rbt_bucket_destroy_recursive(table, tree, node->left, destroy);
  rbt_bucket_destroy_recursive(table, tree, node->right, destroy);
  bucket_node = rbt_bucket_node_from_tree_node(node);
  if (bucket_node->hash_node != NULL) {
    if (destroy != NULL) {
      destroy(bucket_node->hash_node);
    }
    table->deallocator(bucket_node->hash_node);
  }
  table->deallocator(bucket_node);
}

static void rbt_bucket_apply_recursive(ds_rbt_t *tree, ds_rbt_node_t *node,
                                       void (*callback)(ds_hash_node_t *,
                                                        void *),
                                       void *args) {
  if (node == tree->nil) {
    return;
  }
  rbt_bucket_apply_recursive(tree, node->left, callback, args);
  if (rbt_bucket_node_from_tree_node(node)->hash_node != NULL) {
    callback(rbt_bucket_node_from_tree_node(node)->hash_node, args);
  }
  rbt_bucket_apply_recursive(tree, node->right, callback, args);
}

static void *rbt_bucket_create(ds_hash_table_t *table) {
  return rbt_create_alloc(table->allocator, table->deallocator);
}

static ds_hash_node_t *rbt_int_bucket_search(ds_hash_table_t *table,
                                             void *bucket, void *key,
                                             int (*compare)(void *, void *)) {
  (void)table;
  (void)compare;
  return rbt_bucket_search_with_compare(bucket, key, RBT_BUCKET_INT_KEY,
                                        rbt_int_compare_nodes);
}

static ds_hash_node_t *rbt_slice_bucket_search(
    ds_hash_table_t *table, void *bucket, void *key,
    int (*compare)(void *, void *)) {
  (void)table;
  (void)compare;
  return rbt_bucket_search_with_compare(bucket, key, RBT_BUCKET_SLICE_KEY,
                                        rbt_slice_compare_nodes);
}

static void rbt_int_bucket_insert(ds_hash_table_t *table, void **bucket,
                                  ds_hash_node_t *node,
                                  int (*compare)(void *, void *)) {
  (void)compare;
  rbt_bucket_insert_with_compare(table, bucket, node, RBT_BUCKET_INT_KEY,
                                 rbt_int_compare_nodes);
}

static void rbt_slice_bucket_insert(ds_hash_table_t *table, void **bucket,
                                    ds_hash_node_t *node,
                                    int (*compare)(void *, void *)) {
  (void)compare;
  rbt_bucket_insert_with_compare(table, bucket, node, RBT_BUCKET_SLICE_KEY,
                                 rbt_slice_compare_nodes);
}

static ds_hash_node_t *rbt_int_bucket_remove(
    ds_hash_table_t *table, void **bucket, void *key,
    int (*compare)(void *, void *)) {
  (void)compare;
  return rbt_bucket_remove_with_compare(table, bucket, key,
                                        RBT_BUCKET_INT_KEY,
                                        rbt_int_compare_nodes);
}

static ds_hash_node_t *rbt_slice_bucket_remove(
    ds_hash_table_t *table, void **bucket, void *key,
    int (*compare)(void *, void *)) {
  (void)compare;
  return rbt_bucket_remove_with_compare(table, bucket, key,
                                        RBT_BUCKET_SLICE_KEY,
                                        rbt_slice_compare_nodes);
}

static void rbt_bucket_destroy(ds_hash_table_t *table, void *bucket,
                               void (*destroy)(ds_hash_node_t *)) {
  ds_rbt_t *tree;
  tree = (ds_rbt_t *)bucket;
  rbt_bucket_destroy_recursive(table, tree, tree->root, destroy);
  table->deallocator(tree->nil);
  table->deallocator(tree);
}

static void rbt_bucket_apply(ds_hash_table_t *table, void *bucket,
                             void (*callback)(ds_hash_node_t *, void *),
                             void *args) {
  ds_rbt_t *tree;
  (void)table;
  tree = (ds_rbt_t *)bucket;
  rbt_bucket_apply_recursive(tree, tree->root, callback, args);
}

static void *hash_table_int_bucket_create(ds_hash_table_t *table) {
  return hash_table_create_chain_alloc(3, &rbt_int_bucket_store,
                                       table->allocator, table->deallocator);
}

static void *hash_table_slice_bucket_create(ds_hash_table_t *table) {
  return hash_table_create_chain_alloc(3, &rbt_slice_bucket_store,
                                       table->allocator, table->deallocator);
}

static ds_hash_node_t *hash_table_int_bucket_search(
    ds_hash_table_t *table, void *bucket, void *key,
    int (*compare)(void *, void *)) {
  ds_hash_table_t *inner;
  void *result;
  (void)table;
  (void)compare;
  inner = (ds_hash_table_t *)bucket;
  result = hash_table_lookup(inner, key, constant_hash, compare_int);
  if (result == inner->nil) {
    return NULL;
  }
  return (ds_hash_node_t *)result;
}

static ds_hash_node_t *hash_table_slice_bucket_search(
    ds_hash_table_t *table, void *bucket, void *key,
    int (*compare)(void *, void *)) {
  ds_hash_table_t *inner;
  void *result;
  (void)table;
  (void)compare;
  inner = (ds_hash_table_t *)bucket;
  result = hash_table_lookup(inner, key, constant_hash, compare_slice);
  if (result == inner->nil) {
    return NULL;
  }
  return (ds_hash_node_t *)result;
}

static void hash_table_int_bucket_insert(ds_hash_table_t *table, void **bucket,
                                         ds_hash_node_t *node,
                                         int (*compare)(void *, void *)) {
  ds_hash_table_t *inner;
  (void)table;
  (void)compare;
  inner = (ds_hash_table_t *)*bucket;
  hash_table_insert(inner, node->key, node, constant_hash, compare_int);
}

static void hash_table_slice_bucket_insert(ds_hash_table_t *table,
                                           void **bucket, ds_hash_node_t *node,
                                           int (*compare)(void *, void *)) {
  ds_hash_table_t *inner;
  (void)table;
  (void)compare;
  inner = (ds_hash_table_t *)*bucket;
  hash_table_insert(inner, node->key, node, constant_hash, compare_slice);
}

static ds_hash_node_t *hash_table_int_bucket_remove(
    ds_hash_table_t *table, void **bucket, void *key,
    int (*compare)(void *, void *)) {
  ds_hash_table_t *inner;
  ds_hash_node_t *outer_node;
  (void)table;
  (void)compare;
  inner = (ds_hash_table_t *)*bucket;
  outer_node = hash_table_int_bucket_search(table, inner, key, compare_int);
  if (outer_node == NULL) {
    return NULL;
  }
  hash_table_remove(inner, key, constant_hash, compare_int, NULL);
  return outer_node;
}

static ds_hash_node_t *hash_table_slice_bucket_remove(
    ds_hash_table_t *table, void **bucket, void *key,
    int (*compare)(void *, void *)) {
  ds_hash_table_t *inner;
  ds_hash_node_t *outer_node;
  (void)table;
  (void)compare;
  inner = (ds_hash_table_t *)*bucket;
  outer_node = hash_table_slice_bucket_search(table, inner, key, compare_slice);
  if (outer_node == NULL) {
    return NULL;
  }
  hash_table_remove(inner, key, constant_hash, compare_slice, NULL);
  return outer_node;
}

static void hash_table_bucket_destroy_outer_node(ds_hash_node_t *inner_node,
                                                 void *args) {
  hash_table_bucket_destroy_context_t *context;
  ds_hash_node_t *outer_node;
  context = (hash_table_bucket_destroy_context_t *)args;
  outer_node = (ds_hash_node_t *)inner_node->value;
  if (context->destroy != NULL) {
    context->destroy(outer_node);
  }
  context->owner->deallocator(outer_node);
}

static void hash_table_bucket_destroy(ds_hash_table_t *table, void *bucket,
                                      void (*destroy)(ds_hash_node_t *)) {
  hash_table_bucket_destroy_context_t context;
  ds_hash_table_t *inner;
  inner = (ds_hash_table_t *)bucket;
  context.owner = table;
  context.destroy = destroy;
  hash_table_for_each(inner, hash_table_bucket_destroy_outer_node, &context);
  hash_table_destroy(inner, NULL);
}

static void hash_table_bucket_apply_inner_node(ds_hash_node_t *inner_node,
                                               void *args) {
  hash_table_bucket_apply_context_t *context;
  context = (hash_table_bucket_apply_context_t *)args;
  context->callback((ds_hash_node_t *)inner_node->value, context->args);
}

static void hash_table_bucket_apply(ds_hash_table_t *table, void *bucket,
                                    void (*callback)(ds_hash_node_t *, void *),
                                    void *args) {
  hash_table_bucket_apply_context_t context;
  ds_hash_table_t *inner;
  (void)table;
  inner = (ds_hash_table_t *)bucket;
  context.callback = callback;
  context.args = args;
  hash_table_for_each(inner, hash_table_bucket_apply_inner_node, &context);
}

static const ds_hash_bucket_store_t rbt_int_bucket_store = {
    rbt_bucket_create, rbt_int_bucket_search, rbt_int_bucket_insert,
    rbt_int_bucket_remove, rbt_bucket_destroy, rbt_bucket_apply};

static const ds_hash_bucket_store_t rbt_slice_bucket_store = {
    rbt_bucket_create, rbt_slice_bucket_search, rbt_slice_bucket_insert,
    rbt_slice_bucket_remove, rbt_bucket_destroy, rbt_bucket_apply};

static const ds_hash_bucket_store_t hash_table_int_bucket_store = {
    hash_table_int_bucket_create,
    hash_table_int_bucket_search,
    hash_table_int_bucket_insert,
    hash_table_int_bucket_remove,
    hash_table_bucket_destroy,
    hash_table_bucket_apply};

static const ds_hash_bucket_store_t hash_table_slice_bucket_store = {
    hash_table_slice_bucket_create,
    hash_table_slice_bucket_search,
    hash_table_slice_bucket_insert,
    hash_table_slice_bucket_remove,
    hash_table_bucket_destroy,
    hash_table_bucket_apply};

static size_t get_char_slice(void *data, size_t slice) {
  char *word;
  word = (char *)data;
  return (size_t)word[slice];
}

static bool has_char_slice(void *data, size_t slice) {
  char *word;
  word = (char *)data;
  return word[slice] != '\0';
}

static ds_trie_node_t *trie_nested_store_search(void *store, size_t slice) {
  ds_hash_table_t *table;
  void *result;
  table = (ds_hash_table_t *)store;
  result = hash_table_lookup(table, (void *)slice, constant_hash,
                             compare_slice);
  if (result == table->nil) {
    return NULL;
  }
  return (ds_trie_node_t *)result;
}

static void *trie_nested_store_create(ds_trie_t *trie, size_t size) {
  return hash_table_create_chain_alloc(size, &hash_table_slice_bucket_store,
                                       trie->allocator, trie->deallocator);
}

static void trie_nested_store_insert(void *store, ds_trie_node_t *node) {
  ds_hash_table_t *table;
  table = (ds_hash_table_t *)store;
  hash_table_insert(table, (void *)node->data_slice, node, constant_hash,
                    compare_slice);
}

static void trie_nested_store_destroy_entry(void *store, ds_trie_node_t *node) {
  ds_hash_table_t *table;
  table = (ds_hash_table_t *)store;
  hash_table_remove(table, (void *)node->data_slice, constant_hash,
                    compare_slice, NULL);
}

static void trie_nested_store_remove(void *store, ds_trie_node_t *node) {
  trie_nested_store_destroy_entry(store, node);
}

static void trie_nested_store_destroy(void *store) {
  hash_table_destroy((ds_hash_table_t *)store, NULL);
}

static size_t trie_nested_store_get_size(void *store) {
  return ((ds_hash_table_t *)store)->size;
}

static void trie_nested_store_apply_node(ds_hash_node_t *hash_node,
                                         void *args) {
  trie_nested_apply_context_t *context;
  ds_trie_node_t *node;
  context = (trie_nested_apply_context_t *)args;
  node = (ds_trie_node_t *)hash_node->value;
  if (context->args != NULL) {
    va_list args_copy1;
    va_list args_copy2;
    DS_VA_COPY(args_copy1, *context->args);
    DS_VA_COPY(args_copy2, *context->args);
    context->trie->store_apply(context->trie, node->children,
                               context->callback, &args_copy1);
    context->callback(context->trie, node, &args_copy2);
    va_end(args_copy1);
    va_end(args_copy2);
  } else {
    context->trie->store_apply(context->trie, node->children,
                               context->callback, NULL);
    context->callback(context->trie, node, NULL);
  }
}

static void trie_nested_store_apply(
    ds_trie_t *trie, void *store,
    void (*callback)(ds_trie_t *, ds_trie_node_t *, va_list *), va_list *args) {
  trie_nested_apply_context_t context;
  context.trie = trie;
  context.callback = callback;
  context.args = args;
  hash_table_for_each((ds_hash_table_t *)store, trie_nested_store_apply_node,
                      &context);
}

static void *trie_nested_store_clone(ds_trie_t *trie, void *store,
                                     ds_trie_node_t *parent_node,
                                     void *(*clone_data)(void *)) {
  ds_hash_table_t *table;
  ds_hash_table_t *new_table;
  ds_hash_node_t *cur;
  ds_hash_node_t *temp;
  ds_trie_node_t *node;
  ds_trie_node_t *new_node;
  table = (ds_hash_table_t *)store;
  new_table = hash_table_create_chain_alloc(table->capacity,
                                            &hash_table_slice_bucket_store,
                                            table->allocator,
                                            table->deallocator);
  cur = table->last_node;
  while (cur != NULL) {
    temp = cur->list_prev;
    node = (ds_trie_node_t *)cur->value;
    new_node = trie_clone_node(trie, node, parent_node, clone_data);
    trie_nested_store_insert(new_table, new_node);
    cur = temp;
  }
  return new_table;
}

static ds_trie_t *trie_create_hash_hash_rbt(size_t num_splits) {
  return trie_create(num_splits, trie_nested_store_search,
                     trie_nested_store_create, trie_nested_store_insert,
                     trie_nested_store_remove,
                     trie_nested_store_destroy_entry,
                     trie_nested_store_destroy,
                     trie_nested_store_get_size,
                     trie_nested_store_apply,
                     trie_nested_store_clone);
}

static bool trie_contains(ds_trie_t *trie, const char *word) {
  return trie_search(trie, (void *)word, get_char_slice, has_char_slice) !=
         NULL;
}

static void count_terminal_node(ds_trie_t *trie, ds_trie_node_t *node,
                                va_list *args) {
  size_t *count;
  (void)trie;
  count = va_arg(*args, size_t *);
  if (node->is_terminal) {
    (*count)++;
  }
}

static void *clone_string_data(void *data) {
  char *original;
  char *copy;
  size_t length;
  original = (char *)data;
  length = strlen(original) + 1u;
  copy = (char *)malloc(length);
  if (copy == NULL) {
    return NULL;
  }
  memcpy(copy, original, length);
  return copy;
}

static void destroy_cloned_terminal(ds_trie_t *trie, ds_trie_node_t *node,
                                    va_list *args) {
  (void)trie;
  (void)args;
  if (node->is_terminal) {
    free(node->terminal_data);
  }
}

static int test_nested_hash_table(void) {
  ds_hash_table_t *table;
  ds_hash_table_t *clone;
  int i;
  int lookup_key;
  int *key;
  int *value;
  int *result;

  table = hash_table_create_chain(3, &hash_table_int_bucket_store);
  for (i = 0; i < 256; i++) {
    key = (int *)malloc(sizeof(*key));
    value = (int *)malloc(sizeof(*value));
    if (key == NULL || value == NULL) {
      return 1;
    }
    *key = i;
    *value = i * 100;
    hash_table_insert(table, key, value, constant_hash, compare_int);
  }

  for (i = 0; i < 256; i++) {
    lookup_key = i;
    result = (int *)hash_table_lookup(table, &lookup_key, constant_hash,
                                      compare_int);
    if (result == table->nil || *result != i * 100) {
      return 2;
    }
  }

  for (i = 0; i < 128; i += 2) {
    lookup_key = i;
    hash_table_remove(table, &lookup_key, constant_hash, compare_int,
                      destroy_int_node);
  }

  for (i = 0; i < 128; i++) {
    lookup_key = i;
    result = (int *)hash_table_lookup(table, &lookup_key, constant_hash,
                                      compare_int);
    if (i % 2 == 0) {
      if (result != table->nil) {
        return 3;
      }
    } else if (result == table->nil || *result != i * 100) {
      return 4;
    }
  }

  clone = hash_table_clone_with(table, NULL, NULL, constant_hash, compare_int);
  if (clone == NULL || clone->size != table->size) {
    return 5;
  }
  lookup_key = 255;
  result = (int *)hash_table_lookup(clone, &lookup_key, constant_hash,
                                    compare_int);
  if (result == clone->nil || *result != 25500) {
    return 6;
  }

  hash_table_destroy(clone, NULL);
  hash_table_destroy(table, destroy_int_node);
  return 0;
}

static int test_trie_hash_hash_rbt(void) {
  const char *words[WORD_COUNT] = {"apple",      "app",      "ape",
                                   "banana",     "band",     "bandana",
                                   "grape",      "grapefruit", "graph",
                                   "orange",     "ornament", "orbit"};
  ds_trie_t *trie;
  ds_trie_t *clone;
  ds_trie_node_t *node;
  size_t count;
  size_t i;

  trie = trie_create_hash_hash_rbt(5);
  for (i = 0; i < WORD_COUNT; i++) {
    trie_insert(trie, (void *)words[i], get_char_slice, has_char_slice);
  }

  for (i = 0; i < WORD_COUNT; i++) {
    if (!trie_contains(trie, words[i])) {
      return 10;
    }
  }
  if (trie_contains(trie, "apples") || trie_contains(trie, "ban")) {
    return 11;
  }

  count = 0;
  trie_apply(trie, trie->root, count_terminal_node, &count);
  if (count != WORD_COUNT) {
    return 12;
  }

  clone = trie_clone(trie, clone_string_data);
  for (i = 0; i < WORD_COUNT; i++) {
    if (!trie_contains(clone, words[i])) {
      return 13;
    }
  }

  node = trie_search(trie, (void *)"grapefruit", get_char_slice,
                     has_char_slice);
  if (node == NULL) {
    return 14;
  }
  trie_delete_node(trie, node);
  if (trie_contains(trie, "grapefruit") || !trie_contains(trie, "grape") ||
      !trie_contains(trie, "graph")) {
    return 15;
  }
  if (!trie_contains(clone, "grapefruit")) {
    return 16;
  }

  trie_destroy_trie(trie, NULL);
  trie_destroy_trie(clone, destroy_cloned_terminal);
  return 0;
}

int main(void) {
  int result;
  result = test_nested_hash_table();
  if (result != 0) {
    return result;
  }
  result = test_trie_hash_hash_rbt();
  if (result != 0) {
    return result;
  }
  printf("hash table -> hash table -> red-black tree ok\n");
  printf("trie -> hash table -> hash table -> red-black tree ok\n");
  return 0;
}
