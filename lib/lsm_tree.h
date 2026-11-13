#ifndef DS_LSM_TREE_H
#define DS_LSM_TREE_H

/*
 * Log-structured merge tree (O'Neil et al., 1996) over byte-string keys,
 * assembled from lib/sstable:
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
 * lib/amq_iface), loaded when the table is opened.
 * Iterators merge every source in key order with the same precedence and
 * skip deleted keys.
 *
 * Key order: config.comparator (NULL: bytewise) orders the memtable, the
 * merges and the iterators, and is recorded in every table it writes (see
 * lib/sstable); the comparator is copied into lsm->cmp at init and the
 * caller's pointer is not kept (config.comparator reads NULL afterwards).
 *
 * Budgets are in bytes: memtable_bytes counts each entry's key, value and
 * entry header (not the skip-list links), and level i > 0 may hold
 * memtable_bytes * l0_tables * fanout^(i-1) bytes of table data; level 0
 * is bounded by its table count, l0_tables.
 *
 * Scope: tables live in memory. There is no write-ahead log, manifest or
 * recovery, and compaction does not go through storage (a store-backed
 * variant would write finished table bytes through a ds_page_store_t), so
 * the structure is an in-memory LSM organization, not a durable store.
 * Values returned by get and by iterators stay valid until the next
 * modification. Memory comes from the context.
 */

#include "context.h"
#include "sstable.h"
#include "status.h"
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
  ds_context_t *context;
} ds_lsm_config_t;

typedef struct ds_lsm_table {
  unsigned char *data;
  size_t len;
  ds_sstable_t table;
} ds_lsm_table_t;

typedef struct ds_lsm {
  ds_lsm_config_t config;
  ds_sstable_comparator_t cmp; /* copied from config.comparator */
  void **mem;                  /* array memtable: sorted entries */
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
ds_status_t ds_lsm_put(ds_lsm_t *lsm, const void *key, size_t key_len,
                       const void *value, size_t value_len);
ds_status_t ds_lsm_delete(ds_lsm_t *lsm, const void *key, size_t key_len);
ds_status_t ds_lsm_get(ds_lsm_t *lsm, const void *key, size_t key_len,
                       const unsigned char **out_value, size_t *out_len);
/* move the memtable into level 0 (and compact as needed) */
ds_status_t ds_lsm_flush(ds_lsm_t *lsm);
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
