#ifndef DS_PAGER_H
#define DS_PAGER_H

/*
 * Page stores and a pager (buffer pool).
 *
 * The page-store capability is the minimum a disk-oriented structure
 * needs: fixed-size pages addressed by number, read, write, sync, count
 * and truncate. Backends:
 *
 *   ds_page_store_memory    pages in memory (tests, caches, tmpfs-like use)
 *   ds_page_store_file      a C stdio file (offsets must fit in a long)
 *
 * Traits state what sync and truncate really guarantee:
 *   DS_PAGE_STORE_DURABLE_SYNC    sync makes written pages survive a crash
 *                                 (neither backend: the file store's sync
 *                                 is fflush, which only hands data to the
 *                                 operating system)
 *   DS_PAGE_STORE_TRUNCATE_PERSISTS  truncation releases the pages for good
 *                                 (memory store); without it truncation is
 *                                 logical for this session only (file
 *                                 store: C89 cannot shrink a file, so the
 *                                 pages reappear when the file is reopened)
 *
 * Any other medium (mmap, a network block device, an encrypted or
 * checksumming layer stacked on another store) fits by filling in a
 * ds_page_store_ops_t.
 *
 * The pager caches up to `frames` pages of any store, with pin counts and
 * dirty tracking; free frames are used first, then the replacement policy
 * (lib/replacement: LRU by default, or FIFO, CLOCK, SIEVE, ...) picks an
 * unpinned victim. Dirty pages are written back on eviction and on flush. Its
 * page table is a map from the map capability (lib/map_iface). DS_ERR_STATE
 * when every frame is pinned, and (as in algo/external_sort) when a stdio call
 * fails. Memory comes from the context.
 */

#include "context.h"
#include "map_iface.h"
#include "replacement.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_PAGE_STORE_DURABLE_SYNC 0x1u
#define DS_PAGE_STORE_TRUNCATE_PERSISTS 0x2u

typedef struct ds_page_store_ops {
  const char *name;
  unsigned traits;
  /* pages at or past count read as zeros */
  ds_status_t (*read)(void *impl, unsigned long page, void *buf);
  /* writing past the end grows the store (gaps read as zeros) */
  ds_status_t (*write)(void *impl, unsigned long page, const void *buf);
  ds_status_t (*sync)(void *impl);
  unsigned long (*count)(const void *impl);
  ds_status_t (*truncate)(void *impl, unsigned long pages);
  void (*close)(void *impl);
} ds_page_store_ops_t;

typedef struct ds_page_store {
  const ds_page_store_ops_t *ops;
  void *impl;
  size_t page_size;
} ds_page_store_t;

ds_status_t ds_page_store_memory(ds_page_store_t *store, size_t page_size,
                                 ds_context_t *context);
/* truncate: start empty; otherwise the file must exist (or is created) */
ds_status_t ds_page_store_file(ds_page_store_t *store, const char *path,
                               size_t page_size, bool truncate,
                               ds_context_t *context);
void ds_page_store_close(ds_page_store_t *store);
ds_status_t ds_page_store_read(ds_page_store_t *store, unsigned long page,
                               void *buf);
ds_status_t ds_page_store_write(ds_page_store_t *store, unsigned long page,
                                const void *buf);
ds_status_t ds_page_store_sync(ds_page_store_t *store);
unsigned long ds_page_store_count(const ds_page_store_t *store);
ds_status_t ds_page_store_truncate(ds_page_store_t *store, unsigned long pages);

typedef struct ds_pager_frame {
  unsigned long page;
  size_t pins;
  int used;
  int dirty;
} ds_pager_frame_t;

typedef struct ds_pager {
  ds_page_store_t *store;
  size_t frames;
  unsigned char *data; /* frames * page_size */
  ds_pager_frame_t *frame;
  ds_map_t table; /* page -> frame */
  ds_replacement_t policy;
  size_t *free_frames; /* stack of unused frames */
  size_t nfree;
  unsigned long pages; /* logical page count (includes unflushed) */
  unsigned long hits;
  unsigned long misses;
  unsigned long writes;
  ds_context_t *context;
} ds_pager_t;

/* map_ops NULL: Robin Hood map; LRU replacement */
ds_status_t ds_pager_init(ds_pager_t *pager, ds_page_store_t *store,
                          size_t frames, const ds_map_ops_t *map_ops,
                          ds_context_t *context);
/* policy NULL: LRU */
ds_status_t ds_pager_init_policy(ds_pager_t *pager, ds_page_store_t *store,
                                 size_t frames, const ds_map_ops_t *map_ops,
                                 const ds_replacement_ops_t *policy,
                                 ds_context_t *context);
/* flush and free the pager (not the store). If the flush fails the pager
 * stays alive with its dirty pages, so the caller can retry or discard */
ds_status_t ds_pager_close(ds_pager_t *pager);
/* free the pager without writing anything (dirty pages are dropped) */
void ds_pager_discard(ds_pager_t *pager);
/* legacy: flush, then free whatever the flush returned (dirty pages are
 * lost when it fails); prefer ds_pager_close / ds_pager_discard */
ds_status_t ds_pager_destroy(ds_pager_t *pager);
/* pin page and return its buffer (valid until unpinned) */
ds_status_t ds_pager_get(ds_pager_t *pager, unsigned long page, void **data);
/* append a zeroed page, pinned */
ds_status_t ds_pager_new(ds_pager_t *pager, unsigned long *page, void **data);
ds_status_t ds_pager_mark_dirty(ds_pager_t *pager, unsigned long page);
ds_status_t ds_pager_unpin(ds_pager_t *pager, unsigned long page);
/* write back every dirty page and sync the store */
ds_status_t ds_pager_flush(ds_pager_t *pager);
unsigned long ds_pager_page_count(const ds_pager_t *pager);

#ifdef __cplusplus
}
#endif

#endif /* DS_PAGER_H */
