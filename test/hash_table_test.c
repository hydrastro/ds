#include "../lib/hash_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t string_hash(void *key) {
  char *str = (char *)key;
  size_t hash = 5381;
  size_t c;
  while ((c = (size_t)*str++)) {
    hash = ((hash << 5) + hash) + c;
  }
  printf("hash of '%s': %lu\n", (char *)key, hash);
  return hash;
}

int string_compare(void *key1, void *key2) {
  return strcmp((char *)key1, (char *)key2);
}

void *clone_key(void *key) { return (void *)strdup(key); }

void *clone_value(void *value) { return (void *)strdup(value); }

void destroy_node(ds_hash_node_t *node) {
  free(node->key);
  free(node->value);
}

int main(void) {
  ds_hash_table_t *table = hash_table_create((size_t)4000, HASH_CHAINING, NULL);
  ds_hash_node_t *node;

  char *value;
  node = hash_table_lookup(table, "key2", string_hash, string_compare);
  value = CAST(node, char);

  if (value == table->nil) {
    printf("Key 'key2' not found\n");
  } else {
    printf("Found value for key2: %s\n", value);
  }

  hash_table_insert(table, "key1", "hmmm", string_hash, string_compare);
  hash_table_insert(table, "key2", "hmmm", string_hash, string_compare);
  hash_table_insert(table, "key3", "hmmm", string_hash, string_compare);
  hash_table_insert(table, "key4", "hmmm", string_hash, string_compare);
  hash_table_insert(table, "key5", "hmmm", string_hash, string_compare);
  hash_table_insert(table, "key6", "hmmm", string_hash, string_compare);

  node = hash_table_lookup(table, "key2", string_hash, string_compare);
  value = CAST(node, char);
  if (value == table->nil) {
    printf("Key 'key2' not found\n");
  } else {
    printf("Found value for key2: %s\n", value);
  }

  printf("Cloning hash table\n");
  ds_hash_table_t *new_table = hash_table_clone(table, clone_key, clone_value);
  printf("Cloned hash table\n");

  hash_table_remove(table, "key2", string_hash, string_compare, NULL);

  node = hash_table_lookup(table, "key2", string_hash, string_compare);
  value = CAST(node, char);
  if (value == table->nil) {
    printf("Key 'key2' not found\n");
  } else {
    printf("Found value for key2: %s\n", value);
  }
  hash_table_destroy(table, NULL);

  printf("Destroyed first hash table\n\n");

  node = hash_table_lookup(new_table, "key", string_hash, string_compare);
  value = CAST(node, char);
  if (value == new_table->nil) {
    printf("Key 'key2' not found\n");
  } else {
    printf("Found value for key2: %s\n", value);
  }

  hash_table_remove(new_table, "key2", string_hash, string_compare, NULL);

  node = hash_table_lookup(new_table, "key2", string_hash, string_compare);
  value = CAST(node, char);
  if (value == new_table->nil) {
    printf("Key 'key2' not found\n");
  } else {
    printf("Found value for key2: %s\n", value);
  }
  hash_table_destroy(new_table, destroy_node);
  printf("Second hash table destroyed\n");

  return 0;
}
