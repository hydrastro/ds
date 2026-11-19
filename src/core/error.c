#define _POSIX_C_SOURCE 200112L
#include <dsa/core/context.h>
#include <dsa/core/error.h>
#include <stdio.h>
#include <string.h>

void ds_error_clear(ds_error_t *error) {
  if (error == NULL) {
    return;
  }
  error->status = DS_OK;
  error->id = NULL;
  error->module = NULL;
  error->message = NULL;
  error->file = NULL;
  error->line = 0UL;
  error->function = NULL;
  error->category = DS_ERROR_UNCLASSIFIED;
  error->native_domain = NULL;
  error->native_code = 0;
}

ds_status_t ds_error_set(ds_error_t *error, ds_status_t status, const char *id,
                         const char *module, const char *message,
                         const char *file, unsigned long line,
                         const char *function) {
  if (error != NULL) {
    error->status = status;
    error->id = id;
    error->module = module;
    error->message = message;
    error->file = file;
    error->line = line;
    error->function = function;
    error->category = DS_ERROR_UNCLASSIFIED;
    error->native_domain = NULL;
    error->native_code = 0;
  }
  return status;
}

ds_status_t ds_error_set_desc(ds_error_t *error, const ds_error_desc_t *desc,
                              const char *file, unsigned long line,
                              const char *function) {
  if (desc == NULL) {
    return ds_error_set(error, DS_ERR_INTERNAL, "ds.error/null_desc", "error",
                        "error descriptor is NULL", file, line, function);
  }
  return ds_error_set(error, desc->status, desc->id, desc->module,
                      desc->message, file, line, function);
}

int ds_error_format(const ds_error_t *error, char *buffer,
                    size_t buffer_size) {
  const char *id;
  const char *module;
  const char *message;
  const char *file;
  const char *function;

  if (buffer == NULL || buffer_size == 0U) {
    return -1;
  }
  if (error == NULL || error->status == DS_OK) {
    return snprintf(buffer, buffer_size, "[OK]");
  }

  id = error->id != NULL ? error->id : "ds.error/unknown";
  module = error->module != NULL ? error->module : "unknown";
  message = error->message != NULL ? error->message : "unknown error";
  file = error->file != NULL ? error->file : "?";
  function = error->function != NULL ? error->function : "?";

  return snprintf(buffer, buffer_size, "[%s] %s (%s): %s at %s:%lu:%s",
                  ds_status_name(error->status), id, module, message, file,
                  error->line, function);
}

bool ds_error_matches(const ds_error_t *error, const char *id) {
  return error != NULL && error->id != NULL && id != NULL &&
         strcmp(error->id, id) == 0;
}
struct ds_error_snapshot {
  ds_error_t view;
  ds_diagnostic_snapshot_t *record;
  struct ds_error_snapshot *cause;
  ds_context_t *context;
  size_t refs;
};
ds_status_t ds_error_snapshot_create(const ds_error_t *error,
                                     ds_error_snapshot_t *cause,
                                     ds_context_t *context,
                                     ds_error_snapshot_t **out) {
  ds_error_snapshot_t *s;
  ds_diagnostic_t d;
  ds_diagnostic_arg_t args[2];
  const ds_diagnostic_t *owned;
  ds_status_t st;
  if (error == NULL || out == NULL)
    return DS_ERR_NULL;
  if (error->category < DS_ERROR_UNCLASSIFIED ||
      error->category > DS_ERROR_INTERNAL)
    return DS_ERR_INVALID;
  if (cause != NULL && cause->refs == (size_t)-1)
    return DS_ERR_OVERFLOW;
  s = (ds_error_snapshot_t *)ds_context_alloc(context, sizeof(*s));
  if (s == NULL)
    return DS_ERR_ALLOC;
  memset(&d, 0, sizeof(d));
  d.id = error->id;
  d.module = error->module;
  d.message = error->message;
  d.file = error->file;
  d.line = error->line;
  d.function = error->function;
  d.level = DS_DIAG_ERROR;
  args[0].name = "native_domain";
  args[0].type = DS_ARG_CSTR;
  args[0].value.cstr = error->native_domain;
  args[1].name = "native_code";
  args[1].type = DS_ARG_INT;
  args[1].value.i = error->native_code;
  d.args = args;
  d.arg_count = 2;
  st = ds_diagnostic_snapshot_create(&s->record, &d, context);
  if (st != DS_OK) {
    ds_context_free(context, s);
    return st;
  }
  owned = ds_diagnostic_snapshot_get(s->record);
  s->view = *error;
  s->view.id = owned->id;
  s->view.module = owned->module;
  s->view.message = owned->message;
  s->view.file = owned->file;
  s->view.function = owned->function;
  s->view.native_domain = owned->args[0].value.cstr;
  s->cause = cause;
  s->refs = 1;
  s->context = context != NULL ? context : ds_default_context();
  if (cause != NULL)
    cause->refs++;
  *out = s;
  return DS_OK;
}
ds_status_t ds_error_snapshot_retain(ds_error_snapshot_t *s) {
  if (s == NULL)
    return DS_ERR_NULL;
  if (s->refs == (size_t)-1)
    return DS_ERR_OVERFLOW;
  s->refs++;
  return DS_OK;
}
void ds_error_snapshot_release(ds_error_snapshot_t *s) {
  while (s != NULL && --s->refs == 0) {
    ds_error_snapshot_t *cause = s->cause;
    ds_diagnostic_snapshot_release(s->record);
    ds_context_free(s->context, s);
    s = cause;
  }
}
const ds_error_t *ds_error_snapshot_get(const ds_error_snapshot_t *s) {
  return s != NULL ? &s->view : NULL;
}
ds_error_category_t ds_error_snapshot_category(const ds_error_snapshot_t *s) {
  return s != NULL ? s->view.category : DS_ERROR_UNCLASSIFIED;
}
const char *ds_error_snapshot_native_domain(const ds_error_snapshot_t *s) {
  return s != NULL ? s->view.native_domain : NULL;
}
long ds_error_snapshot_native_code(const ds_error_snapshot_t *s) {
  return s != NULL ? s->view.native_code : 0;
}
ds_error_snapshot_t *ds_error_snapshot_cause(const ds_error_snapshot_t *s) {
  return s != NULL ? s->cause : NULL;
}

const ds_diagnostic_t *
ds_error_snapshot_diagnostic(const ds_error_snapshot_t *s) {
  return s != NULL ? ds_diagnostic_snapshot_get(s->record) : NULL;
}
