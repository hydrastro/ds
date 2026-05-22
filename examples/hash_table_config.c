#include "ds.h"
#include <stdio.h>
#include <string.h>

static size_t hash_str(void *key, void *user) {
  (void)user;
  return hash_func_string_djb2(key);
}

static int cmp_str(void *a, void *b, void *user) {
  (void)user;
  return strcmp((char *)a, (char *)b);
}

int main(void) {
  ds_hash_table_config_t config;
  ds_hash_table_t *table;
  void *value;

  ds_hash_table_config_init(&config);
  config.hash = hash_str;
  config.compare = cmp_str;
  table = ds_hash_table_create_config(&config);

  ds_hash_table_insert(table, "language", "C");
  if (ds_hash_table_get(table, "language", &value) == DS_OK) {
    printf("language=%s\n", (char *)value);
  }

  ds_hash_table_destroy(table);
  return 0;
}
