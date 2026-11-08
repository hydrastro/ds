#ifndef DS_CONFLUENT_LIST_H
#define DS_CONFLUENT_LIST_H

/*
 * Confluently persistent lists (sequences).
 *
 * Every version is immutable and stays valid; updates return new
 * versions sharing structure with old ones. Unlike fully persistent
 * structures, versions can also be combined: any two versions, including
 * a version with itself, can be concatenated, so the version graph is a
 * DAG and lengths can grow exponentially (a version concatenated with
 * itself k times has 2^k times the length; DS_ERR_OVERFLOW when the
 * length no longer fits in a size_t).
 *
 * Versions are reference-counted, height-balanced (AVL) trees over the
 * positions, with elements stored by value in a ds_confluent_store_t.
 * Concatenation is an AVL join, O(|h1 - h2| + log n); split, insert,
 * erase and set copy one path, O(log n) (n being the length, so also
 * fine for exponentially long versions); get is a descent. Only the
 * created nodes are allocated, so k self-concatenations cost O(k log)
 * memory for a length-2^k list.
 *
 * Ownership: functions returning a version hand over one reference
 * (ds_confluent_release); versions passed in are borrowed.
 * DS_CONFLUENT_EMPTY is the empty list.
 */

#include "context.h"
#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_CONFLUENT_EMPTY (~(size_t)0)

typedef struct ds_confluent_store {
  size_t elem_size;
  unsigned char *value;
  size_t *left;
  size_t *right;
  size_t *size;
  size_t *refs; /* free nodes: next free in left */
  unsigned char *height;
  size_t nodes;
  size_t capacity;
  size_t free_list;
  size_t live;
  size_t *work;
  size_t work_capacity;
  ds_context_t *context;
} ds_confluent_store_t;

ds_status_t ds_confluent_store_init(ds_confluent_store_t *s, size_t elem_size,
                                    ds_context_t *context);
void ds_confluent_store_destroy(ds_confluent_store_t *s);
/* live tree nodes (shared nodes count once) */
size_t ds_confluent_store_nodes(const ds_confluent_store_t *s);

void ds_confluent_retain(ds_confluent_store_t *s, size_t list);
void ds_confluent_release(ds_confluent_store_t *s, size_t list);
size_t ds_confluent_length(const ds_confluent_store_t *s, size_t list);
unsigned ds_confluent_height(const ds_confluent_store_t *s, size_t list);

ds_status_t ds_confluent_from_array(ds_confluent_store_t *s, const void *elems,
                                    size_t count, size_t *out);
const void *ds_confluent_get(const ds_confluent_store_t *s, size_t list,
                             size_t index);
ds_status_t ds_confluent_set(ds_confluent_store_t *s, size_t list, size_t index,
                             const void *elem, size_t *out);
ds_status_t ds_confluent_insert(ds_confluent_store_t *s, size_t list,
                                size_t index, const void *elem, size_t *out);
ds_status_t ds_confluent_push_front(ds_confluent_store_t *s, size_t list,
                                    const void *elem, size_t *out);
ds_status_t ds_confluent_push_back(ds_confluent_store_t *s, size_t list,
                                   const void *elem, size_t *out);
ds_status_t ds_confluent_erase(ds_confluent_store_t *s, size_t list,
                               size_t index, size_t *out);
ds_status_t ds_confluent_concat(ds_confluent_store_t *s, size_t a, size_t b,
                                size_t *out);
/* out_left: the first index elements, out_right: the rest */
ds_status_t ds_confluent_split(ds_confluent_store_t *s, size_t list,
                               size_t index, size_t *out_left,
                               size_t *out_right);
/* elements [from, from + count) as a new version */
ds_status_t ds_confluent_slice(ds_confluent_store_t *s, size_t list,
                               size_t from, size_t count, size_t *out);
/* copy elements [from, from + count) out, in order */
ds_status_t ds_confluent_read(const ds_confluent_store_t *s, size_t list,
                              size_t from, size_t count, void *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_CONFLUENT_LIST_H */
