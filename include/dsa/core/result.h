#ifndef DS_RESULT_H
#define DS_RESULT_H

#include <dsa/core/diagnostic.h>
#include <dsa/core/schema.h>
#include <dsa/core/status.h>
#include <dsa/structures/rrb_vector.h>
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
  ds_rrb_config_t diagnostics;
} ds_result_config_t;
/* RRB branching defaults to 32; callers may change diagnostics.bits (2..8).
 * The config is immutable while used and must outlive its result versions.
 * Other RRB fields are managed here. NULL config uses a static default. */
void ds_result_config_init(ds_result_config_t *config, ds_context_t *context);

typedef enum ds_result_state {
  DS_RESULT_UNINITIALIZED = 0,
  DS_RESULT_OK,
  DS_RESULT_ERR
} ds_result_state_t;
typedef struct ds_result {
  ds_result_state_t state;
  ds_result_value_t payload;
  ds_rrb_t diagnostics;
} ds_result_t;

/* The C adaptation of Result<T,E> + an ordered diagnostic sequence:
 * - branch tags do not depend on NULL, status codes or severity;
 * - payloads are borrowed or explicitly owned/shared;
 * - diagnostics own snapshots, and versions share an RRB vector;
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
const void *ds_result_value_or(const ds_result_t *result, const void *fallback);
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
/* Drain emits without clearing/mutating the result. A
 * sink must snapshot a record if it keeps it beyond the callback. */
void ds_result_drain_to(const ds_result_t *result, ds_diagnostic_sink_t *sink);

#ifdef __cplusplus
}
#endif
#endif /* DS_RESULT_H */
