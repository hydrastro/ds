/* One set of records, two independent intrusive indexes, context allocation,
 * checked edits, cloned snapshots and callback-local state. */
#include "ds.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct record {
  ds_btree_node_t by_group;
  ds_treap_node_t by_id;
  unsigned group, id;
} record_t;

typedef struct ordering {
  bool descending;
} ordering_t;
typedef struct totals {
  size_t count;
  unsigned long ids;
} totals_t;

static int group_order(const ds_btree_node_t *a, const ds_btree_node_t *b,
                       void *user) {
  const record_t *x = DS_CONTAINER_OF_CONST(a, record_t, by_group);
  const record_t *y = DS_CONTAINER_OF_CONST(b, record_t, by_group);
  const ordering_t *order = (const ordering_t *)user;
  int cmp = (x->group > y->group) - (x->group < y->group);
  return order->descending ? -cmp : cmp;
}
static int id_order(const ds_treap_node_t *a, const ds_treap_node_t *b,
                    void *user) {
  const record_t *x = DS_CONTAINER_OF_CONST(a, record_t, by_id);
  const record_t *y = DS_CONTAINER_OF_CONST(b, record_t, by_id);
  const ordering_t *order = (const ordering_t *)user;
  int cmp = (x->id > y->id) - (x->id < y->id);
  return order->descending ? -cmp : cmp;
}
static ds_status_t total_group(ds_btree_node_t *node, void *user) {
  record_t *r = DS_CONTAINER_OF(node, record_t, by_group);
  totals_t *total = (totals_t *)user;
  total->count++;
  total->ids += r->id;
  return DS_OK;
}
static ds_status_t total_id(ds_treap_node_t *node, void *user) {
  record_t *r = DS_CONTAINER_OF(node, record_t, by_id);
  totals_t *total = (totals_t *)user;
  total->count++;
  total->ids += r->id;
  return DS_OK;
}
static ds_status_t copy_group(const ds_btree_node_t *node, void *user,
                              ds_btree_node_t **out) {
  const record_t *r = DS_CONTAINER_OF_CONST(node, record_t, by_group);
  record_t *copy =
      (record_t *)ds_context_alloc((ds_context_t *)user, sizeof(*copy));
  if (copy == NULL)
    return DS_ERR_ALLOC;
  memset(copy, 0, sizeof(*copy));
  copy->id = r->id;
  copy->group = r->group;
  *out = &copy->by_group;
  return DS_OK;
}
static ds_status_t copy_id(const ds_treap_node_t *node, void *user,
                           ds_treap_node_t **out) {
  const record_t *r = DS_CONTAINER_OF_CONST(node, record_t, by_id);
  record_t *copy =
      (record_t *)ds_context_alloc((ds_context_t *)user, sizeof(*copy));
  if (copy == NULL)
    return DS_ERR_ALLOC;
  memset(copy, 0, sizeof(*copy));
  copy->id = r->id;
  copy->group = r->group;
  *out = &copy->by_id;
  return DS_OK;
}
static void free_group(ds_btree_node_t *node, void *user) {
  ds_context_free((ds_context_t *)user,
                  DS_CONTAINER_OF(node, record_t, by_group));
}
static void free_id(ds_treap_node_t *node, void *user) {
  ds_context_free((ds_context_t *)user, DS_CONTAINER_OF(node, record_t, by_id));
}

int main(void) {
  record_t records[96];
  ds_context_t context;
  ordering_t order;
  ds_treap_config_t config;
  ds_btree_t *groups = NULL, *group_snapshot = NULL;
  ds_treap_t *ids = NULL, *id_snapshot = NULL;
  ds_status_t st;
  size_t i;
  totals_t group_total, id_total;
  memset(records, 0, sizeof(records));
  ds_context_init(&context);
  order.descending = false;
  ds_treap_config_init(&config);
  config.compare = id_order;
  config.user = &order;
  config.context = &context;
  st = ds_btree_create_context(2, &context, &groups);
  if (st == DS_OK)
    st = ds_treap_create_with(&config, &ids);
  for (i = 0; st == DS_OK && i < 96; i++) {
    records[i].id = (unsigned)(i * 37 % 96);
    records[i].group = records[i].id % 8;
    st =
        ds_btree_insert_with(groups, &records[i].by_group, group_order, &order);
    if (st == DS_OK)
      st = ds_treap_insert_ex(ids, &records[i].by_id);
  }
  if (st == DS_OK)
    st = ds_btree_clone_with(groups, copy_group, free_group, &context,
                             &group_snapshot);
  if (st == DS_OK)
    st = ds_treap_clone_with(ids, copy_id, free_id, &context, &id_snapshot);
  for (i = 0; st == DS_OK && i < 96; i += 3) {
    ds_btree_node_t *group_removed;
    ds_treap_node_t *id_removed;
    st = ds_btree_remove_ex(groups, &records[i].by_group, &group_removed);
    if (st == DS_OK)
      st = ds_treap_remove_ex(ids, &records[i].by_id, &id_removed);
  }
  memset(&group_total, 0, sizeof(group_total));
  memset(&id_total, 0, sizeof(id_total));
  if (st == DS_OK)
    st = ds_btree_walk_with(groups, total_group, &group_total);
  if (st == DS_OK)
    st = ds_treap_walk_with(ids, total_id, &id_total);
  if (st == DS_OK) {
    printf("Active group index: %lu records, id sum %lu\n",
           (unsigned long)group_total.count, group_total.ids);
    printf("Active id index: %lu records, id sum %lu\n",
           (unsigned long)id_total.count, id_total.ids);
    printf("Snapshots retain %lu and %lu records\n",
           (unsigned long)group_snapshot->size,
           (unsigned long)id_snapshot->size);
  }
  ds_btree_destroy_with(group_snapshot, free_group, &context);
  ds_treap_destroy_with(id_snapshot, free_id, &context);
  ds_btree_destroy_with(groups, NULL, NULL);
  ds_treap_destroy_with(ids, NULL, NULL);
  if (st != DS_OK)
    fprintf(stderr, "indexes: %s\n", ds_status_name(st));
  return st == DS_OK ? 0 : 1;
}
