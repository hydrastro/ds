#ifndef DS_PERSISTENT_RBT_H
#define DS_PERSISTENT_RBT_H

#include "common.h"

#include "context.h"
#include "rbt.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ds_prbt;
struct ds_prbt_version;
typedef struct ds_prbt ds_prbt_t;
typedef struct ds_prbt_version ds_prbt_version_t;

typedef int (*ds_prbt_compare_func_t)(void *a, void *b, void *user);
typedef void *(*ds_prbt_clone_func_t)(void *ptr, void *user);
typedef void (*ds_prbt_destroy_func_t)(void *ptr, void *user);

typedef struct ds_prbt_config {
  ds_prbt_compare_func_t compare;
  ds_prbt_clone_func_t clone_key;
  ds_prbt_clone_func_t clone_value;
  ds_prbt_destroy_func_t destroy_key;
  ds_prbt_destroy_func_t destroy_value;
  void *user;
  ds_context_t *context;
} ds_prbt_config_t;

void ds_prbt_config_init(ds_prbt_config_t *config);
ds_prbt_t *FUNC(ds_prbt_create)(const ds_prbt_config_t *config);
void FUNC(ds_prbt_destroy)(ds_prbt_t *tree);
ds_prbt_version_t *FUNC(ds_prbt_root)(ds_prbt_t *tree);
ds_prbt_version_t *FUNC(ds_prbt_insert)(ds_prbt_t *tree,
                                         ds_prbt_version_t *base, void *key,
                                         void *value);
ds_status_t FUNC(ds_prbt_get)(ds_prbt_t *tree, ds_prbt_version_t *version,
                               void *key, void **out_value);
void FUNC(ds_prbt_version_retain)(ds_prbt_version_t *version);
void FUNC(ds_prbt_version_release)(ds_prbt_t *tree,
                                   ds_prbt_version_t *version);

#ifdef __cplusplus
}
#endif

#endif /* DS_PERSISTENT_RBT_H */
