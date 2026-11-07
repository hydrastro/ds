#ifndef DS_BUDDY_ALLOCATOR_H
#define DS_BUDDY_ALLOCATOR_H

/*
 * The buddy system (Knowlton; Knuth's binary buddies): an arena of
 * min_block * 2^max_order bytes is handed out in blocks of
 * min_block * 2^k bytes. A request takes the smallest free block that
 * fits, splitting larger blocks in halves ("buddies") on the way down; a
 * freed block merges with its buddy as long as the buddy is free too, so
 * free space coalesces in O(log n) without searching.
 *
 * The bookkeeping (free lists per order, the order and state of each
 * block) lives outside the arena, so the arena can be any memory: a
 * caller buffer or one allocated from a ds_context_t. Blocks are aligned
 * to min_block relative to the arena start. ds_context_use_buddy routes a
 * context's allocations to the buddy system.
 *
 * Concurrency: an allocator may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_buddy {
  unsigned char *arena;
  size_t min_block;
  size_t max_order;
  size_t blocks; /* min blocks: 2^max_order */
  size_t *next;  /* free lists, per min-block index */
  size_t *prev;
  unsigned char *order; /* order of the block starting here */
  unsigned char *state; /* 0 inside a block, 1 free head, 2 used head */
  size_t *head;         /* free list head per order */
  size_t free_bytes;
  bool owns_arena;
  ds_context_t *context;
} ds_buddy_t;

/* buffer: min_block << max_order bytes, or NULL to allocate it.
 * min_block >= 1 and a power of two; max_order <= 40. */
ds_status_t ds_buddy_init(ds_buddy_t *b, void *buffer, size_t min_block,
                          size_t max_order, ds_context_t *context);
void ds_buddy_destroy(ds_buddy_t *b);
/* NULL when no free block is large enough (or size is 0). */
void *ds_buddy_alloc(ds_buddy_t *b, size_t size);
ds_status_t ds_buddy_free(ds_buddy_t *b, void *ptr);
/* Grows or shrinks in place when the buddy allows, else moves. */
void *ds_buddy_realloc(ds_buddy_t *b, void *ptr, size_t size);
/* Bytes of the block holding ptr (0 for an invalid pointer). */
size_t ds_buddy_block_size(const ds_buddy_t *b, const void *ptr);
size_t ds_buddy_free_bytes(const ds_buddy_t *b);
/* Size of the largest free block. */
size_t ds_buddy_largest_free(const ds_buddy_t *b);
void ds_context_use_buddy(ds_context_t *context, ds_buddy_t *b);

#ifdef __cplusplus
}
#endif

#endif /* DS_BUDDY_ALLOCATOR_H */
