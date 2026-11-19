#ifndef DS_I18N_H
#define DS_I18N_H

#include <dsa/core/context.h>
#include <dsa/core/diagnostic.h>
#include <dsa/structures/map_iface.h>
#include <dsa/structures/vec.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Locale/domain/key catalogs and diagnostic rendering. Text remains UTF-8
 * bytes; translation does not mutate the process locale. Registered strings
 * are owned copies. Formatter/provider user data is borrowed. A translator is
 * externally synchronized.
 *
 * Local entries override an optional provider; within a locale later sets
 * replace earlier ones (module catalogs first, application overrides last).
 * Lookup tries the selected locale, then the explicit fallback locale.
 * Locale changes never reuse another locale's entries by accident.
 * Provider IO/errors propagate; only DS_NOT_FOUND invokes fallback.
 *
 * Catalogs use ds_map_ops_t (Robin Hood by default). Output uses a vector
 * with the caller's context. Callbacks can implement plural rules, number
 * formatting, nested translations or any external catalog format. They must
 * not mutate catalogs, locale or destroy the translator during rendering.
 */
typedef struct ds_i18n ds_i18n_t;
typedef struct ds_i18n_record ds_i18n_record_t;
typedef struct ds_i18n_writer {
  ds_vec_t bytes;
  ds_status_t status; /* sticky: ignored append errors still fail rendering */
} ds_i18n_writer_t;
typedef struct ds_i18n_request {
  ds_i18n_t *translator;
  const char *locale; /* locale of the selected entry */
  const char *domain;
  const char *key;
  const ds_diagnostic_arg_t *args;
  size_t arg_count;
  const ds_diagnostic_t *diagnostic; /* set only by diagnostic rendering */
} ds_i18n_request_t;
typedef ds_status_t (*ds_i18n_format_fn)(const ds_i18n_request_t *request,
                                         ds_i18n_writer_t *writer, void *user);
typedef struct ds_i18n_message {
  const char *text; /* exactly one of text or format is non-NULL */
  ds_i18n_format_fn format;
  void *user;
} ds_i18n_message_t;
/* DS_OK with a borrowed message valid through this render, DS_NOT_FOUND,
 * or a negative operational failure. */
typedef ds_status_t (*ds_i18n_lookup_fn)(const char *locale,
                                         const char *domain, const char *key,
                                         ds_i18n_message_t *out, void *user);

typedef struct ds_i18n_config {
  ds_context_t *context;
  const ds_map_ops_t *map;
  const char *locale;
  const char *fallback_locale; /* NULL/empty disables locale fallback */
  bool fallback_to_key;
  unsigned max_depth;
  ds_diagnostic_sink_t *sink;
  ds_i18n_lookup_fn lookup;
  void *lookup_user;
} ds_i18n_config_t;
struct ds_i18n {
  ds_map_t catalog;
  ds_i18n_record_t *first;
  ds_context_t *context;
  char *locale;
  char *fallback_locale;
  bool fallback_to_key;
  bool emitting;
  unsigned depth;
  unsigned max_depth;
  ds_diagnostic_sink_t *sink;
  ds_i18n_lookup_fn lookup;
  void *lookup_user;
};

typedef struct ds_i18n_text {
  char *data;
  size_t length; /* excludes terminating NUL */
  ds_context_t *context;
} ds_i18n_text_t;

void ds_i18n_config_init(ds_i18n_config_t *config);
ds_status_t ds_i18n_init(ds_i18n_t *translator,
                         const ds_i18n_config_t *config);
void ds_i18n_destroy(ds_i18n_t *translator);
ds_status_t ds_i18n_set_locale(ds_i18n_t *translator, const char *locale);
/* DS_OK on insertion, DS_EXISTS on replacement; failure changes nothing. */
ds_status_t ds_i18n_set(ds_i18n_t *translator, const char *locale,
                        const char *domain, const char *key,
                        const ds_i18n_message_t *message);
ds_status_t ds_i18n_set_text(ds_i18n_t *translator, const char *locale,
                             const char *domain, const char *key,
                             const char *text);
typedef struct ds_i18n_entry {
  const char *locale;
  const char *domain;
  const char *key;
  ds_i18n_message_t message;
} ds_i18n_entry_t;
/* Apply entries in order, publishing all changes together. Existing entries
 * and options are preserved on failure. O(existing + incoming) copied records
 * plus the selected map's insertion costs. Formatter user data remains
 * borrowed and must not point into storage owned by this translator. */
ds_status_t ds_i18n_set_many(ds_i18n_t *translator,
                             const ds_i18n_entry_t *entries, size_t count);
ds_status_t ds_i18n_remove(ds_i18n_t *translator, const char *locale,
                           const char *domain, const char *key);
/* Quiet probe, including provider and locale fallback; no missing diagnostic.
 */
ds_status_t ds_i18n_has(ds_i18n_t *translator, const char *domain,
                        const char *key, bool *out);
/* Enumerates locally registered entries in reverse insertion order. The
 * callback must not modify the catalog; false stops with DS_STOP. */
ds_status_t ds_i18n_catalog_walk(
    const ds_i18n_t *translator,
    bool (*visit)(const char *locale, const char *domain, const char *key,
                  const ds_i18n_message_t *message, void *user),
    void *user);

void ds_i18n_text_init(ds_i18n_text_t *text, ds_context_t *context);
void ds_i18n_text_destroy(ds_i18n_text_t *text);
const char *ds_i18n_text_cstr(const ds_i18n_text_t *text);
ds_status_t ds_i18n_write(ds_i18n_writer_t *writer, const void *data,
                          size_t len);
ds_status_t ds_i18n_write_cstr(ds_i18n_writer_t *writer, const char *text);
/* Single-pass {name} substitution. Unknown placeholders remain literal;
 * substitutions are never reinterpreted. NULL strings become empty text.
 * Numeric formatting is portable C formatting, not a CLDR implementation;
 * application formatters supply locale-specific numeric/plural rules. */
ds_status_t ds_i18n_interpolate(ds_i18n_writer_t *writer, const char *text,
                                const ds_diagnostic_arg_t *args, size_t count);
/* out must be initialized. DS_OK publishes complete text; a failure leaves
 * out unchanged. Missing entries emit NOTICE and render fallback, else key
 * or empty text according to config. Nested rendering has a depth limit. */
ds_status_t ds_i18n_translate(ds_i18n_t *translator, const char *domain,
                              const char *key, const ds_diagnostic_arg_t *args,
                              size_t count, const char *fallback,
                              ds_i18n_text_t *out);
/* Domain "diagnostics", keyed by diagnostic ID. Missing translations use
 * [FALLBACK:LEVEL] id without emitting another diagnostic. */
ds_status_t ds_i18n_render_diagnostic(ds_i18n_t *translator,
                                      const ds_diagnostic_t *diagnostic,
                                      ds_i18n_text_t *out);
/* Domain "levels", keys DEBUG..EMERGENCY; quiet fallback to the key. */
ds_status_t ds_i18n_render_level(ds_i18n_t *translator,
                                 ds_diagnostic_level_t level,
                                 ds_i18n_text_t *out);
/* A viewer policy may hide a diagnostic or override its level without
 * changing the source event. DS_NOT_FOUND means hidden/below min_level. */
typedef bool (*ds_i18n_filter_fn)(const ds_diagnostic_t *diagnostic,
                                  ds_diagnostic_level_t *effective_level,
                                  void *user);
ds_status_t ds_i18n_render_filtered(ds_i18n_t *translator,
                                    const ds_diagnostic_t *diagnostic,
                                    ds_diagnostic_level_t min_level,
                                    ds_i18n_filter_fn filter, void *user,
                                    ds_i18n_text_t *out);

#ifdef __cplusplus
}
#endif
#endif /* DS_I18N_H */
