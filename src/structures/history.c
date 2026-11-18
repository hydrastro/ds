#include <dsa/structures/history.h>
#include <dsa/structures/ordered_iface.h>
#include <limits.h>
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
  ds_context_t *context;
  bool use_context;
};

static char *history_strdup(ds_history_t *history, const char *str);
static ds_history_branch_t *history_branch_alloc(ds_history_t *history,
                                                 ds_history_branch_t *parent,
                                                 unsigned long fork_time,
                                                 const char *name);
static void history_branch_destroy(ds_history_branch_t *branch);
static bool history_branch_register(ds_history_t *history,
                                    ds_history_branch_t *branch);
static void
history_destroy_operation_list(ds_history_t *history,
                               struct ds_history_operation_node *op);
static void
history_destroy_checkpoint_list(ds_history_t *history,
                                struct ds_history_checkpoint_node *checkpoint);
static void history_destroy_checkpoints_from(ds_history_branch_t *branch,
                                             unsigned long time);
static void history_destroy_all_checkpoints(ds_history_branch_t *branch);
static void history_destroy_all_branch_checkpoints(ds_history_t *history);
static void history_note_edit(ds_history_branch_t *branch,
                              unsigned long edit_time);
static void history_invalidate_descendants(ds_history_branch_t *branch,
                                           unsigned long edit_time);
static void history_invalidate_after_edit(ds_history_branch_t *branch,
                                          unsigned long edit_time);
static struct ds_history_checkpoint_node *
history_find_checkpoint(ds_history_branch_t *branch, unsigned long time);
static struct ds_history_checkpoint_node *
history_find_exact_checkpoint(ds_history_branch_t *branch, unsigned long time);
static bool history_store_checkpoint(ds_history_branch_t *branch,
                                     unsigned long time, void *state);
static int history_replay_branch_ops(ds_history_branch_t *branch, void *state,
                                     unsigned long lower_time,
                                     bool lower_is_checkpoint,
                                     unsigned long upper_time,
                                     size_t *replayed_count);
static void *history_materialize(ds_history_branch_t *branch,
                                 unsigned long time, size_t *replayed_count);
static unsigned long
history_branch_local_head_time(ds_history_branch_t *branch);
static bool history_branch_has_descendant(ds_history_t *history,
                                          ds_history_branch_t *ancestor,
                                          ds_history_branch_t *branch);
static bool history_time_before_fork(ds_history_branch_t *branch,
                                     unsigned long time);
static unsigned long history_branch_insert_with_id(ds_history_branch_t *branch,
                                                   unsigned long id,
                                                   unsigned long time, int kind,
                                                   void *payload);
static size_t history_branch_operation_count(ds_history_branch_t *branch);

static void *history_alloc(ds_history_t *h, size_t bytes) {
  return h->use_context ? ds_context_alloc(h->context, bytes)
                        : h->allocator(bytes);
}

static void history_free(ds_history_t *h, void *p) {
  if (h->use_context)
    ds_context_free(h->context, p);
  else
    h->deallocator(p);
}

static char *history_strdup(ds_history_t *history, const char *str) {
  size_t len;
  char *copy;

  if (str == NULL) {
    return NULL;
  }

  len = strlen(str) + 1U;
  copy = (char *)history_alloc(history, len);
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

  if (history->next_branch_id == 0 || history->next_branch_id == ULONG_MAX)
    return NULL;
  branch = (ds_history_branch_t *)history_alloc(history, sizeof(*branch));
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
    history_free(history, branch);
    return NULL;
  }

  return branch;
}

static bool history_branch_register(ds_history_t *history,
                                    ds_history_branch_t *branch) {
  struct ds_history_branch_node *node;

  node = (struct ds_history_branch_node *)history_alloc(history, sizeof(*node));
  if (node == NULL) {
    return false;
  }

  node->branch = branch;
  node->next = history->branches;
  history->branches = node;
  return true;
}

static void
history_destroy_operation_list(ds_history_t *history,
                               struct ds_history_operation_node *op) {
  struct ds_history_operation_node *next;

  while (op != NULL) {
    next = op->next;
    if (history->ops.destroy_payload != NULL) {
      history->ops.destroy_payload(op->operation.kind, op->operation.payload);
    }
    history_free(history, op);
    op = next;
  }
}

static void
history_destroy_checkpoint_list(ds_history_t *history,
                                struct ds_history_checkpoint_node *checkpoint) {
  struct ds_history_checkpoint_node *next;

  while (checkpoint != NULL) {
    next = checkpoint->next;
    if (history->ops.destroy_state != NULL) {
      history->ops.destroy_state(checkpoint->state);
    }
    history_free(history, checkpoint);
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
    history_free(history, branch->name);
  }
  history_free(history, branch);
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
      history_free(history, checkpoint);
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
    if (child != branch &&
        history_branch_has_descendant(history, branch, child) &&
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

static void history_note_edit(ds_history_branch_t *branch,
                              unsigned long edit_time) {
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

static struct ds_history_checkpoint_node *
history_find_checkpoint(ds_history_branch_t *branch, unsigned long time) {
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

static struct ds_history_checkpoint_node *
history_find_exact_checkpoint(ds_history_branch_t *branch, unsigned long time) {
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

  checkpoint = (struct ds_history_checkpoint_node *)history_alloc(
      history, sizeof(*checkpoint));
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

/* where a materialization at time on branch starts: the branch itself,
 * or the ancestor whose own operations hold that time */
static ds_history_branch_t *history_owner(ds_history_branch_t *branch,
                                          unsigned long time) {
  while (history_time_before_fork(branch, time)) {
    branch = branch->parent;
  }
  return branch;
}

/* The state at time on branch: a checkpoint (or a fresh state at the root)
 * and every operation after it, through the chain of ancestors. The chain
 * is collected first, from branch upwards until a checkpoint or the root,
 * then replayed downwards, so a deep chain of forks costs an array, not
 * call frames. */
static void *history_materialize(ds_history_branch_t *branch,
                                 unsigned long time, size_t *replayed_count) {
  ds_history_t *history = branch->history;
  ds_history_branch_t **chain = NULL;
  unsigned long *times = NULL;
  size_t depth = 0;
  size_t cap = 0;
  struct ds_history_checkpoint_node *checkpoint = NULL;
  void *state = NULL;
  int rc = 0;
  branch = history_owner(branch, time);
  for (;;) {
    if (depth == cap) {
      size_t new_cap = cap == 0 ? 8 : cap * 2;
      ds_history_branch_t **c;
      unsigned long *t;
      size_t i;
      if (cap > (size_t)-1 / 2 / sizeof(*chain)) {
        break;
      }
      c = (ds_history_branch_t **)history_alloc(history, new_cap * sizeof(*c));
      t = (unsigned long *)history_alloc(history, new_cap * sizeof(*t));
      if (c == NULL || t == NULL) {
        if (c != NULL) {
          history_free(history, c);
        }
        if (t != NULL) {
          history_free(history, t);
        }
        break;
      }
      for (i = 0; i < depth; i++) {
        c[i] = chain[i];
        t[i] = times[i];
      }
      if (chain != NULL) {
        history_free(history, chain);
        history_free(history, times);
      }
      chain = c;
      times = t;
      cap = new_cap;
    }
    chain[depth] = branch;
    times[depth] = time;
    depth++;
    checkpoint = history_find_checkpoint(branch, time);
    if ((checkpoint != NULL && history->ops.clone_state != NULL) ||
        branch->parent == NULL) {
      break;
    }
    time = branch->fork_time;
    branch = history_owner(branch->parent, time);
  }
  if (depth > 0 && chain[depth - 1] == branch) {
    if (checkpoint != NULL && history->ops.clone_state != NULL) {
      state = history->ops.clone_state(checkpoint->state, history->clone_data);
    } else {
      checkpoint = NULL;
      state = history->ops.create_state(history->config);
    }
  }
  while (state != NULL && depth > 0) {
    ds_history_branch_t *b = chain[depth - 1];
    bool from_checkpoint = checkpoint != NULL;
    depth--;
    rc = history_replay_branch_ops(
        b, state, from_checkpoint ? checkpoint->time : b->fork_time,
        from_checkpoint, times[depth], replayed_count);
    checkpoint = NULL; /* only the deepest link starts from one */
    if (rc != 0) {
      if (history->ops.destroy_state != NULL) {
        history->ops.destroy_state(state);
      }
      state = NULL;
    }
  }
  if (chain != NULL) {
    history_free(history, chain);
    history_free(history, times);
  }
  return state;
}

/* Keeps a checkpoint of state, the state at time on branch, when its
 * replay was long enough. It goes to the branch that owns that time: kept
 * on a branch before its fork, it would stand for the parent's state
 * there, and a later replay on the branch would start from it and skip the
 * parent's operations up to the fork. None is kept once a transaction has
 * edited, as the transaction's later edits invalidate nothing. */
static void history_cache(ds_history_branch_t *branch, unsigned long time,
                          void *state, size_t replayed_count) {
  ds_history_t *history = branch->history;
  if (history->checkpoint_interval == 0U ||
      replayed_count < history->checkpoint_interval ||
      history->transaction_dirty) {
    return;
  }
  (void)history_store_checkpoint(history_owner(branch, time), time, state);
}

static unsigned long
history_branch_local_head_time(ds_history_branch_t *branch) {
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

ds_history_t *ds_history_create(const ds_history_ops_t *ops, void *config) {
  ds_history_t *history = NULL;
  (void)ds_history_create_context(ops, config, NULL, &history);
  return history;
}

static ds_history_t *history_create(const ds_history_ops_t *ops, void *config,
                                    void *(*allocator)(size_t),
                                    void (*deallocator)(void *),
                                    ds_context_t *context, bool use_context) {
  ds_history_t *history;
  ds_history_branch_t *main_branch;

  if (ops == NULL || ops->create_state == NULL || ops->clone_state == NULL ||
      ops->destroy_state == NULL || ops->apply == NULL) {
    return NULL;
  }

  if (!use_context && ((allocator == NULL) != (deallocator == NULL)))
    return NULL;
  if (allocator == NULL && deallocator == NULL)
    use_context = true;
  if (use_context && context == NULL)
    context = ds_default_context();

  history =
      (ds_history_t *)(use_context ? ds_context_alloc(context, sizeof(*history))
                                   : allocator(sizeof(*history)));
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
  history->context = context != NULL ? context : ds_default_context();
  history->use_context = use_context;
  history->allocator = allocator;
  history->deallocator = deallocator;

  main_branch = history_branch_alloc(history, NULL, 0UL, "main");
  if (main_branch == NULL) {
    history_free(history, history);
    return NULL;
  }

  if (!history_branch_register(history, main_branch)) {
    history_branch_destroy(main_branch);
    history_free(history, history);
    return NULL;
  }

  history->main_branch = main_branch;
  return history;
}

ds_history_t *ds_history_create_alloc(const ds_history_ops_t *ops, void *config,
                                      void *(*allocator)(size_t),
                                      void (*deallocator)(void *)) {
  return history_create(ops, config, allocator, deallocator, NULL, false);
}

ds_status_t ds_history_create_context(const ds_history_ops_t *ops, void *config,
                                      ds_context_t *context,
                                      ds_history_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (ops == NULL || ops->create_state == NULL || ops->clone_state == NULL ||
      ops->destroy_state == NULL || ops->apply == NULL)
    return DS_ERR_NULL;
  *out = history_create(ops, config, NULL, NULL, context, true);
  return *out == NULL ? DS_ERR_ALLOC : DS_OK;
}

void ds_history_destroy(ds_history_t *history) {
  struct ds_history_branch_node *node;
  struct ds_history_branch_node *next;

  if (history == NULL) {
    return;
  }

  node = history->branches;
  while (node != NULL) {
    next = node->next;
    history_branch_destroy(node->branch);
    history_free(history, node);
    node = next;
  }
  history->branches = NULL;
  history_free(history, history);
}

void ds_history_set_clone_data(ds_history_t *history, void *clone_data) {
  if (history == NULL) {
    return;
  }

  history->clone_data = clone_data;
}

void ds_history_set_checkpoint_interval(ds_history_t *history,
                                        size_t interval) {
  if (history == NULL) {
    return;
  }

  history->checkpoint_interval = interval;
}

ds_status_t ds_history_transaction_begin_ex(ds_history_t *history) {
  if (history == NULL)
    return DS_ERR_NULL;
  if (history->transaction_depth == (size_t)-1)
    return DS_ERR_OVERFLOW;
  history->transaction_depth++;
  return DS_OK;
}

void ds_history_transaction_begin(ds_history_t *history) {
  (void)ds_history_transaction_begin_ex(history);
}

void ds_history_transaction_commit(ds_history_t *history) {
  if (history == NULL) {
    return;
  }
  if (history->transaction_depth != 0U) {
    history->transaction_depth--;
    if (history->transaction_depth == 0U) {
      history->transaction_dirty = false;
    }
  }
}

bool ds_history_in_transaction(ds_history_t *history) {
  bool result;
  if (history == NULL) {
    return false;
  }
  result = history->transaction_depth != 0U;
  return result;
}

const ds_history_ops_t *ds_history_get_ops(ds_history_t *history) {
  if (history == NULL) {
    return NULL;
  }
  return &history->ops;
}

void *ds_history_get_config(ds_history_t *history) {
  if (history == NULL) {
    return NULL;
  }
  return history->config;
}

ds_history_branch_t *ds_history_main(ds_history_t *history) {
  if (history == NULL) {
    return NULL;
  }
  return history->main_branch;
}

ds_history_branch_t *ds_history_branch_create(ds_history_t *history,
                                              ds_history_branch_t *parent,
                                              unsigned long fork_time,
                                              const char *name) {
  ds_history_branch_t *branch;

  if (history == NULL) {
    return NULL;
  }

  if (parent == NULL) {
    parent = history->main_branch;
  }
  if (parent == NULL || parent->history != history) {
    return NULL;
  }

  branch = history_branch_alloc(history, parent, fork_time, name);
  if (branch == NULL) {
    return NULL;
  }

  if (!history_branch_register(history, branch)) {
    history_branch_destroy(branch);
    return NULL;
  }

  return branch;
}

const char *ds_history_branch_name(ds_history_branch_t *branch) {
  if (branch == NULL) {
    return NULL;
  }
  return branch->name;
}

unsigned long ds_history_branch_id(ds_history_branch_t *branch) {
  if (branch == NULL) {
    return 0UL;
  }
  return branch->id;
}

unsigned long ds_history_branch_fork_time(ds_history_branch_t *branch) {
  if (branch == NULL) {
    return 0UL;
  }
  return branch->fork_time;
}

unsigned long ds_history_branch_head_time(ds_history_branch_t *branch) {
  if (branch == NULL) {
    return 0UL;
  }
  return history_branch_local_head_time(branch);
}

ds_history_branch_t *ds_history_branch_parent(ds_history_branch_t *branch) {
  if (branch == NULL) {
    return NULL;
  }
  return branch->parent;
}

ds_history_branch_t *ds_history_branch_find_name(ds_history_t *history,
                                                 const char *name) {
  struct ds_history_branch_node *node;

  if (history == NULL || name == NULL) {
    return NULL;
  }
  node = history->branches;
  while (node != NULL) {
    if (node->branch->name != NULL && strcmp(node->branch->name, name) == 0) {
      return node->branch;
    }
    node = node->next;
  }
  return NULL;
}

ds_history_operation_t *ds_history_branch_find_op(ds_history_branch_t *branch,
                                                  unsigned long op_id) {
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

static unsigned long history_branch_insert_with_id(ds_history_branch_t *branch,
                                                   unsigned long id,
                                                   unsigned long time, int kind,
                                                   void *payload) {
  ds_history_t *history;
  struct ds_history_operation_node *node;
  struct ds_history_operation_node *walk;
  struct ds_history_operation_node *prev;

  if (branch == NULL || id == 0UL || id == ULONG_MAX) {
    return 0UL;
  }

  history = branch->history;

  if (time < branch->fork_time) {
    return 0UL;
  }

  node =
      (struct ds_history_operation_node *)history_alloc(history, sizeof(*node));
  if (node == NULL) {
    return 0UL;
  }

  node->operation.id = id;
  node->operation.time = time;
  node->operation.kind = kind;
  node->operation.payload = payload;
  node->next = NULL;

  prev = NULL;
  walk = branch->operations;
  while (walk != NULL && (walk->operation.time < time ||
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

  if (history->next_operation_id <= id) {
    history->next_operation_id = id + 1UL;
  }
  history_note_edit(branch, time);
  return node->operation.id;
}

ds_status_t ds_history_branch_insert_at_ex(ds_history_branch_t *branch,
                                           unsigned long time, int kind,
                                           void *payload,
                                           unsigned long *out_id) {
  ds_history_t *history;
  unsigned long id;
  if (out_id != NULL)
    *out_id = 0;
  if (branch == NULL || out_id == NULL)
    return DS_ERR_NULL;
  if (time < branch->fork_time)
    return DS_ERR_RANGE;
  history = branch->history;
  id = history->next_operation_id;
  if (id == 0 || id == ULONG_MAX)
    return DS_ERR_OVERFLOW;
  if (history_branch_insert_with_id(branch, id, time, kind, payload) == 0)
    return DS_ERR_ALLOC;
  *out_id = id;
  return DS_OK;
}

unsigned long ds_history_branch_insert_at(ds_history_branch_t *branch,
                                          unsigned long time, int kind,
                                          void *payload) {
  unsigned long id = 0;
  (void)ds_history_branch_insert_at_ex(branch, time, kind, payload, &id);
  return id;
}

ds_status_t ds_history_branch_append_ex(ds_history_branch_t *branch, int kind,
                                        void *payload, unsigned long *out_id) {
  unsigned long time;
  if (out_id != NULL)
    *out_id = 0;
  if (branch == NULL || out_id == NULL)
    return DS_ERR_NULL;
  time = history_branch_local_head_time(branch);
  if (time < branch->fork_time)
    time = branch->fork_time;
  if (time == ULONG_MAX)
    return DS_ERR_OVERFLOW;
  return ds_history_branch_insert_at_ex(branch, time + 1, kind, payload,
                                        out_id);
}

unsigned long ds_history_branch_append(ds_history_branch_t *branch, int kind,
                                       void *payload) {
  unsigned long id = 0;
  (void)ds_history_branch_append_ex(branch, kind, payload, &id);
  return id;
}

bool ds_history_branch_delete_op(ds_history_branch_t *branch,
                                 unsigned long op_id) {
  ds_history_t *history;
  struct ds_history_operation_node *op;
  struct ds_history_operation_node *prev;
  unsigned long edit_time;

  if (branch == NULL) {
    return false;
  }

  history = branch->history;

  op = branch->operations;
  prev = NULL;
  while (op != NULL && op->operation.id != op_id) {
    prev = op;
    op = op->next;
  }

  if (op == NULL) {
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
  history_free(history, op);

  history_note_edit(branch, edit_time);
  return true;
}

ds_status_t ds_history_branch_merge_with_ex(
    ds_history_branch_t *target, ds_history_branch_t *source,
    unsigned long from_time, unsigned long through_time, unsigned int flags,
    ds_history_merge_policy_func_t policy, void *policy_user,
    size_t *out_merged) {
  ds_history_t *history;
  struct ds_history_operation_node *op;
  unsigned long append_time;
  ds_status_t st = DS_OK;
  if (out_merged != NULL)
    *out_merged = 0;
  if (target == NULL || source == NULL || out_merged == NULL)
    return DS_ERR_NULL;
  if (target == source || target->history != source->history ||
      from_time > through_time ||
      (flags & ~DS_HISTORY_MERGE_APPEND_AFTER_HEAD) != 0)
    return DS_ERR_INVALID;
  history = target->history;
  append_time = history_branch_local_head_time(target);
  if (append_time < target->fork_time)
    append_time = target->fork_time;
  for (op = source->operations; op != NULL; op = op->next) {
    void *payload;
    bool cloned = history->ops.clone_payload != NULL;
    unsigned long time, id;
    int kind;
    unsigned int action = DS_HISTORY_MERGE_TAKE;
    if (op->operation.time < from_time || op->operation.time > through_time)
      continue;
    if (history->ops.clone_payload != NULL) {
      payload = history->ops.clone_payload(
          op->operation.kind, op->operation.payload, history->clone_data);
      if (payload == NULL && op->operation.payload != NULL)
        return DS_ERR_CALLBACK;
    } else {
      if (history->ops.destroy_payload != NULL && op->operation.payload != NULL)
        return DS_ERR_UNSUPPORTED;
      payload = op->operation.payload;
    }
    time = op->operation.time;
    kind = op->operation.kind;
    if ((flags & DS_HISTORY_MERGE_APPEND_AFTER_HEAD) != 0) {
      if (append_time == ULONG_MAX)
        st = DS_ERR_OVERFLOW;
      else
        time = append_time + 1;
    }
    if (st == DS_OK && policy != NULL)
      action = policy(target, source, &op->operation, &time, &kind, &payload,
                      policy_user);
    if (st == DS_OK) {
      if (action == DS_HISTORY_MERGE_STOP)
        st = DS_STOP;
      else if (action != DS_HISTORY_MERGE_SKIP &&
               action != DS_HISTORY_MERGE_TAKE)
        st = DS_ERR_CALLBACK;
      else if (action == DS_HISTORY_MERGE_TAKE) {
        st = ds_history_branch_insert_at_ex(target, time, kind, payload, &id);
        if (st == DS_OK) {
          (*out_merged)++;
          if (time > append_time)
            append_time = time;
          continue;
        }
      }
    }
    if (history->ops.destroy_payload != NULL &&
        (cloned || payload != op->operation.payload))
      history->ops.destroy_payload(kind, payload);
    if (st != DS_OK)
      return st;
  }
  return DS_OK;
}

size_t ds_history_branch_merge_with(
    ds_history_branch_t *target, ds_history_branch_t *source,
    unsigned long from_time, unsigned long through_time, unsigned int flags,
    ds_history_merge_policy_func_t policy, void *policy_user) {
  size_t merged = 0;
  (void)ds_history_branch_merge_with_ex(target, source, from_time, through_time,
                                        flags, policy, policy_user, &merged);
  return merged;
}

size_t ds_history_branch_merge(ds_history_branch_t *target,
                               ds_history_branch_t *source,
                               unsigned long from_time,
                               unsigned long through_time, unsigned int flags) {
  return ds_history_branch_merge_with(target, source, from_time, through_time,
                                      flags, NULL, NULL);
}

void *ds_history_branch_snapshot_at(ds_history_branch_t *branch,
                                    unsigned long time) {
  void *state;
  size_t replayed_count;

  if (branch == NULL) {
    return NULL;
  }

  replayed_count = 0U;
  state = history_materialize(branch, time, &replayed_count);
  if (state != NULL) {
    history_cache(branch, time, state, replayed_count);
  }
  return state;
}

void *ds_history_branch_snapshot_head(ds_history_branch_t *branch) {
  if (branch == NULL) {
    return NULL;
  }
  return ds_history_branch_snapshot_at(branch,
                                       ds_history_branch_head_time(branch));
}

void ds_history_snapshot_destroy(ds_history_t *history, void *snapshot) {
  if (history == NULL || snapshot == NULL) {
    return;
  }

  history->ops.destroy_state(snapshot);
}

void *ds_history_branch_query_at(ds_history_branch_t *branch,
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

  replayed_count = 0U;
  state = history_materialize(branch, time, &replayed_count);
  if (state == NULL) {
    return NULL;
  }

  history_cache(branch, time, state, replayed_count);

  result = history->ops.query(state, query);
  history->ops.destroy_state(state);
  return result;
}

void *ds_history_branch_query_head(ds_history_branch_t *branch,
                                   const ds_history_query_t *query) {
  if (branch == NULL) {
    return NULL;
  }
  return ds_history_branch_query_at(branch, ds_history_branch_head_time(branch),
                                    query);
}

size_t ds_history_branch_export_ops(ds_history_branch_t *branch,
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

static size_t history_branch_operation_count(ds_history_branch_t *branch) {
  struct ds_history_operation_node *op;
  size_t count;
  count = 0U;
  if (branch == NULL) {
    return 0U;
  }
  op = branch->operations;
  while (op != NULL) {
    count++;
    op = op->next;
  }
  return count;
}

bool ds_history_write_ulong(ds_history_write_func_t write, void *io_user,
                            unsigned long value) {
  unsigned char bytes[8];
  size_t i;

  if (write == NULL) {
    return false;
  }
  for (i = 0U; i < 8U; i++) {
    bytes[i] = (unsigned char)(value & 0xffUL);
    value >>= 8;
  }
  return write(bytes, sizeof(bytes), io_user);
}

bool ds_history_read_ulong(ds_history_read_func_t read, void *io_user,
                           unsigned long *out_value) {
  unsigned char bytes[8];
  unsigned long value;
  size_t i;
  size_t ulong_bytes;

  if (read == NULL || out_value == NULL) {
    return false;
  }
  if (!read(bytes, sizeof(bytes), io_user)) {
    return false;
  }
  value = 0UL;
  ulong_bytes = sizeof(unsigned long);
  for (i = 0U; i < 8U; i++) {
    if (i >= ulong_bytes) {
      if (bytes[i] != 0U) {
        return false;
      }
    } else {
      value |= ((unsigned long)bytes[i]) << (i * 8U);
    }
  }
  *out_value = value;
  return true;
}

bool ds_history_write_int(ds_history_write_func_t write, void *io_user,
                          int value) {
  unsigned long encoded;
  long signed_value;

  signed_value = (long)value;
  if (signed_value < 0L) {
    encoded = ((unsigned long)(-(signed_value + 1L)) * 2UL) + 1UL;
  } else {
    encoded = ((unsigned long)signed_value) * 2UL;
  }
  return ds_history_write_ulong(write, io_user, encoded);
}

bool ds_history_read_int(ds_history_read_func_t read, void *io_user,
                         int *out_value) {
  unsigned long encoded;
  unsigned long magnitude;
  long signed_value;

  if (out_value == NULL) {
    return false;
  }
  if (!ds_history_read_ulong(read, io_user, &encoded)) {
    return false;
  }
  magnitude = encoded / 2UL;
  if ((encoded & 1UL) != 0UL) {
    if (magnitude > (unsigned long)LONG_MAX) {
      return false;
    }
    signed_value = -((long)magnitude) - 1L;
  } else {
    if (magnitude > (unsigned long)INT_MAX) {
      return false;
    }
    signed_value = (long)magnitude;
  }
#if LONG_MAX > INT_MAX
  if (signed_value < (long)INT_MIN || signed_value > (long)INT_MAX) {
    return false;
  }
#endif
  *out_value = (int)signed_value;
  return true;
}

bool ds_history_write_size(ds_history_write_func_t write, void *io_user,
                           size_t value) {
  if ((size_t)(unsigned long)value != value) {
    return false; /* wider than unsigned long (LLP64) */
  }
  return ds_history_write_ulong(write, io_user, (unsigned long)value);
}

bool ds_history_read_size(ds_history_read_func_t read, void *io_user,
                          size_t *out_value) {
  unsigned long value;

  if (out_value == NULL) {
    return false;
  }
  if (!ds_history_read_ulong(read, io_user, &value)) {
    return false;
  }
  *out_value = (size_t)value;
  return true;
}

size_t
ds_history_branch_serialize_ops(ds_history_branch_t *branch,
                                ds_history_write_func_t write,
                                ds_history_payload_write_func_t write_payload,
                                void *io_user, void *payload_user) {
  struct ds_history_operation_node *op;
  size_t count;
  size_t written;

  if (branch == NULL || write == NULL || write_payload == NULL) {
    return 0U;
  }

  count = history_branch_operation_count(branch);
  if (!write(&count, sizeof(count), io_user)) {
    return 0U;
  }

  written = 0U;
  op = branch->operations;
  while (op != NULL) {
    if (!write(&op->operation.id, sizeof(op->operation.id), io_user) ||
        !write(&op->operation.time, sizeof(op->operation.time), io_user) ||
        !write(&op->operation.kind, sizeof(op->operation.kind), io_user) ||
        !write_payload(&op->operation, write, io_user, payload_user)) {
      break;
    }
    written++;
    op = op->next;
  }
  return written;
}

size_t
ds_history_branch_deserialize_ops(ds_history_branch_t *branch,
                                  ds_history_read_func_t read,
                                  ds_history_payload_read_func_t read_payload,
                                  void *io_user, void *payload_user) {
  ds_history_t *history;
  size_t count;
  size_t imported;
  unsigned long id;
  unsigned long time;
  int kind;
  void *payload;

  if (branch == NULL || read == NULL || read_payload == NULL) {
    return 0U;
  }
  if (!read(&count, sizeof(count), io_user)) {
    return 0U;
  }

  history = branch->history;
  imported = 0U;
  while (imported < count) {
    if (!read(&id, sizeof(id), io_user) ||
        !read(&time, sizeof(time), io_user) ||
        !read(&kind, sizeof(kind), io_user)) {
      break;
    }
    payload = read_payload(id, time, kind, read, io_user, payload_user);
    if (payload == NULL && read_payload != NULL) {
      break;
    }
    if (history_branch_insert_with_id(branch, id, time, kind, payload) == 0UL) {
      if (history->ops.destroy_payload != NULL) {
        history->ops.destroy_payload(kind, payload);
      }
      break;
    }
    imported++;
  }
  return imported;
}

static size_t history_branch_count(ds_history_t *history) {
  struct ds_history_branch_node *node;
  size_t count;

  count = 0U;
  node = history->branches;
  while (node != NULL) {
    count++;
    node = node->next;
  }
  return count;
}

static ds_history_branch_t *history_find_branch_by_id(ds_history_t *history,
                                                      unsigned long id) {
  struct ds_history_branch_node *node;

  node = history->branches;
  while (node != NULL) {
    if (node->branch->id == id) {
      return node->branch;
    }
    node = node->next;
  }
  return NULL;
}

static bool
history_write_branch_ops(ds_history_branch_t *branch,
                         ds_history_write_func_t write,
                         ds_history_payload_write_func_t write_payload,
                         void *io_user, void *payload_user) {
  struct ds_history_operation_node *op;
  size_t count;

  count = history_branch_operation_count(branch);
  if (!write(&count, sizeof(count), io_user)) {
    return false;
  }
  op = branch->operations;
  while (op != NULL) {
    if (!write(&op->operation.id, sizeof(op->operation.id), io_user) ||
        !write(&op->operation.time, sizeof(op->operation.time), io_user) ||
        !write(&op->operation.kind, sizeof(op->operation.kind), io_user) ||
        !write_payload(&op->operation, write, io_user, payload_user)) {
      return false;
    }
    op = op->next;
  }
  return true;
}

/* The branch after b in pre-order of the branch tree, children in the
 * order of the branch list: its first child, else the next sibling of b
 * or of its nearest ancestor that has one; NULL after the last. Found by
 * scanning the list, so walking the tree needs no stack however deep the
 * forks go (O(branches) a step, as finding the children always cost). */
static ds_history_branch_t *history_preorder_next(ds_history_t *history,
                                                  ds_history_branch_t *b) {
  struct ds_history_branch_node *node;
  for (node = history->branches; node != NULL; node = node->next) {
    if (node->branch->parent == b) {
      return node->branch;
    }
  }
  while (b->parent != NULL) {
    node = history->branches;
    while (node != NULL && node->branch != b) {
      node = node->next;
    }
    for (node = node == NULL ? NULL : node->next; node != NULL;
         node = node->next) {
      if (node->branch->parent == b->parent) {
        return node->branch;
      }
    }
    b = b->parent;
  }
  return NULL;
}

static bool
history_serialize_branch(ds_history_branch_t *branch,
                         ds_history_write_func_t write,
                         ds_history_payload_write_func_t write_payload,
                         void *io_user, void *payload_user) {
  unsigned long parent_id;
  size_t name_len;

  parent_id = branch->parent == NULL ? 0UL : branch->parent->id;
  name_len = branch->name == NULL ? 0U : strlen(branch->name);

  if (!write(&branch->id, sizeof(branch->id), io_user) ||
      !write(&parent_id, sizeof(parent_id), io_user) ||
      !write(&branch->fork_time, sizeof(branch->fork_time), io_user) ||
      !write(&name_len, sizeof(name_len), io_user)) {
    return false;
  }
  if (name_len != 0U && !write(branch->name, name_len, io_user)) {
    return false;
  }
  return history_write_branch_ops(branch, write, write_payload, io_user,
                                  payload_user);
}

static bool
history_serialize_branches(ds_history_t *history, ds_history_write_func_t write,
                           ds_history_payload_write_func_t write_payload,
                           void *io_user, void *payload_user) {
  ds_history_branch_t *b;
  for (b = history->main_branch; b != NULL;
       b = history_preorder_next(history, b)) {
    if (!history_serialize_branch(b, write, write_payload, io_user,
                                  payload_user)) {
      return false;
    }
  }
  return true;
}

static char *history_read_name(ds_history_t *history,
                               ds_history_read_func_t read, void *io_user,
                               size_t name_len) {
  char *name;

  if (name_len == 0U || name_len == (size_t)-1) {
    return NULL; /* none, or one whose terminator would not fit */
  }
  name = (char *)history_alloc(history, name_len + 1U);
  if (name == NULL) {
    return NULL;
  }
  if (!read(name, name_len, io_user)) {
    history_free(history, name);
    return NULL;
  }
  name[name_len] = '\0';
  return name;
}

static bool history_read_branch_ops(ds_history_branch_t *branch,
                                    ds_history_read_func_t read,
                                    ds_history_payload_read_func_t read_payload,
                                    void *io_user, void *payload_user) {
  ds_history_t *history;
  size_t count;
  size_t i;
  unsigned long id;
  unsigned long time;
  int kind;
  void *payload;

  history = branch->history;
  if (!read(&count, sizeof(count), io_user)) {
    return false;
  }
  for (i = 0U; i < count; i++) {
    if (!read(&id, sizeof(id), io_user) ||
        !read(&time, sizeof(time), io_user) ||
        !read(&kind, sizeof(kind), io_user)) {
      return false;
    }
    payload = read_payload(id, time, kind, read, io_user, payload_user);
    if (payload == NULL)
      return false;
    if (history_branch_insert_with_id(branch, id, time, kind, payload) == 0UL) {
      if (history->ops.destroy_payload != NULL) {
        history->ops.destroy_payload(kind, payload);
      }
      return false;
    }
  }
  return true;
}

bool ds_history_serialize(ds_history_t *history, ds_history_write_func_t write,
                          ds_history_payload_write_func_t write_payload,
                          void *io_user, void *payload_user) {
  static const unsigned char magic[8] = {'D', 'S', 'H', 'I', 'S', 'T', '3', 0};
  size_t branch_count;
  bool ok;

  if (history == NULL || write == NULL || write_payload == NULL) {
    return false;
  }

  branch_count = history_branch_count(history);
  ok = write(magic, sizeof(magic), io_user) &&
       write(&branch_count, sizeof(branch_count), io_user) &&
       write(&history->next_operation_id, sizeof(history->next_operation_id),
             io_user) &&
       write(&history->next_branch_id, sizeof(history->next_branch_id),
             io_user) &&
       write(&history->checkpoint_interval,
             sizeof(history->checkpoint_interval), io_user) &&
       history_serialize_branches(history, write, write_payload, io_user,
                                  payload_user);
  return ok;
}

ds_history_t *
ds_history_deserialize(const ds_history_ops_t *ops, void *config,
                       ds_history_read_func_t read,
                       ds_history_payload_read_func_t read_payload,
                       void *io_user, void *payload_user) {
  static const unsigned char expected_magic[8] = {'D', 'S', 'H', 'I',
                                                  'S', 'T', '3', 0};
  unsigned char magic[8];
  ds_history_t *history;
  ds_history_branch_t *branch;
  ds_history_branch_t *parent;
  size_t branch_count;
  size_t i;
  unsigned long next_operation_id;
  unsigned long next_branch_id;
  size_t checkpoint_interval;
  unsigned long id;
  unsigned long parent_id;
  unsigned long fork_time;
  size_t name_len;
  char *name;

  if (ops == NULL || read == NULL || read_payload == NULL) {
    return NULL;
  }
  if (!read(magic, sizeof(magic), io_user) ||
      memcmp(magic, expected_magic, sizeof(magic)) != 0 ||
      !read(&branch_count, sizeof(branch_count), io_user) ||
      !read(&next_operation_id, sizeof(next_operation_id), io_user) ||
      !read(&next_branch_id, sizeof(next_branch_id), io_user) ||
      !read(&checkpoint_interval, sizeof(checkpoint_interval), io_user)) {
    return NULL;
  }
  if (branch_count == 0U) {
    return NULL;
  }

  history = ds_history_create(ops, config);
  if (history == NULL) {
    return NULL;
  }
  history->checkpoint_interval = checkpoint_interval;

  for (i = 0U; i < branch_count; i++) {
    if (!read(&id, sizeof(id), io_user) ||
        !read(&parent_id, sizeof(parent_id), io_user) ||
        !read(&fork_time, sizeof(fork_time), io_user) ||
        !read(&name_len, sizeof(name_len), io_user)) {
      ds_history_destroy(history);
      return NULL;
    }
    name = history_read_name(history, read, io_user, name_len);
    if (name_len != 0U && name == NULL) {
      ds_history_destroy(history);
      return NULL;
    }

    if (parent_id == 0UL) {
      branch = history->main_branch;
      if (branch == NULL) {
        if (name != NULL) {
          history_free(history, name);
        }
        ds_history_destroy(history);
        return NULL;
      }
      if (branch->name != NULL) {
        history_free(history, branch->name);
      }
      branch->name = name;
      branch->id = id;
      branch->fork_time = fork_time;
    } else {
      parent = history_find_branch_by_id(history, parent_id);
      if (parent == NULL) {
        if (name != NULL) {
          history_free(history, name);
        }
        ds_history_destroy(history);
        return NULL;
      }
      branch = history_branch_alloc(history, parent, fork_time, name);
      if (name != NULL) {
        history_free(history, name);
      }
      if (branch == NULL) {
        ds_history_destroy(history);
        return NULL;
      }
      branch->id = id;
      if (!history_branch_register(history, branch)) {
        history_branch_destroy(branch);
        ds_history_destroy(history);
        return NULL;
      }
    }

    if (history->next_branch_id <= id) {
      history->next_branch_id = id + 1UL;
    }
    if (!history_read_branch_ops(branch, read, read_payload, io_user,
                                 payload_user)) {
      ds_history_destroy(history);
      return NULL;
    }
  }

  if (history->next_operation_id < next_operation_id) {
    history->next_operation_id = next_operation_id;
  }
  if (history->next_branch_id < next_branch_id) {
    history->next_branch_id = next_branch_id;
  }
  return history;
}

static bool
history_write_branch_ops_portable(ds_history_branch_t *branch,
                                  ds_history_write_func_t write,
                                  ds_history_payload_write_func_t write_payload,
                                  void *io_user, void *payload_user) {
  struct ds_history_operation_node *op;
  size_t count;

  count = history_branch_operation_count(branch);
  if (!ds_history_write_size(write, io_user, count)) {
    return false;
  }
  op = branch->operations;
  while (op != NULL) {
    if (!ds_history_write_ulong(write, io_user, op->operation.id) ||
        !ds_history_write_ulong(write, io_user, op->operation.time) ||
        !ds_history_write_int(write, io_user, op->operation.kind) ||
        !write_payload(&op->operation, write, io_user, payload_user)) {
      return false;
    }
    op = op->next;
  }
  return true;
}

static bool
history_serialize_branch_portable(ds_history_branch_t *branch,
                                  ds_history_write_func_t write,
                                  ds_history_payload_write_func_t write_payload,
                                  void *io_user, void *payload_user) {
  unsigned long parent_id;
  size_t name_len;

  parent_id = branch->parent == NULL ? 0UL : branch->parent->id;
  name_len = branch->name == NULL ? 0U : strlen(branch->name);

  if (!ds_history_write_ulong(write, io_user, branch->id) ||
      !ds_history_write_ulong(write, io_user, parent_id) ||
      !ds_history_write_ulong(write, io_user, branch->fork_time) ||
      !ds_history_write_size(write, io_user, name_len)) {
    return false;
  }
  if (name_len != 0U && !write(branch->name, name_len, io_user)) {
    return false;
  }
  return history_write_branch_ops_portable(branch, write, write_payload,
                                           io_user, payload_user);
}

static bool history_serialize_branches_portable(
    ds_history_t *history, ds_history_write_func_t write,
    ds_history_payload_write_func_t write_payload, void *io_user,
    void *payload_user) {
  ds_history_branch_t *b;
  for (b = history->main_branch; b != NULL;
       b = history_preorder_next(history, b)) {
    if (!history_serialize_branch_portable(b, write, write_payload, io_user,
                                           payload_user)) {
      return false;
    }
  }
  return true;
}

void ds_history_archive_init(ds_history_archive_t *archive) {
  (void)ds_history_archive_init_context(archive, NULL);
}

ds_status_t ds_history_archive_init_ex(ds_history_archive_t *a,
                                       void *(*allocator)(size_t),
                                       void (*deallocator)(void *)) {
  if (a == NULL)
    return DS_ERR_NULL;
  memset(a, 0, sizeof(*a));
  if ((allocator == NULL) != (deallocator == NULL))
    return a->status = DS_ERR_INVALID;
  a->allocator = allocator;
  a->deallocator = deallocator;
  a->use_context = allocator == NULL;
  a->context = ds_default_context();
  return DS_OK;
}

void ds_history_archive_init_alloc(ds_history_archive_t *a,
                                   void *(*allocator)(size_t),
                                   void (*deallocator)(void *)) {
  (void)ds_history_archive_init_ex(a, allocator, deallocator);
}

ds_status_t ds_history_archive_init_context(ds_history_archive_t *a,
                                            ds_context_t *context) {
  ds_status_t st = ds_history_archive_init_ex(a, NULL, NULL);
  if (st == DS_OK) {
    a->context = context != NULL ? context : ds_default_context();
    a->use_context = true;
  }
  return st;
}

ds_status_t ds_history_archive_status(const ds_history_archive_t *a) {
  return a == NULL ? DS_ERR_NULL : a->status;
}

static void history_archive_free(ds_history_archive_t *a, void *p) {
  if (a->use_context)
    ds_context_free(a->context, p);
  else if (p != NULL && a->deallocator != NULL)
    a->deallocator(p);
}

void ds_history_archive_destroy(ds_history_archive_t *a) {
  if (a == NULL)
    return;
  history_archive_free(a, a->data);
  a->data = NULL;
  a->capacity = 0;
  ds_history_archive_clear(a);
}

void ds_history_archive_clear(ds_history_archive_t *a) {
  if (a == NULL)
    return;
  a->size = 0;
  ds_history_archive_rewind(a);
}

void ds_history_archive_rewind(ds_history_archive_t *a) {
  if (a != NULL) {
    a->offset = 0;
    a->status =
        a->use_context || (a->allocator != NULL && a->deallocator != NULL)
            ? DS_OK
            : DS_ERR_INVALID;
  }
}

const unsigned char *
ds_history_archive_data(const ds_history_archive_t *archive) {
  return archive == NULL ? NULL : archive->data;
}

size_t ds_history_archive_size(const ds_history_archive_t *archive) {
  return archive == NULL ? 0U : archive->size;
}

bool ds_history_archive_write(const void *data, size_t size, void *user) {
  ds_history_archive_t *a = (ds_history_archive_t *)user;
  unsigned char *next;
  size_t capacity, needed;
  if (a == NULL || a->status != DS_OK)
    return false;
  if (data == NULL && size != 0) {
    a->status = DS_ERR_NULL;
    return false;
  }
  if (size > (size_t)-1 - a->size) {
    a->status = DS_ERR_OVERFLOW;
    return false;
  }
  if (!a->use_context && (a->allocator == NULL || a->deallocator == NULL)) {
    a->status = DS_ERR_INVALID;
    return false;
  }
  needed = a->size + size;
  if (needed > a->capacity) {
    capacity = a->capacity == 0 ? 64 : a->capacity;
    while (capacity < needed) {
      if (capacity > (size_t)-1 / 2) {
        capacity = needed;
        break;
      }
      capacity *= 2;
    }
    next = (unsigned char *)(a->use_context
                                 ? ds_context_alloc(a->context, capacity)
                                 : a->allocator(capacity));
    if (next == NULL) {
      a->status = DS_ERR_ALLOC;
      return false;
    }
    if (a->size != 0)
      memcpy(next, a->data, a->size);
    if (size != 0)
      memcpy(next + a->size, data, size);
    history_archive_free(a, a->data);
    a->data = next;
    a->capacity = capacity;
  } else if (size != 0) {
    memmove(a->data + a->size, data, size);
  }
  a->size = needed;
  return true;
}

bool ds_history_archive_read(void *data, size_t size, void *user) {
  ds_history_archive_t *a = (ds_history_archive_t *)user;
  if (a == NULL || a->status != DS_OK)
    return false;
  if (data == NULL && size != 0) {
    a->status = DS_ERR_NULL;
    return false;
  }
  if (a->offset > a->size || size > a->size - a->offset) {
    a->status = DS_ERR_INVALID;
    return false;
  }
  if (size != 0)
    memmove(data, a->data + a->offset, size);
  a->offset += size;
  return true;
}

bool ds_history_serialize_portable(
    ds_history_t *history, ds_history_write_func_t write,
    ds_history_payload_write_func_t write_payload, void *io_user,
    void *payload_user) {
  static const unsigned char magic[8] = {'D', 'S', 'H', 'I', 'S', 'T', '4', 0};
  size_t branch_count;
  bool ok;

  if (history == NULL || write == NULL || write_payload == NULL) {
    return false;
  }

  branch_count = history_branch_count(history);
  ok = write(magic, sizeof(magic), io_user) &&
       ds_history_write_size(write, io_user, branch_count) &&
       ds_history_write_ulong(write, io_user, history->next_operation_id) &&
       ds_history_write_ulong(write, io_user, history->next_branch_id) &&
       ds_history_write_size(write, io_user, history->checkpoint_interval) &&
       history_serialize_branches_portable(history, write, write_payload,
                                           io_user, payload_user);
  return ok;
}

void ds_history_load_options_init(ds_history_load_options_t *options) {
  if (options != NULL)
    memset(options, 0, sizeof(*options));
}

ds_status_t ds_history_deserialize_portable_ex(
    const ds_history_ops_t *ops, void *config, ds_history_read_func_t read,
    ds_history_payload_read_ex_func_t read_payload, void *io_user,
    void *payload_user, const ds_history_load_options_t *options,
    ds_history_t **out) {
  static const unsigned char magic[8] = {'D', 'S', 'H', 'I', 'S', 'T', '4', 0};
  ds_history_load_options_t defaults;
  ds_history_t *h = NULL;
  ds_ordered_t branch_ids, operation_ids;
  unsigned char got_magic[8];
  size_t branches, interval, i, total = 0;
  unsigned long next_operation, next_branch;
  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (ops == NULL || read == NULL || read_payload == NULL)
    return DS_ERR_NULL;
  if (options == NULL) {
    ds_history_load_options_init(&defaults);
    options = &defaults;
  }
  memset(&branch_ids, 0, sizeof(branch_ids));
  memset(&operation_ids, 0, sizeof(operation_ids));
  if (!read(got_magic, sizeof(got_magic), io_user) ||
      memcmp(got_magic, magic, sizeof(magic)) != 0 ||
      !ds_history_read_size(read, io_user, &branches) ||
      !ds_history_read_ulong(read, io_user, &next_operation) ||
      !ds_history_read_ulong(read, io_user, &next_branch) ||
      !ds_history_read_size(read, io_user, &interval))
    return DS_ERR_INVALID;
  if (branches == 0 || next_operation == 0 || next_branch == 0)
    return DS_ERR_INVALID;
  if (options->max_branches != 0 && branches > options->max_branches)
    return DS_ERR_OVERFLOW;
  st = ds_history_create_context(ops, config, options->context, &h);
  if (st != DS_OK)
    return st;
  h->checkpoint_interval = interval;
  st = ds_ordered_init(&branch_ids,
                       options->index_ops == NULL ? ds_ordered_ab_ops()
                                                  : options->index_ops,
                       sizeof(ds_history_branch_t *), options->context);
  if (st == DS_OK)
    st = ds_ordered_init(&operation_ids,
                         options->index_ops == NULL ? ds_ordered_ab_ops()
                                                    : options->index_ops,
                         0, options->context);
  if (st != DS_OK)
    goto done;
  for (i = 0; i < branches; i++) {
    unsigned long id, parent_id, fork, previous_time = 0, previous_id = 0;
    size_t name_len, count, j;
    char *name = NULL;
    ds_history_branch_t *b, *parent = NULL;
    struct ds_history_operation_node **tail;
    void *found;
    st = DS_ERR_INVALID;
    if (!ds_history_read_ulong(read, io_user, &id) ||
        !ds_history_read_ulong(read, io_user, &parent_id) ||
        !ds_history_read_ulong(read, io_user, &fork) ||
        !ds_history_read_size(read, io_user, &name_len))
      goto done;
    if (id == 0 || id == ULONG_MAX ||
        ds_ordered_find(&branch_ids, id) != NULL ||
        ((i == 0) != (parent_id == 0)) || name_len == (size_t)-1)
      goto done;
    if (options->max_name_bytes != 0 && name_len > options->max_name_bytes) {
      st = DS_ERR_OVERFLOW;
      goto done;
    }
    if (parent_id != 0) {
      found = ds_ordered_find(&branch_ids, parent_id);
      if (found == NULL)
        goto done;
      memcpy(&parent, found, sizeof(parent));
    }
    if (name_len != 0) {
      name = (char *)history_alloc(h, name_len + 1);
      if (name == NULL) {
        st = DS_ERR_ALLOC;
        goto done;
      }
      if (!read(name, name_len, io_user) || memchr(name, 0, name_len) != NULL) {
        history_free(h, name);
        goto done;
      }
      name[name_len] = '\0';
    }
    if (i == 0) {
      b = h->main_branch;
      history_free(h, b->name);
      b->name = name;
      b->id = id;
      b->fork_time = fork;
    } else {
      b = (ds_history_branch_t *)history_alloc(h, sizeof(*b));
      if (b == NULL) {
        history_free(h, name);
        st = DS_ERR_ALLOC;
        goto done;
      }
      memset(b, 0, sizeof(*b));
      b->history = h;
      b->id = id;
      b->fork_time = fork;
      b->name = name;
      b->parent = parent;
      if (!history_branch_register(h, b)) {
        history_branch_destroy(b);
        st = DS_ERR_ALLOC;
        goto done;
      }
    }
    if (id >= h->next_branch_id)
      h->next_branch_id = id + 1;
    st = ds_ordered_insert(&branch_ids, id, &b);
    if (st != DS_OK)
      goto done;
    st = DS_ERR_INVALID;
    if (!ds_history_read_size(read, io_user, &count))
      goto done;
    if (count > (size_t)-1 - total ||
        (options->max_operations != 0 &&
         (total > options->max_operations ||
          count > options->max_operations - total))) {
      st = DS_ERR_OVERFLOW;
      goto done;
    }
    total += count;
    tail = &b->operations;
    for (j = 0; j < count; j++) {
      unsigned long op_id, time;
      int kind;
      void *payload = NULL;
      struct ds_history_operation_node *node;
      if (!ds_history_read_ulong(read, io_user, &op_id) ||
          !ds_history_read_ulong(read, io_user, &time) ||
          !ds_history_read_int(read, io_user, &kind) || op_id == 0 ||
          op_id == ULONG_MAX || time < fork ||
          (j != 0 && (time < previous_time ||
                      (time == previous_time && op_id <= previous_id))) ||
          ds_ordered_find(&operation_ids, op_id) != NULL) {
        st = DS_ERR_INVALID;
        goto done;
      }
      st = ds_ordered_insert(&operation_ids, op_id, NULL);
      if (st != DS_OK)
        goto done;
      st = read_payload(op_id, time, kind, read, io_user, payload_user,
                        &payload);
      if (st != DS_OK) {
        if (st > DS_OK)
          st = DS_ERR_CALLBACK;
        goto done;
      }
      node =
          (struct ds_history_operation_node *)history_alloc(h, sizeof(*node));
      if (node == NULL) {
        if (h->ops.destroy_payload != NULL)
          h->ops.destroy_payload(kind, payload);
        st = DS_ERR_ALLOC;
        goto done;
      }
      node->operation.id = op_id;
      node->operation.time = time;
      node->operation.kind = kind;
      node->operation.payload = payload;
      node->next = NULL;
      *tail = node;
      tail = &node->next;
      previous_time = time;
      previous_id = op_id;
      if (op_id >= h->next_operation_id)
        h->next_operation_id = op_id + 1;
    }
  }
  if (next_operation < h->next_operation_id ||
      next_branch < h->next_branch_id) {
    st = DS_ERR_INVALID;
    goto done;
  }
  h->next_operation_id = next_operation;
  h->next_branch_id = next_branch;
  *out = h;
  h = NULL;
  st = DS_OK;
done:
  ds_ordered_destroy(&operation_ids);
  ds_ordered_destroy(&branch_ids);
  ds_history_destroy(h);
  return st;
}

typedef struct history_direct_reader {
  ds_history_payload_read_func_t read;
  void *user;
} history_direct_reader_t;

static ds_status_t history_read_direct(unsigned long id, unsigned long time,
                                       int kind, ds_history_read_func_t read,
                                       void *io_user, void *user, void **out) {
  history_direct_reader_t *reader = (history_direct_reader_t *)user;
  *out = reader->read(id, time, kind, read, io_user, reader->user);
  return *out == NULL ? DS_ERR_CALLBACK : DS_OK;
}

ds_history_t *
ds_history_deserialize_portable(const ds_history_ops_t *ops, void *config,
                                ds_history_read_func_t read,
                                ds_history_payload_read_func_t read_payload,
                                void *io_user, void *payload_user) {
  history_direct_reader_t reader;
  ds_history_t *out = NULL;
  if (read_payload == NULL)
    return NULL;
  reader.read = read_payload;
  reader.user = payload_user;
  (void)ds_history_deserialize_portable_ex(
      ops, config, read, history_read_direct, io_user, &reader, NULL, &out);
  return out;
}

ds_status_t ds_history_branch_create_ex(ds_history_t *history,
                                        ds_history_branch_t *parent,
                                        unsigned long fork_time,
                                        const char *name,
                                        ds_history_branch_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (history == NULL)
    return DS_ERR_NULL;
  if (parent != NULL && parent->history != history)
    return DS_ERR_INVALID;
  if (history->next_branch_id == 0 || history->next_branch_id == ULONG_MAX)
    return DS_ERR_OVERFLOW;
  *out = ds_history_branch_create(history, parent, fork_time, name);
  return *out == NULL ? DS_ERR_ALLOC : DS_OK;
}
