#ifndef DS_DIAGNOSTIC_LOG_H
#define DS_DIAGNOSTIC_LOG_H
#include <dsa/core/context.h>
#include <dsa/core/diagnostic.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_diagnostic_log_config ds_diagnostic_log_config_t;
typedef ds_status_t (*ds_diagnostic_log_visit_fn)(
    ds_diagnostic_snapshot_t *snapshot, void *user);
/* Immutable backend states own retained snapshots. NULL represents an empty
 * state. retain/release/count/at are called only for non-NULL states. append
 * accepts NULL. Successful append/concat returns an independently owned state;
 * failure must leave its output NULL and preserve inputs. Positive callback
 * statuses are invalid. concat may be NULL, or return DS_ERR_UNSUPPORTED to
 * request ordered import. Callbacks may not mutate their inputs or reenter
 * the operation. The operation table is copied, not borrowed. Backend states
 * must copy config data they retain: the config argument is temporary. Fields
 * of initialized log handles are read-only; use copy rather than assignment.
 */
typedef struct ds_diagnostic_log_ops {
  ds_status_t (*retain)(void *state);
  void (*release)(void *state);
  size_t (*count)(const void *state);
  ds_diagnostic_snapshot_t *(*at)(const void *state, size_t index);
  ds_status_t (*append)(const void *state, ds_diagnostic_snapshot_t *record,
                        const ds_diagnostic_log_config_t *config, void **out);
  ds_status_t (*concat)(const void *left, const void *right,
                        const ds_diagnostic_log_config_t *config, void **out);
  /* Optional ordered traversal; stop on the first non-DS_OK status.
   * Snapshots are borrowed during each callback. */
  ds_status_t (*walk)(const void *state, ds_diagnostic_log_visit_fn visit,
                      void *user);
} ds_diagnostic_log_ops_t;
struct ds_diagnostic_log_config {
  ds_diagnostic_log_ops_t ops;
  ds_context_t *context;
  unsigned bits; /* RRB branching 2..8; ignored by the flat backend */
  void *user;    /* borrowed custom backend policy; must outlive its logs */
};
typedef struct ds_diagnostic_log {
  void *state;
  ds_diagnostic_log_config_t config;
} ds_diagnostic_log_t;
const ds_diagnostic_log_ops_t *ds_diagnostic_log_rrb_ops(void);
const ds_diagnostic_log_ops_t *ds_diagnostic_log_flat_ops(void);
void ds_diagnostic_log_config_init(ds_diagnostic_log_config_t *config,
                                   ds_context_t *context);
/* Initialize output handles before use. Config is copied; its context/user
 * remain borrowed. NULL chooses defaults. An empty log allocates nothing. */
ds_status_t ds_diagnostic_log_init(ds_diagnostic_log_t *log,
                                   const ds_diagnostic_log_config_t *config);
void ds_diagnostic_log_destroy(ds_diagnostic_log_t *log);
ds_status_t ds_diagnostic_log_copy(const ds_diagnostic_log_t *source,
                                   ds_diagnostic_log_t *out);
size_t ds_diagnostic_log_count(const ds_diagnostic_log_t *log);
/* Walk is allocation-free for both builtins. Neither visit nor callbacks
 * invoked by it may mutate/release the source log. Positive statuses are
 * rejected. The indexed fallback is used when a backend omits walk. */
ds_status_t ds_diagnostic_log_walk(const ds_diagnostic_log_t *log,
                                   ds_diagnostic_log_visit_fn visit,
                                   void *user);
const ds_diagnostic_t *ds_diagnostic_log_at(const ds_diagnostic_log_t *log,
                                            size_t index);
ds_status_t ds_diagnostic_log_append(const ds_diagnostic_log_t *source,
                                     ds_diagnostic_snapshot_t *record,
                                     ds_diagnostic_log_t *out);
/* Left policy determines the destination. Compatible backends may concatenate
 * structurally; otherwise snapshots are imported in left-to-right order.
 * Aliased outputs are supported. Errors preserve inputs and output. */
ds_status_t ds_diagnostic_log_concat(const ds_diagnostic_log_t *left,
                                     const ds_diagnostic_log_t *right,
                                     ds_diagnostic_log_t *out);
#ifdef __cplusplus
}
#endif
#endif
