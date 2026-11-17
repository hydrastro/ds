#define _POSIX_C_SOURCE 200112L
#include "diagnostic.h"
#include "checked.h"
#include "context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *diag_default_alloc(size_t size) { return malloc(size); }
static void diag_default_free(void *ptr) { free(ptr); }
static void diagnostics_collector_emit(ds_diagnostic_sink_t *sink,
                                       const ds_diagnostic_t *diagnostic);

const char *ds_diagnostic_level_name(ds_diagnostic_level_t level) {
  switch (level) {
  case DS_DIAG_DEBUG:
    return "DEBUG";
  case DS_DIAG_INFO:
    return "INFO";
  case DS_DIAG_NOTICE:
    return "NOTICE";
  case DS_DIAG_WARNING:
    return "WARNING";
  case DS_DIAG_ERROR:
    return "ERROR";
  case DS_DIAG_CRITICAL:
    return "CRITICAL";
  case DS_DIAG_ALERT:
    return "ALERT";
  case DS_DIAG_EMERGENCY:
    return "EMERGENCY";
  default:
    return "UNKNOWN";
  }
}

void ds_diagnostic_sink_emit(ds_diagnostic_sink_t *sink,
                             const ds_diagnostic_t *diagnostic) {
  if (sink == NULL || sink->emit == NULL || diagnostic == NULL) {
    return;
  }
  sink->emit(sink, diagnostic);
}

void ds_diagnostic_emit_desc(ds_diagnostic_sink_t *sink,
                             const ds_diagnostic_desc_t *desc,
                             const ds_diagnostic_arg_t *args, size_t arg_count,
                             const char *file, unsigned long line,
                             const char *function) {
  ds_diagnostic_t diagnostic;

  if (desc == NULL) {
    return;
  }

  diagnostic.id = desc->id;
  diagnostic.level = desc->level;
  diagnostic.module = desc->module;
  diagnostic.message = desc->message;
  diagnostic.file = file;
  diagnostic.line = line;
  diagnostic.function = function;
  diagnostic.args = args;
  diagnostic.arg_count = arg_count;

  ds_diagnostic_sink_emit(sink, &diagnostic);
}

int ds_diagnostic_format(const ds_diagnostic_t *diagnostic, char *buffer,
                         size_t buffer_size) {
  const char *id;
  const char *module;
  const char *message;
  int n;

  if (buffer == NULL || buffer_size == 0U) {
    return -1;
  }
  if (diagnostic == NULL) {
    return snprintf(buffer, buffer_size, "[DIAGNOSTIC NULL]");
  }

  id = diagnostic->id != NULL ? diagnostic->id : "ds.diagnostic/unknown";
  module = diagnostic->module != NULL ? diagnostic->module : "unknown";
  message = diagnostic->message != NULL ? diagnostic->message : "";
  n = snprintf(buffer, buffer_size, "[%s] %s (%s): %s",
               ds_diagnostic_level_name(diagnostic->level), id, module,
               message);
  return n;
}

struct ds_diagnostic_snapshot {
  ds_diagnostic_t diagnostic;
  size_t refs;
  ds_context_t *context;
  void (*deallocator)(void *);
};

static bool diag_string_size(const char *s, size_t *size) {
  size_t n;
  if (s == NULL)
    return true;
  n = strlen(s);
  return n != (size_t)-1 && ds_size_add(*size, n + 1, size);
}

static const char *diag_string_copy(const char *s, char **cursor) {
  char *out;
  size_t n;
  if (s == NULL)
    return NULL;
  out = *cursor;
  n = strlen(s) + 1;
  memcpy(out, s, n);
  *cursor += n;
  return out;
}

static ds_status_t diag_snapshot(ds_diagnostic_snapshot_t **out,
                                 const ds_diagnostic_t *d,
                                 ds_context_t *context,
                                 void *(*allocator)(size_t),
                                 void (*deallocator)(void *)) {
  ds_diagnostic_snapshot_t *s;
  ds_diagnostic_arg_t *args;
  size_t head, bytes, strings = 0, i;
  char *cursor;
  if (out == NULL || d == NULL || (d->arg_count != 0 && d->args == NULL))
    return DS_ERR_NULL;
  if (d->level < DS_DIAG_DEBUG || d->level > DS_DIAG_EMERGENCY)
    return DS_ERR_INVALID;
  if (!ds_size_align(sizeof(*s), DS_MAX_ALIGN, &head) ||
      !ds_size_mul_add(d->arg_count, sizeof(*args), head, &bytes))
    return DS_ERR_OVERFLOW;
  if (!diag_string_size(d->id, &strings) ||
      !diag_string_size(d->module, &strings) ||
      !diag_string_size(d->message, &strings) ||
      !diag_string_size(d->file, &strings) ||
      !diag_string_size(d->function, &strings))
    return DS_ERR_OVERFLOW;
  for (i = 0; i < d->arg_count; i++) {
    if (d->args[i].type < DS_ARG_CSTR || d->args[i].type > DS_ARG_BOOL)
      return DS_ERR_INVALID;
    if (!diag_string_size(d->args[i].name, &strings) ||
        (d->args[i].type == DS_ARG_CSTR &&
         !diag_string_size(d->args[i].value.cstr, &strings)))
      return DS_ERR_OVERFLOW;
  }
  if (!ds_size_add(bytes, strings, &bytes))
    return DS_ERR_OVERFLOW;
  s = (ds_diagnostic_snapshot_t *)(allocator != NULL
                                       ? allocator(bytes)
                                       : ds_context_alloc(context, bytes));
  if (s == NULL)
    return DS_ERR_ALLOC;
  s->refs = 1;
  s->context = context;
  s->deallocator = deallocator;
  s->diagnostic = *d;
  args = (ds_diagnostic_arg_t *)(void *)((unsigned char *)s + head);
  cursor = (char *)(void *)(args + d->arg_count);
  s->diagnostic.id = diag_string_copy(d->id, &cursor);
  s->diagnostic.module = diag_string_copy(d->module, &cursor);
  s->diagnostic.message = diag_string_copy(d->message, &cursor);
  s->diagnostic.file = diag_string_copy(d->file, &cursor);
  s->diagnostic.function = diag_string_copy(d->function, &cursor);
  for (i = 0; i < d->arg_count; i++) {
    args[i] = d->args[i];
    args[i].name = diag_string_copy(d->args[i].name, &cursor);
    if (args[i].type == DS_ARG_CSTR)
      args[i].value.cstr = diag_string_copy(d->args[i].value.cstr, &cursor);
  }
  s->diagnostic.args = d->arg_count == 0 ? NULL : args;
  *out = s;
  return DS_OK;
}

ds_status_t ds_diagnostic_snapshot_create(ds_diagnostic_snapshot_t **out,
                                          const ds_diagnostic_t *diagnostic,
                                          ds_context_t *context) {
  return diag_snapshot(out, diagnostic, context, NULL, NULL);
}

ds_status_t ds_diagnostic_snapshot_create_alloc(
    ds_diagnostic_snapshot_t **out, const ds_diagnostic_t *diagnostic,
    void *(*allocator)(size_t), void (*deallocator)(void *)) {
  if (allocator == NULL || deallocator == NULL)
    return DS_ERR_NULL;
  return diag_snapshot(out, diagnostic, NULL, allocator, deallocator);
}

ds_status_t ds_diagnostic_snapshot_retain(ds_diagnostic_snapshot_t *s) {
  if (s == NULL)
    return DS_ERR_NULL;
  if (s->refs == (size_t)-1)
    return DS_ERR_OVERFLOW;
  s->refs++;
  return DS_OK;
}

void ds_diagnostic_snapshot_release(ds_diagnostic_snapshot_t *s) {
  if (s != NULL && --s->refs == 0) {
    if (s->deallocator != NULL)
      s->deallocator(s);
    else
      ds_context_free(s->context, s);
  }
}

const ds_diagnostic_t *
ds_diagnostic_snapshot_get(const ds_diagnostic_snapshot_t *s) {
  return s == NULL ? NULL : &s->diagnostic;
}

static void *diag_alloc(ds_diagnostics_t *d, size_t n) {
  return d->use_context ? ds_context_alloc(d->context, n) : d->allocator(n);
}

static void diag_free(ds_diagnostics_t *d, void *p) {
  if (p == NULL)
    return;
  if (d->use_context)
    ds_context_free(d->context, p);
  else
    d->deallocator(p);
}

void ds_diagnostics_init(ds_diagnostics_t *d) {
  ds_diagnostics_init_alloc(d, diag_default_alloc, diag_default_free);
}

void ds_diagnostics_init_alloc(ds_diagnostics_t *d, void *(*allocator)(size_t),
                               void (*deallocator)(void *)) {
  if (d == NULL)
    return;
  memset(d, 0, sizeof(*d));
  if ((allocator == NULL) != (deallocator == NULL)) {
    d->status = DS_ERR_INVALID;
    allocator = diag_default_alloc;
    deallocator = diag_default_free;
  }
  d->allocator = allocator == NULL ? diag_default_alloc : allocator;
  d->deallocator = deallocator == NULL ? diag_default_free : deallocator;
  d->sink.emit = diagnostics_collector_emit;
  d->sink.user = d;
}

void ds_diagnostics_init_context(ds_diagnostics_t *d, ds_context_t *context) {
  if (d == NULL)
    return;
  ds_diagnostics_init(d);
  d->context = context;
  d->use_context = true;
}

void ds_diagnostics_clear(ds_diagnostics_t *d) {
  size_t i;
  if (d == NULL)
    return;
  for (i = 0; i < d->size; i++)
    ds_diagnostic_snapshot_release(d->snapshots[i]);
  d->size = 0;
  d->status = DS_OK;
}

void ds_diagnostics_destroy(ds_diagnostics_t *d) {
  if (d == NULL)
    return;
  ds_diagnostics_clear(d);
  diag_free(d, d->items);
  diag_free(d, d->snapshots);
  d->items = NULL;
  d->snapshots = NULL;
  d->capacity = 0;
}

static ds_status_t diag_reserve(ds_diagnostics_t *d, size_t need) {
  size_t cap, bytes;
  ds_diagnostic_t *items;
  ds_diagnostic_snapshot_t **snapshots;
  if (need <= d->capacity)
    return DS_OK;
  cap = d->capacity < 8 ? 8 : d->capacity;
  while (cap < need) {
    if (cap > (size_t)-1 / 2) {
      cap = need;
      break;
    }
    cap *= 2;
  }
  if (!ds_size_mul(cap, sizeof(*items), &bytes))
    return DS_ERR_OVERFLOW;
  items = (ds_diagnostic_t *)diag_alloc(d, bytes);
  if (items == NULL)
    return DS_ERR_ALLOC;
  if (!ds_size_mul(cap, sizeof(*snapshots), &bytes)) {
    diag_free(d, items);
    return DS_ERR_OVERFLOW;
  }
  snapshots = (ds_diagnostic_snapshot_t **)diag_alloc(d, bytes);
  if (snapshots == NULL) {
    diag_free(d, items);
    return DS_ERR_ALLOC;
  }
  if (d->size != 0) {
    memcpy(items, d->items, d->size * sizeof(*items));
    memcpy(snapshots, d->snapshots, d->size * sizeof(*snapshots));
  }
  diag_free(d, d->items);
  diag_free(d, d->snapshots);
  d->items = items;
  d->snapshots = snapshots;
  d->capacity = cap;
  return DS_OK;
}

ds_status_t ds_diagnostics_add(ds_diagnostics_t *d,
                               const ds_diagnostic_t *item) {
  ds_diagnostic_snapshot_t *s = NULL;
  ds_status_t st;
  if (d == NULL || item == NULL)
    return DS_ERR_NULL;
  if (d->size == (size_t)-1)
    return DS_ERR_OVERFLOW;
  st = d->use_context ? ds_diagnostic_snapshot_create(&s, item, d->context)
                      : ds_diagnostic_snapshot_create_alloc(
                            &s, item, d->allocator, d->deallocator);
  if (st != DS_OK)
    return st;
  st = diag_reserve(d, d->size + 1);
  if (st == DS_OK) {
    d->items[d->size] = *ds_diagnostic_snapshot_get(s);
    d->snapshots[d->size++] = s;
  } else
    ds_diagnostic_snapshot_release(s);
  return st;
}

ds_status_t ds_diagnostics_append(ds_diagnostics_t *d,
                                  const ds_diagnostics_t *more) {
  size_t n, i, retained = 0;
  ds_status_t st = DS_OK;
  if (d == NULL || more == NULL)
    return DS_ERR_NULL;
  if (more->status != DS_OK)
    return more->status;
  n = more->size;
  if (n > (size_t)-1 - d->size)
    return DS_ERR_OVERFLOW;
  for (i = 0; i < n; i++) {
    st = ds_diagnostic_snapshot_retain(more->snapshots[i]);
    if (st != DS_OK)
      break;
    retained++;
  }
  if (st == DS_OK)
    st = diag_reserve(d, d->size + n);
  if (st != DS_OK) {
    for (i = 0; i < retained; i++)
      ds_diagnostic_snapshot_release(more->snapshots[i]);
    return st;
  }
  for (i = 0; i < n; i++) {
    d->items[d->size + i] = more->items[i];
    d->snapshots[d->size + i] = more->snapshots[i];
  }
  d->size += n;
  return DS_OK;
}

ds_status_t ds_diagnostics_filter(const ds_diagnostics_t *source,
                                  bool (*keep)(const ds_diagnostic_t *, void *),
                                  void *user, ds_diagnostics_t *out) {
  ds_diagnostics_t temp;
  ds_status_t st = DS_OK;
  size_t i;
  if (source == NULL || keep == NULL || out == NULL)
    return DS_ERR_NULL;
  if (source->status != DS_OK)
    return source->status;
  if (out->use_context)
    ds_diagnostics_init_context(&temp, out->context);
  else
    ds_diagnostics_init_alloc(&temp, out->allocator, out->deallocator);
  for (i = 0; i < source->size; i++) {
    if (keep(&source->items[i], user)) {
      ds_diagnostic_snapshot_t *snapshot = source->snapshots[i];
      st = ds_diagnostic_snapshot_retain(snapshot);
      if (st != DS_OK)
        break;
      st = diag_reserve(&temp, temp.size + 1);
      if (st != DS_OK) {
        ds_diagnostic_snapshot_release(snapshot);
        break;
      }
      temp.items[temp.size] = *ds_diagnostic_snapshot_get(snapshot);
      temp.snapshots[temp.size++] = snapshot;
    }
  }
  if (st == DS_OK) {
    ds_diagnostics_destroy(out);
    *out = temp;
    out->sink.user = out;
  } else
    ds_diagnostics_destroy(&temp);
  return st;
}

ds_diagnostic_sink_t *ds_diagnostics_sink(ds_diagnostics_t *d) {
  return d == NULL ? NULL : &d->sink;
}

void ds_diagnostics_emit(const ds_diagnostics_t *d,
                         ds_diagnostic_sink_t *sink) {
  size_t i, n;
  if (d == NULL || sink == NULL || sink == &d->sink)
    return;
  n = d->size;
  for (i = 0; i < n; i++)
    ds_diagnostic_sink_emit(sink, &d->items[i]);
}

static void diagnostics_collector_emit(ds_diagnostic_sink_t *sink,
                                       const ds_diagnostic_t *diagnostic) {
  ds_diagnostics_t *d;
  ds_status_t st;
  if (sink == NULL || diagnostic == NULL)
    return;
  d = (ds_diagnostics_t *)sink->user;
  if (d == NULL)
    return;
  st = ds_diagnostics_add(d, diagnostic);
  if (d->status == DS_OK && st != DS_OK)
    d->status = st;
}
