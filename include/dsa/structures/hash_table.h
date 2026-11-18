#ifndef DS_HASH_TABLE_H
#define DS_HASH_TABLE_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/iter.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HASH_TABLE_RESIZE_FACTOR
#define HASH_TABLE_RESIZE_FACTOR 0.75
#endif

typedef enum {
  HASH_CHAINING,
  HASH_LINEAR_PROBING,
  HASH_QUADRATIC_PROBING,
  HASH_CUSTOM_PROBING
} ds_hash_table_mode_t;

struct hash_node;
struct hash_table;
struct hash_bucket_store;
struct ds_hash_table_config;

typedef struct hash_node ds_hash_node_t;
typedef struct hash_table ds_hash_table_t;
typedef struct hash_bucket_store ds_hash_bucket_store_t;
typedef struct ds_hash_table_config ds_hash_table_config_t;

typedef size_t (*ds_hash_probing_func_t)(size_t base_index, size_t iteration,
                                         size_t capacity);
typedef void *(*ds_hash_bucket_create_func_t)(ds_hash_table_t *table);
typedef ds_hash_node_t *(*ds_hash_bucket_search_func_t)(ds_hash_table_t *table,
                                                        void *bucket, void *key,
                                                        int (*compare)(void *,
                                                                       void *));
typedef void (*ds_hash_bucket_insert_func_t)(ds_hash_table_t *table,
                                             void **bucket,
                                             ds_hash_node_t *node,
                                             int (*compare)(void *, void *));
typedef ds_hash_node_t *(*ds_hash_bucket_remove_func_t)(
    ds_hash_table_t *table, void **bucket, void *key,
    int (*compare)(void *, void *));
typedef void (*ds_hash_bucket_destroy_func_t)(
    ds_hash_table_t *table, void *bucket, void (*destroy)(ds_hash_node_t *));
typedef void (*ds_hash_bucket_apply_func_t)(
    ds_hash_table_t *table, void *bucket,
    void (*callback)(ds_hash_node_t *, void *), void *args);

typedef size_t (*ds_hash_func_t)(void *key, void *user);
typedef int (*ds_hash_compare_func_t)(void *a, void *b, void *user);
typedef void *(*ds_hash_clone_func_t)(void *ptr, void *user);
typedef ds_status_t (*ds_hash_clone_checked_fn)(void *ptr, void **out,
                                                void *user);
typedef void (*ds_hash_destroy_func_t)(void *ptr, void *user);

struct hash_node {
  void *key;
  void *value;
  struct hash_node *next;
  struct hash_node *list_next;
  struct hash_node *list_prev;
};

struct ds_hash_table_config {
  size_t capacity;
  ds_hash_table_mode_t mode;
  ds_hash_probing_func_t probing_func;
  const ds_hash_bucket_store_t *bucket_store;
  ds_hash_func_t hash;
  ds_hash_compare_func_t compare;
  ds_hash_clone_func_t clone_key;
  ds_hash_clone_func_t clone_value;
  ds_hash_destroy_func_t destroy_key;
  ds_hash_destroy_func_t destroy_value;
  void *user;
  ds_context_t *context;
  ds_hash_clone_checked_fn clone_key_ex;
  ds_hash_clone_checked_fn clone_value_ex;
};

struct hash_bucket_store {
  ds_hash_bucket_create_func_t create;
  ds_hash_bucket_search_func_t search;
  ds_hash_bucket_insert_func_t insert;
  ds_hash_bucket_remove_func_t remove;
  ds_hash_bucket_destroy_func_t destroy;
  ds_hash_bucket_apply_func_t apply;
};

struct hash_table {
  union {
    void **buckets;
    ds_hash_node_t *entries;
  } store;
  size_t size;
  size_t capacity;
  void *nil;
  void *tombstone;
  ds_hash_table_mode_t mode;
  ds_hash_probing_func_t probing_func;
  const ds_hash_bucket_store_t *bucket_store;
  ds_hash_node_t *last_node;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
  ds_context_t *context;
  ds_hash_func_t config_hash;
  ds_hash_compare_func_t config_compare;
  ds_hash_clone_func_t config_clone_key;
  ds_hash_clone_func_t config_clone_value;
  ds_hash_destroy_func_t config_destroy_key;
  ds_hash_destroy_func_t config_destroy_value;
  void *config_user;
  size_t deleted;   /* tombstones in probing mode */
  bool use_context; /* storage comes from context, not allocator */
  ds_hash_clone_checked_fn config_clone_key_ex;
  ds_hash_clone_checked_fn config_clone_value_ex;
};

/*
 * Probing invariants: every probe sequence is used for at most `capacity`
 * steps (indices reduced modulo capacity), then continued as a linear
 * sweep, so lookups and insertions terminate and find a free slot for any
 * probing function, including custom ones that do not reach every slot.
 * Tombstones count towards the load: the table is rebuilt (grown, or
 * purged at the same capacity when mostly dead) once live plus deleted
 * entries exceed HASH_TABLE_RESIZE_FACTOR of the capacity.
 *
 * Ownership: with destroy callbacks the table owns its keys/values.
 * ds_hash_table_clone needs the matching clone callback for every owned
 * part (it fails with DS_ERR_UNSUPPORTED recorded on the context
 * otherwise); ds_hash_table_clone_borrowed makes a clone that owns
 * nothing and must not outlive the source's entries. Replacing a value
 * with the very same pointer, or re-inserting the stored key pointer with
 * insert_take_key, destroys nothing.
 *
 * Memory: a configured table with a context allocates everything (the
 * table, its buckets/entries and nodes) through that context; custom
 * bucket stores should allocate with ds_hash_table_alloc/free.
 *
 * Order: the nodes form a list in insertion order, the newest at
 * last_node (list_prev steps to older ones); for_each and the iterator
 * run newest first. Resizing, ds_hash_table_clone and
 * ds_hash_table_clone_with keep that order, so a walk along the list that
 * resumes from a key (ds_hash_table_lookup_node) survives growth. A resize
 * that runs out of memory leaves the table as it was.
 */
void *ds_hash_table_alloc(ds_hash_table_t *table, size_t size);
void ds_hash_table_free(ds_hash_table_t *table, void *ptr);
ds_hash_table_t *ds_hash_table_clone_borrowed(ds_hash_table_t *table);
void ds_hash_table_config_init(ds_hash_table_config_t *config);
ds_hash_table_t *
ds_hash_table_create_config(const ds_hash_table_config_t *config);
ds_status_t ds_hash_table_insert(ds_hash_table_t *table, void *key,
                                 void *value);
ds_status_t ds_hash_table_insert_take_key(ds_hash_table_t *table, void *key,
                                          void *value);
ds_status_t ds_hash_table_get(ds_hash_table_t *table, void *key,
                              void **out_value);
ds_status_t ds_hash_table_remove(ds_hash_table_t *table, void *key);
ds_status_t ds_hash_table_contains(ds_hash_table_t *table, void *key);
void ds_hash_table_destroy(ds_hash_table_t *table);
ds_hash_table_t *ds_hash_table_clone(ds_hash_table_t *table);
/* Borrowed newest-first cursor. No table mutation during iteration: resizing
 * may replace node storage, and removing a pending node invalidates state.
 * Keep the table alive and synchronize the whole iteration externally. */
ds_status_t ds_hash_table_iter_init(ds_hash_table_t *table, ds_iter_t *iter);

ds_hash_node_t *ds_hash_node_create(ds_hash_table_t *table, void *key,
                                    void *value);

size_t ds_hash_func_string_djb2(void *key);
size_t ds_hash_func_int(void *key);
size_t ds_hash_func_size_t(void *key);
size_t ds_hash_func_pointer(void *key);
size_t ds_hash_func_double(void *key);
size_t ds_hash_func_default(void *key);

size_t ds_hash_next_prime_capacity(size_t current_capacity);

size_t ds_hash_quadratic_probing(size_t base_index, size_t iteration,
                                 size_t capacity);
size_t ds_hash_linear_probing(size_t base_index, size_t iteration,
                              size_t capacity);

const ds_hash_bucket_store_t *ds_hash_table_linked_list_bucket_store(void);

ds_hash_table_t *ds_hash_table_create(size_t capacity,
                                      ds_hash_table_mode_t mode,
                                      ds_hash_probing_func_t probing_func);
ds_hash_table_t *
ds_hash_table_create_chain(size_t capacity,
                           const ds_hash_bucket_store_t *bucket_store);
ds_hash_table_t *ds_hash_table_create_alloc(size_t capacity,
                                            ds_hash_table_mode_t mode,
                                            ds_hash_probing_func_t probing_func,
                                            void *(*allocator)(size_t),
                                            void (*deallocator)(void *));
ds_hash_table_t *ds_hash_table_create_chain_alloc(
    size_t capacity, const ds_hash_bucket_store_t *bucket_store,
    void *(*allocator)(size_t), void (*deallocator)(void *));
void ds_hash_table_insert_with(ds_hash_table_t *table, void *key, void *value,
                               size_t (*hash_func)(void *),
                               int (*compare)(void *, void *));
void ds_hash_table_resize(ds_hash_table_t *table, size_t new_capacity,
                          size_t (*hash_func)(void *),
                          int (*compare)(void *, void *));
void *ds_hash_table_lookup(ds_hash_table_t *table, void *key,
                           size_t (*hash_func)(void *),
                           int (*compare)(void *, void *));
/* The node that holds key, or NULL; valid until the next insertion or
 * removal */
ds_hash_node_t *ds_hash_table_lookup_node(ds_hash_table_t *table, void *key,
                                          size_t (*hash_func)(void *),
                                          int (*compare)(void *, void *));
void ds_hash_table_remove_with(ds_hash_table_t *table, void *key,
                               size_t (*hash_func)(void *),
                               int (*compare)(void *, void *),
                               void (*destroy)(ds_hash_node_t *));
void ds_hash_table_for_each(ds_hash_table_t *table,
                            void (*callback)(ds_hash_node_t *, void *),
                            void *args);
bool ds_hash_table_is_empty(ds_hash_table_t *table);
void ds_hash_table_destroy_with(ds_hash_table_t *table,
                                void (*destroy)(ds_hash_node_t *));
ds_hash_table_t *ds_hash_table_clone_callbacks(ds_hash_table_t *table,
                                               void *(*clone_key)(void *),
                                               void *(*clone_value)(void *));
ds_hash_table_t *ds_hash_table_clone_with(ds_hash_table_t *table,
                                          void *(*clone_key)(void *),
                                          void *(*clone_value)(void *),
                                          size_t (*hash_func)(void *),
                                          int (*compare)(void *, void *));

/* Configured checked construction validates modes, sizing and callback tables;
 * NULL context chooses the default. Output is NULL on failure. */
ds_status_t ds_hash_table_create_config_ex(const ds_hash_table_config_t *config,
                                           ds_hash_table_t **out);
typedef ds_status_t (*ds_hash_table_visit_fn)(ds_hash_node_t *, void *);
/* Newest first; stop on first non-DS_OK status. Callbacks cannot mutate the
 * table or retain node views beyond the existing lifetime rules. */
ds_status_t ds_hash_table_walk_with(ds_hash_table_t *table,
                                    ds_hash_table_visit_fn visit, void *user);
/* Checked clone uses *_ex callbacks when present; DS_OK may produce NULL.
 * Pointer-returning clone callbacks use NULL to signal failure here.
 * Each cloned part needs a destroy callback for rollback and must be separately
 * owned; cloning a key preserves hash/equality. Callback failures own
 * unfinished outputs. Completed parts are rolled back. Positive clone statuses
 * become DS_ERR_CALLBACK. A borrowed clone copies pointers and clears
 * ownership. NULL context inherits the source owner, including custom direct
 * allocators. Output is NULL on failure. Compare/hash/user policy lifetimes are
 * inherited. */
ds_status_t ds_hash_table_clone_ex(ds_hash_table_t *table, bool borrowed,
                                   ds_context_t *context,
                                   ds_hash_table_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_HASH_TABLE_H */
