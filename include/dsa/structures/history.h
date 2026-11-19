#ifndef DS_HISTORY_H
#define DS_HISTORY_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ds_history;
struct ds_history_branch;
struct ds_history_operation;
struct ds_history_query;
struct ds_history_ops;

typedef struct ds_history ds_history_t;
typedef struct ds_history_branch ds_history_branch_t;
typedef struct ds_history_operation ds_history_operation_t;
typedef struct ds_history_query ds_history_query_t;
typedef struct ds_history_ops ds_history_ops_t;

typedef void *(*ds_history_create_state_func_t)(void *config);
typedef void *(*ds_history_clone_state_func_t)(void *state, void *clone_data);
typedef void (*ds_history_destroy_state_func_t)(void *state);
typedef int (*ds_history_apply_func_t)(void *state,
                                       const ds_history_operation_t *op);
typedef void *(*ds_history_query_func_t)(void *state,
                                         const ds_history_query_t *query);
typedef void *(*ds_history_clone_payload_func_t)(int kind, void *payload,
                                                 void *clone_data);
typedef void (*ds_history_destroy_payload_func_t)(int kind, void *payload);
typedef bool (*ds_history_export_op_func_t)(
    const ds_history_operation_t *operation, void *user);
typedef bool (*ds_history_write_func_t)(const void *data, size_t size,
                                        void *user);
typedef bool (*ds_history_read_func_t)(void *data, size_t size, void *user);
typedef bool (*ds_history_payload_write_func_t)(
    const ds_history_operation_t *operation, ds_history_write_func_t write,
    void *io_user, void *payload_user);
typedef void *(*ds_history_payload_read_func_t)(unsigned long id,
                                                unsigned long time, int kind,
                                                ds_history_read_func_t read,
                                                void *io_user,
                                                void *payload_user);
/* Checked reader: DS_OK can publish a NULL payload. Failure must release any
 * partial payload; positive statuses other than DS_OK are callback errors. */
typedef ds_status_t (*ds_history_payload_read_ex_func_t)(
    unsigned long id, unsigned long time, int kind,
    ds_history_read_func_t read, void *io_user, void *payload_user,
    void **out);
typedef unsigned int (*ds_history_merge_policy_func_t)(
    ds_history_branch_t *target, ds_history_branch_t *source,
    const ds_history_operation_t *source_operation, unsigned long *in_out_time,
    int *in_out_kind, void **in_out_payload, void *user);

typedef struct ds_history_archive {
  unsigned char *data;
  size_t size;
  size_t capacity;
  size_t offset;

  ds_context_t *context;

  ds_status_t status; /* sticky until clear/rewind */
} ds_history_archive_t;

#define DS_HISTORY_MERGE_PRESERVE_TIME 0U
#define DS_HISTORY_MERGE_APPEND_AFTER_HEAD 1U
#define DS_HISTORY_MERGE_TAKE 0U
#define DS_HISTORY_MERGE_SKIP 1U
#define DS_HISTORY_MERGE_STOP 2U

struct ds_history_operation {
  unsigned long id;
  unsigned long time;
  int kind;
  void *payload;
};

struct ds_history_query {
  int kind;
  void *payload;
};

struct ds_history_ops {
  ds_history_create_state_func_t create_state;
  ds_history_clone_state_func_t clone_state;
  ds_history_destroy_state_func_t destroy_state;
  ds_history_apply_func_t apply;
  ds_history_query_func_t query;
  ds_history_clone_payload_func_t clone_payload;
  ds_history_destroy_payload_func_t destroy_payload;
};

ds_history_t *ds_history_create(const ds_history_ops_t *ops, void *config,
                                ds_context_t *context);

/* Context-backed storage, including branch paths, names and operation nodes.
 * State/payload storage remains the responsibility of the supplied callbacks.
 */
ds_status_t ds_history_create_context(const ds_history_ops_t *ops,
                                      void *config, ds_context_t *context,
                                      ds_history_t **out);
void ds_history_destroy(ds_history_t *history);

void ds_history_set_clone_data(ds_history_t *history, void *clone_data);
void ds_history_set_checkpoint_interval(ds_history_t *history,
                                        size_t interval);
void ds_history_transaction_begin(ds_history_t *history);
ds_status_t ds_history_transaction_begin_ex(ds_history_t *history);
void ds_history_transaction_commit(ds_history_t *history);
bool ds_history_in_transaction(ds_history_t *history);

const ds_history_ops_t *ds_history_get_ops(ds_history_t *history);
void *ds_history_get_config(ds_history_t *history);

ds_history_branch_t *ds_history_main(ds_history_t *history);
ds_history_branch_t *ds_history_branch_create(ds_history_t *history,
                                              ds_history_branch_t *parent,
                                              unsigned long fork_time,
                                              const char *name);
ds_status_t ds_history_branch_create_ex(ds_history_t *history,
                                        ds_history_branch_t *parent,
                                        unsigned long fork_time,
                                        const char *name,
                                        ds_history_branch_t **out);

const char *ds_history_branch_name(ds_history_branch_t *branch);
unsigned long ds_history_branch_id(ds_history_branch_t *branch);
unsigned long ds_history_branch_fork_time(ds_history_branch_t *branch);
unsigned long ds_history_branch_head_time(ds_history_branch_t *branch);
ds_history_branch_t *ds_history_branch_parent(ds_history_branch_t *branch);
ds_history_branch_t *ds_history_branch_find_name(ds_history_t *history,
                                                 const char *name);

ds_history_operation_t *ds_history_branch_find_op(ds_history_branch_t *branch,
                                                  unsigned long op_id);

unsigned long ds_history_branch_insert_at(ds_history_branch_t *branch,
                                          unsigned long time, int kind,
                                          void *payload);
unsigned long ds_history_branch_append(ds_history_branch_t *branch, int kind,
                                       void *payload);
/* Payload ownership transfers only on success. Identifiers and appended times
 * never wrap. The original functions return zero on these failures. */
ds_status_t ds_history_branch_insert_at_ex(ds_history_branch_t *branch,
                                           unsigned long time, int kind,
                                           void *payload,
                                           unsigned long *out_id);
ds_status_t ds_history_branch_append_ex(ds_history_branch_t *branch, int kind,
                                        void *payload, unsigned long *out_id);
bool ds_history_branch_delete_op(ds_history_branch_t *branch,
                                 unsigned long op_id);
size_t ds_history_branch_merge(ds_history_branch_t *target,
                               ds_history_branch_t *source,
                               unsigned long from_time,
                               unsigned long through_time, unsigned int flags);
size_t ds_history_branch_merge_with(
    ds_history_branch_t *target, ds_history_branch_t *source,
    unsigned long from_time, unsigned long through_time, unsigned int flags,
    ds_history_merge_policy_func_t policy, void *policy_user);
/* Checked streaming merge: *out_merged reports committed operations even on
 * failure or DS_STOP. It is not a transaction. Policy may replace a cloned
 * payload; it then owns disposal of the displaced clone. Skipped/stopped or
 * failed-insertion payloads are destroyed by the configured callback. */
ds_status_t ds_history_branch_merge_with_ex(
    ds_history_branch_t *target, ds_history_branch_t *source,
    unsigned long from_time, unsigned long through_time, unsigned int flags,
    ds_history_merge_policy_func_t policy, void *policy_user,
    size_t *out_merged);

void *ds_history_branch_snapshot_at(ds_history_branch_t *branch,
                                    unsigned long time);
void *ds_history_branch_snapshot_head(ds_history_branch_t *branch);
void ds_history_snapshot_destroy(ds_history_t *history, void *snapshot);
void *ds_history_branch_query_at(ds_history_branch_t *branch,
                                 unsigned long time,
                                 const ds_history_query_t *query);
void *ds_history_branch_query_head(ds_history_branch_t *branch,
                                   const ds_history_query_t *query);
size_t ds_history_branch_export_ops(ds_history_branch_t *branch,
                                    ds_history_export_op_func_t writer,
                                    void *user);
size_t
ds_history_branch_serialize_ops(ds_history_branch_t *branch,
                                ds_history_write_func_t write,
                                ds_history_payload_write_func_t write_payload,
                                void *io_user, void *payload_user);
size_t
ds_history_branch_deserialize_ops(ds_history_branch_t *branch,
                                  ds_history_read_func_t read,
                                  ds_history_payload_read_func_t read_payload,
                                  void *io_user, void *payload_user);
bool ds_history_write_ulong(ds_history_write_func_t write, void *io_user,
                            unsigned long value);
bool ds_history_read_ulong(ds_history_read_func_t read, void *io_user,
                           unsigned long *out_value);
bool ds_history_write_int(ds_history_write_func_t write, void *io_user,
                          int value);
bool ds_history_read_int(ds_history_read_func_t read, void *io_user,
                         int *out_value);
bool ds_history_write_size(ds_history_write_func_t write, void *io_user,
                           size_t value);
bool ds_history_read_size(ds_history_read_func_t read, void *io_user,
                          size_t *out_value);
bool ds_history_serialize(ds_history_t *history, ds_history_write_func_t write,
                          ds_history_payload_write_func_t write_payload,
                          void *io_user, void *payload_user);
ds_history_t *ds_history_deserialize(
    const ds_history_ops_t *ops, void *config, ds_history_read_func_t read,
    ds_history_payload_read_func_t read_payload, void *io_user,
    void *payload_user, ds_context_t *context);
void ds_history_archive_init(ds_history_archive_t *archive,
                             ds_context_t *context);

ds_status_t ds_history_archive_init_ex(ds_history_archive_t *archive,
                                       ds_context_t *context);
ds_status_t ds_history_archive_init_context(ds_history_archive_t *archive,
                                            ds_context_t *context);
ds_status_t ds_history_archive_status(const ds_history_archive_t *archive);
void ds_history_archive_destroy(ds_history_archive_t *archive);
void ds_history_archive_clear(ds_history_archive_t *archive);
void ds_history_archive_rewind(ds_history_archive_t *archive);
const unsigned char *
ds_history_archive_data(const ds_history_archive_t *archive);
size_t ds_history_archive_size(const ds_history_archive_t *archive);
bool ds_history_archive_write(const void *data, size_t size, void *user);
bool ds_history_archive_read(void *data, size_t size, void *user);

bool ds_history_serialize_portable(
    ds_history_t *history, ds_history_write_func_t write,
    ds_history_payload_write_func_t write_payload, void *io_user,
    void *payload_user);
ds_history_t *ds_history_deserialize_portable(
    const ds_history_ops_t *ops, void *config, ds_history_read_func_t read,
    ds_history_payload_read_func_t read_payload, void *io_user,
    void *payload_user, ds_context_t *context);

struct ds_ordered_ops;

typedef struct ds_history_load_options {
  ds_context_t *context;
  size_t max_branches;   /* zero: no explicit limit */
  size_t max_operations; /* across all branches */
  size_t max_name_bytes; /* per branch, excluding its terminating NUL */
  const struct ds_ordered_ops *index_ops; /* NULL: (a,b)-tree ID indexes */
} ds_history_load_options_t;
void ds_history_load_options_init(ds_history_load_options_t *options);
/* All-or-nothing import of the portable format, with checked payload decoding,
 * unique identifiers, parent validation and resource limits. Input callbacks
 * own their I/O status (the FILE adapter retains it). Failed boolean reads
 * report DS_ERR_INVALID; checked payload errors retain their negative status.
 * On failure *out is NULL. The direct loader treats a NULL payload as failure.
 */
ds_status_t ds_history_deserialize_portable_ex(
    const ds_history_ops_t *ops, void *config, ds_history_read_func_t read,
    ds_history_payload_read_ex_func_t read_payload, void *io_user,
    void *payload_user, const ds_history_load_options_t *options,
    ds_history_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_HISTORY_H */
