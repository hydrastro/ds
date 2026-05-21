#include "../lib/hash_table.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct vector_bucket {
  ds_hash_node_t **nodes;
  size_t size;
  size_t capacity;
} vector_bucket_t;

int compare_int(void *a, void *b) {
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

void *vector_bucket_create(ds_hash_table_t *table) {
  vector_bucket_t *bucket;
  bucket = (vector_bucket_t *)table->allocator(sizeof(vector_bucket_t));
  bucket->nodes = NULL;
  bucket->size = 0;
  bucket->capacity = 0;
  return bucket;
}

ds_hash_node_t *vector_bucket_search(ds_hash_table_t *table, void *bucket_ptr,
                                     void *key,
                                     int (*compare)(void *, void *)) {
  vector_bucket_t *bucket;
  size_t i;
  (void)table;
  bucket = (vector_bucket_t *)bucket_ptr;
  for (i = 0; i < bucket->size; i++) {
    if (compare(bucket->nodes[i]->key, key) == 0) {
      return bucket->nodes[i];
    }
  }
  return NULL;
}

void vector_bucket_grow(ds_hash_table_t *table, vector_bucket_t *bucket) {
  ds_hash_node_t **nodes;
  size_t new_capacity;
  size_t i;
  new_capacity = bucket->capacity == 0 ? 4 : bucket->capacity * 2;
  nodes = (ds_hash_node_t **)table->allocator(new_capacity *
                                              sizeof(ds_hash_node_t *));
  for (i = 0; i < bucket->size; i++) {
    nodes[i] = bucket->nodes[i];
  }
  if (bucket->nodes != NULL) {
    table->deallocator(bucket->nodes);
  }
  bucket->nodes = nodes;
  bucket->capacity = new_capacity;
}

void vector_bucket_insert(ds_hash_table_t *table, void **bucket_ptr,
                          ds_hash_node_t *node,
                          int (*compare)(void *, void *)) {
  vector_bucket_t *bucket;
  (void)compare;
  bucket = (vector_bucket_t *)*bucket_ptr;
  if (bucket->size == bucket->capacity) {
    vector_bucket_grow(table, bucket);
  }
  bucket->nodes[bucket->size++] = node;
}

ds_hash_node_t *vector_bucket_remove(ds_hash_table_t *table, void **bucket_ptr,
                                     void *key,
                                     int (*compare)(void *, void *)) {
  vector_bucket_t *bucket;
  ds_hash_node_t *node;
  size_t i;
  size_t j;
  (void)table;
  bucket = (vector_bucket_t *)*bucket_ptr;
  for (i = 0; i < bucket->size; i++) {
    if (compare(bucket->nodes[i]->key, key) == 0) {
      node = bucket->nodes[i];
      for (j = i + 1; j < bucket->size; j++) {
        bucket->nodes[j - 1] = bucket->nodes[j];
      }
      bucket->size--;
      return node;
    }
  }
  return NULL;
}

void vector_bucket_destroy(ds_hash_table_t *table, void *bucket_ptr,
                           void (*destroy)(ds_hash_node_t *)) {
  vector_bucket_t *bucket;
  size_t i;
  bucket = (vector_bucket_t *)bucket_ptr;
  for (i = 0; i < bucket->size; i++) {
    if (destroy != NULL) {
      destroy(bucket->nodes[i]);
    }
    table->deallocator(bucket->nodes[i]);
  }
  if (bucket->nodes != NULL) {
    table->deallocator(bucket->nodes);
  }
  table->deallocator(bucket);
}

void vector_bucket_apply(ds_hash_table_t *table, void *bucket_ptr,
                         void (*callback)(ds_hash_node_t *, void *),
                         void *args) {
  vector_bucket_t *bucket;
  size_t i;
  (void)table;
  bucket = (vector_bucket_t *)bucket_ptr;
  for (i = 0; i < bucket->size; i++) {
    callback(bucket->nodes[i], args);
  }
}

void destroy_node(ds_hash_node_t *node) {
  free(node->key);
  free(node->value);
}

int main(void) {
  ds_hash_bucket_store_t bucket_store;
  ds_hash_table_t *table;
  int i;
  int lookup_key;
  int *key;
  int *value;
  int *result;

  bucket_store.create = vector_bucket_create;
  bucket_store.search = vector_bucket_search;
  bucket_store.insert = vector_bucket_insert;
  bucket_store.remove = vector_bucket_remove;
  bucket_store.destroy = vector_bucket_destroy;
  bucket_store.apply = vector_bucket_apply;

  table = hash_table_create_chain(3, &bucket_store);

  for (i = 0; i < 64; i++) {
    key = (int *)malloc(sizeof(int));
    value = (int *)malloc(sizeof(int));
    *key = i;
    *value = i * 10;
    hash_table_insert(table, key, value, hash_func_int, compare_int);
  }

  lookup_key = 42;
  result = (int *)hash_table_lookup(table, &lookup_key, hash_func_int,
                                    compare_int);
  if (result == table->nil || *result != 420) {
    return 1;
  }

  hash_table_remove(table, &lookup_key, hash_func_int, compare_int,
                    destroy_node);
  if (hash_table_lookup(table, &lookup_key, hash_func_int, compare_int) !=
      table->nil) {
    return 2;
  }

  hash_table_destroy(table, destroy_node);
  printf("custom bucket store ok\n");
  return 0;
}
