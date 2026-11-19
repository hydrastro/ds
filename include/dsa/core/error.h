#ifndef DS_ERROR_H
#define DS_ERROR_H

#include <dsa/core/diagnostic.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DS_FUNCTION
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define DS_FUNCTION __func__
#else
#define DS_FUNCTION NULL
#endif
#endif

typedef enum ds_error_category {
  DS_ERROR_UNCLASSIFIED = 0,
  DS_ERROR_ALLOCATION,
  DS_ERROR_INPUT,
  DS_ERROR_IO,
  DS_ERROR_UNAVAILABLE,
  DS_ERROR_CORRUPTION,
  DS_ERROR_INTERNAL
} ds_error_category_t;
typedef struct ds_error {
  ds_status_t status;
  const char *id;
  const char *module;
  const char *message;
  const char *file;
  unsigned long line;
  const char *function;
  ds_error_category_t category;
  const char *native_domain;
  long native_code;
} ds_error_t;

typedef struct ds_error_desc {
  ds_status_t status;
  const char *id;
  const char *module;
  const char *message;
} ds_error_desc_t;

/* Borrowed views: all strings must outlive the view. Use a snapshot to retain
 * dynamic descriptions. Stable IDs are compared by content, never address. */
bool ds_error_matches(const ds_error_t *error, const char *id);
struct ds_context;
typedef struct ds_error_snapshot ds_error_snapshot_t;
/* Immutable owned error plus optional retained cause. Native domain is a
 * stable namespace such as "posix.errno"; code is meaningful within it.
 * No allocation for borrowed ds_error_t views. Snapshots copy every string;
 * contexts remain borrowed. Output unchanged on failure; *out must not already
 * own a snapshot. References are not atomic. Cause chains release iteratively.
 */
ds_status_t ds_error_snapshot_create(const ds_error_t *error,
                                     ds_error_snapshot_t *cause,
                                     struct ds_context *context,
                                     ds_error_snapshot_t **out);
ds_status_t ds_error_snapshot_retain(ds_error_snapshot_t *error);
void ds_error_snapshot_release(ds_error_snapshot_t *error);
const ds_error_t *ds_error_snapshot_get(const ds_error_snapshot_t *error);
/* Borrowed renderable view; native_domain and native_code are typed arguments.
 */
const ds_diagnostic_t *
ds_error_snapshot_diagnostic(const ds_error_snapshot_t *error);
ds_error_category_t
ds_error_snapshot_category(const ds_error_snapshot_t *error);
const char *ds_error_snapshot_native_domain(const ds_error_snapshot_t *error);
long ds_error_snapshot_native_code(const ds_error_snapshot_t *error);
/* Borrowed cause, retained explicitly when it must outlive the parent. */
ds_error_snapshot_t *ds_error_snapshot_cause(const ds_error_snapshot_t *error);

void ds_error_clear(ds_error_t *error);
ds_status_t ds_error_set(ds_error_t *error, ds_status_t status, const char *id,
                         const char *module, const char *message,
                         const char *file, unsigned long line,
                         const char *function);
ds_status_t ds_error_set_desc(ds_error_t *error, const ds_error_desc_t *desc,
                              const char *file, unsigned long line,
                              const char *function);
int ds_error_format(const ds_error_t *error, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* DS_ERROR_H */
