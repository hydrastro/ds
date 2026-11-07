#ifndef DS_SSTABLE_H
#define DS_SSTABLE_H

/*
 * SSTables (sorted string tables): immutable dictionaries of byte-string
 * keys and values, the files of log-structured merge (LSM) trees.
 *
 * Layout (all integers little-endian, lengths as LEB128 varints):
 *   data blocks   key/value entries in key order, each key stored as the
 *                 length it shares with the previous key plus the rest
 *                 (prefix compression), with a restart point (a full key)
 *                 every `restart_interval` entries and the restart offsets
 *                 at the end of the block, so a block is searched by
 *                 binary search over its restarts, then a short scan
 *   index block   the last key of each data block with its offset and size
 *   filter block  optional Bloom filter over all keys (bits_per_key)
 *   footer        index and filter positions, entry count, an FNV-1a
 *                 checksum of everything before it, and a magic number
 * Keys order as bytes (memcmp, shorter prefix first). A value may be a
 * tombstone (a deletion marker for merging).
 *
 * ds_sstable_builder_t writes a table into memory from keys added in
 * strictly increasing order; ds_sstable_t reads one in place (zero-copy)
 * with point lookups, a filter probe and iterators with seek;
 * ds_sstable_merge compacts several tables (newest first: on equal keys
 * the newest entry wins) into one, optionally dropping tombstones, as an
 * LSM compaction does. Tables are limited to 4 GiB.
 *
 * Memory comes from the ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_sstable_builder {
  unsigned char *buf; /* the table so far */
  size_t len;
  size_t cap;
  size_t block_size;
  size_t restart_interval;
  size_t bits_per_key;
  /* current data block */
  size_t block_start;
  size_t block_entries;
  unsigned long *restarts; /* offsets within the block */
  size_t restart_count;
  size_t restart_cap;
  unsigned char *last_key;
  size_t last_len;
  size_t last_cap;
  bool has_last;
  /* index entries: last key, offset, size */
  unsigned char *index; /* encoded index block entries */
  size_t index_len;
  size_t index_cap;
  size_t index_entries;
  /* key hashes for the filter */
  unsigned long *hashes;
  size_t hash_count;
  size_t hash_cap;
  size_t entries;
  ds_status_t error;
  ds_context_t *context;
} ds_sstable_builder_t;

/* block_size (0: 4096), restart_interval >= 1 (0: 16), bits_per_key (0:
 * no filter, at most 32). */
ds_status_t ds_sstable_builder_init(ds_sstable_builder_t *b, size_t block_size,
                                    size_t restart_interval,
                                    size_t bits_per_key, ds_context_t *context);
void ds_sstable_builder_destroy(ds_sstable_builder_t *b);
/* Keys strictly increasing (DS_ERR_INVALID otherwise). */
ds_status_t ds_sstable_builder_add(ds_sstable_builder_t *b, const void *key,
                                   size_t key_len, const void *value,
                                   size_t value_len);
ds_status_t ds_sstable_builder_add_tombstone(ds_sstable_builder_t *b,
                                             const void *key, size_t key_len);
/* The finished table: *out (from the context, freed by the caller with
 * ds_context_free) and its length. The builder is spent afterwards. */
ds_status_t ds_sstable_builder_finish(ds_sstable_builder_t *b,
                                      unsigned char **out, size_t *out_len);

typedef struct ds_sstable {
  const unsigned char *data;
  size_t len;
  size_t index_off;
  size_t index_len;
  size_t filter_off;
  size_t filter_len;
  size_t entries;
} ds_sstable_t;

/* Checks the footer and checksum; the bytes must outlive the table. */
ds_status_t ds_sstable_open(ds_sstable_t *t, const void *data, size_t len);
size_t ds_sstable_entries(const ds_sstable_t *t);
size_t ds_sstable_blocks(const ds_sstable_t *t);
/* false when the filter rules the key out (true without a filter). */
bool ds_sstable_may_contain(const ds_sstable_t *t, const void *key,
                            size_t key_len);
/* DS_OK with the value (pointing into the table) or DS_NOT_FOUND; a
 * tombstone is found with *out_tombstone set. */
ds_status_t ds_sstable_get(const ds_sstable_t *t, const void *key,
                           size_t key_len, const unsigned char **out_value,
                           size_t *out_value_len, bool *out_tombstone,
                           ds_context_t *context);

typedef struct ds_sstable_iter {
  const ds_sstable_t *table;
  size_t block;       /* index entry */
  size_t block_off;   /* data block start */
  size_t block_len;   /* entries part (before the restarts) */
  size_t pos;         /* next entry offset within the block */
  unsigned char *key; /* decoded current key */
  size_t key_len;
  size_t key_cap;
  const unsigned char *value;
  size_t value_len;
  bool tombstone;
  bool valid;
  ds_status_t error;
  ds_context_t *context;
} ds_sstable_iter_t;

ds_status_t ds_sstable_iter_init(ds_sstable_iter_t *it, const ds_sstable_t *t,
                                 ds_context_t *context);
void ds_sstable_iter_destroy(ds_sstable_iter_t *it);
ds_status_t ds_sstable_iter_first(ds_sstable_iter_t *it);
/* The first entry with key >= the given key. */
ds_status_t ds_sstable_iter_seek(ds_sstable_iter_t *it, const void *key,
                                 size_t key_len);
ds_status_t ds_sstable_iter_next(ds_sstable_iter_t *it);
bool ds_sstable_iter_valid(const ds_sstable_iter_t *it);

/* tables[0] is the newest; the merged table uses the builder settings. */
ds_status_t ds_sstable_merge(const ds_sstable_t *tables, size_t n,
                             bool drop_tombstones, size_t block_size,
                             size_t restart_interval, size_t bits_per_key,
                             unsigned char **out, size_t *out_len,
                             ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_SSTABLE_H */
