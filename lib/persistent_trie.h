#ifndef DS_PERSISTENT_TRIE_H
#define DS_PERSISTENT_TRIE_H

#include "common.h"

#include "context.h"
#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ds_ptrie_node;
typedef struct ds_ptrie ds_ptrie_t;
typedef struct ds_ptrie_version ds_ptrie_version_t;

typedef void *(*ds_ptrie_clone_value_func_t)(void *value, void *user);
typedef void (*ds_ptrie_destroy_value_func_t)(void *value, void *user);

typedef struct ds_ptrie_config {
  ds_ptrie_clone_value_func_t clone_value;
  ds_ptrie_destroy_value_func_t destroy_value;
  void *user;
  ds_context_t *context;
} ds_ptrie_config_t;

void ds_ptrie_config_init(ds_ptrie_config_t *config);
ds_ptrie_t *FUNC(ds_ptrie_create)(const ds_ptrie_config_t *config);
void FUNC(ds_ptrie_destroy)(ds_ptrie_t *trie);
ds_ptrie_version_t *FUNC(ds_ptrie_root)(ds_ptrie_t *trie);
ds_ptrie_version_t *FUNC(ds_ptrie_insert)(ds_ptrie_t *trie,
                                           ds_ptrie_version_t *base,
                                           const unsigned char *key,
                                           size_t len, void *value);
ds_status_t FUNC(ds_ptrie_get)(ds_ptrie_version_t *version,
                                const unsigned char *key, size_t len,
                                void **out_value);
void FUNC(ds_ptrie_version_retain)(ds_ptrie_version_t *version);
void FUNC(ds_ptrie_version_release)(ds_ptrie_t *trie,
                                    ds_ptrie_version_t *version);

#ifdef __cplusplus
}
#endif

#endif /* DS_PERSISTENT_TRIE_H */
