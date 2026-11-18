#ifndef DS_PERSISTENT_TRIE_H
#define DS_PERSISTENT_TRIE_H

#include <dsa/core/common.h>

#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ds_ptrie_node;
typedef struct ds_ptrie ds_ptrie_t;
typedef struct ds_ptrie_version ds_ptrie_version_t;

typedef void *(*ds_ptrie_clone_value_func_t)(void *value, void *user);
typedef ds_status_t (*ds_ptrie_clone_value_ex_func_t)(void *value, void *user,
                                                      void **out);
typedef void (*ds_ptrie_destroy_value_func_t)(void *value, void *user);
typedef bool (*ds_ptrie_visit_func_t)(const unsigned char *key, size_t len,
                                      void *value, void *user);

typedef enum ds_ptrie_storage {
  DS_PTRIE_BYTES = 0, /* one byte per edge, the original representation */
  DS_PTRIE_RADIX = 1  /* coalesce nonterminal unary paths into byte labels */
} ds_ptrie_storage_t;

typedef struct ds_ptrie_config {
  ds_ptrie_clone_value_func_t clone_value;
  ds_ptrie_destroy_value_func_t destroy_value;
  void *user;
  ds_context_t *context;
  ds_ptrie_storage_t storage;
  /* Optional checked clone instead of clone_value. DS_OK may produce NULL;
   * on failure it must clean up its partial result. Positive statuses are
   * callback errors. Never configure both clone forms. */
  ds_ptrie_clone_value_ex_func_t clone_value_ex;
} ds_ptrie_config_t;

/* Both backends share immutable subtrees across versions and copy only an
 * updated path. Radix labels may contain NUL and arbitrary bytes; empty keys
 * and prefixes ending inside a label are supported. Updates and destruction
 * are iterative, even for long keys. clone_value/destroy_value retain their
 * existing ownership policy: owned values require a clone callback. Versions
 * must be released through their creating trie, before destroying that trie.
 * Mutating/retaining/releasing shared versions requires external
 * synchronization. Initialize configs with ds_ptrie_config_init before
 * overriding fields. */

void ds_ptrie_config_init(ds_ptrie_config_t *config);
ds_ptrie_t *ds_ptrie_create(const ds_ptrie_config_t *config);
ds_status_t ds_ptrie_create_ex(const ds_ptrie_config_t *config,
                               ds_ptrie_t **out);
void ds_ptrie_destroy(ds_ptrie_t *trie);
ds_ptrie_version_t *ds_ptrie_root(ds_ptrie_t *trie);
ds_ptrie_version_t *ds_ptrie_insert(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                                    const unsigned char *key, size_t len,
                                    void *value);
ds_ptrie_version_t *ds_ptrie_remove(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                                    const unsigned char *key, size_t len);
/* Checked updates leave base intact and set *out to NULL on failure.
 * DS_NOT_FOUND for a missing removal; DS_ERR_CALLBACK for a failed value
 * clone, DS_ERR_ALLOC/OVERFLOW for storage. Values are cloned before storage;
 * without value callbacks they are borrowed, including NULL. */
ds_status_t ds_ptrie_insert_ex(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                               const unsigned char *key, size_t len,
                               void *value, ds_ptrie_version_t **out);
ds_status_t ds_ptrie_remove_ex(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                               const unsigned char *key, size_t len,
                               ds_ptrie_version_t **out);
ds_ptrie_version_t *ds_ptrie_remove_prefix(ds_ptrie_t *trie,
                                           ds_ptrie_version_t *base,
                                           const unsigned char *prefix,
                                           size_t prefix_len,
                                           size_t *out_removed);
ds_status_t ds_ptrie_remove_prefix_ex(ds_ptrie_t *trie,
                                      ds_ptrie_version_t *base,
                                      const unsigned char *prefix, size_t len,
                                      size_t *out_removed,
                                      ds_ptrie_version_t **out);
ds_status_t ds_ptrie_get(ds_ptrie_version_t *version, const unsigned char *key,
                         size_t len, void **out_value);
ds_status_t ds_ptrie_contains_prefix(ds_ptrie_version_t *version,
                                     const unsigned char *prefix,
                                     size_t prefix_len);
size_t ds_ptrie_count_prefix(ds_ptrie_version_t *version,
                             const unsigned char *prefix, size_t prefix_len);
ds_ptrie_version_t *ds_ptrie_copy_prefix(
    ds_ptrie_t *trie, ds_ptrie_version_t *destination_base,
    ds_ptrie_version_t *source_version, const unsigned char *source_prefix,
    size_t source_prefix_len, const unsigned char *destination_prefix,
    size_t destination_prefix_len, size_t *out_copied);
ds_ptrie_version_t *
ds_ptrie_move_prefix(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                     const unsigned char *source_prefix,
                     size_t source_prefix_len,
                     const unsigned char *destination_prefix,
                     size_t destination_prefix_len, size_t *out_moved);
ds_status_t ds_ptrie_copy_prefix_ex(
    ds_ptrie_t *trie, ds_ptrie_version_t *destination_base,
    ds_ptrie_version_t *source_version, const unsigned char *source_prefix,
    size_t source_len, const unsigned char *destination_prefix,
    size_t destination_len, size_t *out_copied, ds_ptrie_version_t **out);
ds_status_t ds_ptrie_move_prefix_ex(ds_ptrie_t *trie, ds_ptrie_version_t *base,
                                    const unsigned char *source_prefix,
                                    size_t source_len,
                                    const unsigned char *destination_prefix,
                                    size_t destination_len, size_t *out_moved,
                                    ds_ptrie_version_t **out);
ds_status_t ds_ptrie_longest_prefix(ds_ptrie_version_t *version,
                                    const unsigned char *key, size_t len,
                                    size_t *out_len, void **out_value);
/* Visits go in key order: bytes compare as unsigned and a key comes
 * before its extensions. A range is inclusive, a NULL bound leaves its end
 * open, and the walk skips what lies outside it. The visitor stops a visit
 * by returning false (DS_STOP); scratch memory comes from the trie's
 * context (DS_ERR_ALLOC when it is refused). */
ds_status_t ds_ptrie_visit_prefix(ds_ptrie_version_t *version,
                                  const unsigned char *prefix,
                                  size_t prefix_len,
                                  ds_ptrie_visit_func_t visit, void *user);
ds_status_t ds_ptrie_visit_range(ds_ptrie_version_t *version,
                                 const unsigned char *lower, size_t lower_len,
                                 const unsigned char *upper, size_t upper_len,
                                 ds_ptrie_visit_func_t visit, void *user);
void ds_ptrie_version_retain(ds_ptrie_version_t *version);
ds_status_t ds_ptrie_version_retain_ex(ds_ptrie_version_t *version);
void ds_ptrie_version_release(ds_ptrie_t *trie, ds_ptrie_version_t *version);

#ifdef __cplusplus
}
#endif

#endif /* DS_PERSISTENT_TRIE_H */
