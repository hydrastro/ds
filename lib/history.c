#include "history.h"
#include <stdlib.h>
#include <string.h>

struct ds_history_operation_node;
struct ds_history_checkpoint_node;
struct ds_history_branch_node;

struct ds_history_operation_node {
  ds_history_operation_t operation;
  struct ds_history_operation_node *next;
};

struct ds_history_checkpoint_node {
  unsigned long time;
  void *state;
  struct ds_history_checkpoint_node *next;
};

struct ds_history_branch {
  ds_history_t *history;
  unsigned long id;
  unsigned long fork_time;
  char *name;
  ds_history_branch_t *parent;
  struct ds_history_operation_node *operations;
  struct ds_history_checkpoint_node *checkpoints;
};

struct ds_history_branch_node {
  ds_history_branch_t *branch;
  struct ds_history_branch_node *next;
};

struct ds_history {
  ds_history_ops_t ops;
  void *config;
  void *clone_data;
  size_t checkpoint_interval;
  unsigned long next_operation_id;
  unsigned long next_branch_id;
  size_t transaction_depth;
  bool transaction_dirty;
  ds_history_branch_t *main_branch;
  struct ds_history_branch_node *branches;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
  mutex_t lock;
  bool is_thread_safe;
};

static void *history_default_allocator(size_t size);
static void history_default_deallocator(void *ptr);
static char *history_strdup(ds_history_t *history, const char *str);
static ds_history_branch_t *history_branch_alloc(ds_history_t *history,
                                                 ds_history_branch_t *parent,
                                                 unsigned long fork_time,
                                                 const char *name);
static void history_branch_destroy(ds_history_branch_t *branch);
static bool history_branch_register(ds_history_t *history,
                                    ds_history_branch_t *branch);
static void history_destroy_operation_list(ds_history_t *history,
                                           struct ds_history_operation_node *op);
static void history_destroy_checkpoint_list(
    ds_history_t *history, struct ds_history_checkpoint_node *checkpoint);
static void history_destroy_checkpoints_from(ds_history_branch_t *branch,
                                             unsigned long time);
static void history_destroy_all_checkpoints(ds_history_branch_t *branch);
static void history_destroy_all_branch_checkpoints(ds_history_t *history);
static void history_note_edit(ds_history_branch_t *branch, unsigned long edit_time);
static void history_invalidate_descendants(ds_history_branch_t *branch,
                                           unsigned long edit_time);
static void history_invalidate_after_edit(ds_history_branch_t *branch,
                                          unsigned long edit_time);
static struct ds_history_checkpoint_node *history_find_checkpoint(
    ds_history_branch_t *branch, unsigned long time);
static struct ds_history_checkpoint_node *history_find_exact_checkpoint(
    ds_history_branch_t *branch, unsigned long time);
static bool history_store_checkpoint(ds_history_branch_t *branch,
                                     unsigned long time, void *state);
static int history_replay_branch_ops(ds_history_branch_t *branch, void *state,
                                     unsigned long lower_time,
                                     bool lower_is_checkpoint,
                                     unsigned long upper_time,
                                     size_t *replayed_count);
static void *history_materialize(ds_history_branch_t *branch,
                                 unsigned long time,
                                 size_t *replayed_count);
static unsigned long history_branch_local_head_time(ds_history_branch_t *branch);
static bool history_branch_has_descendant(ds_history_t *history,
                                          ds_history_branch_t *ancestor,
                                          ds_history_branch_t *branch);
static bool history_time_before_fork(ds_history_branch_t *branch,
                                     unsigned long time);

static void *history_default_allocator(size_t size) { return malloc(size); }

static void history_default_deallocator(void *ptr) { free(ptr); }

static char *history_strdup(ds_history_t *history, const char *str) {
  size_t len;
  char *copy;

  if (str == NULL) {
    return NULL;
  }

  len = strlen(str) + 1U;
  copy = (char *)history->allocator(len);
  if (copy == NULL) {
    return NULL;
  }
  memcpy(copy, str, len);
  return copy;
}

static ds_history_branch_t *history_branch_alloc(ds_history_t *history,
                                                 ds_history_branch_t *parent,
                                                 unsigned long fork_time,
                                                 const char *name) {
  ds_history_branch_t *branch;

  branch = (ds_history_branch_t *)history->allocator(sizeof(*branch));
  if (branch == NULL) {
    return NULL;
  }

  branch->history = history;
  branch->id = history->next_branch_id++;
  branch->fork_time = fork_time;
  branch->name = history_strdup(history, name);
  branch->parent = parent;
  branch->operations = NULL;
  branch->checkpoints = NULL;

  if (name != NULL && branch->name == NULL) {
    history->deallocator(branch);
    return NULL;
  }

  return branch;
}

static bool history_branch_register(ds_history_t *history,
                                    ds_history_branch_t *branch) {
  struct ds_history_branch_node *node;

  node = (struct ds_history_branch_node *)history->allocator(sizeof(*node));
  if (node == NULL) {
    return false;
  }

  node->branch = branch;
  node->next = history->branches;
  history->branches = node;
  return true;
}

static void history_destroy_operation_list(ds_history_t *history,
                                           struct ds_history_operation_node *op) {
  struct ds_history_operation_node *next;

  while (op != NULL) {
    next = op->next;
    if (history->ops.destroy_payload != NULL) {
      history->ops.destroy_payload(op->operation.kind, op->operation.payload);
    }
    history->deallocator(op);
    op = next;
  }
}

static void history_destroy_checkpoint_list(
    ds_history_t *history, struct ds_history_checkpoint_node *checkpoint) {
  struct ds_history_checkpoint_node *next;

  while (checkpoint != NULL) {
    next = checkpoint->next;
    if (history->ops.destroy_state != NULL) {
      history->ops.destroy_state(checkpoint->state);
    }
    history->deallocator(checkpoint);
    checkpoint = next;
  }
}

static void history_branch_destroy(ds_history_branch_t *branch) {
  ds_history_t *history;

  if (branch == NULL) {
    return;
  }

  history = branch->history;
  history_destroy_operation_list(history, branch->operations);
  history_destroy_checkpoint_list(history, branch->checkpoints);
  if (branch->name != NULL) {
    history->deallocator(branch->name);
  }
  history->deallocator(branch);
}

static void history_destroy_checkpoints_from(ds_history_branch_t *branch,
                                             unsigned long time) {
  ds_history_t *history;
  struct ds_history_checkpoint_node *checkpoint;
  struct ds_history_checkpoint_node *prev;
  struct ds_history_checkpoint_node *next;

  history = branch->history;
  checkpoint = branch->checkpoints;
  prev = NULL;

  while (checkpoint != NULL) {
    next = checkpoint->next;
    if (checkpoint->time >= time) {
      if (prev == NULL) {
        branch->checkpoints = next;
      } else {
        prev->next = next;
      }
      if (history->ops.destroy_state != NULL) {
        history->ops.destroy_state(checkpoint->state);
      }
      history->deallocator(checkpoint);
    } else {
      prev = checkpoint;
    }
    checkpoint = next;
  }
}

static void history_destroy_all_checkpoints(ds_history_branch_t *branch) {
  ds_history_t *history;

  history = branch->history;
  history_destroy_checkpoint_list(history, branch->checkpoints);
  branch->checkpoints = NULL;
}

static void history_destroy_all_branch_checkpoints(ds_history_t *history) {
  struct ds_history_branch_node *node;

  node = history->branches;
  while (node != NULL) {
    history_destroy_all_checkpoints(node->branch);
    node = node->next;
  }
}

static bool history_branch_has_descendant(ds_history_t *history,
                                          ds_history_branch_t *ancestor,
                                          ds_history_branch_t *branch) {
  ds_history_branch_t *walk;

  (void)history;
  walk = branch->parent;
  while (walk != NULL) {
    if (walk == ancestor) {
      return true;
    }
    walk = walk->parent;
  }

  return false;
}

static void history_invalidate_descendants(ds_history_branch_t *branch,
                                           unsigned long edit_time) {
  ds_history_t *history;
  struct ds_history_branch_node *node;
  ds_history_branch_t *child;

  history = branch->history;
  node = history->branches;

  while (node != NULL) {
    child = node->branch;
    if (child != branch && history_branch_has_descendant(history, branch, child) &&
        child->fork_time >= edit_time) {
      history_destroy_all_checkpoints(child);
    }
    node = node->next;
  }
}

static void history_invalidate_after_edit(ds_history_branch_t *branch,
                                          unsigned long edit_time) {
  history_destroy_checkpoints_from(branch, edit_time);
  history_invalidate_descendants(branch, edit_time);
}

static void history_note_edit(ds_history_branch_t *branch, unsigned long edit_time) {
  ds_history_t *history;

  history = branch->history;
  if (history->transaction_depth != 0U) {
    if (!history->transaction_dirty) {
      history_destroy_all_branch_checkpoints(history);
    }
    history->transaction_dirty = true;
  } else {
    history_invalidate_after_edit(branch, edit_time);
  }
}

static struct ds_history_checkpoint_node *history_find_checkpoint(
    ds_history_branch_t *branch, unsigned long time) {
  struct ds_history_checkpoint_node *checkpoint;
  struct ds_history_checkpoint_node *best;

  checkpoint = branch->checkpoints;
  best = NULL;

  while (checkpoint != NULL) {
    if (checkpoint->time <= time &&
        (best == NULL || checkpoint->time > best->time)) {
      best = checkpoint;
    }
    checkpoint = checkpoint->next;
  }

  return best;
}

static struct ds_history_checkpoint_node *history_find_exact_checkpoint(
    ds_history_branch_t *branch, unsigned long time) {
  struct ds_history_checkpoint_node *checkpoint;

  checkpoint = branch->checkpoints;
  while (checkpoint != NULL) {
    if (checkpoint->time == time) {
      return checkpoint;
    }
    checkpoint = checkpoint->next;
  }

  return NULL;
}

static bool history_store_checkpoint(ds_history_branch_t *branch,
                                     unsigned long time, void *state) {
  ds_history_t *history;
  struct ds_history_checkpoint_node *checkpoint;
  struct ds_history_checkpoint_node *prev;
  struct ds_history_checkpoint_node *walk;
  void *copy;

  history = branch->history;
  if (history->ops.clone_state == NULL) {
    return false;
  }

  copy = history->ops.clone_state(state, history->clone_data);
  if (copy == NULL) {
    return false;
  }

  checkpoint = history_find_exact_checkpoint(branch, time);
  if (checkpoint != NULL) {
    if (history->ops.destroy_state != NULL) {
      history->ops.destroy_state(checkpoint->state);
    }
    checkpoint->state = copy;
    return true;
  }

  checkpoint = (struct ds_history_checkpoint_node *)history->allocator(
      sizeof(*checkpoint));
  if (checkpoint == NULL) {
    if (history->ops.destroy_state != NULL) {
      history->ops.destroy_state(copy);
    }
    return false;
  }

  checkpoint->time = time;
  checkpoint->state = copy;
  checkpoint->next = NULL;

  prev = NULL;
  walk = branch->checkpoints;
  while (walk != NULL && walk->time < time) {
    prev = walk;
    walk = walk->next;
  }

  checkpoint->next = walk;
  if (prev == NULL) {
    branch->checkpoints = checkpoint;
  } else {
    prev->next = checkpoint;
  }

  return true;
}

static int history_replay_branch_ops(ds_history_branch_t *branch, void *state,
                                     unsigned long lower_time,
                                     bool lower_is_checkpoint,
                                     unsigned long upper_time,
                                     size_t *replayed_count) {
  struct ds_history_operation_node *op;
  bool after_lower;
  int rc;

  op = branch->operations;
  while (op != NULL) {
    if (lower_is_checkpoint) {
      after_lower = op->operation.time > lower_time;
    } else {
      after_lower = op->operation.time >= lower_time;
    }

    if (after_lower && op->operation.time <= upper_time) {
      rc = branch->history->ops.apply(state, &op->operation);
      if (rc != 0) {
        return rc;
      }
      if (replayed_count != NULL) {
        (*replayed_count)++;
      }
    }
    op = op->next;
  }

  return 0;
}

static bool history_time_before_fork(ds_history_branch_t *branch,
                                     unsigned long time) {
  return branch->parent != NULL && time < branch->fork_time;
}

static void *history_materialize(ds_history_branch_t *branch,
                                 unsigned long time,
                                 size_t *replayed_count) {
  ds_history_t *history;
  struct ds_history_checkpoint_node *checkpoint;
  void *state;
  unsigned long lower_time;
  bool lower_is_checkpoint;
  int rc;

  history = branch->history;

  if (history_time_before_fork(branch, time)) {
    return history_materialize(branch->parent, time, replayed_count);
  }

  checkpoint = history_find_checkpoint(branch, time);
  if (checkpoint != NULL && history->ops.clone_state != NULL) {
    state = history->ops.clone_state(checkpoint->state, history->clone_data);
    if (state == NULL) {
      return NULL;
    }
    lower_time = checkpoint->time;
    lower_is_checkpoint = true;
  } else {
    if (branch->parent != NULL) {
      state = history_materialize(branch->parent, branch->fork_time,
                                  replayed_count);
    } else {
      state = history->ops.create_state(history->config);
    }
    if (state == NULL) {
      return NULL;
    }
    lower_time = branch->fork_time;
    lower_is_checkpoint = false;
  }

  rc = history_replay_branch_ops(branch, state, lower_time, lower_is_checkpoint,
                                 time, replayed_count);
  if (rc != 0) {
    if (history->ops.destroy_state != NULL) {
      history->ops.destroy_state(state);
    }
    return NULL;
  }

  return state;
}

static unsigned long history_branch_local_head_time(ds_history_branch_t *branch) {
  struct ds_history_operation_node *op;
  unsigned long head;

  head = branch->fork_time;
  op = branch->operations;
  while (op != NULL) {
    if (op->operation.time > head) {
      head = op->operation.time;
    }
    op = op->next;
  }

  return head;
}

ds_history_t *FUNC(ds_history_create)(const ds_history_ops_t *ops,
                                      void *config) {
  return FUNC(ds_history_create_alloc)(ops, config, history_default_allocator,
                                       history_default_deallocator);
}

ds_history_t *FUNC(ds_history_create_alloc)(const ds_history_ops_t *ops,
                                            void *config,
                                            void *(*allocator)(size_t),
                                            void (*deallocator)(void *)) {
  ds_history_t *history;
  ds_history_branch_t *main_branch;

  if (ops == NULL || ops->create_state == NULL || ops->clone_state == NULL ||
      ops->destroy_state == NULL || ops->apply == NULL) {
    return NULL;
  }

  if (allocator == NULL) {
    allocator = history_default_allocator;
  }
  if (deallocator == NULL) {
    deallocator = history_default_deallocator;
  }

  history = (ds_history_t *)allocator(sizeof(*history));
  if (history == NULL) {
    return NULL;
  }

  history->ops = *ops;
  history->config = config;
  history->clone_data = NULL;
  history->checkpoint_interval = 32U;
  history->next_operation_id = 1UL;
  history->next_branch_id = 1UL;
  history->transaction_depth = 0U;
  history->transaction_dirty = false;
  history->main_branch = NULL;
  history->branches = NULL;
  history->allocator = allocator;
  history->deallocator = deallocator;
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(history);
#else
  history->is_thread_safe = false;
#endif

  main_branch = history_branch_alloc(history, NULL, 0UL, "main");
  if (main_branch == NULL) {
    if (history->is_thread_safe) {
      LOCK_DESTROY(history);
    }
    deallocator(history);
    return NULL;
  }

  if (!history_branch_register(history, main_branch)) {
    history_branch_destroy(main_branch);
    if (history->is_thread_safe) {
      LOCK_DESTROY(history);
    }
    deallocator(history);
    return NULL;
  }

  history->main_branch = main_branch;
  return history;
}

void FUNC(ds_history_destroy)(ds_history_t *history) {
  struct ds_history_branch_node *node;
  struct ds_history_branch_node *next;

  if (history == NULL) {
    return;
  }

  LOCK(history);
  node = history->branches;
  while (node != NULL) {
    next = node->next;
    history_branch_destroy(node->branch);
    history->deallocator(node);
    node = next;
  }
  history->branches = NULL;
  UNLOCK(history);
  if (history->is_thread_safe) {
    LOCK_DESTROY(history);
  }
  history->deallocator(history);
}

void FUNC(ds_history_set_clone_data)(ds_history_t *history, void *clone_data) {
  if (history == NULL) {
    return;
  }

  LOCK(history);
  history->clone_data = clone_data;
  UNLOCK(history);
}

void FUNC(ds_history_set_checkpoint_interval)(ds_history_t *history,
                                              size_t interval) {
  if (history == NULL) {
    return;
  }

  LOCK(history);
  history->checkpoint_interval = interval;
  UNLOCK(history);
}


void FUNC(ds_history_transaction_begin)(ds_history_t *history) {
  if (history == NULL) {
    return;
  }
  LOCK(history);
  history->transaction_depth++;
  UNLOCK(history);
}

void FUNC(ds_history_transaction_commit)(ds_history_t *history) {
  if (history == NULL) {
    return;
  }
  LOCK(history);
  if (history->transaction_depth != 0U) {
    history->transaction_depth--;
    if (history->transaction_depth == 0U) {
      history->transaction_dirty = false;
    }
  }
  UNLOCK(history);
}

bool FUNC(ds_history_in_transaction)(ds_history_t *history) {
  bool result;
  if (history == NULL) {
    return false;
  }
  LOCK(history);
  result = history->transaction_depth != 0U;
  UNLOCK(history);
  return result;
}

const ds_history_ops_t *FUNC(ds_history_get_ops)(ds_history_t *history) {
  if (history == NULL) {
    return NULL;
  }
  return &history->ops;
}

void *FUNC(ds_history_get_config)(ds_history_t *history) {
  if (history == NULL) {
    return NULL;
  }
  return history->config;
}

ds_history_branch_t *FUNC(ds_history_main)(ds_history_t *history) {
  if (history == NULL) {
    return NULL;
  }
  return history->main_branch;
}

ds_history_branch_t *FUNC(ds_history_branch_create)(ds_history_t *history,
                                                    ds_history_branch_t *parent,
                                                    unsigned long fork_time,
                                                    const char *name) {
  ds_history_branch_t *branch;

  if (history == NULL) {
    return NULL;
  }

  LOCK(history);
  if (parent == NULL) {
    parent = history->main_branch;
  }
  if (parent == NULL || parent->history != history) {
    UNLOCK(history);
    return NULL;
  }

  branch = history_branch_alloc(history, parent, fork_time, name);
  if (branch == NULL) {
    UNLOCK(history);
    return NULL;
  }

  if (!history_branch_register(history, branch)) {
    history_branch_destroy(branch);
    UNLOCK(history);
    return NULL;
  }

  UNLOCK(history);
  return branch;
}

const char *FUNC(ds_history_branch_name)(ds_history_branch_t *branch) {
  if (branch == NULL) {
    return NULL;
  }
  return branch->name;
}

unsigned long FUNC(ds_history_branch_id)(ds_history_branch_t *branch) {
  if (branch == NULL) {
    return 0UL;
  }
  return branch->id;
}

unsigned long FUNC(ds_history_branch_fork_time)(ds_history_branch_t *branch) {
  if (branch == NULL) {
    return 0UL;
  }
  return branch->fork_time;
}

unsigned long FUNC(ds_history_branch_head_time)(ds_history_branch_t *branch) {
  if (branch == NULL) {
    return 0UL;
  }
  return history_branch_local_head_time(branch);
}

ds_history_branch_t *FUNC(ds_history_branch_parent)(ds_history_branch_t *branch) {
  if (branch == NULL) {
    return NULL;
  }
  return branch->parent;
}

ds_history_operation_t *FUNC(ds_history_branch_find_op)(
    ds_history_branch_t *branch, unsigned long op_id) {
  struct ds_history_operation_node *op;

  if (branch == NULL) {
    return NULL;
  }

  op = branch->operations;
  while (op != NULL) {
    if (op->operation.id == op_id) {
      return &op->operation;
    }
    op = op->next;
  }

  return NULL;
}

unsigned long FUNC(ds_history_branch_insert_at)(ds_history_branch_t *branch,
                                                unsigned long time, int kind,
                                                void *payload) {
  ds_history_t *history;
  struct ds_history_operation_node *node;
  struct ds_history_operation_node *walk;
  struct ds_history_operation_node *prev;

  if (branch == NULL) {
    return 0UL;
  }

  history = branch->history;
  LOCK(history);

  if (time < branch->fork_time) {
    UNLOCK(history);
    return 0UL;
  }

  node = (struct ds_history_operation_node *)history->allocator(sizeof(*node));
  if (node == NULL) {
    UNLOCK(history);
    return 0UL;
  }

  node->operation.id = history->next_operation_id++;
  node->operation.time = time;
  node->operation.kind = kind;
  node->operation.payload = payload;
  node->next = NULL;

  prev = NULL;
  walk = branch->operations;
  while (walk != NULL &&
         (walk->operation.time < time ||
          (walk->operation.time == time &&
           walk->operation.id < node->operation.id))) {
    prev = walk;
    walk = walk->next;
  }

  node->next = walk;
  if (prev == NULL) {
    branch->operations = node;
  } else {
    prev->next = node;
  }

  history_note_edit(branch, time);
  UNLOCK(history);
  return node->operation.id;
}

unsigned long FUNC(ds_history_branch_append)(ds_history_branch_t *branch,
                                             int kind, void *payload) {
  unsigned long time;

  if (branch == NULL) {
    return 0UL;
  }

  time = FUNC(ds_history_branch_head_time)(branch);
  if (time != ~0UL) {
    time++;
  }
  return FUNC(ds_history_branch_insert_at)(branch, time, kind, payload);
}

bool FUNC(ds_history_branch_delete_op)(ds_history_branch_t *branch,
                                       unsigned long op_id) {
  ds_history_t *history;
  struct ds_history_operation_node *op;
  struct ds_history_operation_node *prev;
  unsigned long edit_time;

  if (branch == NULL) {
    return false;
  }

  history = branch->history;
  LOCK(history);

  op = branch->operations;
  prev = NULL;
  while (op != NULL && op->operation.id != op_id) {
    prev = op;
    op = op->next;
  }

  if (op == NULL) {
    UNLOCK(history);
    return false;
  }

  edit_time = op->operation.time;
  if (prev == NULL) {
    branch->operations = op->next;
  } else {
    prev->next = op->next;
  }

  if (history->ops.destroy_payload != NULL) {
    history->ops.destroy_payload(op->operation.kind, op->operation.payload);
  }
  history->deallocator(op);

  history_note_edit(branch, edit_time);
  UNLOCK(history);
  return true;
}

void *FUNC(ds_history_branch_snapshot_at)(ds_history_branch_t *branch,
                                          unsigned long time) {
  ds_history_t *history;
  void *state;
  size_t replayed_count;

  if (branch == NULL) {
    return NULL;
  }

  history = branch->history;
  LOCK(history);
  replayed_count = 0U;
  state = history_materialize(branch, time, &replayed_count);
  if (state != NULL && history->checkpoint_interval != 0U &&
      replayed_count >= history->checkpoint_interval) {
    (void)history_store_checkpoint(branch, time, state);
  }
  UNLOCK(history);
  return state;
}

void *FUNC(ds_history_branch_snapshot_head)(ds_history_branch_t *branch) {
  if (branch == NULL) {
    return NULL;
  }
  return FUNC(ds_history_branch_snapshot_at)(branch,
                                             FUNC(ds_history_branch_head_time)(branch));
}


void FUNC(ds_history_snapshot_destroy)(ds_history_t *history, void *snapshot) {
  if (history == NULL || snapshot == NULL) {
    return;
  }

  history->ops.destroy_state(snapshot);
}

void *FUNC(ds_history_branch_query_at)(ds_history_branch_t *branch,
                                       unsigned long time,
                                       const ds_history_query_t *query) {
  ds_history_t *history;
  void *state;
  void *result;
  size_t replayed_count;

  if (branch == NULL || query == NULL) {
    return NULL;
  }

  history = branch->history;
  if (history->ops.query == NULL) {
    return NULL;
  }

  LOCK(history);
  replayed_count = 0U;
  state = history_materialize(branch, time, &replayed_count);
  if (state == NULL) {
    UNLOCK(history);
    return NULL;
  }

  if (history->checkpoint_interval != 0U &&
      replayed_count >= history->checkpoint_interval) {
    (void)history_store_checkpoint(branch, time, state);
  }

  result = history->ops.query(state, query);
  history->ops.destroy_state(state);
  UNLOCK(history);
  return result;
}

void *FUNC(ds_history_branch_query_head)(ds_history_branch_t *branch,
                                         const ds_history_query_t *query) {
  if (branch == NULL) {
    return NULL;
  }
  return FUNC(ds_history_branch_query_at)(branch,
                                          FUNC(ds_history_branch_head_time)(branch),
                                          query);
}


size_t FUNC(ds_history_branch_export_ops)(ds_history_branch_t *branch,
                                           ds_history_export_op_func_t writer,
                                           void *user) {
  struct ds_history_operation_node *op;
  size_t count;

  if (branch == NULL || writer == NULL) {
    return 0U;
  }
  count = 0U;
  op = branch->operations;
  while (op != NULL) {
    if (!writer(&op->operation, user)) {
      break;
    }
    count++;
    op = op->next;
  }
  return count;
}
