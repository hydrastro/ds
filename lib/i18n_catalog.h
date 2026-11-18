#ifndef DS_I18N_CATALOG_H
#define DS_I18N_CATALOG_H

#include "i18n.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_i18n_catalog_format {
  DS_I18N_CATALOG_TSV = 0,
  DS_I18N_CATALOG_JSON = 1
} ds_i18n_catalog_format_t;

typedef struct ds_i18n_catalog_limits {
  size_t max_bytes;
  size_t max_entries;
  size_t max_field_bytes;
} ds_i18n_catalog_limits_t;
/* Defaults: 16 MiB input, 100000 entries, 1 MiB per decoded field.
 * A zero limit disables that bound. Counts include duplicate entries. */
void ds_i18n_catalog_limits_init(ds_i18n_catalog_limits_t *limits);

/* Reader returns DS_OK with 0 bytes at EOF, otherwise up to capacity bytes.
 * Writer accepts the entire span or returns a negative status. Other positive
 * statuses are callback errors. Adapters never close the supplied stream. */
typedef ds_status_t (*ds_i18n_catalog_read_fn)(void *user, void *buffer,
                                               size_t capacity, size_t *count);
typedef ds_status_t (*ds_i18n_catalog_write_fn)(void *user, const void *buffer,
                                                size_t count);
ds_status_t ds_i18n_catalog_file_read(void *stream, void *buffer,
                                      size_t capacity, size_t *count);
ds_status_t ds_i18n_catalog_file_write(void *stream, const void *buffer,
                                       size_t count);

/* Data only. TSV starts with "#!ds-i18n-tsv 1\n" and has four tab-separated
 * fields: locale, domain, key, text. Escapes: \\, \t, \n, \r, \#.
 * Blank lines and lines starting with # are ignored; CRLF is accepted.
 * JSON is one flat object of string keys and string values, for the supplied
 * locale/domain. JSON escapes and paired UTF-16 surrogate escapes are decoded.
 * Both formats require valid UTF-8 and reject embedded NUL. No BOM is accepted.
 * Later duplicate entries replace earlier ones. Loads are atomic: parsing or
 * allocation failures leave the translator unchanged, with *out_count = 0.
 * A load stages its input and a catalog copy; it is not a streaming database.
 * locale/domain are ignored for TSV and required for JSON. out_count may be
 * NULL.
 */
ds_status_t ds_i18n_catalog_load_memory(ds_i18n_t *translator,
                                        ds_i18n_catalog_format_t format,
                                        const char *locale, const char *domain,
                                        const void *data, size_t size,
                                        const ds_i18n_catalog_limits_t *limits,
                                        size_t *out_count);
ds_status_t ds_i18n_catalog_load(ds_i18n_t *translator,
                                 ds_i18n_catalog_format_t format,
                                 const char *locale, const char *domain,
                                 ds_i18n_catalog_read_fn read, void *user,
                                 const ds_i18n_catalog_limits_t *limits,
                                 size_t *out_count);

/* TSV saves all local entries; JSON saves only the supplied locale/domain.
 * Providers are not enumerated. A formatter entry in the selected catalog
 * yields DS_ERR_UNSUPPORTED before writing any bytes. I/O failure can leave a
 * partial output; callers needing atomic files should write a temporary file.
 * Strings and iteration order follow the local catalog; output is UTF-8.
 * The writer must not mutate/destroy the translator. No implicit fflush/fsync.
 */
ds_status_t ds_i18n_catalog_save(const ds_i18n_t *translator,
                                 ds_i18n_catalog_format_t format,
                                 const char *locale, const char *domain,
                                 ds_i18n_catalog_write_fn write, void *user);

#ifdef __cplusplus
}
#endif
#endif
