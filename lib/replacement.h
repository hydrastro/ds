#ifndef DS_REPLACEMENT_H
#define DS_REPLACEMENT_H

/*
 * The cache-replacement capability: which of n resident frames to evict,
 * for buffer pools, page caches and object caches that manage their own
 * storage and only need the decision.
 *
 * The owner reports events by frame index: admit (a frame was filled; it
 * starts pinned), touch (a hit), set_evictable (its pin count dropped to
 * zero, or rose from zero), evict (it left). victim() proposes an
 * evictable resident frame, or (size_t)-1 when every resident frame is
 * pinned; the owner then evicts it (or not, e.g. when a write-back fails).
 *
 * Policies:
 *   ds_replacement_lru()    least recently released frame
 *   ds_replacement_fifo()   oldest admitted evictable frame
 *   ds_replacement_clock()  second chance: a hand sweeps reference bits
 *   ds_replacement_sieve()  SIEVE (Zhang et al., NSDI 2024): FIFO order
 *                           with visited bits and a hand that keeps its
 *                           place; no reordering on hits
 *
 * Memory comes from the context.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_REPLACEMENT_NONE ((size_t) - 1)

typedef struct ds_replacement_ops {
  const char *name;
  size_t instance_size;
  ds_status_t (*init)(void *p, size_t frames, ds_context_t *context);
  void (*destroy)(void *p);
  void (*admit)(void *p, size_t frame);
  void (*touch)(void *p, size_t frame);
  void (*set_evictable)(void *p, size_t frame, bool evictable);
  size_t (*victim)(void *p);
  void (*evict)(void *p, size_t frame);
} ds_replacement_ops_t;

const ds_replacement_ops_t *ds_replacement_lru(void);
const ds_replacement_ops_t *ds_replacement_fifo(void);
const ds_replacement_ops_t *ds_replacement_clock(void);
const ds_replacement_ops_t *ds_replacement_sieve(void);

typedef struct ds_replacement {
  const ds_replacement_ops_t *ops;
  void *impl;
  ds_context_t *context;
} ds_replacement_t;

/* ops NULL selects LRU */
ds_status_t ds_replacement_init(ds_replacement_t *r,
                                const ds_replacement_ops_t *ops, size_t frames,
                                ds_context_t *context);
void ds_replacement_destroy(ds_replacement_t *r);
void ds_replacement_admit(ds_replacement_t *r, size_t frame);
void ds_replacement_touch(ds_replacement_t *r, size_t frame);
void ds_replacement_set_evictable(ds_replacement_t *r, size_t frame,
                                  bool evictable);
size_t ds_replacement_victim(ds_replacement_t *r);
void ds_replacement_evict(ds_replacement_t *r, size_t frame);

#ifdef __cplusplus
}
#endif

#endif /* DS_REPLACEMENT_H */
