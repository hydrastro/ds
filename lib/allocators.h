#ifndef DS_ALLOCATORS_H
#define DS_ALLOCATORS_H

#include "context.h"
#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Alignment: arenas and pools align the actual addresses they return
 * (not just offsets into a buffer of unknown alignment) to `alignment`,
 * DS_MAX_ALIGN by default (enough for any C89 object); a power of two can
 * be chosen with ds_arena_set_alignment / ds_pool_init_aligned.
 *
 * Pools on caller buffers: blocks are laid out at an aligned stride (the
 * block size rounded up to the alignment, and at least a pointer), from
 * the first aligned address of the buffer, and never past its end. The
 * number of blocks that fit can therefore be smaller than requested; it is
 * reported by ds_pool_init_buffer and ds_pool_available, and
 * ds_pool_storage_size gives the bytes that guarantee a count.
 */
typedef struct ds_arena {
  unsigned char *buffer;
  size_t capacity;
  size_t offset;
  int owns_buffer;
  size_t alignment;
} ds_arena_t;

typedef struct ds_pool_free_node {
  struct ds_pool_free_node *next;
} ds_pool_free_node_t;

typedef struct ds_pool {
  unsigned char *buffer; /* first (aligned) block */
  size_t block_size;
  size_t block_count;
  ds_pool_free_node_t *free_list;
  size_t available_count;
  int owns_buffer;
  unsigned char *base; /* the buffer as given or allocated */
  size_t alignment;
} ds_pool_t;

typedef struct ds_debug_allocator_stats {
  size_t allocations;
  size_t frees;
  size_t reallocations;
  size_t failed_allocations;
  size_t bytes_live;
  size_t bytes_peak;
  size_t bytes_total;
} ds_debug_allocator_stats_t;

typedef struct ds_debug_allocator {
  ds_context_t *backing;
  ds_debug_allocator_stats_t stats;
} ds_debug_allocator_t;

void ds_arena_init(ds_arena_t *arena, void *buffer, size_t capacity);
ds_status_t ds_arena_create(ds_arena_t *arena, size_t capacity,
                            ds_context_t *context);
void ds_arena_destroy(ds_arena_t *arena, ds_context_t *context);
void ds_arena_reset(ds_arena_t *arena);
size_t ds_arena_used(const ds_arena_t *arena);
size_t ds_arena_remaining(const ds_arena_t *arena);
void ds_context_use_arena(ds_context_t *context, ds_arena_t *arena);
/* alignment: a power of two (DS_ERR_INVALID otherwise) */
ds_status_t ds_arena_set_alignment(ds_arena_t *arena, size_t alignment);
/* one allocation with its own alignment (NULL when out of space) */
void *ds_arena_alloc_aligned(ds_arena_t *arena, size_t size, size_t alignment);

/* a pool over buffer_bytes caller bytes; *out_count (optional) receives
 * the number of blocks that fit. alignment: a power of two, 0 means
 * DS_MAX_ALIGN */
ds_status_t ds_pool_init_buffer(ds_pool_t *pool, void *buffer,
                                size_t buffer_bytes, size_t block_size,
                                size_t alignment, size_t *out_count);
/* bytes a caller buffer needs so that block_count blocks fit at any
 * starting address */
ds_status_t ds_pool_storage_size(size_t block_size, size_t block_count,
                                 size_t alignment, size_t *out_bytes);
/* legacy forms: the buffer is block_size * block_count bytes; blocks that
 * do not fit at the aligned stride are dropped (see ds_pool_available) */
void ds_pool_init(ds_pool_t *pool, void *buffer, size_t block_size,
                  size_t block_count);
ds_status_t ds_pool_init_aligned(ds_pool_t *pool, void *buffer,
                                 size_t block_size, size_t block_count,
                                 size_t alignment);
ds_status_t ds_pool_create(ds_pool_t *pool, size_t block_size,
                           size_t block_count, ds_context_t *context);
void ds_pool_destroy(ds_pool_t *pool, ds_context_t *context);
size_t ds_pool_available(const ds_pool_t *pool);
void ds_context_use_pool(ds_context_t *context, ds_pool_t *pool);

void ds_debug_allocator_init(ds_debug_allocator_t *debug,
                             ds_context_t *backing);
void ds_context_use_debug_allocator(ds_context_t *context,
                                    ds_debug_allocator_t *debug);
void ds_debug_allocator_get_stats(const ds_debug_allocator_t *debug,
                                  ds_debug_allocator_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALLOCATORS_H */
