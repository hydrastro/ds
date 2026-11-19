#ifndef DS_HISTORY_ADAPTERS_H
#define DS_HISTORY_ADAPTERS_H

#include <dsa/structures/history.h>
#include <dsa/structures/map_iface.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Checked FILE adapter for the history serializer callbacks. limit is a total
 * transfer budget, zero for unlimited. An error is sticky until rewind
 * succeeds or the adapter is initialized again. The stream position belongs to
 * this adapter while in use; externally synchronize it. flush is C fflush and
 * is valid after output (not on a read-only stream). This is not fsync
 * durability.
 */
typedef struct ds_history_file {
  FILE *stream;
  bool owns_stream;
  size_t transferred;
  size_t limit;
  ds_status_t status;
} ds_history_file_t;

ds_status_t ds_history_file_init(ds_history_file_t *archive, FILE *stream,
                                 bool take_ownership, size_t limit);
/* Modes: rb, wb, ab, r+b, w+b, a+b. Write modes have normal fopen semantics.
 */
ds_status_t ds_history_file_open(ds_history_file_t *archive, const char *path,
                                 const char *mode, size_t limit);
bool ds_history_file_write(const void *data, size_t size, void *archive);
bool ds_history_file_read(void *data, size_t size, void *archive);
ds_status_t ds_history_file_flush(ds_history_file_t *archive);
ds_status_t ds_history_file_rewind(ds_history_file_t *archive);
/* An owned FILE is closed even if fclose fails. A borrowed FILE stays open. */
ds_status_t ds_history_file_close(ds_history_file_t *archive);
ds_status_t ds_history_file_status(const ds_history_file_t *archive);
ds_status_t
ds_history_serialize_file(ds_history_t *history, ds_history_file_t *archive,
                          ds_history_payload_write_func_t write_payload,
                          void *payload_user);

ds_status_t ds_history_deserialize_file(
    const ds_history_ops_t *ops, void *config, ds_history_file_t *archive,
    ds_history_payload_read_ex_func_t read_payload, void *payload_user,
    const ds_history_load_options_t *options, ds_history_t **out);

typedef enum ds_history_conflict {
  DS_HISTORY_KEEP_TARGET = 0,
  DS_HISTORY_TAKE_SOURCE = 1,
  DS_HISTORY_LATEST_OPERATION = 2, /* lexicographic (original time, id) */
  DS_HISTORY_REJECT_CONFLICT = 3
} ds_history_conflict_t;

/* Project a fixed-width semantic key (map/set key, record id, etc.).
 * DS_NOT_FOUND skips an operation; errors stop the merge. The key is copied.
 * A bytewise key must be fully initialized, including any padding. */
typedef ds_status_t (*ds_history_project_key_fn)(
    const ds_history_operation_t *operation, void *key, size_t key_size,
    void *user);
/* Optional resolver on conflict; action is TAKE, SKIP or STOP. The two
 * operations are borrowed, and must not be changed. */
typedef ds_status_t (*ds_history_resolve_key_fn)(
    const ds_history_operation_t *existing,
    const ds_history_operation_t *incoming, unsigned int *action, void *user);

typedef struct ds_history_keyed_merge {
  size_t key_size;
  ds_history_project_key_fn project;
  ds_history_resolve_key_fn resolve; /* NULL: conflict policy below */
  ds_history_conflict_t conflict;
  ds_map_hash_fn
      hash; /* NULL: byte hash; custom equality needs a matching hash */
  ds_map_equal_fn equal; /* NULL: memcmp over key_size */
  void *user;
  const ds_map_ops_t *map; /* NULL: Robin Hood; accepts any map capability */
  ds_context_t *context;
  unsigned long target_from_time;
  unsigned long target_through_time;
  bool include_ancestors; /* clip each parent's time window at its fork */
} ds_history_keyed_merge_t;

void ds_history_keyed_merge_init(ds_history_keyed_merge_t *config);
/* Merge the source branch's local operations in the given range. Index the
 * selected target history by semantic key, then resolve conflicting changes
 * using that index, including accepted source operations. This is an operation
 * merge, not an automatic three-way snapshot diff or sequence-index rebasing.
 * Operations on the same map/set key conflict even when their kinds differ.
 * Partial progress is reported on failure as with merge_with_ex. */
ds_status_t ds_history_branch_merge_keyed(
    ds_history_branch_t *target, ds_history_branch_t *source,
    unsigned long from_time, unsigned long through_time, unsigned int flags,
    const ds_history_keyed_merge_t *config, size_t *out_merged);

/* A convenient payload view for map/set operations. The history's own payload
 * callbacks determine whether these pointers are borrowed or deep-cloned. */
typedef struct ds_history_keyed_record {
  const void *key;
  size_t key_size;
  const void *value;
} ds_history_keyed_record_t;
ds_status_t ds_history_record_key(const ds_history_operation_t *operation,
                                  void *key, size_t key_size, void *user);

#ifdef __cplusplus
}
#endif
#endif
