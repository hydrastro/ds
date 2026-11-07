#include "ds.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_COUNT 16U
#define OP_COUNT 80U

enum { OP_SET = 1, OP_DEL = 2 };
enum { Q_GET = 1 };

struct state {
  bool present[KEY_COUNT];
  int value[KEY_COUNT];
};

struct payload {
  size_t key;
  int value;
};

struct oracle_op {
  unsigned long id;
  unsigned long time;
  int kind;
  struct payload payload;
  bool live;
};

static void *create_state(void *config) {
  (void)config;
  return calloc(1U, sizeof(struct state));
}

static void *clone_state(void *state, void *clone_data) {
  struct state *copy;
  (void)clone_data;
  copy = (struct state *)malloc(sizeof(*copy));
  assert(copy != NULL);
  memcpy(copy, state, sizeof(*copy));
  return copy;
}

static void destroy_state(void *state) { free(state); }

static int apply_op(void *state, const ds_history_operation_t *op) {
  struct state *s;
  struct payload *p;
  s = (struct state *)state;
  p = (struct payload *)op->payload;
  if (op->kind == OP_SET) {
    s->present[p->key] = true;
    s->value[p->key] = p->value;
    return 0;
  }
  if (op->kind == OP_DEL) {
    s->present[p->key] = false;
    return 0;
  }
  return -1;
}

static void *query_state(void *state, const ds_history_query_t *query) {
  struct state *s;
  size_t key;
  int *result;
  s = (struct state *)state;
  key = *(size_t *)query->payload;
  if (!s->present[key]) {
    return NULL;
  }
  result = (int *)malloc(sizeof(*result));
  assert(result != NULL);
  *result = s->value[key];
  return result;
}

static void *clone_payload(int kind, void *payload, void *clone_data) {
  struct payload *copy;
  (void)kind;
  (void)clone_data;
  copy = (struct payload *)malloc(sizeof(*copy));
  assert(copy != NULL);
  *copy = *(struct payload *)payload;
  return copy;
}

static void destroy_payload(int kind, void *payload) {
  (void)kind;
  free(payload);
}

static unsigned long rnd(unsigned long *seed) {
  *seed = (*seed * 1103515245UL + 12345UL) & 0x7fffffffUL;
  return *seed;
}

static struct payload *make_payload(size_t key, int value) {
  struct payload *payload;
  payload = (struct payload *)malloc(sizeof(*payload));
  assert(payload != NULL);
  payload->key = key;
  payload->value = value;
  return payload;
}

static bool oracle_get(struct oracle_op *ops, size_t count, unsigned long time,
                       size_t key, int *out) {
  size_t i;
  size_t best;
  bool found;
  best = 0U;
  found = false;
  for (i = 0U; i < count; i++) {
    if (ops[i].live && ops[i].time <= time &&
        (!found || ops[i].time < time + 1UL) && ops[i].payload.key == key) {
      if (!found || ops[i].time > ops[best].time ||
          (ops[i].time == ops[best].time && ops[i].id > ops[best].id)) {
        best = i;
        found = true;
      }
    }
  }
  if (!found || ops[best].kind == OP_DEL) {
    return false;
  }
  *out = ops[best].payload.value;
  return true;
}

int main(void) {
  ds_history_ops_t hops;
  ds_history_t *history;
  ds_history_branch_t *branch;
  struct oracle_op oracle[OP_COUNT];
  unsigned long seed;
  size_t i;
  size_t key;
  unsigned long time;
  int kind;
  int value;
  unsigned long id;
  ds_history_query_t query;
  int *actual;
  int expected;
  bool expected_found;

  hops.create_state = create_state;
  hops.clone_state = clone_state;
  hops.destroy_state = destroy_state;
  hops.apply = apply_op;
  hops.query = query_state;
  hops.clone_payload = clone_payload;
  hops.destroy_payload = destroy_payload;

  history = ds_history_create(&hops, NULL);
  assert(history != NULL);
  ds_history_set_checkpoint_interval(history, 3U);
  branch = ds_history_main(history);
  seed = 7UL;

  ds_history_transaction_begin(history);
  for (i = 0U; i < OP_COUNT; i++) {
    key = rnd(&seed) % KEY_COUNT;
    time = rnd(&seed) % 50UL;
    kind = (rnd(&seed) % 5UL) == 0UL ? OP_DEL : OP_SET;
    value = (int)(rnd(&seed) % 1000UL);
    id = ds_history_branch_insert_at(branch, time, kind,
                                     make_payload(key, value));
    assert(id != 0UL);
    oracle[i].id = id;
    oracle[i].time = time;
    oracle[i].kind = kind;
    oracle[i].payload.key = key;
    oracle[i].payload.value = value;
    oracle[i].live = true;
  }
  assert(ds_history_in_transaction(history));
  ds_history_transaction_commit(history);
  assert(!ds_history_in_transaction(history));

  for (i = 0U; i < OP_COUNT; i += 7U) {
    assert(ds_history_branch_delete_op(branch, oracle[i].id));
    oracle[i].live = false;
  }

  for (time = 0UL; time < 50UL; time++) {
    for (key = 0U; key < KEY_COUNT; key++) {
      query.kind = Q_GET;
      query.payload = &key;
      actual = (int *)ds_history_branch_query_at(branch, time, &query);
      expected_found = oracle_get(oracle, OP_COUNT, time, key, &expected);
      if (expected_found) {
        assert(actual != NULL);
        assert(*actual == expected);
        free(actual);
      } else {
        assert(actual == NULL);
      }
    }
  }

  ds_history_destroy(history);
  puts("history_oracle_test passed");
  return 0;
}
