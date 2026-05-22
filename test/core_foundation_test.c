#include "ds.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t cfg_hash(void *key, void *user) {
  (void)user;
  return hash_func_string_djb2(key);
}

static int cfg_compare(void *a, void *b, void *user) {
  (void)user;
  return strcmp((const char *)a, (const char *)b);
}

static size_t cstr_slice(void *data, size_t i) {
  return (size_t)((unsigned char *)data)[i];
}

static bool cstr_has_slice(void *data, size_t i) {
  return ((unsigned char *)data)[i] != '\0';
}

static int int_compare(void *a, void *b, void *user) {
  int av;
  int bv;
  (void)user;
  av = *(int *)a;
  bv = *(int *)b;
  return (av > bv) - (av < bv);
}

static bool export_counter(const ds_history_operation_t *operation,
                           void *user) {
  size_t *count;
  count = (size_t *)user;
  assert(operation != NULL);
  (*count)++;
  return true;
}

static void test_diagnostics_context(void) {
  ds_context_t context;
  ds_diagnostics_t diagnostics;
  ds_diagnostic_desc_t desc;
  char buffer[256];

  ds_context_init(&context);
  ds_diagnostics_init(&diagnostics);
  ds_context_set_diagnostic_sink(&context, ds_diagnostics_sink(&diagnostics));
  desc.id = "ds.test/notice";
  desc.level = DS_DIAG_NOTICE;
  desc.module = "test";
  desc.message = "notice";
  DS_CONTEXT_DIAG(&context, &desc, NULL, 0U);
  assert(diagnostics.size == 1U);
  assert(strcmp(diagnostics.items[0].id, "ds.test/notice") == 0);
  (void)ds_diagnostic_format(&diagnostics.items[0], buffer, sizeof(buffer));
  assert(strstr(buffer, "ds.test/notice") != NULL);
  (void)DS_CONTEXT_ERROR(&context, DS_ERR_INVALID, "ds.test/error", "test",
                         "error");
  assert(ds_context_last_error(&context)->status == DS_ERR_INVALID);
  ds_diagnostics_destroy(&diagnostics);
}

static void test_hash_config_iter_string_map(void) {
  ds_hash_table_config_t config;
  ds_hash_table_t *table;
  ds_iter_t iter;
  void *out;
  size_t count;
  ds_string_map_t *map;
  ds_string_set_t *set;

  ds_hash_table_config_init(&config);
  config.capacity = 5U;
  config.hash = cfg_hash;
  config.compare = cfg_compare;
  table = ds_hash_table_create_config(&config);
  assert(table != NULL);
  assert(ds_hash_table_insert(table, "alpha", "1") == DS_OK);
  assert(ds_hash_table_insert(table, "beta", "2") == DS_OK);
  assert(ds_hash_table_get(table, "alpha", &out) == DS_OK);
  assert(strcmp((char *)out, "1") == 0);
  assert(ds_hash_table_iter_init(table, &iter) == DS_OK);
  count = 0U;
  while (ds_iter_next(&iter) == DS_OK) {
    count++;
  }
  assert(count == 2U);
  assert(ds_hash_table_remove(table, "alpha") == DS_OK);
  assert(ds_hash_table_get(table, "alpha", &out) == DS_NOT_FOUND);
  ds_hash_table_destroy(table);

  map = ds_string_map_create();
  assert(map != NULL);
  assert(ds_string_map_put(map, "hello", "world") == DS_OK);
  assert(ds_string_map_get(map, "hello", &out) == DS_OK);
  assert(strcmp((char *)out, "world") == 0);
  ds_string_map_destroy(map);

  set = ds_string_set_create();
  assert(set != NULL);
  assert(ds_string_set_add(set, "x") == DS_OK);
  assert(ds_string_set_contains(set, "x") == DS_OK);
  assert(ds_string_set_contains(set, "y") == DS_NOT_FOUND);
  ds_string_set_destroy(set);
}

static void test_trie_config(void) {
  ds_trie_config_t config;
  ds_trie_t *trie;
  ds_trie_node_t *node;

  ds_trie_config_init(&config);
  config.num_splits = 256U;
  config.store_ops = ds_trie_hash_store_ops();
  trie = ds_trie_create_config(&config);
  assert(trie != NULL);
  assert(ds_trie_insert(trie, "cat", cstr_slice, cstr_has_slice) == DS_OK);
  assert(ds_trie_get(trie, "cat", cstr_slice, cstr_has_slice, &node) == DS_OK);
  assert(node != NULL && node->terminal_data == (void *)"cat");
  assert(ds_trie_remove(trie, "cat", cstr_slice, cstr_has_slice) == DS_OK);
  assert(ds_trie_get(trie, "cat", cstr_slice, cstr_has_slice, &node) ==
         DS_NOT_FOUND);
  ds_trie_destroy(trie);
}

static void test_persistent_structures(void) {
  ds_ptrie_t *ptrie;
  ds_ptrie_version_t *pv0;
  ds_ptrie_version_t *pv1;
  void *out;
  ds_prbt_config_t rconfig;
  ds_prbt_t *prbt;
  ds_prbt_version_t *rv0;
  ds_prbt_version_t *rv1;
  int k1;
  int k2;

  ptrie = ds_ptrie_create(NULL);
  assert(ptrie != NULL);
  pv0 = ds_ptrie_root(ptrie);
  pv1 = ds_ptrie_insert(ptrie, pv0, (const unsigned char *)"cat", 3U,
                        "value");
  assert(pv1 != NULL);
  assert(ds_ptrie_get(pv0, (const unsigned char *)"cat", 3U, &out) ==
         DS_NOT_FOUND);
  assert(ds_ptrie_get(pv1, (const unsigned char *)"cat", 3U, &out) == DS_OK);
  assert(strcmp((char *)out, "value") == 0);
  ds_ptrie_version_release(ptrie, pv1);
  ds_ptrie_destroy(ptrie);

  ds_prbt_config_init(&rconfig);
  rconfig.compare = int_compare;
  prbt = ds_prbt_create(&rconfig);
  assert(prbt != NULL);
  rv0 = ds_prbt_root(prbt);
  k1 = 1;
  k2 = 2;
  rv1 = ds_prbt_insert(prbt, rv0, &k1, "one");
  assert(rv1 != NULL);
  assert(ds_prbt_get(prbt, rv0, &k1, &out) == DS_NOT_FOUND);
  assert(ds_prbt_get(prbt, rv1, &k1, &out) == DS_OK);
  assert(strcmp((char *)out, "one") == 0);
  assert(ds_prbt_get(prbt, rv1, &k2, &out) == DS_NOT_FOUND);
  ds_prbt_version_release(prbt, rv1);
  ds_prbt_destroy(prbt);
}

static void test_graph_history_extensions(void) {
  ds_graph_t *graph;
  size_t a;
  size_t b;
  void *out;
  ds_history_ops_t ops;
  ds_history_t *history;
  ds_history_branch_t *main_branch;
  size_t exported;

  graph = ds_graph_create();
  assert(graph != NULL);
  assert(ds_graph_add_vertex(graph, "a", &a) == DS_OK);
  assert(ds_graph_add_vertex(graph, "b", &b) == DS_OK);
  assert(ds_graph_add_edge(graph, a, b, NULL) == DS_OK);
  assert(ds_graph_vertex_count(graph) == 2U);
  assert(ds_graph_edge_count(graph, a) == 1U);
  assert(ds_graph_get_vertex(graph, b, &out) == DS_OK);
  assert(strcmp((char *)out, "b") == 0);
  ds_graph_destroy(graph, NULL, NULL);

  memset(&ops, 0, sizeof(ops));
  ops.create_state = NULL;
  history = NULL;
  (void)history;
  /* Export/transaction are covered by history_test adapters; here we only
   * verify that the new APIs are link-visible through ds.h. */
  main_branch = NULL;
  exported = ds_history_branch_export_ops(main_branch, export_counter, &a);
  assert(exported == 0U);
}

int main(void) {
  test_diagnostics_context();
  test_hash_config_iter_string_map();
  test_trie_config();
  test_persistent_structures();
  test_graph_history_extensions();
  puts("core_foundation_test passed");
  return 0;
}
