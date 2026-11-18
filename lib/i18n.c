#define _POSIX_C_SOURCE 200112L
#include "i18n.h"
#include "../algo/hash.h"
#include <stdio.h>
#include <string.h>

typedef struct i18n_key {
  const char *locale;
  const char *domain;
  const char *key;
} i18n_key_t;

struct ds_i18n_record {
  i18n_key_t key;
  char *locale;
  char *domain;
  char *id;
  char *text;
  ds_i18n_message_t message;
  ds_i18n_record_t *next;
  ds_i18n_record_t *prev;
};

static unsigned long i18n_hash(const void *p, void *user) {
  i18n_key_t k;
  unsigned long h;
  (void)user;
  memcpy(&k, p, sizeof(k));
  h = ds_hash_murmur3_32(k.locale, strlen(k.locale), 0UL);
  h = ds_hash_murmur3_32(k.domain, strlen(k.domain), h);
  return ds_hash_murmur3_32(k.key, strlen(k.key), h);
}

static bool i18n_equal(const void *a, const void *b, void *user) {
  i18n_key_t x, y;
  (void)user;
  memcpy(&x, a, sizeof(x));
  memcpy(&y, b, sizeof(y));
  return strcmp(x.locale, y.locale) == 0 && strcmp(x.domain, y.domain) == 0 &&
         strcmp(x.key, y.key) == 0;
}

static char *i18n_copy(ds_context_t *context, const char *s) {
  size_t len = strlen(s);
  char *out;
  if (len == (size_t)-1)
    return NULL;
  out = (char *)ds_context_alloc(context, len + 1);
  if (out != NULL)
    memcpy(out, s, len + 1);
  return out;
}

static void i18n_record_free(ds_i18n_t *t, ds_i18n_record_t *r) {
  ds_context_free(t->context, r->locale);
  ds_context_free(t->context, r->domain);
  ds_context_free(t->context, r->id);
  ds_context_free(t->context, r->text);
  ds_context_free(t->context, r);
}

void ds_i18n_config_init(ds_i18n_config_t *c) {
  if (c == NULL)
    return;
  memset(c, 0, sizeof(*c));
  c->locale = "en";
  c->fallback_locale = "en";
  c->fallback_to_key = true;
  c->max_depth = 32;
}

ds_status_t ds_i18n_init(ds_i18n_t *t, const ds_i18n_config_t *c) {
  ds_i18n_config_t defaults;
  ds_status_t st;
  if (t == NULL)
    return DS_ERR_NULL;
  if (c == NULL) {
    ds_i18n_config_init(&defaults);
    c = &defaults;
  }
  memset(t, 0, sizeof(*t));
  t->context = c->context;
  t->locale = i18n_copy(t->context, c->locale == NULL ? "en" : c->locale);
  t->fallback_locale = i18n_copy(
      t->context, c->fallback_locale == NULL ? "" : c->fallback_locale);
  if (t->locale == NULL || t->fallback_locale == NULL) {
    ds_i18n_destroy(t);
    return DS_ERR_ALLOC;
  }
  st = ds_map_init(&t->catalog, c->map, sizeof(i18n_key_t),
                   sizeof(ds_i18n_record_t *), i18n_hash, i18n_equal, NULL,
                   t->context);
  if (st != DS_OK) {
    ds_i18n_destroy(t);
    return st;
  }
  t->fallback_to_key = c->fallback_to_key;
  t->max_depth = c->max_depth == 0 ? 32 : c->max_depth;
  t->sink = c->sink;
  t->lookup = c->lookup;
  t->lookup_user = c->lookup_user;
  return DS_OK;
}

void ds_i18n_destroy(ds_i18n_t *t) {
  ds_i18n_record_t *r;
  if (t == NULL)
    return;
  ds_map_destroy(&t->catalog);
  r = t->first;
  while (r != NULL) {
    ds_i18n_record_t *next = r->next;
    i18n_record_free(t, r);
    r = next;
  }
  ds_context_free(t->context, t->locale);
  ds_context_free(t->context, t->fallback_locale);
  memset(t, 0, sizeof(*t));
}

ds_status_t ds_i18n_set_locale(ds_i18n_t *t, const char *locale) {
  char *copy;
  if (t == NULL || locale == NULL)
    return DS_ERR_NULL;
  if (t->depth != 0 || t->catalog.ops == NULL)
    return DS_ERR_STATE;
  copy = i18n_copy(t->context, locale);
  if (copy == NULL)
    return DS_ERR_ALLOC;
  ds_context_free(t->context, t->locale);
  t->locale = copy;
  return DS_OK;
}

static ds_i18n_record_t *i18n_find(ds_i18n_t *t, const char *locale,
                                   const char *domain, const char *key) {
  i18n_key_t k;
  void *p;
  ds_i18n_record_t *r = NULL;
  k.locale = locale;
  k.domain = domain;
  k.key = key;
  p = ds_map_get(&t->catalog, &k);
  if (p != NULL)
    memcpy(&r, p, sizeof(r));
  return r;
}

ds_status_t ds_i18n_set(ds_i18n_t *t, const char *locale, const char *domain,
                        const char *key, const ds_i18n_message_t *message) {
  ds_i18n_record_t *r;
  ds_i18n_message_t m;
  char *text = NULL;
  ds_status_t st;
  if (t == NULL || locale == NULL || domain == NULL || key == NULL ||
      message == NULL)
    return DS_ERR_NULL;
  if (t->depth != 0 || t->catalog.ops == NULL)
    return DS_ERR_STATE;
  m = *message;
  if ((m.text == NULL) == (m.format == NULL))
    return DS_ERR_INVALID;
  if (m.text != NULL) {
    text = i18n_copy(t->context, m.text);
    if (text == NULL)
      return DS_ERR_ALLOC;
  }
  r = i18n_find(t, locale, domain, key);
  if (r != NULL) {
    ds_context_free(t->context, r->text);
    r->text = text;
    r->message = m;
    r->message.text = text;
    return DS_EXISTS;
  }
  r = (ds_i18n_record_t *)ds_context_calloc(t->context, 1, sizeof(*r));
  if (r == NULL) {
    ds_context_free(t->context, text);
    return DS_ERR_ALLOC;
  }
  r->text = text;
  r->locale = i18n_copy(t->context, locale);
  r->domain = i18n_copy(t->context, domain);
  r->id = i18n_copy(t->context, key);
  if (r->locale == NULL || r->domain == NULL || r->id == NULL) {
    i18n_record_free(t, r);
    return DS_ERR_ALLOC;
  }
  r->key.locale = r->locale;
  r->key.domain = r->domain;
  r->key.key = r->id;
  r->message = m;
  r->message.text = text;
  st = ds_map_put(&t->catalog, &r->key, &r);
  if (st != DS_OK) {
    i18n_record_free(t, r);
    return st;
  }
  r->next = t->first;
  if (r->next != NULL)
    r->next->prev = r;
  t->first = r;
  return DS_OK;
}

ds_status_t ds_i18n_set_text(ds_i18n_t *t, const char *locale,
                             const char *domain, const char *key,
                             const char *text) {
  ds_i18n_message_t m;
  m.text = text;
  m.format = NULL;
  m.user = NULL;
  return ds_i18n_set(t, locale, domain, key, &m);
}

ds_status_t ds_i18n_set_many(ds_i18n_t *t, const ds_i18n_entry_t *entries,
                             size_t count) {
  ds_i18n_t staged, old;
  ds_i18n_config_t config;
  ds_i18n_record_t *record;
  ds_status_t st;
  size_t i;
  if (t == NULL || (entries == NULL && count != 0))
    return DS_ERR_NULL;
  if (t->depth != 0 || t->catalog.ops == NULL)
    return DS_ERR_STATE;
  if (count == 0)
    return DS_OK;
  ds_i18n_config_init(&config);
  config.context = t->context;
  config.map = t->catalog.ops;
  config.locale = t->locale;
  config.fallback_locale = t->fallback_locale;
  config.fallback_to_key = t->fallback_to_key;
  config.max_depth = t->max_depth;
  config.sink = t->sink;
  config.lookup = t->lookup;
  config.lookup_user = t->lookup_user;
  st = ds_i18n_init(&staged, &config);
  if (st != DS_OK)
    return st;
  record = t->first;
  if (record != NULL)
    while (record->next != NULL)
      record = record->next;
  for (; record != NULL; record = record->prev) {
    st = ds_i18n_set(&staged, record->locale, record->domain, record->id,
                     &record->message);
    if (st != DS_OK)
      goto done;
  }
  for (i = 0; i < count; i++) {
    st = ds_i18n_set(&staged, entries[i].locale, entries[i].domain,
                     entries[i].key, &entries[i].message);
    if (st != DS_OK && st != DS_EXISTS)
      goto done;
  }
  old = *t;
  *t = staged;
  staged = old;
  st = DS_OK;
done:
  ds_i18n_destroy(&staged);
  return st;
}

ds_status_t ds_i18n_remove(ds_i18n_t *t, const char *locale, const char *domain,
                           const char *key) {
  ds_i18n_record_t *r;
  ds_status_t st;
  if (t == NULL || locale == NULL || domain == NULL || key == NULL)
    return DS_ERR_NULL;
  if (t->depth != 0)
    return DS_ERR_STATE;
  r = i18n_find(t, locale, domain, key);
  if (r == NULL)
    return DS_NOT_FOUND;
  st = ds_map_remove(&t->catalog, &r->key, NULL);
  if (st != DS_OK)
    return st;
  if (r->prev != NULL)
    r->prev->next = r->next;
  else
    t->first = r->next;
  if (r->next != NULL)
    r->next->prev = r->prev;
  i18n_record_free(t, r);
  return DS_OK;
}

static ds_status_t i18n_lookup(ds_i18n_t *t, const char *locale,
                               const char *domain, const char *key,
                               ds_i18n_message_t *out) {
  ds_i18n_record_t *r = i18n_find(t, locale, domain, key);
  ds_status_t st;
  if (r != NULL) {
    *out = r->message;
    return DS_OK;
  }
  if (t->lookup == NULL)
    return DS_NOT_FOUND;
  memset(out, 0, sizeof(*out));
  st = t->lookup(locale, domain, key, out, t->lookup_user);
  if (st == DS_OK && ((out->text == NULL) == (out->format == NULL)))
    return DS_ERR_CALLBACK;
  return st > DS_OK && st != DS_NOT_FOUND ? DS_ERR_CALLBACK : st;
}

static ds_status_t i18n_resolve(ds_i18n_t *t, const char *domain,
                                const char *key, ds_i18n_message_t *out,
                                const char **locale) {
  ds_status_t st;
  *locale = t->locale;
  st = i18n_lookup(t, *locale, domain, key, out);
  if (st == DS_NOT_FOUND && t->fallback_locale[0] != '\0' &&
      strcmp(t->locale, t->fallback_locale) != 0) {
    *locale = t->fallback_locale;
    st = i18n_lookup(t, *locale, domain, key, out);
  }
  return st;
}

ds_status_t ds_i18n_has(ds_i18n_t *t, const char *domain, const char *key,
                        bool *out) {
  ds_i18n_message_t m;
  const char *locale;
  ds_status_t st;
  if (t == NULL || domain == NULL || key == NULL || out == NULL)
    return DS_ERR_NULL;
  if (t->catalog.ops == NULL)
    return DS_ERR_STATE;
  if (t->depth >= t->max_depth)
    return DS_ERR_RANGE;
  t->depth++;
  st = i18n_resolve(t, domain, key, &m, &locale);
  t->depth--;
  if (st == DS_OK || st == DS_NOT_FOUND) {
    *out = st == DS_OK;
    return DS_OK;
  }
  return st;
}

ds_status_t
ds_i18n_catalog_walk(const ds_i18n_t *t,
                     bool (*visit)(const char *, const char *, const char *,
                                   const ds_i18n_message_t *, void *),
                     void *user) {
  ds_i18n_record_t *r;
  if (t == NULL || visit == NULL)
    return DS_ERR_NULL;
  for (r = t->first; r != NULL; r = r->next)
    if (!visit(r->locale, r->domain, r->id, &r->message, user))
      return DS_STOP;
  return DS_OK;
}

void ds_i18n_text_init(ds_i18n_text_t *text, ds_context_t *context) {
  if (text == NULL)
    return;
  memset(text, 0, sizeof(*text));
  text->context = context;
}

void ds_i18n_text_destroy(ds_i18n_text_t *text) {
  if (text == NULL)
    return;
  ds_context_free(text->context, text->data);
  text->data = NULL;
  text->length = 0;
}

const char *ds_i18n_text_cstr(const ds_i18n_text_t *text) {
  return text == NULL || text->data == NULL ? "" : text->data;
}

ds_status_t ds_i18n_write(ds_i18n_writer_t *w, const void *data, size_t len) {
  if (w == NULL)
    return DS_ERR_NULL;
  if (w->status != DS_OK)
    return w->status;
  if (data == NULL && len != 0)
    w->status = DS_ERR_NULL;
  else if (len != 0)
    w->status = ds_vec_append(&w->bytes, data, len);
  return w->status;
}

ds_status_t ds_i18n_write_cstr(ds_i18n_writer_t *w, const char *text) {
  return ds_i18n_write(w, text, text == NULL ? 0 : strlen(text));
}

static ds_status_t i18n_arg(ds_i18n_writer_t *w,
                            const ds_diagnostic_arg_t *arg) {
  char number[3 * sizeof(size_t) + 3 * sizeof(long) + 64];
  int n;
  switch (arg->type) {
  case DS_ARG_CSTR:
    return ds_i18n_write_cstr(w, arg->value.cstr);
  case DS_ARG_BOOL:
    return ds_i18n_write_cstr(w, arg->value.boolean ? "true" : "false");
  case DS_ARG_SIZE: {
    size_t value = arg->value.size;
    size_t pos = sizeof(number);
    do {
      number[--pos] = (char)('0' + value % 10);
      value /= 10;
    } while (value != 0);
    return ds_i18n_write(w, number + pos, sizeof(number) - pos);
  }
  case DS_ARG_INT:
    n = snprintf(number, sizeof(number), "%ld", arg->value.i);
    break;
  case DS_ARG_UINT:
    n = snprintf(number, sizeof(number), "%lu", arg->value.u);
    break;
  case DS_ARG_DOUBLE:
    n = snprintf(number, sizeof(number), "%.17g", arg->value.real);
    break;
  case DS_ARG_PTR:
    n = snprintf(number, sizeof(number), "%p", arg->value.ptr);
    break;
  default:
    return DS_ERR_INVALID;
  }
  if (n < 0 || (size_t)n >= sizeof(number))
    return DS_ERR_OVERFLOW;
  return ds_i18n_write(w, number, (size_t)n);
}

ds_status_t ds_i18n_interpolate(ds_i18n_writer_t *w, const char *text,
                                const ds_diagnostic_arg_t *args, size_t count) {
  const char *p, *literal;
  ds_status_t st = DS_OK;
  if (w == NULL)
    return DS_ERR_NULL;
  if (w->status != DS_OK)
    return w->status;
  if (text == NULL || (args == NULL && count != 0)) {
    w->status = DS_ERR_NULL;
    return w->status;
  }
  p = literal = text;
  while (*p != '\0' && st == DS_OK) {
    if (*p == '{') {
      const char *end = strchr(p + 1, '}');
      size_t i;
      if (end != NULL) {
        for (i = 0; i < count; i++) {
          const char *name = args[i].name;
          if (name != NULL && strlen(name) == (size_t)(end - p - 1) &&
              memcmp(name, p + 1, (size_t)(end - p - 1)) == 0)
            break;
        }
        if (i < count) {
          st = ds_i18n_write(w, literal, (size_t)(p - literal));
          if (st == DS_OK)
            st = i18n_arg(w, &args[i]);
          p = literal = end + 1;
          continue;
        }
      }
    }
    p++;
  }
  if (st == DS_OK)
    st = ds_i18n_write(w, literal, (size_t)(p - literal));
  if (w->status == DS_OK)
    w->status = st;
  return st;
}

static void i18n_missing(ds_i18n_t *t, const char *domain, const char *key) {
  ds_diagnostic_t d;
  ds_diagnostic_arg_t args[3];
  if (t->sink == NULL || t->emitting)
    return;
  memset(&d, 0, sizeof(d));
  d.id = "ds.i18n/missing_translation";
  d.level = DS_DIAG_NOTICE;
  d.module = "i18n";
  d.message = "Missing translation for {domain}/{key} in {locale}";
  args[0].name = "locale";
  args[0].type = DS_ARG_CSTR;
  args[0].value.cstr = t->locale;
  args[1].name = "domain";
  args[1].type = DS_ARG_CSTR;
  args[1].value.cstr = domain;
  args[2].name = "key";
  args[2].type = DS_ARG_CSTR;
  args[2].value.cstr = key;
  d.args = args;
  d.arg_count = 3;
  t->emitting = true;
  ds_diagnostic_sink_emit(t->sink, &d);
  t->emitting = false;
}

static ds_status_t i18n_render(ds_i18n_t *t, const char *domain,
                               const char *key, const ds_diagnostic_arg_t *args,
                               size_t count, const char *fallback,
                               const ds_diagnostic_t *diagnostic, bool quiet,
                               ds_i18n_text_t *out) {
  ds_i18n_message_t m;
  ds_i18n_request_t request;
  ds_i18n_writer_t w;
  ds_status_t st;
  const char *locale;
  char zero = '\0';
  if (t == NULL || domain == NULL || key == NULL || out == NULL ||
      (args == NULL && count != 0))
    return DS_ERR_NULL;
  if (t->catalog.ops == NULL)
    return DS_ERR_STATE;
  if (t->depth >= t->max_depth)
    return DS_ERR_RANGE;
  st = ds_vec_init(&w.bytes, 1, out->context);
  if (st != DS_OK)
    return st;
  w.status = DS_OK;
  t->depth++;
  st = i18n_resolve(t, domain, key, &m, &locale);
  if (st == DS_NOT_FOUND) {
    if (!quiet)
      i18n_missing(t, domain, key);
    if (diagnostic != NULL) {
      st = ds_i18n_write_cstr(&w, "[FALLBACK:");
      if (st == DS_OK)
        st =
            ds_i18n_write_cstr(&w, ds_diagnostic_level_name(diagnostic->level));
      if (st == DS_OK)
        st = ds_i18n_write_cstr(&w, "] ");
      if (st == DS_OK)
        st = ds_i18n_write_cstr(&w, key);
    } else {
      const char *text = fallback != NULL     ? fallback
                         : t->fallback_to_key ? key
                                              : "";
      st = ds_i18n_interpolate(&w, text, args, count);
    }
  } else if (st == DS_OK && m.text != NULL) {
    st = ds_i18n_interpolate(&w, m.text, args, count);
  } else if (st == DS_OK) {
    request.translator = t;
    request.locale = locale;
    request.domain = domain;
    request.key = key;
    request.args = args;
    request.arg_count = count;
    request.diagnostic = diagnostic;
    st = m.format(&request, &w, m.user);
    if (st > DS_OK)
      st = DS_ERR_CALLBACK;
  }
  t->depth--;
  if (st == DS_OK)
    st = w.status;
  if (st == DS_OK)
    st = ds_i18n_write(&w, &zero, 1);
  if (st == DS_OK) {
    ds_i18n_text_destroy(out);
    out->data = (char *)(void *)w.bytes.data;
    out->length = w.bytes.size - 1;
    w.bytes.data = NULL;
    w.bytes.size = w.bytes.capacity = 0;
  }
  ds_vec_destroy(&w.bytes);
  return st;
}

ds_status_t ds_i18n_translate(ds_i18n_t *t, const char *domain, const char *key,
                              const ds_diagnostic_arg_t *args, size_t count,
                              const char *fallback, ds_i18n_text_t *out) {
  return i18n_render(t, domain, key, args, count, fallback, NULL, false, out);
}

ds_status_t ds_i18n_render_diagnostic(ds_i18n_t *t, const ds_diagnostic_t *d,
                                      ds_i18n_text_t *out) {
  if (d == NULL)
    return DS_ERR_NULL;
  if (d->level < DS_DIAG_DEBUG || d->level > DS_DIAG_EMERGENCY)
    return DS_ERR_INVALID;
  return i18n_render(t, "diagnostics",
                     d->id == NULL ? "ds.diagnostic/unknown" : d->id, d->args,
                     d->arg_count, NULL, d, true, out);
}

ds_status_t ds_i18n_render_level(ds_i18n_t *t, ds_diagnostic_level_t level,
                                 ds_i18n_text_t *out) {
  const char *key = ds_diagnostic_level_name(level);
  if (level < DS_DIAG_DEBUG || level > DS_DIAG_EMERGENCY)
    return DS_ERR_INVALID;
  return i18n_render(t, "levels", key, NULL, 0, key, NULL, true, out);
}

ds_status_t ds_i18n_render_filtered(ds_i18n_t *t, const ds_diagnostic_t *d,
                                    ds_diagnostic_level_t min_level,
                                    ds_i18n_filter_fn filter, void *user,
                                    ds_i18n_text_t *out) {
  ds_diagnostic_t view;
  if (d == NULL || out == NULL || t == NULL)
    return DS_ERR_NULL;
  if (min_level < DS_DIAG_DEBUG || min_level > DS_DIAG_EMERGENCY ||
      d->level < DS_DIAG_DEBUG || d->level > DS_DIAG_EMERGENCY)
    return DS_ERR_INVALID;
  view = *d;
  if (filter != NULL && !filter(d, &view.level, user))
    return DS_NOT_FOUND;
  if (view.level < DS_DIAG_DEBUG || view.level > DS_DIAG_EMERGENCY)
    return DS_ERR_CALLBACK;
  if (view.level < min_level)
    return DS_NOT_FOUND;
  return ds_i18n_render_diagnostic(t, &view, out);
}
