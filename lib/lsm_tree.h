#ifndef DS_LSM_TREE_H
#define DS_LSM_TREE_H

/*
 * Log-structured merge tree (O'Neil et al., 1996) over byte-string keys,
 * assembled from lib/sstable:
 *
 *   memtable   sorted in memory (binary search over an array of entries),
 *              absorbing puts and deletes (tombstones)
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
 * Bloom filter first. Iterators merge every source in key order with the
 * same precedence and skip deleted keys.
 *
 * Tables live in memory (a store-backed variant would write the finished
 * table bytes through a ds_page_store_t). Values returned by get and by
 * iterators stay valid until the next modification. Memory comes from the
 * context.
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

typedef struct ds_lsm_config {
  size_t memtable_bytes; /* 0: 1 MiB */
  size_t l0_tables;      /* 0: 4, at most 63 */
  size_t fanout;         /* 0: 10 */
  size_t block_size;     /* sstable settings (0: defaults) */
  size_t restart_interval;
  size_t bits_per_key; /* 0: 10 */
  ds_context_t *context;
} ds_lsm_config_t;

typedef struct ds_lsm_table {
  unsigned char *data;
  size_t len;
  ds_sstable_t table;
} ds_lsm_table_t;

typedef struct ds_lsm {
  ds_lsm_config_t config;
  void **mem; /* sorted memtable entries */
  size_t mem_count;
  size_t mem_capacity;
  size_t mem_bytes;
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
  size_t mem_pos;
  size_t current; /* winning source, (size_t)-1 when exhausted */
  const unsigned char *key;
  size_t key_len;
  const unsigned char *value;
  size_t value_len;
  ds_status_t error;
} ds_lsm_iter_t;

void ds_lsm_config_init(ds_lsm_config_t *config);
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
