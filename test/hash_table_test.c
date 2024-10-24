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
  printf("%lu\n", hash);
  return hash;
}

int string_compare(void *key1, void *key2) {
  return strcmp((char *)key1, (char *)key2);
}

int main(void) {
  hash_table_t *table = hash_table_create((size_t)4000, HASH_CHAINING, NULL);

  char *value =
      CAST(hash_table_lookup(table, "key2", string_hash, string_compare), char);
  if (value != table->nil) {
    printf("Found value for key2: %s\n", value);
  }

  hash_table_insert(table, "key1", "hmmm", string_hash, string_compare);
  hash_table_insert(table, "key2", "hmmm", string_hash, string_compare);
  hash_table_insert(table, "key3", "hmmm", string_hash, string_compare);
  hash_table_insert(table, "key4", "hmmm", string_hash, string_compare);
  hash_table_insert(table, "key5", "hmmm", string_hash, string_compare);
  hash_table_insert(table, "key6", "hmmm", string_hash, string_compare);

  value =
      CAST(hash_table_lookup(table, "key2", string_hash, string_compare), char);
  if (value) {
    printf("Found value for key2: %s\n", value);
  }

  hash_table_remove(table, "key2", string_hash, string_compare, NULL);

  value =
      CAST(hash_table_lookup(table, "key2", string_hash, string_compare), char);
  if (value == table->nil) {
    printf("Key 'key2' not found\n");
  }

  hash_table_destroy(table, NULL);

  return 0;
}
