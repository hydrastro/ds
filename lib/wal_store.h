#ifndef DS_WAL_STORE_H
#define DS_WAL_STORE_H

/*
 * A write-ahead-logged page store: atomic, crash-safe commits for any
 * structure built on the page-store capability (the pager and the paged
 * B+ tree on it, or anything else that reads and writes pages), stacked on
 * two other stores: a data store that holds the pages at home and a log
 * store.
 *
 * Transactions. The writes and truncations since the last sync form a
 * transaction. They go to the log, and reads find them there; sync
 * appends a commit record and syncs the log store. Opening the store
 * again after a crash recovers the state of the last sync that completed
 * (or of a sync that was under way, if its commit record reached the
 * log), never a mixture: every log page is checksummed (CRC-32C), so torn
 * or missing pages end the log where they occur. Through a pager, a
 * structure commits by flushing the pager (ds_pager_flush), which writes
 * the dirty pages back and syncs the store.
 *
 * Checkpoints copy the committed pages home, sync the data store, then
 * restart the log under a new epoch, recorded in one of two superblocks at
 * the head of the log: the other keeps the previous epoch until the new
 * one is written, so a torn superblock loses nothing.
 * ds_wal_store_checkpoint commits and checkpoints; sync checkpoints by
 * itself once the log holds checkpoint_pages pages. Such an automatic
 * checkpoint does not change the result of sync, which is the commit's:
 * if it fails, the log simply stays (ds_wal_store_stats reports the
 * failure) and the next sync tries again.
 *
 * Page count. A truncation is logged like a write and the page count is
 * part of the committed state, so truncation persists even over a data
 * store whose own truncation does not (the stdio file store), and pages
 * past the committed count read as zeros whatever the data store holds.
 *
 * Failures. A failed write or truncation changes nothing. A failed sync
 * leaves its commit pending: the next write, truncation or sync completes
 * it first and fails, changing nothing else, while it cannot. So a pager
 * that retries its flush, or that writes pages back meanwhile, finishes
 * the commit before adding to the store. A log restart that fails after
 * the data store was synced is pending in the same way. A corrupt log
 * page found on a read is DS_ERR_INVALID.
 *
 * Traits: DS_PAGE_STORE_ATOMIC_SYNC and DS_PAGE_STORE_TRUNCATE_PERSISTS,
 * and DS_PAGE_STORE_DURABLE_SYNC when both underlying stores have it (a
 * commit is as durable as the log store's sync, a checkpoint as the data
 * store's).
 *
 * Opening. A log store without a valid superblock and with at most its
 * two superblock pages is formatted: the data store's pages are taken as
 * they are, so an existing store can be put under a log. A log with
 * further pages but no valid superblock is DS_ERR_INVALID. Both stores
 * need the same page size, at least 128 bytes and at most 2^32 - 1.
 *
 * Log format (little-endian, 8-byte counters):
 *
 *   pages 0, 1  superblocks: magic, page size, epoch (stored in slot
 *               epoch % 2), first transaction number of the epoch, page
 *               count at the checkpoint, CRC
 *   pages 2...  groups: a header page, then the data pages it describes.
 *               The header holds its epoch and transaction number, the
 *               page count after its entries, whether it ends the
 *               transaction (commit), and up to (page size - 48) / 16
 *               entries: a page write (target page, CRC of the data page,
 *               which follows in order) or a truncation (new count)
 *
 * A group's data pages are written as they come and its header when the
 * group is full or at commit, so a transaction costs one log sync. The
 * latest log position (and CRC) of every page logged since the last
 * checkpoint lives in an index, a map from the map capability (Robin
 * Hood by default); recovery rebuilds it by reading the log.
 *
 * The WAL store does not own the data and log stores: close them after
 * it. Closing does not commit (the transaction is lost, as in a crash).
 */

#include "context.h"
#include "map_iface.h"
#include "pager.h"
#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_wal_store_config {
  /* sync checkpoints once the log holds at least this many pages; 0 leaves
   * checkpoints to ds_wal_store_checkpoint */
  unsigned long checkpoint_pages;
  const ds_map_ops_t *index; /* NULL: Robin Hood map */
} ds_wal_store_config_t;

typedef struct ds_wal_store_stats {
  unsigned long epoch;
  unsigned long sequence;      /* number of the open transaction */
  unsigned long log_pages;     /* log pages in use, superblocks included */
  unsigned long logged_pages;  /* pages whose latest version is in the log */
  unsigned long commits;       /* commits in this session */
  unsigned long checkpoints;   /* checkpoints in this session */
  unsigned long recovered;     /* transactions replayed when opening */
  ds_status_t last_checkpoint; /* result of the latest checkpoint attempt */
} ds_wal_store_stats_t;

/* checkpoint_pages 1024, index NULL */
void ds_wal_store_config_init(ds_wal_store_config_t *config);
/* opens (recovering) or formats the store; config NULL: defaults */
ds_status_t ds_page_store_wal(ds_page_store_t *store, ds_page_store_t *data,
                              ds_page_store_t *log,
                              const ds_wal_store_config_t *config,
                              ds_context_t *context);
/* commits the open transaction, then checkpoints */
ds_status_t ds_wal_store_checkpoint(ds_page_store_t *store);
ds_status_t ds_wal_store_stats(const ds_page_store_t *store,
                               ds_wal_store_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_WAL_STORE_H */
