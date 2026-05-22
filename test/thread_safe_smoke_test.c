#include "ds.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t smoke_hash(void *key, void *user) {
  (void)user;
  return hash_func_string_djb2(key);
}

static int smoke_compare(void *a, void *b, void *user) {
  (void)user;
  return strcmp((char *)a, (char *)b);
}

int main(void) {
  ds_hash_table_config_t config;
  ds_hash_table_t *table;
  void *out;

  ds_hash_table_config_init(&config);
  config.hash = smoke_hash;
  config.compare = smoke_compare;
  table = FUNC(ds_hash_table_create_config)(&config);
  assert(table != NULL);
  assert(FUNC(ds_hash_table_insert)(table, "k", "v") == DS_OK);
  assert(FUNC(ds_hash_table_get)(table, "k", &out) == DS_OK);
  assert(strcmp((char *)out, "v") == 0);
  FUNC(ds_hash_table_destroy)(table);
  puts("thread_safe_smoke_test passed");
  return 0;
}
