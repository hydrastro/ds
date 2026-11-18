#include <dsa/core/result.h>
#include <string.h>

struct ds_result_owner {
  size_t refs;
  void *data;
  ds_context_t *context;
  void (*destroy)(void *, void *);
  void *user;
  bool uses_schema;
  ds_schema_t schema;
};

void ds_result_value_init(ds_result_value_t *v) {
  if (v != NULL)
    memset(v, 0, sizeof(*v));
}

void ds_result_value_release(ds_result_value_t *v) {
  ds_result_owner_t *o;
  if (v == NULL)
    return;
  o = v->owner;
  if (o != NULL && --o->refs == 0) {
    if (o->uses_schema) {
      ds_schema_destroy(&o->schema, o->data);
      ds_context_free(o->context, o->data);
    } else if (o->destroy != NULL)
      o->destroy(o->data, o->user);
    ds_context_free(o->context, o);
  }
  ds_result_value_init(v);
}

void ds_result_value_borrow(ds_result_value_t *out, void *data) {
  if (out == NULL)
    return;
  /* Borrowing the same view need not discard an existing ownership right. */
  if (out->data == data)
    return;
  ds_result_value_release(out);
  out->data = data;
}

ds_status_t ds_result_value_view(const ds_result_value_t *src, void *data,
                                 ds_result_value_t *out) {
  ds_result_value_t v;
  if (src == NULL || out == NULL)
    return DS_ERR_NULL;
  v = *src;
  v.data = data;
  if (v.owner != NULL) {
    if (v.owner->refs == (size_t)-1)
      return DS_ERR_OVERFLOW;
    v.owner->refs++;
  }
  ds_result_value_release(out);
  *out = v;
  return DS_OK;
}

ds_status_t ds_result_value_copy(const ds_result_value_t *src,
                                 ds_result_value_t *out) {
  if (src == NULL || out == NULL)
    return DS_ERR_NULL;
  return src == out ? DS_OK : ds_result_value_view(src, src->data, out);
}

ds_status_t ds_result_value_take(ds_result_value_t *out, void *data,
                                 void (*destroy)(void *, void *), void *user,
                                 ds_context_t *context) {
  ds_result_owner_t *o;
  if (out == NULL)
    return DS_ERR_NULL;
  if (destroy == NULL) {
    ds_result_value_borrow(out, data);
    return DS_OK;
  }
  if (out->owner != NULL && out->data == data)
    return DS_ERR_INVALID;
  o = (ds_result_owner_t *)ds_context_calloc(context, 1, sizeof(*o));
  if (o == NULL)
    return DS_ERR_ALLOC;
  o->refs = 1;
  o->data = data;
  o->context = context;
  o->destroy = destroy;
  o->user = user;
  ds_result_value_release(out);
  out->data = data;
  out->owner = o;
  return DS_OK;
}

ds_status_t ds_result_value_from_schema(ds_result_value_t *out,
                                        const void *data,
                                        const ds_schema_t *schema,
                                        ds_context_t *context) {
  ds_result_owner_t *o;
  ds_status_t st;
  if (out == NULL || data == NULL || schema == NULL)
    return DS_ERR_NULL;
  st = ds_schema_validate(schema);
  if (st != DS_OK)
    return st;
  if (schema->size == 0)
    return DS_ERR_INVALID;
  o = (ds_result_owner_t *)ds_context_calloc(context, 1, sizeof(*o));
  if (o == NULL)
    return DS_ERR_ALLOC;
  o->data = ds_context_alloc(context, schema->size);
  if (o->data == NULL) {
    ds_context_free(context, o);
    return DS_ERR_ALLOC;
  }
  st = ds_schema_copy(schema, o->data, data);
  if (st != DS_OK) {
    ds_context_free(context, o->data);
    ds_context_free(context, o);
    return st;
  }
  o->refs = 1;
  o->context = context;
  o->schema = *schema;
  o->uses_schema = true;
  ds_result_value_release(out);
  out->data = o->data;
  out->owner = o;
  return DS_OK;
}

static bool result_diag_copy(void *dst, const void *src, void *user) {
  ds_diagnostic_snapshot_t *s;
  (void)user;
  memcpy(&s, src, sizeof(s));
  if (ds_diagnostic_snapshot_retain(s) != DS_OK)
    return false;
  memcpy(dst, &s, sizeof(s));
  return true;
}

static void result_diag_destroy(void *elem, void *user) {
  ds_diagnostic_snapshot_t *s;
  (void)user;
  memcpy(&s, elem, sizeof(s));
  ds_diagnostic_snapshot_release(s);
}

static const ds_result_config_t result_default = {
    {sizeof(ds_diagnostic_snapshot_t *), 5, result_diag_copy,
     result_diag_destroy, NULL, NULL}};

void ds_result_config_init(ds_result_config_t *config, ds_context_t *context) {
  if (config == NULL)
    return;
  *config = result_default;
  config->diagnostics.context = context;
}

void ds_result_init(ds_result_t *r) {
  if (r != NULL)
    memset(r, 0, sizeof(*r));
}

void ds_result_destroy(ds_result_t *r) {
  if (r == NULL)
    return;
  ds_result_value_release(&r->payload);
  if (r->diagnostics.config != NULL)
    ds_rrb_release(&r->diagnostics);
  ds_result_init(r);
}

void ds_result_move(ds_result_t *src, ds_result_t *out) {
  if (src == NULL || out == NULL || src == out)
    return;
  ds_result_destroy(out);
  *out = *src;
  ds_result_init(src);
}

static bool result_valid(const ds_result_t *r) {
  return r != NULL && (r->state == DS_RESULT_OK || r->state == DS_RESULT_ERR);
}

ds_status_t ds_result_copy(const ds_result_t *src, ds_result_t *out) {
  ds_result_t temp;
  ds_status_t st;
  if (src == NULL || out == NULL)
    return DS_ERR_NULL;
  if (!result_valid(src))
    return DS_ERR_STATE;
  if (src == out)
    return DS_OK;
  if (src->diagnostics.root != NULL &&
      src->diagnostics.root->refs == (size_t)-1)
    return DS_ERR_OVERFLOW;
  ds_result_init(&temp);
  st = ds_result_value_copy(&src->payload, &temp.payload);
  if (st != DS_OK)
    return st;
  ds_rrb_copy(&src->diagnostics, &temp.diagnostics);
  temp.state = src->state;
  ds_result_move(&temp, out);
  return DS_OK;
}

static ds_status_t result_append(ds_rrb_t *log, const ds_diagnostics_t *more) {
  size_t i;
  ds_status_t st = DS_OK;
  if (more == NULL)
    return DS_OK;
  if (more->status != DS_OK)
    return more->status;
  for (i = 0; i < more->size && st == DS_OK; i++)
    st = ds_rrb_push_back(log, &more->snapshots[i], log);
  return st;
}

static ds_status_t result_make(ds_result_t *out, ds_result_state_t state,
                               const ds_result_value_t *value,
                               const ds_diagnostics_t *diagnostics,
                               const ds_result_config_t *config) {
  ds_result_t temp;
  ds_status_t st;
  if (out == NULL || value == NULL)
    return DS_ERR_NULL;
  if (config == NULL)
    config = &result_default;
  if (config->diagnostics.elem_size != sizeof(ds_diagnostic_snapshot_t *) ||
      config->diagnostics.copy_elem != result_diag_copy ||
      config->diagnostics.destroy_elem != result_diag_destroy)
    return DS_ERR_INVALID;
  ds_result_init(&temp);
  st = ds_rrb_init(&temp.diagnostics, &config->diagnostics);
  if (st == DS_OK)
    st = ds_result_value_copy(value, &temp.payload);
  if (st == DS_OK)
    st = result_append(&temp.diagnostics, diagnostics);
  if (st == DS_OK) {
    temp.state = state;
    ds_result_move(&temp, out);
  }
  ds_result_destroy(&temp);
  return st;
}

ds_status_t ds_result_ok(ds_result_t *out, const ds_result_value_t *value,
                         const ds_diagnostics_t *diagnostics,
                         const ds_result_config_t *config) {
  return result_make(out, DS_RESULT_OK, value, diagnostics, config);
}

ds_status_t ds_result_err(ds_result_t *out, const ds_result_value_t *error,
                          const ds_diagnostics_t *diagnostics,
                          const ds_result_config_t *config) {
  return result_make(out, DS_RESULT_ERR, error, diagnostics, config);
}

ds_status_t ds_result_ok_ptr(ds_result_t *out, void *value,
                             const ds_diagnostics_t *diagnostics,
                             const ds_result_config_t *config) {
  ds_result_value_t v;
  ds_result_value_init(&v);
  v.data = value;
  return ds_result_ok(out, &v, diagnostics, config);
}

ds_status_t ds_result_err_ptr(ds_result_t *out, void *error,
                              const ds_diagnostics_t *diagnostics,
                              const ds_result_config_t *config) {
  ds_result_value_t v;
  ds_result_value_init(&v);
  v.data = error;
  return ds_result_err(out, &v, diagnostics, config);
}

bool ds_result_is_ok(const ds_result_t *r) {
  return r != NULL && r->state == DS_RESULT_OK;
}
bool ds_result_is_err(const ds_result_t *r) {
  return r != NULL && r->state == DS_RESULT_ERR;
}

ds_status_t ds_result_unwrap(const ds_result_t *r, const void **out) {
  if (r == NULL || out == NULL)
    return DS_ERR_NULL;
  if (!ds_result_is_ok(r))
    return DS_ERR_STATE;
  *out = r->payload.data;
  return DS_OK;
}

ds_status_t ds_result_error(const ds_result_t *r, const void **out) {
  if (r == NULL || out == NULL)
    return DS_ERR_NULL;
  if (!ds_result_is_err(r))
    return DS_ERR_STATE;
  *out = r->payload.data;
  return DS_OK;
}

const void *ds_result_value_or(const ds_result_t *r, const void *fallback) {
  return ds_result_is_ok(r) ? r->payload.data : fallback;
}

size_t ds_result_diagnostic_count(const ds_result_t *r) {
  return result_valid(r) ? ds_rrb_size(&r->diagnostics) : 0;
}

const ds_diagnostic_t *ds_result_diagnostic_at(const ds_result_t *r,
                                               size_t index) {
  const void *p;
  ds_diagnostic_snapshot_t *s;
  if (!result_valid(r))
    return NULL;
  p = ds_rrb_get(&r->diagnostics, index);
  if (p == NULL)
    return NULL;
  memcpy(&s, p, sizeof(s));
  return ds_diagnostic_snapshot_get(s);
}

ds_status_t ds_result_with_diagnostics(const ds_result_t *src,
                                       const ds_diagnostics_t *more,
                                       ds_result_t *out) {
  ds_result_t temp;
  ds_status_t st;
  if (out == NULL)
    return DS_ERR_NULL;
  ds_result_init(&temp);
  st = ds_result_copy(src, &temp);
  if (st == DS_OK)
    st = result_append(&temp.diagnostics, more);
  if (st == DS_OK)
    ds_result_move(&temp, out);
  ds_result_destroy(&temp);
  return st;
}

static ds_status_t result_map(const ds_result_t *src, ds_result_map_fn f,
                              void *user, ds_result_t *out,
                              ds_result_state_t branch) {
  ds_result_t temp;
  ds_result_value_t mapped;
  ds_status_t st;
  if (src == NULL || out == NULL || f == NULL)
    return DS_ERR_NULL;
  if (!result_valid(src))
    return DS_ERR_STATE;
  if (src->state != branch)
    return ds_result_copy(src, out);
  ds_result_init(&temp);
  ds_result_value_init(&mapped);
  st = f(&src->payload, user, &mapped);
  if (st > DS_OK)
    st = DS_ERR_CALLBACK;
  if (st == DS_OK)
    st = ds_result_copy(src, &temp);
  if (st == DS_OK) {
    ds_result_value_release(&temp.payload);
    temp.payload = mapped;
    ds_result_value_init(&mapped);
    ds_result_move(&temp, out);
  }
  ds_result_value_release(&mapped);
  ds_result_destroy(&temp);
  return st;
}

ds_status_t ds_result_map(const ds_result_t *src, ds_result_map_fn f,
                          void *user, ds_result_t *out) {
  return result_map(src, f, user, out, DS_RESULT_OK);
}

ds_status_t ds_result_map_error(const ds_result_t *src, ds_result_map_fn f,
                                void *user, ds_result_t *out) {
  return result_map(src, f, user, out, DS_RESULT_ERR);
}

static ds_status_t result_bind(const ds_result_t *src, ds_result_bind_fn f,
                               void *user, ds_result_t *out,
                               ds_result_state_t branch) {
  ds_result_t next;
  ds_rrb_t joined;
  ds_status_t st;
  if (src == NULL || out == NULL || f == NULL)
    return DS_ERR_NULL;
  if (!result_valid(src))
    return DS_ERR_STATE;
  if (src->state != branch)
    return ds_result_copy(src, out);
  ds_result_init(&next);
  memset(&joined, 0, sizeof(joined));
  st = f(&src->payload, user, &next);
  if (st > DS_OK || (st == DS_OK && !result_valid(&next)))
    st = DS_ERR_CALLBACK;
  if (st == DS_OK && src->diagnostics.config == next.diagnostics.config) {
    st = ds_rrb_concat(&src->diagnostics, &next.diagnostics, &joined);
  } else if (st == DS_OK) {
    ds_rrb_iter_t it;
    const void *item;
    /* Different allocator/branching configs cannot share RRB inner nodes.
     * Snapshot records still share their own immutable allocations. */
    st = ds_rrb_init(&joined, next.diagnostics.config);
    ds_rrb_iter_init(&it, &src->diagnostics, 0);
    while (st == DS_OK && (item = ds_rrb_iter_next(&it)) != NULL)
      st = ds_rrb_push_back(&joined, item, &joined);
    if (st == DS_OK)
      st = ds_rrb_concat(&joined, &next.diagnostics, &joined);
  }
  if (st == DS_OK) {
    ds_rrb_release(&next.diagnostics);
    next.diagnostics = joined;
    memset(&joined, 0, sizeof(joined));
    ds_result_move(&next, out);
  }
  if (joined.config != NULL)
    ds_rrb_release(&joined);
  ds_result_destroy(&next);
  return st;
}

ds_status_t ds_result_and_then(const ds_result_t *src, ds_result_bind_fn f,
                               void *user, ds_result_t *out) {
  return result_bind(src, f, user, out, DS_RESULT_OK);
}

ds_status_t ds_result_or_else(const ds_result_t *src, ds_result_bind_fn f,
                              void *user, ds_result_t *out) {
  return result_bind(src, f, user, out, DS_RESULT_ERR);
}

void ds_result_drain_to(const ds_result_t *r, ds_diagnostic_sink_t *sink) {
  ds_rrb_iter_t it;
  const void *item;
  if (!result_valid(r) || sink == NULL)
    return;
  ds_rrb_iter_init(&it, &r->diagnostics, 0);
  while ((item = ds_rrb_iter_next(&it)) != NULL) {
    ds_diagnostic_snapshot_t *s;
    memcpy(&s, item, sizeof(s));
    ds_diagnostic_sink_emit(sink, ds_diagnostic_snapshot_get(s));
  }
}
