#ifndef DS_DIAGNOSTIC_H
#define DS_DIAGNOSTIC_H

#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_diagnostic_level {
  DS_DIAG_DEBUG = 0,
  DS_DIAG_INFO = 1,
  DS_DIAG_NOTICE = 2,
  DS_DIAG_WARNING = 3,
  DS_DIAG_ERROR = 4,
  DS_DIAG_CRITICAL = 5,
  DS_DIAG_ALERT = 6,
  DS_DIAG_EMERGENCY = 7
} ds_diagnostic_level_t;

typedef enum ds_diagnostic_arg_type {
  DS_ARG_CSTR,
  DS_ARG_INT,
  DS_ARG_UINT,
  DS_ARG_SIZE,
  DS_ARG_PTR,
  DS_ARG_DOUBLE,
  DS_ARG_BOOL
} ds_diagnostic_arg_type_t;

typedef struct ds_diagnostic_arg {
  const char *name;
  ds_diagnostic_arg_type_t type;
  union {
    const char *cstr;
    long i;
    unsigned long u;
    size_t size;
    const void *ptr;
    double real;
    bool boolean;
  } value;
} ds_diagnostic_arg_t;

typedef struct ds_diagnostic {
  const char *id;
  ds_diagnostic_level_t level;
  const char *module;
  const char *message;
  const char *file;
  unsigned long line;
  const char *function;
  const ds_diagnostic_arg_t *args;
  size_t arg_count;
} ds_diagnostic_t;

typedef struct ds_diagnostic_desc {
  const char *id;
  ds_diagnostic_level_t level;
  const char *module;
  const char *message;
} ds_diagnostic_desc_t;

struct ds_diagnostic_sink;
typedef struct ds_diagnostic_sink ds_diagnostic_sink_t;

struct ds_diagnostic_sink {
  void (*emit)(ds_diagnostic_sink_t *sink, const ds_diagnostic_t *diagnostic);
  void *user;
};

struct ds_context;
typedef struct ds_diagnostic_snapshot ds_diagnostic_snapshot_t;
/* Owns a deep copy of all strings and arguments. DS_ARG_PTR copies only
 * the pointer, never its pointee. Snapshots are immutable and shareable;
 * references are not atomic. The allocator/context must outlive them. */
ds_status_t ds_diagnostic_snapshot_create(ds_diagnostic_snapshot_t **out,
                                          const ds_diagnostic_t *diagnostic,
                                          struct ds_context *context);
ds_status_t ds_diagnostic_snapshot_create_alloc(
    ds_diagnostic_snapshot_t **out, const ds_diagnostic_t *diagnostic,
    void *(*allocator)(size_t), void (*deallocator)(void *));
ds_status_t ds_diagnostic_snapshot_retain(ds_diagnostic_snapshot_t *snapshot);
void ds_diagnostic_snapshot_release(ds_diagnostic_snapshot_t *snapshot);
const ds_diagnostic_t *
ds_diagnostic_snapshot_get(const ds_diagnostic_snapshot_t *snapshot);

typedef struct ds_diagnostics {
  ds_diagnostic_t *items;
  size_t size;
  size_t capacity;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
  ds_diagnostic_sink_t sink;
  ds_diagnostic_snapshot_t **snapshots;
  struct ds_context *context;
  bool use_context;
  ds_status_t status; /* first failure through the void sink; clear resets it */
} ds_diagnostics_t;

const char *ds_diagnostic_level_name(ds_diagnostic_level_t level);
void ds_diagnostic_sink_emit(ds_diagnostic_sink_t *sink,
                             const ds_diagnostic_t *diagnostic);
void ds_diagnostic_emit_desc(ds_diagnostic_sink_t *sink,
                             const ds_diagnostic_desc_t *desc,
                             const ds_diagnostic_arg_t *args, size_t arg_count,
                             const char *file, unsigned long line,
                             const char *function);
int ds_diagnostic_format(const ds_diagnostic_t *diagnostic, char *buffer,
                         size_t buffer_size);

void ds_diagnostics_init(ds_diagnostics_t *diagnostics);
void ds_diagnostics_init_context(ds_diagnostics_t *diagnostics,
                                 struct ds_context *context);
void ds_diagnostics_init_alloc(ds_diagnostics_t *diagnostics,
                               void *(*allocator)(size_t),
                               void (*deallocator)(void *));
void ds_diagnostics_destroy(ds_diagnostics_t *diagnostics);
void ds_diagnostics_clear(ds_diagnostics_t *diagnostics);
ds_diagnostic_sink_t *ds_diagnostics_sink(ds_diagnostics_t *diagnostics);
/* Checked collection. Failure leaves the sequence unchanged. Borrowed
 * input records may point into this collection: they are snapshotted first. */
ds_status_t ds_diagnostics_add(ds_diagnostics_t *diagnostics,
                               const ds_diagnostic_t *diagnostic);
ds_status_t ds_diagnostics_append(ds_diagnostics_t *diagnostics,
                                  const ds_diagnostics_t *more);
ds_status_t ds_diagnostics_filter(const ds_diagnostics_t *source,
                                  bool (*keep)(const ds_diagnostic_t *, void *),
                                  void *user, ds_diagnostics_t *out);
/* Emission does not clear the sequence. The sink sees records only for the
 * duration of emit; use a snapshot to retain one. */
void ds_diagnostics_emit(const ds_diagnostics_t *diagnostics,
                         ds_diagnostic_sink_t *sink);

#ifdef __cplusplus
}
#endif

#endif /* DS_DIAGNOSTIC_H */
