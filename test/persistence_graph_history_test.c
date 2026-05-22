#include "ds.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KCOUNT 8U

enum { HSET = 1, HDEL = 2 };
enum { HGET = 1 };

struct hstate {
  bool present[KCOUNT];
  int value[KCOUNT];
};

struct hpayload {
  size_t key;
  int value;
};

struct memio {
  unsigned char data[4096];
  size_t pos;
  size_t len;
};

struct visit_log {
  size_t items[16];
  size_t count;
};

struct ptrie_visit_log {
  char keys[16][16];
  size_t lens[16];
  size_t count;
};

struct prbt_visit_log {
  int keys[64];
  int values[64];
  size_t count;
};

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

static bool ptrie_visit_record(const unsigned char *key, size_t len,
                                void *value, void *user) {
  struct ptrie_visit_log *log;
  (void)value;
  log = (struct ptrie_visit_log *)user;
  assert(log->count < 16U);
  assert(len < 16U);
  memcpy(log->keys[log->count], key, len);
  log->keys[log->count][len] = '\0';
  log->lens[log->count] = len;
  log->count++;
  return true;
}

static bool prbt_visit_record(void *key, void *value, void *user) {
  struct prbt_visit_log *log;
  log = (struct prbt_visit_log *)user;
  assert(log->count < 64U);
  log->keys[log->count] = *(int *)key;
  log->values[log->count] = *(int *)value;
  log->count++;
  return true;
}

static bool visit_record(size_t vertex, void *data, void *user) {
  struct visit_log *log;
  (void)data;
  log = (struct visit_log *)user;
  log->items[log->count] = vertex;
  log->count++;
  return true;
}

static void *h_create(void *config) {
  (void)config;
  return calloc(1U, sizeof(struct hstate));
}

static void *h_clone(void *state, void *clone_data) {
  struct hstate *copy;
  (void)clone_data;
  copy = (struct hstate *)malloc(sizeof(*copy));
  assert(copy != NULL);
  memcpy(copy, state, sizeof(*copy));
  return copy;
}

static void h_destroy(void *state) { free(state); }

static int h_apply(void *state, const ds_history_operation_t *op) {
  struct hstate *s;
  struct hpayload *p;
  s = (struct hstate *)state;
  p = (struct hpayload *)op->payload;
  if (op->kind == HSET) {
    s->present[p->key] = true;
    s->value[p->key] = p->value;
    return 0;
  }
  if (op->kind == HDEL) {
    s->present[p->key] = false;
    return 0;
  }
  return -1;
}

static void *h_query(void *state, const ds_history_query_t *query) {
  struct hstate *s;
  size_t key;
  int *out;
  s = (struct hstate *)state;
  key = *(size_t *)query->payload;
  if (!s->present[key]) {
    return NULL;
  }
  out = (int *)malloc(sizeof(*out));
  assert(out != NULL);
  *out = s->value[key];
  return out;
}

static void *h_clone_payload(int kind, void *payload, void *clone_data) {
  struct hpayload *copy;
  (void)kind;
  (void)clone_data;
  copy = (struct hpayload *)malloc(sizeof(*copy));
  assert(copy != NULL);
  *copy = *(struct hpayload *)payload;
  return copy;
}

static void h_destroy_payload(int kind, void *payload) {
  (void)kind;
  free(payload);
}

static struct hpayload *make_payload(size_t key, int value) {
  struct hpayload *p;
  p = (struct hpayload *)malloc(sizeof(*p));
  assert(p != NULL);
  p->key = key;
  p->value = value;
  return p;
}

static bool mem_write(const void *data, size_t size, void *user) {
  struct memio *io;
  io = (struct memio *)user;
  if (io->len + size > sizeof(io->data)) {
    return false;
  }
  memcpy(io->data + io->len, data, size);
  io->len += size;
  return true;
}

static bool mem_read(void *data, size_t size, void *user) {
  struct memio *io;
  io = (struct memio *)user;
  if (io->pos + size > io->len) {
    return false;
  }
  memcpy(data, io->data + io->pos, size);
  io->pos += size;
  return true;
}

static bool payload_write(const ds_history_operation_t *op,
                          ds_history_write_func_t write, void *io_user,
                          void *payload_user) {
  (void)payload_user;
  return write(op->payload, sizeof(struct hpayload), io_user);
}

static void *payload_read(unsigned long id, unsigned long time, int kind,
                          ds_history_read_func_t read, void *io_user,
                          void *payload_user) {
  struct hpayload *p;
  (void)id;
  (void)time;
  (void)kind;
  (void)payload_user;
  p = (struct hpayload *)malloc(sizeof(*p));
  assert(p != NULL);
  if (!read(p, sizeof(*p), io_user)) {
    free(p);
    return NULL;
  }
  return p;
}

static bool payload_write_portable(const ds_history_operation_t *op,
                                   ds_history_write_func_t write,
                                   void *io_user, void *payload_user) {
  struct hpayload *p;
  (void)payload_user;
  p = (struct hpayload *)op->payload;
  return ds_history_write_size(write, io_user, p->key) &&
         ds_history_write_int(write, io_user, p->value);
}

static void *payload_read_portable(unsigned long id, unsigned long time,
                                   int kind, ds_history_read_func_t read,
                                   void *io_user, void *payload_user) {
  struct hpayload *p;
  (void)id;
  (void)time;
  (void)kind;
  (void)payload_user;
  p = (struct hpayload *)malloc(sizeof(*p));
  assert(p != NULL);
  if (!ds_history_read_size(read, io_user, &p->key) ||
      !ds_history_read_int(read, io_user, &p->value)) {
    free(p);
    return NULL;
  }
  return p;
}

static int query_int(ds_history_branch_t *branch, unsigned long time,
                     size_t key) {
  ds_history_query_t query;
  int *result;
  int value;
  query.kind = HGET;
  query.payload = &key;
  result = (int *)ds_history_branch_query_at(branch, time, &query);
  assert(result != NULL);
  value = *result;
  free(result);
  return value;
}

static void test_persistent_trie_remove(void) {
  ds_ptrie_t *trie;
  ds_ptrie_version_t *v0;
  ds_ptrie_version_t *v1;
  ds_ptrie_version_t *v2;
  ds_ptrie_version_t *v3;
  ds_ptrie_version_t *v4;
  void *out;
  size_t prefix_len;
  struct ptrie_visit_log log;
  trie = ds_ptrie_create(NULL);
  assert(trie != NULL);
  v0 = ds_ptrie_root(trie);
  v1 = ds_ptrie_insert(trie, v0, (const unsigned char *)"cat", 3U, "cat");
  v2 = ds_ptrie_insert(trie, v1, (const unsigned char *)"car", 3U, "car");
  v3 = ds_ptrie_insert(trie, v2, (const unsigned char *)"dog", 3U, "dog");
  v4 = ds_ptrie_remove(trie, v3, (const unsigned char *)"cat", 3U);
  assert(v1 != NULL && v2 != NULL && v3 != NULL && v4 != NULL);
  assert(ds_ptrie_get(v1, (const unsigned char *)"cat", 3U, &out) == DS_OK);
  assert(ds_ptrie_get(v3, (const unsigned char *)"cat", 3U, &out) == DS_OK);
  assert(ds_ptrie_get(v4, (const unsigned char *)"cat", 3U, &out) == DS_NOT_FOUND);
  assert(ds_ptrie_get(v4, (const unsigned char *)"car", 3U, &out) == DS_OK);
  assert(ds_ptrie_contains_prefix(v4, (const unsigned char *)"ca", 2U) == DS_OK);
  assert(ds_ptrie_contains_prefix(v4, (const unsigned char *)"cz", 2U) == DS_NOT_FOUND);
  assert(ds_ptrie_longest_prefix(v4, (const unsigned char *)"carpet", 6U,
                                  &prefix_len, &out) == DS_OK);
  assert(prefix_len == 3U && strcmp((char *)out, "car") == 0);
  memset(&log, 0, sizeof(log));
  assert(ds_ptrie_visit_prefix(v4, (const unsigned char *)"ca", 2U,
                                ptrie_visit_record, &log) == DS_OK);
  assert(log.count == 1U && strcmp(log.keys[0], "car") == 0);
  memset(&log, 0, sizeof(log));
  assert(ds_ptrie_visit_range(v4, (const unsigned char *)"c", 1U,
                               (const unsigned char *)"d", 1U,
                               ptrie_visit_record, &log) == DS_OK);
  assert(log.count == 1U && strcmp(log.keys[0], "car") == 0);
  assert(ds_ptrie_remove(trie, v4, (const unsigned char *)"missing", 7U) == NULL);
  {
    ds_ptrie_version_t *v5;
    ds_ptrie_version_t *v6;
    ds_ptrie_version_t *v7;
    size_t removed_count;
    v5 = ds_ptrie_insert(trie, v4, (const unsigned char *)"cart", 4U, "cart");
    assert(v5 != NULL);
    v6 = ds_ptrie_insert(trie, v5, (const unsigned char *)"can", 3U, "can");
    assert(v6 != NULL);
    removed_count = 0U;
    v7 = ds_ptrie_remove_prefix(trie, v6, (const unsigned char *)"ca", 2U,
                                &removed_count);
    assert(v7 != NULL);
    assert(removed_count == 3U);
    assert(ds_ptrie_get(v7, (const unsigned char *)"car", 3U, &out) ==
           DS_NOT_FOUND);
    assert(ds_ptrie_get(v7, (const unsigned char *)"cart", 4U, &out) ==
           DS_NOT_FOUND);
    assert(ds_ptrie_get(v7, (const unsigned char *)"can", 3U, &out) ==
           DS_NOT_FOUND);
    assert(ds_ptrie_get(v7, (const unsigned char *)"dog", 3U, &out) == DS_OK);
    assert(ds_ptrie_get(v4, (const unsigned char *)"car", 3U, &out) == DS_OK);
    ds_ptrie_version_release(trie, v7);
    ds_ptrie_version_release(trie, v6);
    ds_ptrie_version_release(trie, v5);
  }
  ds_ptrie_version_release(trie, v4);
  ds_ptrie_version_release(trie, v3);
  ds_ptrie_version_release(trie, v2);
  ds_ptrie_version_release(trie, v1);
  ds_ptrie_destroy(trie);
}

static void test_persistent_rbt_many_versions(void) {
  ds_prbt_config_t config;
  ds_prbt_t *tree;
  ds_prbt_version_t *versions[33];
  ds_prbt_version_t *removed;
  int keys[32];
  int values[32];
  int low;
  int high;
  void *out;
  size_t i;
  size_t j;
  struct prbt_visit_log log;
  ds_prbt_config_init(&config);
  config.compare = int_compare;
  tree = ds_prbt_create(&config);
  assert(tree != NULL);
  versions[0] = ds_prbt_root(tree);
  for (i = 0U; i < 32U; i++) {
    keys[i] = (int)((i * 7U) % 32U);
    values[i] = (int)(i + 100U);
    versions[i + 1U] = ds_prbt_insert(tree, versions[i], &keys[i], &values[i]);
    assert(versions[i + 1U] != NULL);
    assert(ds_prbt_size(versions[i + 1U]) == i + 1U);
  }
  for (i = 0U; i < 32U; i++) {
    for (j = 0U; j < i; j++) {
      assert(ds_prbt_get(tree, versions[i], &keys[j], &out) == DS_OK);
    }
    assert(ds_prbt_get(tree, versions[i], &keys[i], &out) == DS_NOT_FOUND);
  }
  memset(&log, 0, sizeof(log));
  assert(ds_prbt_visit(tree, versions[32], prbt_visit_record, &log) == DS_OK);
  assert(log.count == 32U);
  for (i = 1U; i < log.count; i++) {
    assert(log.keys[i - 1U] < log.keys[i]);
  }
  low = 10;
  high = 15;
  memset(&log, 0, sizeof(log));
  assert(ds_prbt_visit_range(tree, versions[32], &low, &high, prbt_visit_record,
                              &log) == DS_OK);
  assert(log.count == 6U && log.keys[0] == 10 && log.keys[5] == 15);
  {
    int probe;
    void *out_key;
    size_t rank;
    probe = 14;
    assert(ds_prbt_floor(tree, versions[32], &probe, &out_key, &out) == DS_OK);
    assert(*(int *)out_key == 14);
    probe = 14;
    assert(ds_prbt_ceiling(tree, versions[32], &probe, &out_key, &out) == DS_OK);
    assert(*(int *)out_key == 14);
    probe = 14;
    assert(ds_prbt_rank(tree, versions[32], &probe, &rank) == DS_OK);
    assert(rank == 14U);
    assert(ds_prbt_select(tree, versions[32], 14U, &out_key, &out) == DS_OK);
    assert(*(int *)out_key == 14);
    probe = -1;
    assert(ds_prbt_floor(tree, versions[32], &probe, &out_key, &out) ==
           DS_NOT_FOUND);
    probe = 32;
    assert(ds_prbt_ceiling(tree, versions[32], &probe, &out_key, &out) ==
           DS_NOT_FOUND);
  }
  removed = ds_prbt_remove(tree, versions[32], &keys[10]);
  assert(removed != NULL);
  assert(ds_prbt_size(removed) == 31U);
  assert(ds_prbt_get(tree, removed, &keys[10], &out) == DS_NOT_FOUND);
  assert(ds_prbt_get(tree, versions[32], &keys[10], &out) == DS_OK);
  assert(ds_prbt_remove(tree, removed, &keys[10]) == NULL);
  ds_prbt_version_release(tree, removed);
  for (i = 32U; i > 0U; i--) {
    ds_prbt_version_release(tree, versions[i]);
  }
  ds_prbt_destroy(tree);
}

static void test_graph_algorithms(void) {
  ds_graph_t *graph;
  size_t a;
  size_t b;
  size_t c;
  size_t d;
  size_t order[4];
  size_t count;
  struct visit_log log;
  graph = ds_graph_create();
  assert(graph != NULL);
  assert(ds_graph_add_vertex(graph, "a", &a) == DS_OK);
  assert(ds_graph_add_vertex(graph, "b", &b) == DS_OK);
  assert(ds_graph_add_vertex(graph, "c", &c) == DS_OK);
  assert(ds_graph_add_vertex(graph, "d", &d) == DS_OK);
  assert(ds_graph_add_edge(graph, a, b, NULL) == DS_OK);
  assert(ds_graph_add_edge(graph, a, c, NULL) == DS_OK);
  assert(ds_graph_add_edge(graph, b, d, NULL) == DS_OK);
  assert(ds_graph_add_edge(graph, c, d, NULL) == DS_OK);
  log.count = 0U;
  assert(ds_graph_bfs(graph, a, visit_record, &log) == DS_OK);
  assert(log.count == 4U && log.items[0] == a && log.items[3] == d);
  log.count = 0U;
  assert(ds_graph_dfs(graph, a, visit_record, &log) == DS_OK);
  assert(log.count == 4U && log.items[0] == a);
  assert(ds_graph_topological_sort(graph, order, 4U, &count) == DS_OK);
  assert(count == 4U);
  assert(order[0] == a);
  assert(order[3] == d);
  {
    double dist[4];
    size_t prev[4];
    size_t comps[4];
    ds_graph_t *scc_graph;
    size_t s0;
    size_t s1;
    size_t s2;
    size_t s3;
    assert(ds_graph_add_weighted_edge(graph, a, d, 10.0, NULL) == DS_OK);
    assert(ds_graph_dijkstra(graph, a, dist, prev, 4U) == DS_OK);
    assert(dist[d] == 2.0);
    assert(prev[d] == b || prev[d] == c);
    assert(ds_graph_connected_components(graph, comps, 4U, &count) == DS_OK);
    assert(count == 1U);
    scc_graph = ds_graph_create();
    assert(scc_graph != NULL);
    assert(ds_graph_add_vertex(scc_graph, "0", &s0) == DS_OK);
    assert(ds_graph_add_vertex(scc_graph, "1", &s1) == DS_OK);
    assert(ds_graph_add_vertex(scc_graph, "2", &s2) == DS_OK);
    assert(ds_graph_add_vertex(scc_graph, "3", &s3) == DS_OK);
    assert(ds_graph_add_edge(scc_graph, s0, s1, NULL) == DS_OK);
    assert(ds_graph_add_edge(scc_graph, s1, s0, NULL) == DS_OK);
    assert(ds_graph_add_edge(scc_graph, s1, s2, NULL) == DS_OK);
    assert(ds_graph_add_edge(scc_graph, s2, s3, NULL) == DS_OK);
    assert(ds_graph_add_edge(scc_graph, s3, s2, NULL) == DS_OK);
    assert(ds_graph_strongly_connected_components(scc_graph, comps, 4U,
                                                   &count) == DS_OK);
    assert(count == 2U);
    assert(comps[s0] == comps[s1]);
    assert(comps[s2] == comps[s3]);
    assert(comps[s0] != comps[s2]);
    ds_graph_destroy(scc_graph, NULL, NULL);
  }
  ds_graph_destroy(graph, NULL, NULL);
}

static void test_history_serialization(void) {
  ds_history_ops_t ops;
  ds_history_t *h1;
  ds_history_t *h2;
  ds_history_branch_t *main1;
  ds_history_branch_t *main2;
  ds_history_branch_t *feature1;
  ds_history_branch_t *feature2;
  struct memio io;
  size_t key;
  ds_history_query_t query;
  memset(&ops, 0, sizeof(ops));
  ops.create_state = h_create;
  ops.clone_state = h_clone;
  ops.destroy_state = h_destroy;
  ops.apply = h_apply;
  ops.query = h_query;
  ops.clone_payload = h_clone_payload;
  ops.destroy_payload = h_destroy_payload;
  h1 = ds_history_create(&ops, NULL);
  assert(h1 != NULL);
  main1 = ds_history_main(h1);
  assert(ds_history_branch_insert_at(main1, 10UL, HSET, make_payload(1U, 11)) != 0UL);
  assert(ds_history_branch_insert_at(main1, 20UL, HSET, make_payload(2U, 22)) != 0UL);
  feature1 = ds_history_branch_create(h1, main1, 15UL, "feature");
  assert(feature1 != NULL);
  assert(ds_history_branch_insert_at(feature1, 16UL, HSET, make_payload(3U, 33)) != 0UL);
  assert(ds_history_branch_insert_at(feature1, 18UL, HDEL, make_payload(1U, 0)) != 0UL);
  memset(&io, 0, sizeof(io));
  assert(ds_history_serialize(h1, mem_write, payload_write, &io, NULL));
  io.pos = 0U;
  h2 = ds_history_deserialize(&ops, NULL, mem_read, payload_read, &io, NULL);
  assert(h2 != NULL);
  main2 = ds_history_main(h2);
  feature2 = NULL;
  assert(ds_history_branch_name(main2) != NULL);
  feature2 = ds_history_branch_find_name(h2, "feature");
  assert(feature2 != NULL);
  assert(query_int(main1, 20UL, 2U) == query_int(main2, 20UL, 2U));
  assert(query_int(feature2, 16UL, 3U) == 33);
  key = 1U;
  query.kind = HGET;
  query.payload = &key;
  assert(ds_history_branch_query_at(feature2, 18UL, &query) == NULL);
  assert(ds_history_branch_parent(feature2) == main2);
  {
    ds_history_branch_t *merged;
    ds_history_t *h3;
    assert(ds_history_branch_merge(main1, feature1, 16UL, 18UL,
                                    DS_HISTORY_MERGE_PRESERVE_TIME) == 2U);
    assert(query_int(main1, 16UL, 3U) == 33);
    key = 1U;
    query.kind = HGET;
    query.payload = &key;
    assert(ds_history_branch_query_at(main1, 18UL, &query) == NULL);
    memset(&io, 0, sizeof(io));
    assert(ds_history_serialize_portable(h1, mem_write, payload_write_portable,
                                          &io, NULL));
    io.pos = 0U;
    h3 = ds_history_deserialize_portable(&ops, NULL, mem_read,
                                          payload_read_portable, &io, NULL);
    assert(h3 != NULL);
    merged = ds_history_main(h3);
    assert(query_int(merged, 16UL, 3U) == 33);
    assert(ds_history_branch_query_at(merged, 18UL, &query) == NULL);
    ds_history_destroy(h3);
  }
  ds_history_destroy(h2);
  ds_history_destroy(h1);
}

int main(void) {
  test_persistent_trie_remove();
  test_persistent_rbt_many_versions();
  test_graph_algorithms();
  test_history_serialization();
  puts("persistence_graph_history_test passed");
  return 0;
}
