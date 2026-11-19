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

/* Module-owned schemas need no global registration. ID is a stable namespaced
 * string; field names are unique. Schemas are borrowed only during calls.
 * Validation matches fields by name, rejects missing/extra/duplicate fields,
 * and checks types. Message text and severity are presentation, not identity.
 */
typedef struct ds_diagnostic_field {
  const char *name;
  ds_diagnostic_arg_type_t type;
} ds_diagnostic_field_t;
typedef struct ds_diagnostic_schema {
  ds_diagnostic_desc_t descriptor;
  const ds_diagnostic_field_t *fields;
  size_t field_count;
} ds_diagnostic_schema_t;
bool ds_diagnostic_matches(const ds_diagnostic_t *record, const char *id);
ds_status_t ds_diagnostic_validate(const ds_diagnostic_t *record,
                                   const ds_diagnostic_schema_t *schema);
/* On success out borrows the record's argument. Failure preserves out. */
ds_status_t ds_diagnostic_arg_get(const ds_diagnostic_t *record,
                                  const char *name,
                                  ds_diagnostic_arg_type_t type,
                                  const ds_diagnostic_arg_t **out);
/* Construct a validated borrowed view; output unchanged on failure. */
ds_status_t ds_diagnostic_build(const ds_diagnostic_schema_t *schema,
                                const ds_diagnostic_arg_t *args, size_t count,
                                ds_diagnostic_t *out);

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

ds_status_t ds_diagnostic_snapshot_retain(ds_diagnostic_snapshot_t *snapshot);
void ds_diagnostic_snapshot_release(ds_diagnostic_snapshot_t *snapshot);
const ds_diagnostic_t *
ds_diagnostic_snapshot_get(const ds_diagnostic_snapshot_t *snapshot);

typedef enum ds_diagnostics_policy {
  DS_DIAGNOSTICS_STRICT = 0,
  DS_DIAGNOSTICS_TRUNCATE
} ds_diagnostics_policy_t;

typedef struct ds_diagnostics {
  ds_diagnostic_t *items;
  size_t size;
  size_t capacity;
  size_t limit, dropped;
  ds_diagnostics_policy_t policy;
  bool fixed_storage;

  ds_diagnostic_sink_t sink;
  ds_diagnostic_snapshot_t **snapshots;
  struct ds_context *context;

  ds_status_t
      status; /* first failure through the void sink; clear resets it */
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

/* Limit zero accepts no records; default initialization is unlimited/strict.
 * Truncation drops the incoming suffix, counts it, and returns DS_OK. Other
 * errors (including allocation failure) are never silently truncated. */
ds_status_t ds_diagnostics_init_bounded(ds_diagnostics_t *diagnostics,
                                        size_t limit,
                                        ds_diagnostics_policy_t policy,
                                        struct ds_context *context);
/* Arrays remain caller-owned, capacity is the record limit. Snapshots still
 * allocate through context: use an arena for a fully bounded memory budget.
 * Arrays must be distinct and remain alive until destruction. */
ds_status_t ds_diagnostics_init_buffer(ds_diagnostics_t *diagnostics,
                                       ds_diagnostic_t *items,
                                       ds_diagnostic_snapshot_t **snapshots,
                                       size_t capacity,
                                       ds_diagnostics_policy_t policy,
                                       struct ds_context *context);
size_t ds_diagnostics_dropped(const ds_diagnostics_t *diagnostics);
ds_status_t ds_diagnostics_add_schema(ds_diagnostics_t *diagnostics,
                                      const ds_diagnostic_schema_t *schema,
                                      const ds_diagnostic_arg_t *args,
                                      size_t count);

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
                                  bool (*keep)(const ds_diagnostic_t *,
                                               void *),
                                  void *user, ds_diagnostics_t *out);
/* Emission does not clear the sequence. The sink sees records only for the
 * duration of emit; use a snapshot to retain one. */
void ds_diagnostics_emit(const ds_diagnostics_t *diagnostics,
                         ds_diagnostic_sink_t *sink);

/* Typed module facades. FIELDS(X) lists one or more X(TYPE, field_name),
 * where TYPE is CSTR, INT, UINT, SIZE, PTR, DOUBLE or BOOL. DECLARE goes in a
 * header; DEFINE appears once in a source. Generated add snapshots its input;
 * get validates the schema and returns borrowed string/pointer fields.
 * Output is unchanged on failure. Prefix names must be unique to the module.
 */
#define DS_DIAGNOSTIC_TYPE_CSTR const char *
#define DS_DIAGNOSTIC_TYPE_INT long
#define DS_DIAGNOSTIC_TYPE_UINT unsigned long
#define DS_DIAGNOSTIC_TYPE_SIZE size_t
#define DS_DIAGNOSTIC_TYPE_PTR const void *
#define DS_DIAGNOSTIC_TYPE_DOUBLE double
#define DS_DIAGNOSTIC_TYPE_BOOL bool
#define DS_DIAGNOSTIC_VALUE_CSTR cstr
#define DS_DIAGNOSTIC_VALUE_INT i
#define DS_DIAGNOSTIC_VALUE_UINT u
#define DS_DIAGNOSTIC_VALUE_SIZE size
#define DS_DIAGNOSTIC_VALUE_PTR ptr
#define DS_DIAGNOSTIC_VALUE_DOUBLE real
#define DS_DIAGNOSTIC_VALUE_BOOL boolean
#define DS_DIAGNOSTIC_MEMBER(type, field) DS_DIAGNOSTIC_TYPE_##type field;
#define DS_DIAGNOSTIC_FIELD(type, field) {#field, DS_ARG_##type},
#define DS_DIAGNOSTIC_PACK(kind, field)                                       \
  args[index].name = #field;                                                  \
  args[index].type = DS_ARG_##kind;                                           \
  args[index++].value.DS_DIAGNOSTIC_VALUE_##kind = values->field;
#define DS_DIAGNOSTIC_UNPACK(kind, field)                                     \
  status = ds_diagnostic_arg_get(record, #field, DS_ARG_##kind, &arg);        \
  if (status != DS_OK) {                                                      \
    return status;                                                            \
  }                                                                           \
  value.field = arg->value.DS_DIAGNOSTIC_VALUE_##kind;
#define DS_DIAGNOSTIC_DECLARE(name, FIELDS)                                   \
  typedef struct name##_values {                                              \
    FIELDS(DS_DIAGNOSTIC_MEMBER)                                              \
  } name##_values;                                                            \
  extern const ds_diagnostic_schema_t name##_schema;                          \
  ds_status_t name##_add(ds_diagnostics_t *log, const name##_values *values); \
  ds_status_t name##_get(const ds_diagnostic_t *record, name##_values *out)
#define DS_DIAGNOSTIC_DEFINE(name, id_text, severity, module_text,            \
                             message_text, FIELDS)                            \
  static const ds_diagnostic_field_t name##_fields[] = {                      \
      FIELDS(DS_DIAGNOSTIC_FIELD)};                                           \
  const ds_diagnostic_schema_t name##_schema = {                              \
      {id_text, severity, module_text, message_text},                         \
      name##_fields,                                                          \
      sizeof(name##_fields) / sizeof(name##_fields[0])};                      \
  ds_status_t name##_add(ds_diagnostics_t *log,                               \
                         const name##_values *values) {                       \
    ds_diagnostic_arg_t                                                       \
        args[sizeof(name##_fields) / sizeof(name##_fields[0])];               \
    size_t index = 0;                                                         \
    if (values == NULL) {                                                     \
      return DS_ERR_NULL;                                                     \
    }                                                                         \
    FIELDS(DS_DIAGNOSTIC_PACK)                                                \
    return ds_diagnostics_add_schema(log, &name##_schema, args, index);       \
  }                                                                           \
  ds_status_t name##_get(const ds_diagnostic_t *record, name##_values *out) { \
    name##_values value;                                                      \
    const ds_diagnostic_arg_t *arg;                                           \
    ds_status_t status;                                                       \
    if (out == NULL) {                                                        \
      return DS_ERR_NULL;                                                     \
    }                                                                         \
    status = ds_diagnostic_validate(record, &name##_schema);                  \
    if (status != DS_OK) {                                                    \
      return status;                                                          \
    }                                                                         \
    FIELDS(DS_DIAGNOSTIC_UNPACK)                                              \
    *out = value;                                                             \
    return DS_OK;                                                             \
  }

#ifdef __cplusplus
}
#endif

#endif /* DS_DIAGNOSTIC_H */
