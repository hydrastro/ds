#include <dsa/core/checked.h>
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
  o->context = context != NULL ? context : ds_default_context();
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
  o->context = context != NULL ? context : ds_default_context();
  o->schema = *schema;
  o->uses_schema = true;
  ds_result_value_release(out);
  out->data = o->data;
  out->owner = o;
  return DS_OK;
}

void ds_result_config_init(ds_result_config_t *config, ds_context_t *context) {
  if (config != NULL)
    ds_diagnostic_log_config_init(&config->diagnostics, context);
}

void ds_result_init(ds_result_t *r) {
  if (r != NULL)
    memset(r, 0, sizeof(*r));
}

void ds_result_destroy(ds_result_t *r) {
  if (r == NULL)
    return;
  ds_result_value_release(&r->payload);
  ds_diagnostic_log_destroy(&r->diagnostics);
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
  ds_result_init(&temp);
  st = ds_result_value_copy(&src->payload, &temp.payload);
  if (st != DS_OK)
    return st;
  st = ds_diagnostic_log_copy(&src->diagnostics, &temp.diagnostics);
  if (st == DS_OK) {
    temp.state = src->state;
    temp.diagnostic_dropped = src->diagnostic_dropped;
    ds_result_move(&temp, out);
  }
  ds_result_destroy(&temp);
  return st;
}

static ds_status_t result_append(ds_result_t *result,
                                 const ds_diagnostics_t *more) {
  size_t i;
  ds_status_t st = DS_OK;
  if (more == NULL)
    return DS_OK;
  if (more->status != DS_OK)
    return more->status;
  if (!ds_size_add(result->diagnostic_dropped, more->dropped,
                   &result->diagnostic_dropped))
    return DS_ERR_OVERFLOW;
  for (i = 0; i < more->size && st == DS_OK; i++)
    st = ds_diagnostic_log_append(&result->diagnostics, more->snapshots[i],
                                  &result->diagnostics);
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
  ds_result_init(&temp);
  st = ds_diagnostic_log_init(&temp.diagnostics,
                              config != NULL ? &config->diagnostics : NULL);
  if (st == DS_OK)
    st = ds_result_value_copy(value, &temp.payload);
  if (st == DS_OK)
    st = result_append(&temp, diagnostics);
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
  return result_valid(r) ? ds_diagnostic_log_count(&r->diagnostics) : 0;
}

const ds_diagnostic_t *ds_result_diagnostic_at(const ds_result_t *r,
                                               size_t index) {
  return result_valid(r) ? ds_diagnostic_log_at(&r->diagnostics, index) : NULL;
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
    st = result_append(&temp, more);
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
  ds_diagnostic_log_t joined;
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
  if (st == DS_OK &&
      !ds_size_add(src->diagnostic_dropped, next.diagnostic_dropped,
                   &next.diagnostic_dropped))
    st = DS_ERR_OVERFLOW;
  if (st == DS_OK)
    st = ds_diagnostic_log_concat(&src->diagnostics, &next.diagnostics,
                                  &joined);
  if (st == DS_OK) {
    ds_diagnostic_log_destroy(&next.diagnostics);
    next.diagnostics = joined;
    memset(&joined, 0, sizeof(joined));
    ds_result_move(&next, out);
  }
  ds_diagnostic_log_destroy(&joined);
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

ds_status_t ds_result_zip(const ds_result_t *left, const ds_result_t *right,
                          ds_result_combine_fn combine_ok,
                          ds_result_combine_fn combine_error, void *user,
                          ds_result_t *out) {
  ds_result_t temp;
  ds_result_combine_fn combine;
  ds_status_t st;
  if (left == NULL || right == NULL || out == NULL)
    return DS_ERR_NULL;
  if (!result_valid(left) || !result_valid(right))
    return DS_ERR_STATE;
  combine = left->state == DS_RESULT_OK ? combine_ok : combine_error;
  if (left->state == right->state && combine == NULL)
    return DS_ERR_NULL;
  ds_result_init(&temp);
  if (!ds_size_add(left->diagnostic_dropped, right->diagnostic_dropped,
                   &temp.diagnostic_dropped))
    return DS_ERR_OVERFLOW;
  st = ds_diagnostic_log_concat(&left->diagnostics, &right->diagnostics,
                                &temp.diagnostics);
  if (st == DS_OK) {
    if (left->state == right->state) {
      temp.state = left->state;
      st = combine(&left->payload, &right->payload, user, &temp.payload);
      if (st > DS_OK)
        st = DS_ERR_CALLBACK;
    } else {
      temp.state = DS_RESULT_ERR;
      st = ds_result_value_copy(left->state == DS_RESULT_ERR ? &left->payload
                                                             : &right->payload,
                                &temp.payload);
    }
  }
  if (st == DS_OK)
    ds_result_move(&temp, out);
  ds_result_destroy(&temp);
  return st;
}

static ds_status_t result_emit(ds_diagnostic_snapshot_t *snapshot,
                               void *user) {
  ds_diagnostic_sink_emit((ds_diagnostic_sink_t *)user,
                          ds_diagnostic_snapshot_get(snapshot));
  return DS_OK;
}
ds_status_t ds_result_drain_to(const ds_result_t *r,
                               ds_diagnostic_sink_t *sink) {
  if (r == NULL || sink == NULL)
    return DS_ERR_NULL;
  if (!result_valid(r))
    return DS_ERR_STATE;
  return ds_diagnostic_log_walk(&r->diagnostics, result_emit, sink);
}

size_t ds_result_diagnostic_dropped(const ds_result_t *r) {
  return result_valid(r) ? r->diagnostic_dropped : 0;
}
static void result_error_release(void *p, void *user) {
  (void)user;
  ds_error_snapshot_release((ds_error_snapshot_t *)p);
}
ds_status_t ds_result_err_snapshot(ds_result_t *out,
                                   ds_error_snapshot_t *error,
                                   const ds_diagnostics_t *diagnostics,
                                   const ds_result_config_t *config) {
  ds_result_value_t value;
  ds_status_t st;
  if (out == NULL || error == NULL)
    return DS_ERR_NULL;
  st = ds_error_snapshot_retain(error);
  if (st != DS_OK)
    return st;
  ds_result_value_init(&value);
  st = ds_result_value_take(&value, error, result_error_release, NULL,
                            config != NULL ? config->diagnostics.context
                                           : NULL);
  if (st != DS_OK) {
    ds_error_snapshot_release(error);
    return st;
  }
  st = ds_result_err(out, &value, diagnostics, config);
  ds_result_value_release(&value);
  return st;
}

ds_status_t ds_result_error_snapshot(const ds_result_t *r,
                                     const ds_error_snapshot_t **out) {
  const ds_result_owner_t *owner;
  if (r == NULL || out == NULL)
    return DS_ERR_NULL;
  if (!ds_result_is_err(r))
    return DS_ERR_STATE;
  owner = r->payload.owner;
  if (owner == NULL || owner->uses_schema ||
      owner->destroy != result_error_release || owner->data != r->payload.data)
    return DS_ERR_INVALID;
  *out = (const ds_error_snapshot_t *)r->payload.data;
  return DS_OK;
}
