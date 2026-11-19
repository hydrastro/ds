#ifndef DS_ITER_H
#define DS_ITER_H

#include <dsa/core/status.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ds_iter;
typedef struct ds_iter ds_iter_t;
typedef ds_status_t (*ds_iter_next_func_t)(ds_iter_t *iter);
typedef void (*ds_iter_destroy_func_t)(ds_iter_t *iter);

/* Borrowed cursor shell, not a lock or snapshot. The concrete adapter owns
 * its invalidation contract. Unless that adapter explicitly allows a mutation,
 * hold external synchronization for the entire walk and keep its container
 * alive until destroy. Never copy a live iterator with owning state by value.
 * After next returns DS_STOP/error, do not reuse prior borrowed fields;
 * destroy remains required even when iteration ends early. */
struct ds_iter {
  void *container;
  void *state;
  void *key;
  void *value;
  void *node;
  ds_iter_next_func_t next;
  ds_iter_destroy_func_t destroy;
};

ds_status_t ds_iter_next(ds_iter_t *iter);
void ds_iter_destroy(ds_iter_t *iter);

#ifdef __cplusplus
}
#endif

#endif /* DS_ITER_H */
