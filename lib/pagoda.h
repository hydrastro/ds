#ifndef DS_PAGODA_H
#define DS_PAGODA_H

/*
 * Pagodas (Francon, Viennot and Vuillemin): meldable priority queues as
 * heap-ordered binary trees in which every node's left and right pointers
 * are threaded so that the left branch and the right branch of the root
 * are circular lists running from the bottom up to the root. Melding two
 * pagodas merges the right branch of one with the left branch of the
 * other from the bottom, like merging two sorted lists; insertion is a
 * meld with a one-node pagoda and deletion of the top melds its two
 * branches. O(log n) amortized (expected for random input).
 *
 * Elements are stored by value in node arrays shared by all the queues of
 * one ds_pagoda_pool_t, so queues of a pool can be melded in place. The
 * comparator decides the order: the top is the element that compares
 * smallest (a min-queue; reverse the comparator for a max-queue).
 *
 * Concurrency: a pool may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_PAGODA_EMPTY (~(size_t)0)

typedef int (*ds_pagoda_compare_func_t)(const void *a, const void *b,
                                        void *user);

typedef struct ds_pagoda_pool {
  unsigned char *data;
  size_t *left;
  size_t *right;
  size_t slots;
  size_t capacity;
  size_t free_list;
  size_t elem_size;
  ds_pagoda_compare_func_t compare;
  void *user;
  ds_context_t *context;
} ds_pagoda_pool_t;

/* A queue is the index of its root (DS_PAGODA_EMPTY when empty). */
typedef size_t ds_pagoda_t;

ds_status_t ds_pagoda_pool_init(ds_pagoda_pool_t *pool, size_t elem_size,
                                ds_pagoda_compare_func_t compare, void *user,
                                ds_context_t *context);
void ds_pagoda_pool_destroy(ds_pagoda_pool_t *pool);
ds_status_t ds_pagoda_push(ds_pagoda_pool_t *pool, ds_pagoda_t *q,
                           const void *elem);
ds_status_t ds_pagoda_peek(const ds_pagoda_pool_t *pool, ds_pagoda_t q,
                           void *out);
ds_status_t ds_pagoda_pop(ds_pagoda_pool_t *pool, ds_pagoda_t *q, void *out);
/* *q receives every element of *other; *other becomes empty. */
void ds_pagoda_meld(ds_pagoda_pool_t *pool, ds_pagoda_t *q, ds_pagoda_t *other);

#ifdef __cplusplus
}
#endif

#endif /* DS_PAGODA_H */
