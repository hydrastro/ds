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
 *   footer        index and filter positions, entry count, comparator id,
 *                 a CRC-32C of everything before the checksum (data and
 *                 the footer's own fields) and a magic number
 * Keys order by a comparator: bytes (memcmp, shorter prefix first) by
 * default, or a ds_sstable_comparator_t whose id is recorded in the
 * footer, so a table is only opened with the order it was written in. A
 * value may be a tombstone (a deletion marker for merging).
 *
 * Format versions: version 2 (written now) checksums the footer fields
 * with CRC-32C; version 1 tables (FNV-1a over the data only, bytewise
 * keys) are still read.
 *
 * Untrusted input: every length, offset and count read from a table is
 * checked against the table's bounds with overflow-checked arithmetic
 * before use, and no read path recurses on table contents, so any byte
 * string is safe to open, search and iterate: malformed tables yield
 * DS_ERR_INVALID, never an out-of-bounds read. The checksum detects
 * accidental corruption only; it is not a signature. ds_sstable_open
 * checks the footer, checksum and region layout in O(len);
 * ds_sstable_verify additionally checks the whole structure (block
 * tiling, restart points, key order within and across blocks, index keys,
 * entry count, optionally the filter), so a table that verifies is one
 * the builder could have written. Without verification a corrupted but
 * checksum-matching table can only give wrong answers, not unsafe reads.
 *
 * ds_sstable_builder_t writes a table into memory from keys added in
 * strictly increasing order; ds_sstable_t reads one in place (zero-copy)
 * with point lookups, a filter probe and iterators with seek;
 * ds_sstable_merge compacts several tables (newest first: on equal keys
 * the newest entry wins) into one, optionally dropping tombstones, as an
 * LSM compaction does. Tables are limited to 2 GiB (DS_SSTABLE_MAX_LEN).
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

#define DS_SSTABLE_MAX_LEN 0x7FFFFFFFUL

/* Key order. compare returns <0, 0, >0 like memcmp; it must be a strict
 * weak order consistent across runs, since tables persist it. id names
 * the order in the footer (0 is reserved for the bytewise order); tables
 * and readers must agree on it. Copied into builders and tables. */
typedef int (*ds_sstable_cmp_fn)(const unsigned char *a, size_t a_len,
                                 const unsigned char *b, size_t b_len,
                                 void *user);
typedef struct ds_sstable_comparator {
  ds_sstable_cmp_fn compare;
  void *user;
  unsigned long id;
} ds_sstable_comparator_t;

/* the default: bytes in memcmp order, shorter prefix first; id 0 */
const ds_sstable_comparator_t *ds_sstable_bytewise(void);

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
  ds_sstable_comparator_t cmp;
  ds_context_t *context;
} ds_sstable_builder_t;

/* block_size (0: 4096), restart_interval >= 1 (0: 16), bits_per_key (0:
 * no filter, at most 32). */
ds_status_t ds_sstable_builder_init(ds_sstable_builder_t *b, size_t block_size,
                                    size_t restart_interval,
                                    size_t bits_per_key, ds_context_t *context);
void ds_sstable_builder_destroy(ds_sstable_builder_t *b);
/* the same with a key order (NULL: bytewise); DS_ERR_INVALID for a
 * comparator without compare or a non-bytewise one with id 0 */
ds_status_t ds_sstable_builder_init_cmp(ds_sstable_builder_t *b,
                                        size_t block_size,
                                        size_t restart_interval,
                                        size_t bits_per_key,
                                        const ds_sstable_comparator_t *cmp,
                                        ds_context_t *context);
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
  unsigned version; /* format version, 1 or 2 */
  ds_sstable_comparator_t cmp;
} ds_sstable_t;

/* Checks the footer, checksum and region layout (bytewise order); the
 * bytes must outlive the table. DS_ERR_INVALID for anything malformed or
 * for a table written with another comparator. */
ds_status_t ds_sstable_open(ds_sstable_t *t, const void *data, size_t len);
/* the same with the table's key order (NULL: bytewise) */
ds_status_t ds_sstable_open_cmp(ds_sstable_t *t, const void *data, size_t len,
                                const ds_sstable_comparator_t *cmp);
/* full structural check in O(len) plus O(longest key) memory: DS_OK,
 * DS_ERR_INVALID, or DS_ERR_ALLOC; check_filter also probes every key */
ds_status_t ds_sstable_verify(const ds_sstable_t *t, bool check_filter,
                              ds_context_t *context);
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

/* tables[0] is the newest; the merged table uses the builder settings and
 * the tables' key order, which must be the same for all of them
 * (DS_ERR_INVALID otherwise). */
ds_status_t ds_sstable_merge(const ds_sstable_t *tables, size_t n,
                             bool drop_tombstones, size_t block_size,
                             size_t restart_interval, size_t bits_per_key,
                             unsigned char **out, size_t *out_len,
                             ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_SSTABLE_H */
