/* Branch a map-backed inventory, resolve edits by key, archive and reopen it.
 * Usage: history_catalog OUTPUT.history (replaces this example's archive). */
#include <dsa/structures/history_adapters.h>
#include <dsa/structures/ordered_iface.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct edit {
  ds_history_keyed_record_t record;
  unsigned long key;
  int quantity;
} edit_t;

typedef struct inventory_config {
  const ds_map_ops_t *map;
} inventory_config_t;

static edit_t *edit_create(unsigned long key, int quantity) {
  edit_t *e = (edit_t *)ds_context_alloc(NULL, sizeof(*e));
  if (e != NULL) {
    e->key = key;
    e->quantity = quantity;
    e->record.key = &e->key;
    e->record.key_size = sizeof(e->key);
    e->record.value = &e->quantity;
  }
  return e;
}

static void *create_inventory(void *config) {
  inventory_config_t *c = (inventory_config_t *)config;
  ds_map_t *map = (ds_map_t *)ds_context_alloc(NULL, sizeof(*map));
  if (map != NULL && ds_map_init(map, c->map, sizeof(unsigned long),
                                 sizeof(int), ds_map_hash_ulong,
                                 ds_map_equal_ulong, NULL, NULL) != DS_OK) {
    ds_context_free(NULL, map);
    map = NULL;
  }
  return map;
}

static void destroy_inventory(void *state) {
  ds_map_t *map = (ds_map_t *)state;
  if (map != NULL) {
    ds_map_destroy(map);
    ds_context_free(NULL, map);
  }
}

static void *clone_inventory(void *state, void *user) {
  ds_map_t *source = (ds_map_t *)state;
  ds_map_t *copy = (ds_map_t *)create_inventory(user);
  size_t cursor = 0;
  const void *key;
  void *value;
  ds_status_t st;
  if (copy == NULL) {
    return NULL;
  }
  while ((st = ds_map_next_status(source, &cursor, &key, &value)) == DS_OK) {
    st = ds_map_put(copy, key, value);
    if (st != DS_OK) {
      destroy_inventory(copy);
      return NULL;
    }
  }
  if (st != DS_NOT_FOUND) {
    destroy_inventory(copy);
    return NULL;
  }
  return copy;
}

static int apply_edit(void *state, const ds_history_operation_t *op) {
  const edit_t *e = (const edit_t *)op->payload;
  ds_status_t st;
  if (op->kind == 0) {
    return 0; /* successful NULL-payload marker */
  }
  if (e == NULL) {
    return -1;
  }
  st = op->kind == 1 ? ds_map_put((ds_map_t *)state, &e->key, &e->quantity)
                     : ds_map_remove((ds_map_t *)state, &e->key, NULL);
  return st == DS_OK || st == DS_EXISTS || st == DS_NOT_FOUND ? 0 : -1;
}

static void *clone_edit(int kind, void *payload, void *user) {
  const edit_t *e = (const edit_t *)payload;
  (void)kind;
  (void)user;
  return e == NULL ? NULL : edit_create(e->key, e->quantity);
}

static void destroy_edit(int kind, void *payload) {
  (void)kind;
  ds_context_free(NULL, payload);
}

static ds_status_t project_edit(const ds_history_operation_t *op, void *key,
                                size_t size, void *user) {
  return op->kind == 0 ? DS_NOT_FOUND
                       : ds_history_record_key(op, key, size, user);
}

static bool write_edit(const ds_history_operation_t *op,
                       ds_history_write_func_t write, void *io, void *user) {
  const edit_t *e = (const edit_t *)op->payload;
  (void)user;
  if (op->kind == 0) {
    return true;
  }
  return e != NULL && ds_history_write_ulong(write, io, e->key) &&
         ds_history_write_int(write, io, e->quantity);
}

static ds_status_t read_edit(unsigned long id, unsigned long time, int kind,
                             ds_history_read_func_t read, void *io, void *user,
                             void **out) {
  unsigned long key;
  int quantity;
  (void)id;
  (void)time;
  (void)user;
  *out = NULL;
  if (kind == 0) {
    return DS_OK;
  }
  if (kind != 1 && kind != 2) {
    return DS_ERR_INVALID;
  }
  if (!ds_history_read_ulong(read, io, &key) ||
      !ds_history_read_int(read, io, &quantity)) {
    return DS_ERR_INVALID;
  }
  *out = edit_create(key, quantity);
  return *out == NULL ? DS_ERR_ALLOC : DS_OK;
}

static ds_status_t append_edit(ds_history_branch_t *branch, unsigned long time,
                               unsigned long key, int quantity) {
  edit_t *e = edit_create(key, quantity);
  unsigned long id;
  ds_status_t st;
  if (e == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ds_history_branch_insert_at_ex(branch, time, 1, e, &id);
  if (st != DS_OK) {
    destroy_edit(1, e);
  }
  return st;
}

static ds_status_t show_inventory(ds_history_t *history,
                                  ds_history_branch_t *b) {
  ds_map_t *map = (ds_map_t *)ds_history_branch_snapshot_head(b);
  unsigned long key;
  if (map == NULL) {
    return DS_ERR_STATE;
  }
  for (key = 1; key <= 3; key++) {
    void *value = ds_map_get(map, &key);
    if (value != NULL) {
      int quantity;
      memcpy(&quantity, value, sizeof(quantity));
      printf("  product %lu: %d\n", key, quantity);
    }
  }
  ds_history_snapshot_destroy(history, map);
  return DS_OK;
}

int main(int argc, char **argv) {
  ds_history_ops_t ops;
  inventory_config_t config;
  ds_history_t *history = NULL, *reopened = NULL;
  ds_history_branch_t *main_branch, *draft;
  ds_history_keyed_merge_t merge;
  ds_history_file_t archive;
  ds_history_load_options_t limits;
  ds_status_t st, close_status;
  unsigned long marker_id;
  size_t merged = 0;
  if (argc != 2) {
    fprintf(stderr, "usage: %s OUTPUT.history\n", argv[0]);
    return 1;
  }
  memset(&ops, 0, sizeof(ops));
  memset(&archive, 0, sizeof(archive));
  ops.create_state = create_inventory;
  ops.clone_state = clone_inventory;
  ops.destroy_state = destroy_inventory;
  ops.apply = apply_edit;
  ops.clone_payload = clone_edit;
  ops.destroy_payload = destroy_edit;
  config.map = ds_map_swiss_ops();
  st = ds_history_create_context(&ops, &config, NULL, &history);
  if (st != DS_OK) {
    goto done;
  }
  ds_history_set_clone_data(history, &config);
  main_branch = ds_history_main(history);
  st = append_edit(main_branch, 1, 1, 10);
  if (st == DS_OK) {
    st = ds_history_branch_create_ex(history, main_branch, 1, "draft", &draft);
  }
  if (st == DS_OK) {
    st = append_edit(main_branch, 3, 1, 12);
  }
  if (st == DS_OK) {
    st = append_edit(draft, 2, 1, 11);
  }
  if (st == DS_OK) {
    st = append_edit(draft, 4, 2, 7);
  }
  if (st == DS_OK) {
    st = append_edit(draft, 5, 2, 8);
  }
  if (st != DS_OK) {
    goto done;
  }
  ds_history_keyed_merge_init(&merge);
  merge.key_size = sizeof(unsigned long);
  merge.project = project_edit;
  merge.map =
      ds_map_open_ops(); /* independent of the inventory's map choice */
  st = ds_history_branch_merge_keyed(main_branch, draft, 2, ULONG_MAX,
                                     DS_HISTORY_MERGE_APPEND_AFTER_HEAD,
                                     &merge, &merged);
  if (st != DS_OK) {
    goto done;
  }
  printf("Merged %lu edits; newer target quantity retained:\n",
         (unsigned long)merged);
  st = show_inventory(history, main_branch);
  if (st == DS_OK) {
    st = ds_history_branch_append_ex(main_branch, 0, NULL, &marker_id);
  }
  if (st == DS_OK) {
    st = ds_history_file_open(&archive, argv[1], "w+b", 1024 * 1024);
  }
  if (st == DS_OK) {
    st = ds_history_serialize_file(history, &archive, write_edit, NULL);
  }
  if (st == DS_OK) {
    st = ds_history_file_rewind(&archive);
  }
  ds_history_load_options_init(&limits);
  limits.max_branches = 16;
  limits.max_operations = 1000;
  limits.max_name_bytes = 128;
  limits.index_ops = ds_ordered_ab_ops();
  if (st == DS_OK) {
    st = ds_history_deserialize_file(&ops, &config, &archive, read_edit, NULL,
                                     &limits, &reopened);
  }
  if (st == DS_OK) {
    ds_history_set_clone_data(reopened, &config);
    puts("Reopened portable archive (including its NULL-payload marker):");
    st = show_inventory(reopened, ds_history_main(reopened));
  }
done:
  close_status = ds_history_file_close(&archive);
  if (st == DS_OK) {
    st = close_status;
  }
  ds_history_destroy(reopened);
  ds_history_destroy(history);
  if (st != DS_OK) {
    fprintf(stderr, "%s\n", ds_status_name(st));
  }
  return st == DS_OK ? 0 : 1;
}
