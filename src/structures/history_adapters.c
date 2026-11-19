#include <dsa/structures/history_adapters.h>
#include <limits.h>
#include <string.h>

ds_status_t ds_history_file_init(ds_history_file_t *a, FILE *stream,
                                 bool take_ownership, size_t limit) {
  if (a == NULL) {
    return DS_ERR_NULL;
  }
  memset(a, 0, sizeof(*a));
  a->stream = stream;
  a->owns_stream = take_ownership;
  a->limit = limit;
  a->status = stream == NULL ? DS_ERR_NULL : DS_OK;
  return a->status;
}

ds_status_t ds_history_file_open(ds_history_file_t *a, const char *path,
                                 const char *mode, size_t limit) {
  FILE *stream;
  if (a == NULL) {
    return DS_ERR_NULL;
  }
  memset(a, 0, sizeof(*a));
  if (path == NULL || mode == NULL) {
    return a->status = DS_ERR_NULL;
  }
  if (strcmp(mode, "rb") != 0 && strcmp(mode, "wb") != 0 &&
      strcmp(mode, "ab") != 0 && strcmp(mode, "r+b") != 0 &&
      strcmp(mode, "w+b") != 0 && strcmp(mode, "a+b") != 0) {
    return a->status = DS_ERR_INVALID;
  }
  stream = fopen(path, mode);
  if (stream == NULL) {
    return a->status = DS_ERR_STATE;
  }
  return ds_history_file_init(a, stream, true, limit);
}

static bool hf_ready(ds_history_file_t *a, const void *data, size_t n) {
  if (a == NULL) {
    return false;
  }
  if (a->status != DS_OK) {
    return false;
  }
  if (a->stream == NULL || (data == NULL && n != 0)) {
    a->status = DS_ERR_NULL;
    return false;
  }
  if (n > (size_t)-1 - a->transferred ||
      (a->limit != 0 &&
       (a->transferred > a->limit || n > a->limit - a->transferred))) {
    a->status = DS_ERR_OVERFLOW;
    return false;
  }
  return true;
}

bool ds_history_file_write(const void *data, size_t size, void *archive) {
  ds_history_file_t *a = (ds_history_file_t *)archive;
  size_t wrote;
  if (!hf_ready(a, data, size)) {
    return false;
  }
  wrote = size == 0 ? 0 : fwrite(data, 1, size, a->stream);
  a->transferred += wrote;
  if (wrote != size) {
    a->status = DS_ERR_STATE;
    return false;
  }
  return true;
}

bool ds_history_file_read(void *data, size_t size, void *archive) {
  ds_history_file_t *a = (ds_history_file_t *)archive;
  size_t got;
  if (!hf_ready(a, data, size)) {
    return false;
  }
  got = size == 0 ? 0 : fread(data, 1, size, a->stream);
  a->transferred += got;
  if (got != size) {
    a->status = ferror(a->stream) ? DS_ERR_STATE : DS_ERR_INVALID;
    return false;
  }
  return true;
}

ds_status_t ds_history_file_flush(ds_history_file_t *a) {
  if (a == NULL || a->stream == NULL) {
    return DS_ERR_NULL;
  }
  if (a->status == DS_OK && fflush(a->stream) != 0) {
    a->status = DS_ERR_STATE;
  }
  return a->status;
}

ds_status_t ds_history_file_rewind(ds_history_file_t *a) {
  if (a == NULL || a->stream == NULL) {
    return DS_ERR_NULL;
  }
  if (fseek(a->stream, 0L, SEEK_SET) != 0) {
    return a->status = DS_ERR_STATE;
  }
  clearerr(a->stream);
  a->transferred = 0;
  a->status = DS_OK;
  return DS_OK;
}

ds_status_t ds_history_file_close(ds_history_file_t *a) {
  ds_status_t st;
  if (a == NULL) {
    return DS_ERR_NULL;
  }
  st = a->status;
  if (a->stream != NULL && a->owns_stream && fclose(a->stream) != 0 &&
      st == DS_OK) {
    st = DS_ERR_STATE;
  }
  a->stream = NULL;
  a->owns_stream = false;
  a->status = st;
  return st;
}

ds_status_t ds_history_file_status(const ds_history_file_t *a) {
  return a == NULL ? DS_ERR_NULL : a->status;
}

ds_status_t
ds_history_serialize_file(ds_history_t *history, ds_history_file_t *archive,
                          ds_history_payload_write_func_t write_payload,
                          void *payload_user) {
  if (history == NULL || archive == NULL || write_payload == NULL) {
    return DS_ERR_NULL;
  }
  if (archive->status != DS_OK) {
    return archive->status;
  }
  if (!ds_history_serialize_portable(history, ds_history_file_write,
                                     write_payload, archive, payload_user)) {
    if (archive->status == DS_OK) {
      archive->status = DS_ERR_CALLBACK;
    }
    return archive->status;
  }
  return ds_history_file_flush(archive);
}

ds_status_t ds_history_deserialize_file(
    const ds_history_ops_t *ops, void *config, ds_history_file_t *archive,
    ds_history_payload_read_ex_func_t read_payload, void *payload_user,
    const ds_history_load_options_t *options, ds_history_t **out) {
  ds_status_t st;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  if (archive == NULL) {
    return DS_ERR_NULL;
  }
  if (archive->status != DS_OK) {
    return archive->status;
  }
  st = ds_history_deserialize_portable_ex(ops, config, ds_history_file_read,
                                          read_payload, archive, payload_user,
                                          options, out);
  if (archive->status != DS_OK) {
    ds_history_destroy(*out);
    *out = NULL;
    return archive->status;
  }
  if (st != DS_OK) {
    archive->status = st;
  }
  return st;
}

typedef struct hk_record {
  ds_history_operation_t operation;
  bool target;
} hk_record_t;

typedef struct hk_state {
  const ds_history_keyed_merge_t *config;
  ds_map_t map;
  unsigned char *key;
  unsigned long lo, hi;
  ds_status_t error;
} hk_state_t;

static unsigned long hk_hash(const void *key, void *user) {
  hk_state_t *s = (hk_state_t *)user;
  const unsigned char *p = (const unsigned char *)key;
  unsigned long h = 2166136261UL;
  size_t i;
  if (s->config->hash != NULL) {
    return s->config->hash(key, s->config->user);
  }
  for (i = 0; i < s->config->key_size; i++) {
    h = ((h ^ p[i]) * 16777619UL) & 0xffffffffUL;
  }
  return h;
}

static bool hk_equal(const void *a, const void *b, void *user) {
  hk_state_t *s = (hk_state_t *)user;
  return s->config->equal != NULL ? s->config->equal(a, b, s->config->user)
                                  : memcmp(a, b, s->config->key_size) == 0;
}

static bool hk_later(const ds_history_operation_t *a,
                     const ds_history_operation_t *b) {
  return a->time > b->time || (a->time == b->time && a->id > b->id);
}

static bool hk_index(const ds_history_operation_t *op, void *user) {
  hk_state_t *s = (hk_state_t *)user;
  hk_record_t old, incoming;
  void *p;
  ds_status_t st;
  if (op->time < s->lo || op->time > s->hi) {
    return true;
  }
  st = s->config->project(op, s->key, s->config->key_size, s->config->user);
  if (st == DS_NOT_FOUND) {
    return true;
  }
  if (st != DS_OK) {
    s->error = st < DS_OK ? st : DS_ERR_CALLBACK;
    return false;
  }
  p = ds_map_get(&s->map, s->key);
  if (p != NULL) {
    memcpy(&old, p, sizeof(old));
    if (!hk_later(op, &old.operation)) {
      return true;
    }
  }
  incoming.operation = *op;
  incoming.target = true;
  st = ds_map_put(&s->map, s->key, &incoming);
  if (st != DS_OK && st != DS_EXISTS) {
    s->error = st;
    return false;
  }
  return true;
}

static unsigned int hk_policy(ds_history_branch_t *target,
                              ds_history_branch_t *source,
                              const ds_history_operation_t *incoming,
                              unsigned long *time, int *kind, void **payload,
                              void *user) {
  hk_state_t *s = (hk_state_t *)user;
  hk_record_t old, accepted;
  ds_status_t st;
  unsigned int action = DS_HISTORY_MERGE_TAKE;
  void *p;
  (void)target;
  (void)source;
  (void)time;
  (void)kind;
  (void)payload;
  st = s->config->project(incoming, s->key, s->config->key_size,
                          s->config->user);
  if (st == DS_NOT_FOUND) {
    return DS_HISTORY_MERGE_SKIP;
  }
  if (st != DS_OK) {
    s->error = st < DS_OK ? st : DS_ERR_CALLBACK;
    return DS_HISTORY_MERGE_STOP;
  }
  p = ds_map_get(&s->map, s->key);
  if (p != NULL) {
    memcpy(&old, p, sizeof(old));
    if (s->config->resolve != NULL) {
      st = s->config->resolve(&old.operation, incoming, &action,
                              s->config->user);
      if (st != DS_OK) {
        s->error = st < DS_OK ? st : DS_ERR_CALLBACK;
        return DS_HISTORY_MERGE_STOP;
      }
    } else if (old.target && s->config->conflict == DS_HISTORY_KEEP_TARGET) {
      action = DS_HISTORY_MERGE_SKIP;
    } else if (s->config->conflict == DS_HISTORY_LATEST_OPERATION) {
      action = hk_later(incoming, &old.operation) ? DS_HISTORY_MERGE_TAKE
                                                  : DS_HISTORY_MERGE_SKIP;
    } else if (old.target &&
               s->config->conflict == DS_HISTORY_REJECT_CONFLICT) {
      s->error = DS_ERR_STATE;
      return DS_HISTORY_MERGE_STOP;
    }
  }
  if (action == DS_HISTORY_MERGE_TAKE) {
    accepted.operation = *incoming;
    accepted.target = false;
    st = ds_map_put(&s->map, s->key, &accepted);
    if (st != DS_OK && st != DS_EXISTS) {
      s->error = st;
      return DS_HISTORY_MERGE_STOP;
    }
  }
  return action;
}

void ds_history_keyed_merge_init(ds_history_keyed_merge_t *config) {
  if (config == NULL) {
    return;
  }
  memset(config, 0, sizeof(*config));
  config->conflict = DS_HISTORY_LATEST_OPERATION;
  config->target_through_time = ULONG_MAX;
  config->include_ancestors = true;
}

ds_status_t ds_history_branch_merge_keyed(
    ds_history_branch_t *target, ds_history_branch_t *source,
    unsigned long from_time, unsigned long through_time, unsigned int flags,
    const ds_history_keyed_merge_t *config, size_t *out_merged) {
  hk_state_t state;
  ds_history_branch_t *branch;
  ds_status_t st;
  if (out_merged != NULL) {
    *out_merged = 0;
  }
  if (target == NULL || source == NULL || config == NULL ||
      config->project == NULL || out_merged == NULL) {
    return DS_ERR_NULL;
  }
  if (config->key_size == 0 ||
      config->target_from_time > config->target_through_time ||
      config->conflict < DS_HISTORY_KEEP_TARGET ||
      config->conflict > DS_HISTORY_REJECT_CONFLICT ||
      (config->equal != NULL && config->hash == NULL)) {
    return DS_ERR_INVALID;
  }
  memset(&state, 0, sizeof(state));
  state.config = config;
  state.lo = config->target_from_time;
  state.hi = config->target_through_time;
  state.key =
      (unsigned char *)ds_context_alloc(config->context, config->key_size);
  if (state.key == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ds_map_init(&state.map, config->map, config->key_size,
                   sizeof(hk_record_t), hk_hash, hk_equal, &state,
                   config->context);
  if (st == DS_OK) {
    for (branch = target; branch != NULL && state.error == DS_OK;
         branch = ds_history_branch_parent(branch)) {
      (void)ds_history_branch_export_ops(branch, hk_index, &state);
      if (!config->include_ancestors) {
        break;
      }
      if (ds_history_branch_fork_time(branch) < state.hi) {
        state.hi = ds_history_branch_fork_time(branch);
      }
      if (state.hi < state.lo) {
        break;
      }
    }
    st = state.error;
    if (st == DS_OK) {
      st = ds_history_branch_merge_with_ex(target, source, from_time,
                                           through_time, flags, hk_policy,
                                           &state, out_merged);
    }
    if (state.error != DS_OK) {
      st = state.error;
    }
    ds_map_destroy(&state.map);
  }
  ds_context_free(config->context, state.key);
  return st;
}

ds_status_t ds_history_record_key(const ds_history_operation_t *operation,
                                  void *key, size_t key_size, void *user) {
  const ds_history_keyed_record_t *record;
  (void)user;
  if (operation == NULL || key == NULL || operation->payload == NULL) {
    return DS_ERR_NULL;
  }
  record = (const ds_history_keyed_record_t *)operation->payload;
  if (record->key_size != key_size) {
    return DS_ERR_INVALID;
  }
  if (record->key == NULL && key_size != 0) {
    return DS_ERR_NULL;
  }
  if (key_size != 0) {
    memcpy(key, record->key, key_size);
  }
  return DS_OK;
}
