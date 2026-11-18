#ifndef DS_LSM_TREE_H
#define DS_LSM_TREE_H

/*
 * Log-structured merge tree (O'Neil et al., 1996) over byte-string keys,
 * assembled from dsa/structures/sstable.h:
 *
 *   memtable   sorted in memory, absorbing puts and deletes (tombstones):
 *              a skip list (DS_LSM_MEMTABLE_SKIPLIST, the default:
 *              O(log n) expected per put) or a sorted array of entry
 *              pointers (DS_LSM_MEMTABLE_ARRAY: binary search, O(n)
 *              pointer moves per new key, compact for small memtables)
 *   level 0    the most recent flushed tables, newest first, ranges may
 *              overlap; when more than l0_tables accumulate they are
 *              merged with level 1
 *   level i>0  one sorted table each (a leveled run); when level i grows
 *              past memtable_bytes * l0_tables * fanout^(i-1) bytes it is
 *              merged into level i+1
 *
 * Merges use ds_sstable_merge (newest wins on equal keys); tombstones are
 * dropped only when merging into the deepest occupied level. A lookup
 * checks the memtable, then each table newest to oldest, consulting the
 * table's filter first: the built-in Bloom filter (bits_per_key, or
 * DS_LSM_NO_FILTER for none) or an AMQ filter from the backend in
 * config.filter (cuckoo, quotient, xor or any backend with images, see
 * dsa/structures/amq_iface.h), loaded when the table is opened.
 * Iterators merge every source in key order with the same precedence and
 * skip deleted keys.
 *
 * Key order: config.comparator (NULL: bytewise) orders the memtable, the
 * merges and the iterators, and is recorded in every table it writes (see
 * dsa/structures/sstable.h); the comparator is copied into lsm->cmp at init and
 * the caller's pointer is not kept (config.comparator reads NULL afterwards).
 * Tables with a custom comparator carry filters only with
 * config.key_hash, a hash consistent with it (copied the same way into
 * lsm->key_hash).
 *
 * Budgets are in bytes: memtable_bytes counts each entry's key, value and
 * entry header (not the skip-list links), and level i > 0 may hold
 * memtable_bytes * l0_tables * fanout^(i-1) bytes of table data; level 0
 * is bounded by its table count, l0_tables.
 *
 * Persistence (config.store, a dsa/structures/pager.h page store; NULL keeps
 * everything in memory): every table is also written to a run of pages, a
 * manifest lists the tables, and a write-ahead log keeps the updates since the
 * last flush, so ds_lsm_init on a store that holds a tree recovers it. log each
 * put and delete is appended to the log; ds_lsm_sync writes the pending records
 * and syncs the store (sync_every N does it after every N updates). A log page
 * is never overwritten in place while it holds synced records: the page being
 * filled alternates between two locations, with a version and a checksum, and
 * full pages are sealed, so a torn or lost write costs at most the records
 * after the last sync flush     writes the new tables (from the flush and the
 * compactions it triggers), then a new manifest, syncs, and only then publishes
 * it by writing one of two alternating slot pages (pages 0 and 1, with a
 * sequence number and checksum) and syncing again; the log restarts empty space
 * pages referenced by the published manifest or by the one before it (the
 * fallback if the newest slot is torn) are never reused; other pages are reused
 * first fit, and the store is truncated past the last page in use recovery
 * ds_lsm_init reads both slots, takes the newest whose manifest checks out,
 * loads its tables and replays the log up to the first page that is missing,
 * torn or not sealed before a later one; replayed updates are flushed into a
 * table at once. The result is the state after some prefix of the updates,
 * including every update before the last completed ds_lsm_sync or flush Read
 * failures from the page store abort recovery; only invalid on-disk images
 * trigger fallback to an older slot or truncate the recovered log. Overlapping
 * page runs and cyclic log chains are rejected. Log page versions seal before
 * their 32-bit field wraps; exhausting a sequence or page-index field returns
 * DS_ERR_OVERFLOW instead of wrapping. After a failed log sync, a retry
 * completes the store sync before another log version may reuse the location of
 * the last successfully synced image. Durability is only as good as the store's
 * sync (dsa/structures/pager.h's DS_PAGE_STORE_DURABLE_SYNC trait: the stdio
 * file store's sync is fflush, which survives a crash of the process, not of
 * the machine). An empty store starts a new tree: an initialization marker goes
 * to slot 0's page first, so a store where that start was cut short (an
 * identifiable marker and no valid slot: nothing committed) starts over, while
 * a store with neither is DS_ERR_INVALID, never overwritten. A torn marker
 * whose complete magic did not land, including an all-zero page, is
 * unidentifiable and is refused; recovery cannot distinguish it from a foreign
 * file. By default tables are loaded into memory. With out_of_core, opening
 * streams checksum verification and retains only metadata; lookups and
 * iterators read blocks through per-table pagers. Flush/compaction output
 * is still built in memory. Updates since the last sync are lost at
 * ds_lsm_destroy unless synced first.
 *
 * Values returned by get last until mutation, and in out_of_core mode until
 * the next get. Iterator views last until that iterator moves or the tree is
 * modified. Memory comes from the context.
 */

#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <dsa/structures/pager.h>
#include <dsa/structures/sstable.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_LSM_MAX_LEVELS 24
#define DS_LSM_NO_FILTER ((size_t) - 1) /* bits_per_key: no Bloom filter */

typedef enum ds_lsm_memtable {
  DS_LSM_MEMTABLE_SKIPLIST = 0,
  DS_LSM_MEMTABLE_ARRAY = 1
} ds_lsm_memtable_t;

typedef struct ds_lsm_config {
  size_t memtable_bytes; /* 0: 1 MiB */
  size_t l0_tables;      /* 0: 4, at most 63 */
  size_t fanout;         /* 0: 10 */
  size_t block_size;     /* sstable settings (0: defaults) */
  size_t restart_interval;
  size_t bits_per_key; /* 0: 10; DS_LSM_NO_FILTER: none */
  /* an AMQ backend for the table filters instead of the built-in Bloom
   * filter (bits_per_key must then be 0), with its fp_bits (0: default) */
  const ds_amq_ops_t *filter;
  unsigned filter_fp_bits;
  ds_lsm_memtable_t memtable;
  const ds_sstable_comparator_t *comparator; /* NULL: bytewise */
  /* with a custom comparator, a key hash consistent with it, so tables
   * keep their filters (dsa/structures/sstable.h); NULL: no filters for custom
   * orders */
  const ds_sstable_key_hash_t *key_hash;
  ds_context_t *context;
  /* persistence (see above): a page store with 128 .. 2^32-1-byte pages, and
   * how many updates trigger a sync of the log (0: only ds_lsm_sync and
   * flushes) */
  ds_page_store_t *store;
  size_t sync_every;
  /* Opt-in bounded data reads. Requires store. Retains table metadata and
   * reads blocks through a per-table pager; zero frames means two. The
   * map/replacement capabilities default to Robin Hood/LRU. Per-table caches
   * cannot retain stale pages after a retired extent is reused. */
  bool out_of_core;
  size_t read_cache_frames;
  const ds_map_ops_t *read_cache_map;
  const ds_replacement_ops_t *read_cache_policy;
  size_t max_table_metadata_bytes; /* zero: format limit */
  size_t max_table_block_bytes;    /* zero: format limit */
} ds_lsm_config_t;

struct ds_lsm_table_reader;

typedef struct ds_lsm_table {
  unsigned char *data;
  size_t len;
  ds_sstable_t table;
  unsigned long page; /* where the store keeps it (pages > 0) */
  unsigned long pages;
  struct ds_lsm_table_reader *reader; /* owned pager and source state */
} ds_lsm_table_t;

/* pages of the store in use, and the newest manifest referencing them */
typedef struct ds_lsm_extent {
  unsigned long start;
  unsigned long pages;
  unsigned long seq;
} ds_lsm_extent_t;

typedef struct ds_lsm {
  ds_lsm_config_t config;
  ds_sstable_comparator_t cmp;    /* copied from config.comparator */
  ds_sstable_key_hash_t key_hash; /* copied from config.key_hash */
  void **mem;                     /* array memtable: sorted entries */
  size_t mem_count;
  size_t mem_capacity;
  size_t mem_bytes;
  void *skip_head; /* skip-list memtable */
  size_t skip_level;
  unsigned long skip_rng;
  ds_lsm_table_t *l0; /* newest first */
  size_t l0_count;
  ds_lsm_table_t levels[DS_LSM_MAX_LEVELS]; /* levels[0] is level 1 */
  unsigned long flushes;
  unsigned long compactions;
  bool sync_failed; /* retry the store sync before overwriting a log image */
  /* persistence: the published manifest's sequence, the pages in use, and
   * the log (records not yet written, the page being filled) */
  unsigned long seq;
  ds_lsm_extent_t *extents;
  size_t extent_count;
  size_t extent_capacity;
  unsigned long log_id;    /* the sequence that started the log */
  unsigned long log_head;  /* first page pair */
  unsigned long log_slot;  /* page pair being filled */
  unsigned long log_index; /* its position in the chain */
  unsigned long log_next;  /* the pair that follows it */
  unsigned long log_version;
  int log_written;         /* the page being filled has been written */
  int log_dirty;           /* and holds records since */
  unsigned char *log_page; /* its image */
  size_t log_used;         /* payload bytes in it */
  unsigned char *pending;  /* records not yet written */
  size_t pending_len;
  size_t pending_capacity;
  size_t unsynced; /* updates since the last sync */
  int replaying;
  int broken; /* a publish failed after its slot write: reopen the tree */
} ds_lsm_t;

typedef struct ds_lsm_iter {
  ds_lsm_t *lsm;
  size_t sources;
  ds_sstable_iter_t *iters; /* one per table */
  size_t mem_pos;           /* array memtable cursor */
  void *mem_node;           /* skip-list memtable cursor */
  size_t current;           /* winning source, (size_t)-1 when exhausted */
  const unsigned char *key;
  size_t key_len;
  const unsigned char *value;
  size_t value_len;
  ds_status_t error;
} ds_lsm_iter_t;

void ds_lsm_config_init(ds_lsm_config_t *config);
/* DS_ERR_INVALID for an unknown memtable kind, an invalid comparator, or
 * both bits_per_key and filter; DS_ERR_UNSUPPORTED for a filter backend
 * without images */
ds_status_t ds_lsm_init(ds_lsm_t *lsm, const ds_lsm_config_t *config);
void ds_lsm_destroy(ds_lsm_t *lsm);
/* Persistence errors from an automatic flush or sync are returned. The
 * update may already be visible in memory: an error is not a rollback.
 * Retry sync/flush unless broken is set; an ambiguous manifest publication
 * marks the tree broken and requires destroy/reopen before more writes.
 * No crash durability beyond the page store's sync trait is promised. */
ds_status_t ds_lsm_put(ds_lsm_t *lsm, const void *key, size_t key_len,
                       const void *value, size_t value_len);
ds_status_t ds_lsm_delete(ds_lsm_t *lsm, const void *key, size_t key_len);
/* Returned bytes are borrowed. With out_of_core, valid until the next get
 * or any mutation/destroy; otherwise until mutation/destroy. Reads and writes
 * on the same tree require external synchronization. */
ds_status_t ds_lsm_get(ds_lsm_t *lsm, const void *key, size_t key_len,
                       const unsigned char **out_value, size_t *out_len);
/* move the memtable into level 0 (and compact as needed); with a store,
 * also publishes the new tables (see above). Retrying after an unpublished
 * flush also works when its memtable has already moved into level 0. */
ds_status_t ds_lsm_flush(ds_lsm_t *lsm);
/* with a store: write the log records not yet written and sync, so every
 * update so far is covered by the store's sync guarantee; DS_OK without a
 * store. A broken tree returns DS_ERR_STATE, including for an empty flush. */
ds_status_t ds_lsm_sync(ds_lsm_t *lsm);
/* merge everything into one table at the deepest level */
ds_status_t ds_lsm_compact_all(ds_lsm_t *lsm);
size_t ds_lsm_table_count(const ds_lsm_t *lsm);

ds_status_t ds_lsm_iter_init(ds_lsm_iter_t *it, ds_lsm_t *lsm);
void ds_lsm_iter_destroy(ds_lsm_iter_t *it);
ds_status_t ds_lsm_iter_first(ds_lsm_iter_t *it);
ds_status_t ds_lsm_iter_seek(ds_lsm_iter_t *it, const void *key,
                             size_t key_len);
ds_status_t ds_lsm_iter_next(ds_lsm_iter_t *it);
bool ds_lsm_iter_valid(const ds_lsm_iter_t *it);

#ifdef __cplusplus
}
#endif

#endif /* DS_LSM_TREE_H */
