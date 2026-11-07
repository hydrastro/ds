#ifndef DS_ZIPPER_H
#define DS_ZIPPER_H

/*
 * Zippers (Huet) over persistent binary trees.
 *
 * Trees are immutable and share structure: nodes hold an element by value
 * and two children, and are reference counted inside a ds_zipper_store_t,
 * so every version stays valid as long as someone holds it. A zipper is a
 * tree "opened" at one node: the focus subtree plus the path back to the
 * root, each path frame holding the direction taken, the parent's element,
 * the other subtree and the rest of the path. Moving down pushes a frame,
 * moving up rebuilds the parent from it; editing replaces the focus. Every
 * operation returns a new zipper and leaves the old one intact, all in
 * O(1) (closing rebuilds the path, O(depth)).
 *
 * Ownership: functions that return a tree or a zipper hand over one
 * reference, released with ds_zipper_tree_release or ds_zipper_release;
 * trees passed in are only borrowed. Memory comes from the ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_ZIPPER_EMPTY (~(size_t)0)

typedef struct ds_zipper_store {
  size_t elem_size;
  /* tree nodes */
  unsigned char *value;
  size_t *left;
  size_t *right;
  size_t *refs; /* free nodes: next free in left */
  size_t nodes;
  size_t node_capacity;
  size_t node_free;
  /* path frames */
  unsigned char *fvalue;
  size_t *fsibling;
  size_t *fup;
  unsigned char *fdir; /* 0: went left, 1: went right */
  size_t *frefs;
  size_t frames;
  size_t frame_capacity;
  size_t frame_free;
  size_t live_nodes;
  size_t live_frames;
  size_t *work; /* scratch for releases */
  size_t work_capacity;
  ds_context_t *context;
} ds_zipper_store_t;

typedef struct ds_zipper {
  size_t focus; /* subtree or DS_ZIPPER_EMPTY */
  size_t path;  /* frame or DS_ZIPPER_EMPTY at the root */
} ds_zipper_t;

ds_status_t ds_zipper_store_init(ds_zipper_store_t *s, size_t elem_size,
                                 ds_context_t *context);
void ds_zipper_store_destroy(ds_zipper_store_t *s);
/* live nodes and frames (for leak checks) */
size_t ds_zipper_store_nodes(const ds_zipper_store_t *s);
size_t ds_zipper_store_frames(const ds_zipper_store_t *s);

/* trees */
ds_status_t ds_zipper_tree_node(ds_zipper_store_t *s, const void *elem,
                                size_t left, size_t right, size_t *out);
void ds_zipper_tree_retain(ds_zipper_store_t *s, size_t tree);
void ds_zipper_tree_release(ds_zipper_store_t *s, size_t tree);
const void *ds_zipper_tree_value(const ds_zipper_store_t *s, size_t tree);
size_t ds_zipper_tree_left(const ds_zipper_store_t *s, size_t tree);
size_t ds_zipper_tree_right(const ds_zipper_store_t *s, size_t tree);
size_t ds_zipper_tree_size(ds_zipper_store_t *s, size_t tree);

/* zippers */
ds_status_t ds_zipper_open(ds_zipper_store_t *s, size_t tree, ds_zipper_t *out);
void ds_zipper_release(ds_zipper_store_t *s, ds_zipper_t z);
ds_status_t ds_zipper_copy(ds_zipper_store_t *s, ds_zipper_t z,
                           ds_zipper_t *out);
/* DS_ERR_STATE when the focus is empty (down) or at the root (up). */
ds_status_t ds_zipper_down_left(ds_zipper_store_t *s, ds_zipper_t z,
                                ds_zipper_t *out);
ds_status_t ds_zipper_down_right(ds_zipper_store_t *s, ds_zipper_t z,
                                 ds_zipper_t *out);
ds_status_t ds_zipper_up(ds_zipper_store_t *s, ds_zipper_t z, ds_zipper_t *out);
/* the root tree with every edit applied */
ds_status_t ds_zipper_close(ds_zipper_store_t *s, ds_zipper_t z,
                            size_t *out_tree);
const void *ds_zipper_value(const ds_zipper_store_t *s, ds_zipper_t z);
/* new focus element, children kept */
ds_status_t ds_zipper_set(ds_zipper_store_t *s, ds_zipper_t z, const void *elem,
                          ds_zipper_t *out);
/* new focus subtree (DS_ZIPPER_EMPTY deletes it) */
ds_status_t ds_zipper_replace(ds_zipper_store_t *s, ds_zipper_t z,
                              size_t subtree, ds_zipper_t *out);
bool ds_zipper_at_root(ds_zipper_t z);
size_t ds_zipper_depth(const ds_zipper_store_t *s, ds_zipper_t z);

#ifdef __cplusplus
}
#endif

#endif /* DS_ZIPPER_H */
