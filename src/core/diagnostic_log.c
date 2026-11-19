#include <dsa/core/checked.h>
#include <dsa/core/diagnostic_log.h>
#include <dsa/structures/rrb_vector.h>
#include <string.h>

typedef struct log_rrb {
  size_t refs;
  ds_rrb_config_t config;
  ds_rrb_t vector;
} log_rrb_t;
typedef struct log_flat {
  size_t refs, count;
  ds_context_t *context;
  ds_diagnostic_snapshot_t **items;
} log_flat_t;

static bool log_snapshot_copy(void *out, const void *in, void *user) {
  ds_diagnostic_snapshot_t *snapshot;
  (void)user;
  memcpy(&snapshot, in, sizeof(snapshot));
  if (ds_diagnostic_snapshot_retain(snapshot) != DS_OK)
    return false;
  memcpy(out, &snapshot, sizeof(snapshot));
  return true;
}
static void log_snapshot_destroy(void *item, void *user) {
  ds_diagnostic_snapshot_t *snapshot;
  (void)user;
  memcpy(&snapshot, item, sizeof(snapshot));
  ds_diagnostic_snapshot_release(snapshot);
}
static ds_status_t log_rrb_retain(void *state) {
  log_rrb_t *s = (log_rrb_t *)state;
  if (s->refs == (size_t)-1)
    return DS_ERR_OVERFLOW;
  s->refs++;
  return DS_OK;
}
static void log_rrb_release(void *state) {
  log_rrb_t *s = (log_rrb_t *)state;
  if (--s->refs == 0) {
    ds_rrb_release(&s->vector);
    ds_context_free(s->config.context, s);
  }
}
static size_t log_rrb_count(const void *state) {
  return ((const log_rrb_t *)state)->vector.size;
}
static ds_diagnostic_snapshot_t *log_rrb_at(const void *state, size_t index) {
  const void *item = ds_rrb_get(&((const log_rrb_t *)state)->vector, index);
  ds_diagnostic_snapshot_t *snapshot = NULL;
  if (item != NULL)
    memcpy(&snapshot, item, sizeof(snapshot));
  return snapshot;
}
static ds_status_t log_rrb_new(const ds_diagnostic_log_config_t *config,
                               log_rrb_t **out) {
  log_rrb_t *s;
  ds_status_t st;
  if (config->bits < 2 || config->bits > 8)
    return DS_ERR_RANGE;
  s = (log_rrb_t *)ds_context_alloc(config->context, sizeof(*s));
  if (s == NULL)
    return DS_ERR_ALLOC;
  s->refs = 1;
  ds_rrb_config_init(&s->config, sizeof(ds_diagnostic_snapshot_t *));
  s->config.bits = config->bits;
  s->config.context = config->context;
  s->config.copy_elem = log_snapshot_copy;
  s->config.destroy_elem = log_snapshot_destroy;
  st = ds_rrb_init(&s->vector, &s->config);
  if (st != DS_OK) {
    ds_context_free(config->context, s);
    return st;
  }
  *out = s;
  return DS_OK;
}
static ds_status_t log_rrb_append(const void *state,
                                  ds_diagnostic_snapshot_t *record,
                                  const ds_diagnostic_log_config_t *config,
                                  void **out) {
  const log_rrb_t *src = (const log_rrb_t *)state;
  log_rrb_t *s = NULL;
  ds_rrb_t view;
  ds_status_t st = log_rrb_new(config, &s);
  if (st != DS_OK)
    return st;
  view = src != NULL ? src->vector : s->vector;
  /* Nodes contain no config pointers. Equivalent policy allows sharing with
   * this state's owned config; a temporary view borrows the source root. */
  view.config = &s->config;
  st = ds_rrb_push_back(&view, &record, &s->vector);
  if (st == DS_OK)
    *out = s;
  else
    log_rrb_release(s);
  return st;
}
static ds_status_t log_rrb_concat(const void *left, const void *right,
                                  const ds_diagnostic_log_config_t *config,
                                  void **out) {
  const log_rrb_t *a = (const log_rrb_t *)left;
  const log_rrb_t *b = (const log_rrb_t *)right;
  log_rrb_t *s = NULL;
  ds_rrb_t av, bv;
  ds_status_t st = log_rrb_new(config, &s);
  if (st != DS_OK)
    return st;
  av = a->vector;
  bv = b->vector;
  av.config = bv.config = &s->config;
  st = ds_rrb_concat(&av, &bv, &s->vector);
  if (st == DS_OK)
    *out = s;
  else
    log_rrb_release(s);
  return st;
}
static ds_status_t log_flat_retain(void *state) {
  log_flat_t *s = (log_flat_t *)state;
  if (s->refs == (size_t)-1)
    return DS_ERR_OVERFLOW;
  s->refs++;
  return DS_OK;
}
static void log_flat_release(void *state) {
  log_flat_t *s = (log_flat_t *)state;
  if (--s->refs == 0) {
    size_t i;
    for (i = 0; i < s->count; ++i)
      ds_diagnostic_snapshot_release(s->items[i]);
    ds_context_free(s->context, s->items);
    ds_context_free(s->context, s);
  }
}
static size_t log_flat_count(const void *state) {
  return ((const log_flat_t *)state)->count;
}
static ds_diagnostic_snapshot_t *log_flat_at(const void *state, size_t index) {
  const log_flat_t *s = (const log_flat_t *)state;
  return index < s->count ? s->items[index] : NULL;
}
static ds_status_t log_flat_build(const log_flat_t *a, const log_flat_t *b,
                                  ds_diagnostic_snapshot_t *extra,
                                  const ds_diagnostic_log_config_t *config,
                                  void **out) {
  log_flat_t *s;
  size_t ac = a != NULL ? a->count : 0, bc = b != NULL ? b->count : 0;
  size_t count, bytes, i;
  ds_status_t st = DS_OK;
  if (!ds_size_add(ac, bc, &count) ||
      !ds_size_add(count, extra != NULL ? 1 : 0, &count) ||
      !ds_size_mul(count, sizeof(ds_diagnostic_snapshot_t *), &bytes))
    return DS_ERR_OVERFLOW;
  s = (log_flat_t *)ds_context_alloc(config->context, sizeof(*s));
  if (s == NULL)
    return DS_ERR_ALLOC;
  s->refs = 1;
  s->count = 0;
  s->context = config->context;
  s->items =
      (ds_diagnostic_snapshot_t **)ds_context_alloc(config->context, bytes);
  if (s->items == NULL) {
    ds_context_free(config->context, s);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < count; ++i) {
    ds_diagnostic_snapshot_t *item =
        i < ac ? a->items[i] : (i - ac < bc ? b->items[i - ac] : extra);
    st = ds_diagnostic_snapshot_retain(item);
    if (st != DS_OK)
      break;
    s->items[s->count++] = item;
  }
  if (st == DS_OK)
    *out = s;
  else
    log_flat_release(s);
  return st;
}
static ds_status_t log_flat_append(const void *state,
                                   ds_diagnostic_snapshot_t *record,
                                   const ds_diagnostic_log_config_t *config,
                                   void **out) {
  return log_flat_build((const log_flat_t *)state, NULL, record, config, out);
}
static ds_status_t log_flat_concat(const void *a, const void *b,
                                   const ds_diagnostic_log_config_t *config,
                                   void **out) {
  return log_flat_build((const log_flat_t *)a, (const log_flat_t *)b, NULL,
                        config, out);
}
static ds_status_t log_rrb_walk(const void *state,
                                ds_diagnostic_log_visit_fn visit, void *user) {
  ds_rrb_iter_t it;
  const void *item;
  ds_status_t st;
  ds_diagnostic_snapshot_t *snapshot;
  ds_rrb_iter_init(&it, &((const log_rrb_t *)state)->vector, 0);
  while ((item = ds_rrb_iter_next(&it)) != NULL) {
    memcpy(&snapshot, item, sizeof(snapshot));
    st = visit(snapshot, user);
    if (st != DS_OK)
      return st;
  }
  return DS_OK;
}
static ds_status_t log_flat_walk(const void *state,
                                 ds_diagnostic_log_visit_fn visit,
                                 void *user) {
  const log_flat_t *s = (const log_flat_t *)state;
  size_t i;
  ds_status_t st;
  for (i = 0; i < s->count; ++i) {
    st = visit(s->items[i], user);
    if (st != DS_OK)
      return st;
  }
  return DS_OK;
}
static const ds_diagnostic_log_ops_t rrb_ops = {
    log_rrb_retain, log_rrb_release, log_rrb_count, log_rrb_at,
    log_rrb_append, log_rrb_concat,  log_rrb_walk};
static const ds_diagnostic_log_ops_t flat_ops = {
    log_flat_retain, log_flat_release, log_flat_count, log_flat_at,
    log_flat_append, log_flat_concat,  log_flat_walk};
const ds_diagnostic_log_ops_t *ds_diagnostic_log_rrb_ops(void) {
  return &rrb_ops;
}
const ds_diagnostic_log_ops_t *ds_diagnostic_log_flat_ops(void) {
  return &flat_ops;
}
void ds_diagnostic_log_config_init(ds_diagnostic_log_config_t *config,
                                   ds_context_t *context) {
  if (config == NULL)
    return;
  memset(config, 0, sizeof(*config));
  config->ops = rrb_ops;
  config->bits = 5;
  config->context = context != NULL ? context : ds_default_context();
}
ds_status_t ds_diagnostic_log_init(ds_diagnostic_log_t *log,
                                   const ds_diagnostic_log_config_t *config) {
  ds_diagnostic_log_config_t c;
  if (log == NULL)
    return DS_ERR_NULL;
  if (config == NULL)
    ds_diagnostic_log_config_init(&c, NULL);
  else
    c = *config;
  if (c.ops.retain == NULL || c.ops.release == NULL || c.ops.count == NULL ||
      c.ops.at == NULL || c.ops.append == NULL)
    return DS_ERR_INVALID;
  if (c.ops.append == log_rrb_append && (c.bits < 2 || c.bits > 8))
    return DS_ERR_RANGE;
  c.context = c.context != NULL ? c.context : ds_default_context();
  log->state = NULL;
  log->config = c;
  return DS_OK;
}
void ds_diagnostic_log_destroy(ds_diagnostic_log_t *log) {
  if (log == NULL)
    return;
  if (log->state != NULL)
    log->config.ops.release(log->state);
  memset(log, 0, sizeof(*log));
}
static ds_status_t log_status(ds_status_t st) {
  return st > DS_OK ? DS_ERR_CALLBACK : st;
}
static void log_replace(ds_diagnostic_log_t *out, ds_diagnostic_log_t *value) {
  ds_diagnostic_log_destroy(out);
  *out = *value;
  memset(value, 0, sizeof(*value));
}
ds_status_t ds_diagnostic_log_copy(const ds_diagnostic_log_t *src,
                                   ds_diagnostic_log_t *out) {
  ds_diagnostic_log_t tmp;
  ds_status_t st = DS_OK;
  if (src == NULL || out == NULL)
    return DS_ERR_NULL;
  if (src->config.ops.append == NULL)
    return DS_ERR_STATE;
  if (src == out)
    return DS_OK;
  tmp = *src;
  if (tmp.state != NULL)
    st = log_status(tmp.config.ops.retain(tmp.state));
  if (st == DS_OK)
    log_replace(out, &tmp);
  return st;
}
size_t ds_diagnostic_log_count(const ds_diagnostic_log_t *log) {
  return log != NULL && log->state != NULL ? log->config.ops.count(log->state)
                                           : 0;
}
const ds_diagnostic_t *ds_diagnostic_log_at(const ds_diagnostic_log_t *log,
                                            size_t index) {
  if (index >= ds_diagnostic_log_count(log))
    return NULL;
  return ds_diagnostic_snapshot_get(log->config.ops.at(log->state, index));
}
ds_status_t ds_diagnostic_log_append(const ds_diagnostic_log_t *src,
                                     ds_diagnostic_snapshot_t *record,
                                     ds_diagnostic_log_t *out) {
  ds_diagnostic_log_t tmp;
  ds_status_t st;
  if (src == NULL || record == NULL || out == NULL)
    return DS_ERR_NULL;
  if (src->config.ops.append == NULL)
    return DS_ERR_STATE;
  if (ds_diagnostic_log_count(src) == (size_t)-1)
    return DS_ERR_OVERFLOW;
  tmp = *src;
  tmp.state = NULL;
  st = log_status(
      tmp.config.ops.append(src->state, record, &tmp.config, &tmp.state));
  if (st == DS_OK && tmp.state == NULL)
    st = DS_ERR_CALLBACK;
  if (st == DS_OK)
    log_replace(out, &tmp);
  else
    ds_diagnostic_log_destroy(&tmp);
  return st;
}
static bool log_compatible(const ds_diagnostic_log_t *a,
                           const ds_diagnostic_log_t *b) {
  const ds_diagnostic_log_ops_t *x = &a->config.ops, *y = &b->config.ops;
  return x->retain == y->retain && x->release == y->release &&
         x->count == y->count && x->at == y->at && x->append == y->append &&
         x->concat == y->concat && x->walk == y->walk &&
         a->config.context == b->config.context &&
         a->config.bits == b->config.bits && a->config.user == b->config.user;
}
ds_status_t ds_diagnostic_log_walk(const ds_diagnostic_log_t *log,
                                   ds_diagnostic_log_visit_fn visit,
                                   void *user) {
  size_t i, count;
  ds_status_t st;
  if (log == NULL || visit == NULL)
    return DS_ERR_NULL;
  if (log->config.ops.append == NULL)
    return DS_ERR_STATE;
  if (log->state == NULL)
    return DS_OK;
  if (log->config.ops.walk != NULL)
    return log_status(log->config.ops.walk(log->state, visit, user));
  count = ds_diagnostic_log_count(log);
  for (i = 0; i < count; ++i) {
    st = log_status(visit(log->config.ops.at(log->state, i), user));
    if (st != DS_OK)
      return st;
  }
  return DS_OK;
}
static ds_status_t log_import(ds_diagnostic_snapshot_t *snapshot, void *user) {
  ds_diagnostic_log_t *log = (ds_diagnostic_log_t *)user;
  return ds_diagnostic_log_append(log, snapshot, log);
}
ds_status_t ds_diagnostic_log_concat(const ds_diagnostic_log_t *a,
                                     const ds_diagnostic_log_t *b,
                                     ds_diagnostic_log_t *out) {
  ds_diagnostic_log_t tmp;
  size_t count;
  ds_status_t st;
  if (a == NULL || b == NULL || out == NULL)
    return DS_ERR_NULL;
  if (a->config.ops.append == NULL || b->config.ops.append == NULL)
    return DS_ERR_STATE;
  if (!ds_size_add(ds_diagnostic_log_count(a), ds_diagnostic_log_count(b),
                   &count))
    return DS_ERR_OVERFLOW;
  if (b->state == NULL)
    return ds_diagnostic_log_copy(a, out);
  memset(&tmp, 0, sizeof(tmp));
  if (log_compatible(a, b)) {
    if (a->state == NULL)
      return ds_diagnostic_log_copy(b, out);
    if (a->config.ops.concat != NULL) {
      tmp.config = a->config;
      st = log_status(
          a->config.ops.concat(a->state, b->state, &tmp.config, &tmp.state));
      if (st == DS_OK && tmp.state == NULL)
        st = DS_ERR_CALLBACK;
      if (st == DS_OK) {
        log_replace(out, &tmp);
        return DS_OK;
      }
      ds_diagnostic_log_destroy(&tmp);
      if (st != DS_ERR_UNSUPPORTED)
        return st;
    }
  }
  st = ds_diagnostic_log_copy(a, &tmp);
  if (st == DS_OK)
    st = ds_diagnostic_log_walk(b, log_import, &tmp);
  if (st == DS_OK)
    log_replace(out, &tmp);
  ds_diagnostic_log_destroy(&tmp);
  return st;
}
