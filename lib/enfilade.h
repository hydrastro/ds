#ifndef DS_ENFILADE_H
#define DS_ENFILADE_H

/*
 * Enfilades (Xanadu's model-T enfilade): a sequence of items, each with a
 * width ("wid"), kept in a balanced tree whose nodes summarise the widths
 * below them, so an item's displacement ("dsp", the total width before
 * it) is accumulated on the way down. Retrieval by address, insertion and
 * deletion anywhere, width changes, and the enfilade's signature
 * operation, rearrangement (cut a range and paste it elsewhere), all take
 * O(log n) expected time.
 *
 * Built on a treap keyed implicitly by position (split and join), with
 * subtree counts and width sums; items are stored by value. The random
 * priorities come from an internal generator.
 *
 * Concurrency: an enfilade may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_enfilade {
  unsigned char *data;
  size_t *wid; /* per item */
  size_t *sum; /* subtree width */
  size_t *cnt; /* subtree items */
  size_t *left;
  size_t *right;
  unsigned long *prio;
  size_t root;
  size_t slots;
  size_t capacity;
  size_t free_list;
  size_t elem_size;
  unsigned long seed;
  ds_context_t *context;
} ds_enfilade_t;

typedef bool (*ds_enfilade_visit_func_t)(const void *elem, size_t wid,
                                         size_t dsp, void *user);

ds_status_t ds_enfilade_init(ds_enfilade_t *e, size_t elem_size,
                             ds_context_t *context);
void ds_enfilade_destroy(ds_enfilade_t *e);
size_t ds_enfilade_count(const ds_enfilade_t *e);
/* Sum of all widths (saturating). */
size_t ds_enfilade_width(const ds_enfilade_t *e);
/* Inserts before item `index` (index = count appends). */
ds_status_t ds_enfilade_insert(ds_enfilade_t *e, size_t index, const void *elem,
                               size_t wid);
ds_status_t ds_enfilade_remove(ds_enfilade_t *e, size_t index, void *out);
/* Item at index: element pointer, its width and displacement. */
ds_status_t ds_enfilade_at(const ds_enfilade_t *e, size_t index,
                           void **out_elem, size_t *out_wid, size_t *out_dsp);
/* Item whose span [dsp, dsp + wid) holds address (zero-width items are
 * skipped): its index and displacement. DS_NOT_FOUND past the end. */
ds_status_t ds_enfilade_retrieve(const ds_enfilade_t *e, size_t address,
                                 size_t *out_index, size_t *out_dsp);
ds_status_t ds_enfilade_set_width(ds_enfilade_t *e, size_t index, size_t wid);
/* Moves items [from, to) to stand before item `before` of the original
 * sequence (before outside [from, to]); the cut-and-paste rearrange. */
ds_status_t ds_enfilade_rearrange(ds_enfilade_t *e, size_t from, size_t to,
                                  size_t before);
void ds_enfilade_foreach(const ds_enfilade_t *e, ds_enfilade_visit_func_t f,
                         void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_ENFILADE_H */
