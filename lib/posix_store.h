#ifndef DS_POSIX_STORE_H
#define DS_POSIX_STORE_H

/*
 * A page store on a POSIX file: pread and pwrite at page offsets, fsync
 * for sync, ftruncate for truncation. Unlike the stdio store of
 * lib/pager, its sync makes written pages durable and its truncation
 * shrinks the file: DS_PAGE_STORE_DURABLE_SYNC and
 * DS_PAGE_STORE_TRUNCATE_PERSISTS. Under lib/wal_store it gives crash-safe
 * files.
 *
 * Durability. sync is fsync of the file; when this store created the
 * file, the first sync also syncs the directory that holds it, so that
 * the file itself survives a crash (a file system that cannot sync a
 * directory, EINVAL, is taken at its word). How durable fsync is depends
 * on the system and the device.
 *
 * Pages. Writing past the end grows the file and leaves a hole that reads
 * as zeros; a partial last page reads as zeros past the end of the file.
 * Transfers retry on EINTR and continue after partial transfers. Pages
 * past what off_t can address are DS_ERR_RANGE; page sizes go up to 2^30.
 * A failed system call is DS_ERR_STATE (errno says why).
 *
 * The store owns the descriptor: closing the store closes it.
 */

#include "context.h"
#include "pager.h"
#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_POSIX_STORE_CREATE 0x1u    /* create the file if it is missing */
#define DS_POSIX_STORE_EXCLUSIVE 0x2u /* with CREATE: fail if it exists */
#define DS_POSIX_STORE_TRUNCATE 0x4u  /* start empty */
#define DS_POSIX_STORE_READ_ONLY                                               \
  0x8u /* writes and truncations are                                           \
          DS_ERR_UNSUPPORTED, sync a no-op */

/* opens path (mode 0666 before the umask when created) */
ds_status_t ds_page_store_posix(ds_page_store_t *store, const char *path,
                                size_t page_size, unsigned flags,
                                ds_context_t *context);
/* adopts an open descriptor (its directory is never synced) */
ds_status_t ds_page_store_posix_fd(ds_page_store_t *store, int fd,
                                   size_t page_size, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_POSIX_STORE_H */
