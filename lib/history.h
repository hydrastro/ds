#ifndef DS_HISTORY_H
#define DS_HISTORY_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

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
typedef void *(*ds_history_clone_state_func_t)(void *state,
                                               void *clone_data);
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
typedef void *(*ds_history_payload_read_func_t)(
    unsigned long id, unsigned long time, int kind, ds_history_read_func_t read,
    void *io_user, void *payload_user);

#define DS_HISTORY_MERGE_PRESERVE_TIME 0U
#define DS_HISTORY_MERGE_APPEND_AFTER_HEAD 1U

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

ds_history_t *FUNC(ds_history_create)(const ds_history_ops_t *ops,
                                      void *config);
ds_history_t *FUNC(ds_history_create_alloc)(const ds_history_ops_t *ops,
                                            void *config,
                                            void *(*allocator)(size_t),
                                            void (*deallocator)(void *));
void FUNC(ds_history_destroy)(ds_history_t *history);

void FUNC(ds_history_set_clone_data)(ds_history_t *history, void *clone_data);
void FUNC(ds_history_set_checkpoint_interval)(ds_history_t *history,
                                              size_t interval);
void FUNC(ds_history_transaction_begin)(ds_history_t *history);
void FUNC(ds_history_transaction_commit)(ds_history_t *history);
bool FUNC(ds_history_in_transaction)(ds_history_t *history);

const ds_history_ops_t *FUNC(ds_history_get_ops)(ds_history_t *history);
void *FUNC(ds_history_get_config)(ds_history_t *history);

ds_history_branch_t *FUNC(ds_history_main)(ds_history_t *history);
ds_history_branch_t *FUNC(ds_history_branch_create)(ds_history_t *history,
                                                    ds_history_branch_t *parent,
                                                    unsigned long fork_time,
                                                    const char *name);

const char *FUNC(ds_history_branch_name)(ds_history_branch_t *branch);
unsigned long FUNC(ds_history_branch_id)(ds_history_branch_t *branch);
unsigned long FUNC(ds_history_branch_fork_time)(ds_history_branch_t *branch);
unsigned long FUNC(ds_history_branch_head_time)(ds_history_branch_t *branch);
ds_history_branch_t *FUNC(ds_history_branch_parent)(ds_history_branch_t *branch);
ds_history_branch_t *FUNC(ds_history_branch_find_name)(ds_history_t *history,
                                                        const char *name);

ds_history_operation_t *FUNC(ds_history_branch_find_op)(
    ds_history_branch_t *branch, unsigned long op_id);

unsigned long FUNC(ds_history_branch_insert_at)(ds_history_branch_t *branch,
                                                unsigned long time, int kind,
                                                void *payload);
unsigned long FUNC(ds_history_branch_append)(ds_history_branch_t *branch,
                                             int kind, void *payload);
bool FUNC(ds_history_branch_delete_op)(ds_history_branch_t *branch,
                                       unsigned long op_id);
size_t FUNC(ds_history_branch_merge)(ds_history_branch_t *target,
                                      ds_history_branch_t *source,
                                      unsigned long from_time,
                                      unsigned long through_time,
                                      unsigned int flags);

void *FUNC(ds_history_branch_snapshot_at)(ds_history_branch_t *branch,
                                          unsigned long time);
void *FUNC(ds_history_branch_snapshot_head)(ds_history_branch_t *branch);
void FUNC(ds_history_snapshot_destroy)(ds_history_t *history, void *snapshot);
void *FUNC(ds_history_branch_query_at)(ds_history_branch_t *branch,
                                       unsigned long time,
                                       const ds_history_query_t *query);
void *FUNC(ds_history_branch_query_head)(ds_history_branch_t *branch,
                                         const ds_history_query_t *query);
size_t FUNC(ds_history_branch_export_ops)(ds_history_branch_t *branch,
                                           ds_history_export_op_func_t writer,
                                           void *user);
size_t FUNC(ds_history_branch_serialize_ops)(
    ds_history_branch_t *branch, ds_history_write_func_t write,
    ds_history_payload_write_func_t write_payload, void *io_user,
    void *payload_user);
size_t FUNC(ds_history_branch_deserialize_ops)(
    ds_history_branch_t *branch, ds_history_read_func_t read,
    ds_history_payload_read_func_t read_payload, void *io_user,
    void *payload_user);
bool FUNC(ds_history_write_ulong)(ds_history_write_func_t write, void *io_user,
                                   unsigned long value);
bool FUNC(ds_history_read_ulong)(ds_history_read_func_t read, void *io_user,
                                  unsigned long *out_value);
bool FUNC(ds_history_write_int)(ds_history_write_func_t write, void *io_user,
                                 int value);
bool FUNC(ds_history_read_int)(ds_history_read_func_t read, void *io_user,
                                int *out_value);
bool FUNC(ds_history_write_size)(ds_history_write_func_t write, void *io_user,
                                  size_t value);
bool FUNC(ds_history_read_size)(ds_history_read_func_t read, void *io_user,
                                 size_t *out_value);
bool FUNC(ds_history_serialize)(ds_history_t *history,
                                 ds_history_write_func_t write,
                                 ds_history_payload_write_func_t write_payload,
                                 void *io_user, void *payload_user);
ds_history_t *FUNC(ds_history_deserialize)(
    const ds_history_ops_t *ops, void *config, ds_history_read_func_t read,
    ds_history_payload_read_func_t read_payload, void *io_user,
    void *payload_user);
bool FUNC(ds_history_serialize_portable)(
    ds_history_t *history, ds_history_write_func_t write,
    ds_history_payload_write_func_t write_payload, void *io_user,
    void *payload_user);
ds_history_t *FUNC(ds_history_deserialize_portable)(
    const ds_history_ops_t *ops, void *config, ds_history_read_func_t read,
    ds_history_payload_read_func_t read_payload, void *io_user,
    void *payload_user);

#endif /* DS_HISTORY_H */
