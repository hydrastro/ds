#include "ds.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tiny_state {
  int values[8];
  bool present[8];
};

struct tiny_payload {
  size_t key;
  int value;
};

enum { TINY_SET = 1, TINY_DEL = 2 };

typedef struct edge_counter {
  size_t count;
  double total;
} edge_counter_t;

static int int_compare(void *a, void *b, void *user) {
  int av;
  int bv;
  (void)user;
  av = *(int *)a;
  bv = *(int *)b;
  if (av < bv) {
    return -1;
  }
  if (av > bv) {
    return 1;
  }
  return 0;
}

static struct tiny_payload *tiny_payload_create(size_t key, int value) {
  struct tiny_payload *payload;
  payload = (struct tiny_payload *)malloc(sizeof(*payload));
  assert(payload != NULL);
  payload->key = key;
  payload->value = value;
  return payload;
}

static void *tiny_create(void *config) {
  (void)config;
  return calloc(1U, sizeof(struct tiny_state));
}

static void *tiny_clone(void *state, void *clone_data) {
  struct tiny_state *copy;
  (void)clone_data;
  copy = (struct tiny_state *)malloc(sizeof(*copy));
  assert(copy != NULL);
  memcpy(copy, state, sizeof(*copy));
  return copy;
}

static void tiny_destroy(void *state) { free(state); }

static int tiny_apply(void *state, const ds_history_operation_t *op) {
  struct tiny_state *tiny;
  struct tiny_payload *payload;
  tiny = (struct tiny_state *)state;
  payload = (struct tiny_payload *)op->payload;
  if (op->kind == TINY_SET) {
    tiny->present[payload->key] = true;
    tiny->values[payload->key] = payload->value;
    return 0;
  }
  if (op->kind == TINY_DEL) {
    tiny->present[payload->key] = false;
    return 0;
  }
  return -1;
}

static void *tiny_query(void *state, const ds_history_query_t *query) {
  struct tiny_state *tiny;
  size_t key;
  int *out;
  tiny = (struct tiny_state *)state;
  key = *(size_t *)query->payload;
  if (!tiny->present[key]) {
    return NULL;
  }
  out = (int *)malloc(sizeof(*out));
  assert(out != NULL);
  *out = tiny->values[key];
  return out;
}

static void *tiny_clone_payload(int kind, void *payload, void *clone_data) {
  struct tiny_payload *copy;
  (void)kind;
  (void)clone_data;
  copy = (struct tiny_payload *)malloc(sizeof(*copy));
  assert(copy != NULL);
  *copy = *(struct tiny_payload *)payload;
  return copy;
}

static void tiny_destroy_payload(int kind, void *payload) {
  (void)kind;
  free(payload);
}

static bool tiny_payload_write(const ds_history_operation_t *op,
                               ds_history_write_func_t write, void *io_user,
                               void *payload_user) {
  struct tiny_payload *payload;
  (void)payload_user;
  payload = (struct tiny_payload *)op->payload;
  return ds_history_write_size(write, io_user, payload->key) &&
         ds_history_write_int(write, io_user, payload->value);
}

static void *tiny_payload_read(unsigned long id, unsigned long time, int kind,
                               ds_history_read_func_t read, void *io_user,
                               void *payload_user) {
  struct tiny_payload *payload;
  (void)id;
  (void)time;
  (void)kind;
  (void)payload_user;
  payload = (struct tiny_payload *)malloc(sizeof(*payload));
  assert(payload != NULL);
  if (!ds_history_read_size(read, io_user, &payload->key) ||
      !ds_history_read_int(read, io_user, &payload->value)) {
    free(payload);
    return NULL;
  }
  return payload;
}

static unsigned int merge_skip_deletes(ds_history_branch_t *target,
                                       ds_history_branch_t *source,
                                       const ds_history_operation_t *operation,
                                       unsigned long *time, int *kind,
                                       void **payload, void *user) {
  (void)target;
  (void)source;
  (void)time;
  (void)kind;
  (void)payload;
  (void)user;
  if (operation->kind == TINY_DEL) {
    return DS_HISTORY_MERGE_SKIP;
  }
  return DS_HISTORY_MERGE_TAKE;
}

static int query_value(ds_history_branch_t *branch, unsigned long time,
                       size_t key) {
  ds_history_query_t query;
  int *result;
  int value;
  query.kind = 1;
  query.payload = &key;
  result = (int *)ds_history_branch_query_at(branch, time, &query);
  assert(result != NULL);
  value = *result;
  free(result);
  return value;
}

static bool count_edge(size_t from, const ds_graph_edge_t *edge, void *user) {
  edge_counter_t *counter;
  (void)from;
  counter = (edge_counter_t *)user;
  counter->count++;
  counter->total += edge->weight;
  return true;
}

static void test_allocators(void) {
  ds_context_t context;
  ds_arena_t arena;
  ds_pool_t pool;
  ds_debug_allocator_t debug;
  ds_debug_allocator_stats_t stats;
  void *a;
  void *b;
  unsigned char arena_buffer[128];
  unsigned char pool_buffer[128];

  ds_arena_init(&arena, arena_buffer, sizeof(arena_buffer));
  ds_context_init(&context);
  ds_context_use_arena(&context, &arena);
  a = ds_context_alloc(&context, 16U);
  b = ds_context_calloc(&context, 4U, 4U);
  assert(a != NULL && b != NULL);
  assert(ds_arena_used(&arena) >= 32U);
  ds_arena_reset(&arena);
  assert(ds_arena_used(&arena) == 0U);

  ds_pool_init(&pool, pool_buffer, 16U, 4U);
  ds_context_init(&context);
  ds_context_use_pool(&context, &pool);
  a = ds_context_alloc(&context, 8U);
  b = ds_context_alloc(&context, 8U);
  assert(a != NULL && b != NULL);
  assert(ds_pool_available(&pool) == 2U);
  ds_context_free(&context, a);
  assert(ds_pool_available(&pool) == 3U);
  ds_context_free(&context, a);
  assert(ds_pool_available(&pool) == 3U);
  ds_context_free(&context, b);
  assert(ds_pool_available(&pool) == 4U);

  ds_context_init(&context);
  ds_debug_allocator_init(&debug, NULL);
  ds_context_use_debug_allocator(&context, &debug);
  a = ds_context_alloc(&context, 32U);
  b = ds_context_realloc(&context, a, 64U);
  assert(b != NULL);
  ds_context_free(&context, b);
  ds_debug_allocator_get_stats(&debug, &stats);
  assert(stats.allocations == 2U);
  assert(stats.frees == 2U);
  assert(stats.reallocations == 1U);
  assert(stats.bytes_live == 0U);
  assert(stats.bytes_peak >= 64U);
}

static void test_ptrie_high_level(void) {
  ds_ptrie_t *trie;
  ds_ptrie_version_t *v0;
  ds_ptrie_version_t *v1;
  ds_ptrie_version_t *v2;
  ds_ptrie_version_t *v3;
  ds_ptrie_version_t *copy;
  ds_ptrie_version_t *moved;
  void *out;
  size_t copied;
  size_t moved_count;

  trie = ds_ptrie_create(NULL);
  assert(trie != NULL);
  v0 = ds_ptrie_root(trie);
  v1 = ds_ptrie_insert(trie, v0, (const unsigned char *)"app", 3U, "app");
  v2 = ds_ptrie_insert(trie, v1, (const unsigned char *)"apple", 5U, "apple");
  v3 = ds_ptrie_insert(trie, v2, (const unsigned char *)"bat", 3U, "bat");
  assert(v1 != NULL && v2 != NULL && v3 != NULL);
  assert(ds_ptrie_count_prefix(v3, (const unsigned char *)"app", 3U) == 2U);
  copied = 0U;
  copy = ds_ptrie_copy_prefix(trie, v3, v3, (const unsigned char *)"app", 3U,
                              (const unsigned char *)"copy", 4U, &copied);
  assert(copy != NULL && copied == 2U);
  assert(ds_ptrie_get(copy, (const unsigned char *)"copy", 4U, &out) == DS_OK);
  assert(strcmp((char *)out, "app") == 0);
  assert(ds_ptrie_get(copy, (const unsigned char *)"copyle", 6U, &out) == DS_OK);
  assert(strcmp((char *)out, "apple") == 0);
  moved_count = 0U;
  moved = ds_ptrie_move_prefix(trie, copy, (const unsigned char *)"app", 3U,
                               (const unsigned char *)"m", 1U, &moved_count);
  assert(moved != NULL && moved_count == 2U);
  assert(ds_ptrie_get(moved, (const unsigned char *)"app", 3U, &out) ==
         DS_NOT_FOUND);
  assert(ds_ptrie_get(moved, (const unsigned char *)"m", 1U, &out) == DS_OK);
  assert(ds_ptrie_get(moved, (const unsigned char *)"mle", 3U, &out) == DS_OK);
  ds_ptrie_version_release(trie, moved);
  ds_ptrie_version_release(trie, copy);
  ds_ptrie_version_release(trie, v3);
  ds_ptrie_version_release(trie, v2);
  ds_ptrie_version_release(trie, v1);
  ds_ptrie_destroy(trie);
}

static void test_prbt_high_level(void) {
  ds_prbt_config_t config;
  ds_prbt_t *tree;
  ds_prbt_version_t *v0;
  ds_prbt_version_t *versions[10];
  ds_prbt_version_t *deleted_min;
  ds_prbt_version_t *deleted_max;
  int keys[9];
  int values[9];
  int probe;
  void *out_key;
  void *out_value;
  size_t rank;
  size_t i;

  ds_prbt_config_init(&config);
  config.compare = int_compare;
  tree = ds_prbt_create(&config);
  assert(tree != NULL);
  v0 = ds_prbt_root(tree);
  versions[0] = v0;
  for (i = 0U; i < 9U; i++) {
    keys[i] = (int)i;
    values[i] = (int)(i * 10U);
    versions[i + 1U] = ds_prbt_insert(tree, versions[i], &keys[i], &values[i]);
    assert(versions[i + 1U] != NULL);
    assert(ds_prbt_validate(tree, versions[i + 1U]) == DS_OK);
  }
  probe = 4;
  assert(ds_prbt_predecessor(tree, versions[9], &probe, &out_key, &out_value) ==
         DS_OK);
  assert(*(int *)out_key == 3 && *(int *)out_value == 30);
  assert(ds_prbt_successor(tree, versions[9], &probe, &out_key, &out_value) ==
         DS_OK);
  assert(*(int *)out_key == 5 && *(int *)out_value == 50);
  assert(ds_prbt_rank(tree, versions[9], &probe, &rank) == DS_OK);
  assert(rank == 4U);
  deleted_min = ds_prbt_delete_min(tree, versions[9]);
  assert(deleted_min != NULL);
  assert(ds_prbt_validate(tree, deleted_min) == DS_OK);
  probe = 0;
  assert(ds_prbt_get(tree, deleted_min, &probe, &out_value) == DS_NOT_FOUND);
  deleted_max = ds_prbt_delete_max(tree, deleted_min);
  assert(deleted_max != NULL);
  assert(ds_prbt_validate(tree, deleted_max) == DS_OK);
  probe = 8;
  assert(ds_prbt_get(tree, deleted_max, &probe, &out_value) == DS_NOT_FOUND);
  assert(ds_prbt_size(deleted_max) == 7U);
  ds_prbt_version_release(tree, deleted_max);
  ds_prbt_version_release(tree, deleted_min);
  for (i = 1U; i <= 9U; i++) {
    ds_prbt_version_release(tree, versions[i]);
  }
  ds_prbt_destroy(tree);
}

static void test_history_archive_merge_policy(void) {
  ds_history_ops_t ops;
  ds_history_t *history;
  ds_history_t *loaded;
  ds_history_branch_t *main_branch;
  ds_history_branch_t *feature;
  ds_history_archive_t archive;
  size_t key;
  ds_history_query_t query;
  void *missing;

  memset(&ops, 0, sizeof(ops));
  ops.create_state = tiny_create;
  ops.clone_state = tiny_clone;
  ops.destroy_state = tiny_destroy;
  ops.apply = tiny_apply;
  ops.query = tiny_query;
  ops.clone_payload = tiny_clone_payload;
  ops.destroy_payload = tiny_destroy_payload;

  history = ds_history_create(&ops, NULL);
  assert(history != NULL);
  main_branch = ds_history_main(history);
  assert(ds_history_branch_insert_at(main_branch, 10UL, TINY_SET,
                                     tiny_payload_create(1U, 11)) != 0UL);
  feature = ds_history_branch_create(history, main_branch, 10UL, "feature");
  assert(feature != NULL);
  assert(ds_history_branch_insert_at(feature, 11UL, TINY_SET,
                                     tiny_payload_create(2U, 22)) != 0UL);
  assert(ds_history_branch_insert_at(feature, 12UL, TINY_DEL,
                                     tiny_payload_create(1U, 0)) != 0UL);
  assert(ds_history_branch_merge_with(main_branch, feature, 11UL, 12UL,
                                      DS_HISTORY_MERGE_PRESERVE_TIME,
                                      merge_skip_deletes, NULL) == 1U);
  assert(query_value(main_branch, 11UL, 2U) == 22);
  key = 1U;
  query.kind = 1;
  query.payload = &key;
  missing = ds_history_branch_query_at(main_branch, 12UL, &query);
  assert(missing != NULL);
  free(missing);

  ds_history_archive_init(&archive);
  assert(ds_history_serialize_portable(history, ds_history_archive_write,
                                       tiny_payload_write, &archive, NULL));
  assert(ds_history_archive_size(&archive) > 0U);
  ds_history_archive_rewind(&archive);
  loaded = ds_history_deserialize_portable(&ops, NULL, ds_history_archive_read,
                                           tiny_payload_read, &archive, NULL);
  assert(loaded != NULL);
  assert(query_value(ds_history_main(loaded), 11UL, 2U) == 22);
  ds_history_destroy(loaded);
  ds_history_archive_destroy(&archive);
  ds_history_destroy(history);
}

static void test_graph_high_level(void) {
  ds_graph_t *graph;
  ds_graph_t *mst;
  size_t a;
  size_t b;
  size_t c;
  double dist[3];
  size_t prev[3];
  bool negative_cycle;
  edge_counter_t counter;

  graph = ds_graph_create();
  mst = ds_graph_create();
  assert(graph != NULL && mst != NULL);
  assert(ds_graph_add_vertex(graph, "a", &a) == DS_OK);
  assert(ds_graph_add_vertex(graph, "b", &b) == DS_OK);
  assert(ds_graph_add_vertex(graph, "c", &c) == DS_OK);
  assert(ds_graph_add_weighted_edge(graph, a, b, 4.0, "ab") == DS_OK);
  assert(ds_graph_add_weighted_edge(graph, b, c, -2.0, "bc") == DS_OK);
  assert(ds_graph_add_weighted_edge(graph, a, c, 5.0, "ac") == DS_OK);
  assert(ds_graph_dijkstra(graph, a, dist, prev, 3U) == DS_ERR_RANGE);
  assert(ds_graph_bellman_ford(graph, a, dist, prev, 3U, &negative_cycle) ==
         DS_OK);
  assert(!negative_cycle);
  assert(dist[c] == 2.0);
  assert(ds_graph_update_edge(graph, a, b, 1.0, "ab2", NULL) == DS_OK);
  memset(&counter, 0, sizeof(counter));
  assert(ds_graph_visit_edges(graph, a, count_edge, &counter) == DS_OK);
  assert(counter.count == 2U);
  assert(ds_graph_remove_edge(graph, a, c, NULL) == DS_OK);
  assert(ds_graph_edge_count(graph, a) == 1U);
  assert(ds_graph_minimum_spanning_tree(graph, mst) == DS_OK);
  assert(ds_graph_vertex_count(mst) == 3U);
  assert(ds_graph_remove_edge(graph, a, c, NULL) == DS_NOT_FOUND);
  ds_graph_destroy(mst, NULL, NULL);
  ds_graph_destroy(graph, NULL, NULL);
}

int main(void) {
  test_allocators();
  test_ptrie_high_level();
  test_prbt_high_level();
  test_history_archive_merge_policy();
  test_graph_high_level();
  puts("high_value_test passed");
  return 0;
}
