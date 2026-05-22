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

ds_history_operation_t *FUNC(ds_history_branch_find_op)(
    ds_history_branch_t *branch, unsigned long op_id);

unsigned long FUNC(ds_history_branch_insert_at)(ds_history_branch_t *branch,
                                                unsigned long time, int kind,
                                                void *payload);
unsigned long FUNC(ds_history_branch_append)(ds_history_branch_t *branch,
                                             int kind, void *payload);
bool FUNC(ds_history_branch_delete_op)(ds_history_branch_t *branch,
                                       unsigned long op_id);

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

#endif /* DS_HISTORY_H */
