#ifndef DS_XFAST_TRIE_H
#define DS_XFAST_TRIE_H

/*
 * X-fast tries (Willard): predecessor search over w-bit integer keys
 * (w <= bits of unsigned long), values stored by value.
 *
 * Every prefix of every key lives in a per-level hash table (level l holds
 * the l-bit prefixes, level w the keys themselves), each internal prefix
 * remembering the smallest and largest key below it, and the keys form a
 * doubly linked list. A query binary-searches the levels for the longest
 * stored prefix of x, O(log w) table lookups, then reads the answer from
 * that prefix's min/max and the list. Insertions and removals touch every
 * level, O(w); space is O(n w).
 *
 * The per-level tables are a ds_map_ops_t chosen at init (Robin Hood by
 * default, or the Swiss table, or any adapted map), so the bounds are
 * those of the chosen map: expected O(log w) queries and O(w) updates for
 * the hashed backends. Updates are atomic: an allocation failure undoes
 * the levels already changed.
 */

#include "context.h"
#include "map_iface.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_xfast {
  unsigned bits;
  size_t value_size;
  size_t leaf_size; /* link header + value */
  ds_map_t *levels; /* bits + 1 tables */
  size_t count;
  ds_context_t *context;
} ds_xfast_t;

/* bits in 1 .. bits of unsigned long; ops NULL = Robin Hood */
ds_status_t ds_xfast_init(ds_xfast_t *t, unsigned bits, size_t value_size,
                          const ds_map_ops_t *ops, ds_context_t *context);
void ds_xfast_destroy(ds_xfast_t *t);
size_t ds_xfast_size(const ds_xfast_t *t);
/* DS_EXISTS (value replaced) or DS_OK; DS_ERR_RANGE for keys >= 2^bits */
ds_status_t ds_xfast_insert(ds_xfast_t *t, unsigned long key,
                            const void *value);
ds_status_t ds_xfast_remove(ds_xfast_t *t, unsigned long key, void *out_value);
/* value slot of key, NULL if absent */
void *ds_xfast_find(const ds_xfast_t *t, unsigned long key);
/* largest key <= x / < x, smallest key >= x / > x: DS_NOT_FOUND if none */
ds_status_t ds_xfast_floor(const ds_xfast_t *t, unsigned long x,
                           unsigned long *out);
ds_status_t ds_xfast_predecessor(const ds_xfast_t *t, unsigned long x,
                                 unsigned long *out);
ds_status_t ds_xfast_ceiling(const ds_xfast_t *t, unsigned long x,
                             unsigned long *out);
ds_status_t ds_xfast_successor(const ds_xfast_t *t, unsigned long x,
                               unsigned long *out);
ds_status_t ds_xfast_min(const ds_xfast_t *t, unsigned long *out);
ds_status_t ds_xfast_max(const ds_xfast_t *t, unsigned long *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_XFAST_TRIE_H */
