#ifndef DS_RESULT_H
#define DS_RESULT_H

#include <dsa/core/diagnostic.h>
#include <dsa/core/diagnostic_log.h>
#include <dsa/core/schema.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lightweight operational status/value pairs. */
typedef struct ds_result_ptr {
  ds_status_t status;
  void *value;
} ds_result_ptr_t;
typedef struct ds_result_size {
  ds_status_t status;
  size_t value;
} ds_result_size_t;

typedef struct ds_result_owner ds_result_owner_t;
typedef struct ds_result_value {
  void *data;
  ds_result_owner_t
      *owner; /* NULL: borrowed; otherwise shared immutable data */
} ds_result_value_t;

/* Initialize handles before first use; do not copy owning handles with '='.
 * Values may be NULL. Ownership is independent of the result's branch.
 * Contexts/schema callback state must outlive every handle sharing them. */
void ds_result_value_init(ds_result_value_t *value);
void ds_result_value_release(ds_result_value_t *value);
/* Rebinding to the exact same pointer preserves an existing owner, so it
 * cannot turn a live owned value into a dangling borrowed pointer. */
void ds_result_value_borrow(ds_result_value_t *out, void *data);
ds_status_t ds_result_value_copy(const ds_result_value_t *source,
                                 ds_result_value_t *out);
/* A subview keeps the source's owner alive. The caller supplies a pointer
 * whose lifetime is covered by that owner (or by the borrowed source). */
ds_status_t ds_result_value_view(const ds_result_value_t *source, void *data,
                                 ds_result_value_t *out);
ds_status_t ds_result_value_from_schema(ds_result_value_t *out,
                                        const void *data,
                                        const ds_schema_t *schema,
                                        ds_context_t *context);
/* Transfers data only on DS_OK; release invokes destroy exactly once,
 * including for a NULL payload. A NULL destroy callback uses borrow. */
ds_status_t ds_result_value_take(ds_result_value_t *out, void *data,
                                 void (*destroy)(void *, void *), void *user,
                                 ds_context_t *context);

typedef struct ds_result_config {
  ds_diagnostic_log_config_t diagnostics;
} ds_result_config_t;
/* Configurations and backend operation tables are copied into each Result.
 * Contexts and custom backend user data remain borrowed. NULL selects the
 * RRB log with branching 32; empty borrowed Results allocate nothing. */
void ds_result_config_init(ds_result_config_t *config, ds_context_t *context);

typedef enum ds_result_state {
  DS_RESULT_UNINITIALIZED = 0,
  DS_RESULT_OK,
  DS_RESULT_ERR
} ds_result_state_t;
typedef struct ds_result {
  ds_result_state_t state;
  ds_result_value_t payload;
  ds_diagnostic_log_t diagnostics;
} ds_result_t;

/* The C adaptation of Result<T,E> + an ordered diagnostic sequence:
 * - branch tags do not depend on NULL, status codes or severity;
 * - payloads are borrowed or explicitly owned/shared;
 * - diagnostics own snapshots through a replaceable persistent log;
 * - ds_status_t from these functions reports failure of the operation
 *   itself (allocation/callback/etc.), not the domain branch it constructs.
 * All outputs must be initialized. They may alias inputs; errors leave
 * inputs/outputs unchanged. Callback side effects are the caller's policy.
 * Handles are not internally synchronized. */
void ds_result_init(ds_result_t *result);
void ds_result_destroy(ds_result_t *result);
ds_status_t ds_result_copy(const ds_result_t *source, ds_result_t *out);
void ds_result_move(ds_result_t *source, ds_result_t *out);
ds_status_t ds_result_ok(ds_result_t *out, const ds_result_value_t *value,
                         const ds_diagnostics_t *diagnostics,
                         const ds_result_config_t *config);
ds_status_t ds_result_err(ds_result_t *out, const ds_result_value_t *error,
                          const ds_diagnostics_t *diagnostics,
                          const ds_result_config_t *config);
ds_status_t ds_result_ok_ptr(ds_result_t *out, void *value,
                             const ds_diagnostics_t *diagnostics,
                             const ds_result_config_t *config);
ds_status_t ds_result_err_ptr(ds_result_t *out, void *error,
                              const ds_diagnostics_t *diagnostics,
                              const ds_result_config_t *config);
bool ds_result_is_ok(const ds_result_t *result);
bool ds_result_is_err(const ds_result_t *result);
/* DS_ERR_STATE on the wrong branch, leaving *out unchanged. */
ds_status_t ds_result_unwrap(const ds_result_t *result, const void **out);
ds_status_t ds_result_error(const ds_result_t *result, const void **out);
const void *ds_result_value_or(const ds_result_t *result,
                               const void *fallback);
size_t ds_result_diagnostic_count(const ds_result_t *result);
const ds_diagnostic_t *ds_result_diagnostic_at(const ds_result_t *result,
                                               size_t index);
ds_status_t ds_result_with_diagnostics(const ds_result_t *source,
                                       const ds_diagnostics_t *more,
                                       ds_result_t *out);

/* Callback outputs arrive initialized and empty. Return DS_OK after filling
 * them; an operational failure is a negative status. map receives an owned
 * handle view so it can retain an owner when producing a subview. */
typedef ds_status_t (*ds_result_map_fn)(const ds_result_value_t *value,
                                        void *user, ds_result_value_t *out);
typedef ds_status_t (*ds_result_bind_fn)(const ds_result_value_t *value,
                                         void *user, ds_result_t *out);
ds_status_t ds_result_map(const ds_result_t *source, ds_result_map_fn f,
                          void *user, ds_result_t *out);
ds_status_t ds_result_map_error(const ds_result_t *source, ds_result_map_fn f,
                                void *user, ds_result_t *out);
ds_status_t ds_result_and_then(const ds_result_t *source, ds_result_bind_fn f,
                               void *user, ds_result_t *out);
ds_status_t ds_result_or_else(const ds_result_t *source, ds_result_bind_fn f,
                              void *user, ds_result_t *out);
/* Independent validation of two already-computed Results. Both logs are
 * retained in left-to-right order using the left log's policy. Two successes
 * call combine_ok; two errors call combine_error; one error retains that
 * error's payload. Only the selected callback is required. No error is
 * silently discarded. Callbacks receive initialized value outputs, as for map.
 */
typedef ds_status_t (*ds_result_combine_fn)(const ds_result_value_t *left,
                                            const ds_result_value_t *right,
                                            void *user,
                                            ds_result_value_t *out);
ds_status_t ds_result_zip(const ds_result_t *left, const ds_result_t *right,
                          ds_result_combine_fn combine_ok,
                          ds_result_combine_fn combine_error, void *user,
                          ds_result_t *out);
/* Drain emits without clearing/mutating the result. A
 * sink must snapshot a record if it keeps it beyond the callback. */
ds_status_t ds_result_drain_to(const ds_result_t *result,
                               ds_diagnostic_sink_t *sink);

/* Typed borrowed-pointer facades. Put DECLARE in a header and DEFINE once in
 * a source file, after DECLARE. Type arguments must be typedef names or simple
 * object types. These helpers check the domain branch before accessing data;
 * the generic core remains available for owned payloads and composition.
 * Only use it with payloads of the facade's declared types. */
#define DS_RESULT_DECLARE_POINTERS(name, ok_type, error_type)                 \
  typedef struct name {                                                       \
    ds_result_t core;                                                         \
  } name;                                                                     \
  void name##_init(name *result);                                             \
  void name##_destroy(name *result);                                          \
  ds_status_t name##_copy(const name *source, name *out);                     \
  ds_status_t name##_ok(name *out, ok_type *value,                            \
                        const ds_diagnostics_t *diagnostics,                  \
                        const ds_result_config_t *config);                    \
  ds_status_t name##_err(name *out, error_type *value,                        \
                         const ds_diagnostics_t *diagnostics,                 \
                         const ds_result_config_t *config);                   \
  ds_status_t name##_unwrap(const name *result, const ok_type **out);         \
  ds_status_t name##_error(const name *result, const error_type **out)

#define DS_RESULT_DEFINE_POINTERS(name, ok_type, error_type)                  \
  void name##_init(name *r) {                                                 \
    if (r != NULL)                                                            \
      ds_result_init(&r->core);                                               \
  }                                                                           \
  void name##_destroy(name *r) {                                              \
    if (r != NULL)                                                            \
      ds_result_destroy(&r->core);                                            \
  }                                                                           \
  ds_status_t name##_copy(const name *r, name *out) {                         \
    return r != NULL && out != NULL ? ds_result_copy(&r->core, &out->core)    \
                                    : DS_ERR_NULL;                            \
  }                                                                           \
  ds_status_t name##_ok(name *out, ok_type *value, const ds_diagnostics_t *d, \
                        const ds_result_config_t *c) {                        \
    return out != NULL ? ds_result_ok_ptr(&out->core, value, d, c)            \
                       : DS_ERR_NULL;                                         \
  }                                                                           \
  ds_status_t name##_err(name *out, error_type *value,                        \
                         const ds_diagnostics_t *d,                           \
                         const ds_result_config_t *c) {                       \
    return out != NULL ? ds_result_err_ptr(&out->core, value, d, c)           \
                       : DS_ERR_NULL;                                         \
  }                                                                           \
  ds_status_t name##_unwrap(const name *r, const ok_type **out) {             \
    const void *value;                                                        \
    ds_status_t st;                                                           \
    if (r == NULL || out == NULL)                                             \
      return DS_ERR_NULL;                                                     \
    st = ds_result_unwrap(&r->core, &value);                                  \
    if (st == DS_OK)                                                          \
      *out = (const ok_type *)value;                                          \
    return st;                                                                \
  }                                                                           \
  ds_status_t name##_error(const name *r, const error_type **out) {           \
    const void *value;                                                        \
    ds_status_t st;                                                           \
    if (r == NULL || out == NULL)                                             \
      return DS_ERR_NULL;                                                     \
    st = ds_result_error(&r->core, &value);                                   \
    if (st == DS_OK)                                                          \
      *out = (const error_type *)value;                                       \
    return st;                                                                \
  }

#ifdef __cplusplus
}
#endif
#endif /* DS_RESULT_H */
