#ifndef DS_IBLT_H
#define DS_IBLT_H

/*
 * Invertible Bloom lookup table (Goodrich and Mitzenmacher, 2011), with
 * fixed-size keys and values stored by value.
 *
 * Each entry is added into `hashes` cells (one per sub-table, so never
 * twice into the same cell); a cell keeps a count and the XOR of the keys,
 * the values and a check hash of the keys it holds. Insert and erase are
 * O(hashes) and never fail; a lookup succeeds whenever one of the key's
 * cells holds only that key. As long as the number of entries stays below
 * about 0.8 of the cell count (for 3 or 4 hashes), the whole content can
 * be listed by "peeling" pure cells.
 *
 * Erasing entries that were never inserted is allowed, which is what makes
 * set reconciliation work: subtract the IBLT of set B from the one of set A
 * (same configuration and seed) and list the result: entries of A - B come
 * out with sign +1 and entries of B - A with sign -1, in space proportional
 * to the difference, not to the sets.
 *
 * Storage: dynamic (ds_iblt_init) through a ds_context_t, or one caller
 * block of ds_iblt_buffer_size() bytes (ds_iblt_init_buffer).
 * Concurrency: a table may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hash of key bytes with a seed (NULL: MurmurHash3). */
typedef unsigned long (*ds_iblt_hash_func_t)(const void *key, size_t length,
                                             unsigned long seed, void *user);
/* Listing callback: sign is +1 (inserted) or -1 (erased without insert). */
typedef ds_status_t (*ds_iblt_visit_func_t)(const void *key, const void *value,
                                            int sign, void *user);

typedef struct ds_iblt_config {
  size_t key_size;
  size_t value_size;   /* may be 0 */
  size_t cells;        /* rounded up to a multiple of hashes */
  unsigned int hashes; /* 2..8, default 3 */
  unsigned long seed;
  ds_iblt_hash_func_t hash;
  void *user;
  ds_context_t *context;
} ds_iblt_config_t;

typedef struct ds_iblt {
  long *counts;
  unsigned long *hash_sums;
  unsigned char *key_sums;
  unsigned char *value_sums;
  size_t cells;
  ds_iblt_config_t config;
  void *block;
  bool fixed;
} ds_iblt_t;

void ds_iblt_config_init(ds_iblt_config_t *config);
size_t ds_iblt_buffer_size(const ds_iblt_config_t *config);
ds_status_t ds_iblt_init(ds_iblt_t *iblt, const ds_iblt_config_t *config);
ds_status_t ds_iblt_init_buffer(ds_iblt_t *iblt, const ds_iblt_config_t *config,
                                void *buffer);
void ds_iblt_destroy(ds_iblt_t *iblt);
void ds_iblt_clear(ds_iblt_t *iblt);

void ds_iblt_insert(ds_iblt_t *iblt, const void *key, const void *value);
void ds_iblt_erase(ds_iblt_t *iblt, const void *key, const void *value);
/* DS_OK (value copied out), DS_NOT_FOUND (certainly absent) or DS_ERR_STATE
 * (cannot tell: every cell of the key is shared). */
ds_status_t ds_iblt_get(const ds_iblt_t *iblt, const void *key,
                        void *out_value);
/* iblt -= other (cell by cell). Same configuration required. */
ds_status_t ds_iblt_subtract(ds_iblt_t *iblt, const ds_iblt_t *other);
/* List every entry by peeling a scratch copy (from the context). Returns
 * DS_OK when everything was listed, DS_ERR_STATE when entries remain
 * (too full), or the visitor's status if it stopped the walk. */
ds_status_t ds_iblt_list(const ds_iblt_t *iblt, ds_iblt_visit_func_t visit,
                         void *user);
/* The same, peeling the table itself (no allocation; it is left with the
 * entries that could not be listed). */
ds_status_t ds_iblt_peel(ds_iblt_t *iblt, ds_iblt_visit_func_t visit,
                         void *user);
bool ds_iblt_is_empty(const ds_iblt_t *iblt);

#ifdef __cplusplus
}
#endif

#endif /* DS_IBLT_H */
