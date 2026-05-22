#include "../lib/history.h"
#include "../lib/hash_table.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  TEST_OP_INSERT = 1,
  TEST_OP_REMOVE = 2
};

enum {
  TEST_QUERY_GET = 1,
  TEST_QUERY_SIZE = 2
};

struct test_kv_payload {
  char *key;
  char *value;
};

static char *test_strdup(const char *str);
static struct test_kv_payload *test_payload_create(const char *key,
                                                   const char *value);
static struct test_kv_payload *test_insert_payload(const char *key,
                                                   const char *value);
static struct test_kv_payload *test_remove_payload(const char *key);
static size_t test_hash(void *key);
static int test_compare(void *left, void *right);
static void test_destroy_hash_node(ds_hash_node_t *node);
static void *test_clone_key(void *key);
static void *test_clone_value(void *value);
static void *test_history_create_state(void *config);
static void *test_history_clone_state(void *state, void *clone_data);
static void test_history_destroy_state(void *state);
static int test_history_apply(void *state, const ds_history_operation_t *op);
static void *test_history_query(void *state, const ds_history_query_t *query);
static void *test_history_clone_payload(int kind, void *payload,
                                        void *clone_data);
static void test_history_destroy_payload(int kind, void *payload);
static char *test_query_get(ds_history_branch_t *branch, unsigned long time,
                            const char *key);
static size_t test_query_size(ds_history_branch_t *branch, unsigned long time);
static void test_expect_get(ds_history_branch_t *branch, unsigned long time,
                            const char *key, const char *expected);
static void test_expect_missing(ds_history_branch_t *branch, unsigned long time,
                                const char *key);

static char *test_strdup(const char *str) {
  size_t len;
  char *copy;

  len = strlen(str) + 1U;
  copy = (char *)malloc(len);
  assert(copy != NULL);
  memcpy(copy, str, len);
  return copy;
}

static struct test_kv_payload *test_payload_create(const char *key,
                                                   const char *value) {
  struct test_kv_payload *payload;

  payload = (struct test_kv_payload *)malloc(sizeof(*payload));
  assert(payload != NULL);
  payload->key = test_strdup(key);
  payload->value = value != NULL ? test_strdup(value) : NULL;
  return payload;
}

static struct test_kv_payload *test_insert_payload(const char *key,
                                                   const char *value) {
  return test_payload_create(key, value);
}

static struct test_kv_payload *test_remove_payload(const char *key) {
  return test_payload_create(key, NULL);
}

static size_t test_hash(void *key) {
  return hash_func_string_djb2(key);
}

static int test_compare(void *left, void *right) {
  return strcmp((const char *)left, (const char *)right);
}

static void test_destroy_hash_node(ds_hash_node_t *node) {
  free(node->key);
  free(node->value);
}

static void *test_clone_key(void *key) {
  return test_strdup((const char *)key);
}

static void *test_clone_value(void *value) {
  return test_strdup((const char *)value);
}

static void *test_history_create_state(void *config) {
  (void)config;
  return hash_table_create(11U, HASH_CHAINING, NULL);
}

static void *test_history_clone_state(void *state, void *clone_data) {
  (void)clone_data;
  return hash_table_clone_with((ds_hash_table_t *)state, test_clone_key,
                               test_clone_value, test_hash, test_compare);
}

static void test_history_destroy_state(void *state) {
  hash_table_destroy((ds_hash_table_t *)state, test_destroy_hash_node);
}

static int test_history_apply(void *state, const ds_history_operation_t *op) {
  ds_hash_table_t *table;
  struct test_kv_payload *payload;
  char *key;
  char *value;

  table = (ds_hash_table_t *)state;
  payload = (struct test_kv_payload *)op->payload;

  if (op->kind == TEST_OP_INSERT) {
    key = test_strdup(payload->key);
    value = test_strdup(payload->value);
    hash_table_insert(table, key, value, test_hash, test_compare);
    return 0;
  }

  if (op->kind == TEST_OP_REMOVE) {
    hash_table_remove(table, payload->key, test_hash, test_compare,
                      test_destroy_hash_node);
    return 0;
  }

  return -1;
}

static void *test_history_query(void *state, const ds_history_query_t *query) {
  ds_hash_table_t *table;
  char *value;
  size_t *size_value;

  table = (ds_hash_table_t *)state;

  if (query->kind == TEST_QUERY_GET) {
    value = (char *)hash_table_lookup(table, query->payload, test_hash,
                                      test_compare);
    if (value == table->nil) {
      return NULL;
    }
    return test_strdup(value);
  }

  if (query->kind == TEST_QUERY_SIZE) {
    size_value = (size_t *)malloc(sizeof(*size_value));
    assert(size_value != NULL);
    *size_value = table->size;
    return size_value;
  }

  return NULL;
}

static void *test_history_clone_payload(int kind, void *payload,
                                        void *clone_data) {
  struct test_kv_payload *source;

  (void)clone_data;
  source = (struct test_kv_payload *)payload;
  if (kind == TEST_OP_INSERT) {
    return test_insert_payload(source->key, source->value);
  }
  if (kind == TEST_OP_REMOVE) {
    return test_remove_payload(source->key);
  }
  return NULL;
}

static void test_history_destroy_payload(int kind, void *payload) {
  struct test_kv_payload *kv;

  (void)kind;
  kv = (struct test_kv_payload *)payload;
  if (kv == NULL) {
    return;
  }
  free(kv->key);
  free(kv->value);
  free(kv);
}

static char *test_query_get(ds_history_branch_t *branch, unsigned long time,
                            const char *key) {
  ds_history_query_t query;

  query.kind = TEST_QUERY_GET;
  query.payload = (void *)key;
  return (char *)ds_history_branch_query_at(branch, time, &query);
}

static size_t test_query_size(ds_history_branch_t *branch, unsigned long time) {
  ds_history_query_t query;
  size_t *size_value;
  size_t result;

  query.kind = TEST_QUERY_SIZE;
  query.payload = NULL;
  size_value = (size_t *)ds_history_branch_query_at(branch, time, &query);
  assert(size_value != NULL);
  result = *size_value;
  free(size_value);
  return result;
}

static void test_expect_get(ds_history_branch_t *branch, unsigned long time,
                            const char *key, const char *expected) {
  char *actual;

  actual = test_query_get(branch, time, key);
  assert(actual != NULL);
  assert(strcmp(actual, expected) == 0);
  free(actual);
}

static void test_expect_missing(ds_history_branch_t *branch, unsigned long time,
                                const char *key) {
  char *actual;

  actual = test_query_get(branch, time, key);
  assert(actual == NULL);
}

int main(void) {
  ds_history_ops_t ops;
  ds_history_t *history;
  ds_history_branch_t *main_branch;
  ds_history_branch_t *feature_branch;
  unsigned long alpha_op;
  unsigned long deleted;

  ops.create_state = test_history_create_state;
  ops.clone_state = test_history_clone_state;
  ops.destroy_state = test_history_destroy_state;
  ops.apply = test_history_apply;
  ops.query = test_history_query;
  ops.clone_payload = test_history_clone_payload;
  ops.destroy_payload = test_history_destroy_payload;

  history = ds_history_create(&ops, NULL);
  assert(history != NULL);
  ds_history_set_checkpoint_interval(history, 1U);

  main_branch = ds_history_main(history);
  assert(main_branch != NULL);
  assert(strcmp(ds_history_branch_name(main_branch), "main") == 0);

  alpha_op = ds_history_branch_insert_at(
      main_branch, 10UL, TEST_OP_INSERT, test_insert_payload("alpha", "1"));
  assert(alpha_op != 0UL);
  assert(ds_history_branch_insert_at(
             main_branch, 20UL, TEST_OP_INSERT,
             test_insert_payload("beta", "2")) != 0UL);

  test_expect_missing(main_branch, 9UL, "alpha");
  test_expect_get(main_branch, 10UL, "alpha", "1");
  test_expect_get(main_branch, 20UL, "beta", "2");
  assert(test_query_size(main_branch, 20UL) == 2U);

  feature_branch = ds_history_branch_create(history, main_branch, 15UL,
                                            "feature");
  assert(feature_branch != NULL);
  assert(ds_history_branch_parent(feature_branch) == main_branch);
  assert(ds_history_branch_fork_time(feature_branch) == 15UL);

  assert(ds_history_branch_insert_at(
             feature_branch, 16UL, TEST_OP_INSERT,
             test_insert_payload("gamma", "3")) != 0UL);

  test_expect_get(feature_branch, 16UL, "alpha", "1");
  test_expect_get(feature_branch, 16UL, "gamma", "3");
  test_expect_missing(feature_branch, 16UL, "beta");
  assert(test_query_size(feature_branch, 16UL) == 2U);

  assert(ds_history_branch_insert_at(
             main_branch, 12UL, TEST_OP_INSERT,
             test_insert_payload("retro", "9")) != 0UL);

  test_expect_get(main_branch, 20UL, "retro", "9");
  test_expect_get(feature_branch, 16UL, "retro", "9");
  assert(test_query_size(feature_branch, 16UL) == 3U);

  deleted = ds_history_branch_delete_op(main_branch, alpha_op);
  assert(deleted);
  test_expect_missing(main_branch, 20UL, "alpha");
  test_expect_missing(feature_branch, 16UL, "alpha");
  test_expect_get(feature_branch, 16UL, "retro", "9");
  test_expect_get(feature_branch, 16UL, "gamma", "3");

  assert(ds_history_branch_insert_at(
             feature_branch, 15UL, TEST_OP_REMOVE,
             test_remove_payload("retro")) != 0UL);
  test_expect_missing(feature_branch, 16UL, "retro");
  test_expect_get(main_branch, 20UL, "retro", "9");

  ds_history_destroy(history);
  puts("history_test passed");
  return 0;
}
