#ifndef DS_HB_TREE_H
#define DS_HB_TREE_H

/*
 * Multidimensional access methods with k-d tree or bit-prefix directories,
 * for points and boxes in d <= 8 dimensions, every item with a size_t
 * value. Coordinates must be finite; a box is 2d doubles (low corner, then
 * high corner, closed).
 *
 * ds_hb_tree_t, two related paged structures sharing one engine. Index
 * nodes hold a small k-d tree whose leaves point to child nodes or data
 * pages; searching descends the k-d trees as in a k-d-B-tree, and all data
 * pages sit at the same depth.
 *   DS_HB_HOLEY  Lomet and Salzberg's hB-tree (holey-brick tree): a full
 *                data page extracts a brick (a box of a few median cuts
 *                holding between one and two thirds of its points) into a
 *                new page, so the old page becomes a brick with a hole,
 *                described by several k-d leaves. Index nodes split at
 *                their k-d root; pages that root cuts are split with it.
 *   DS_HB_BUDDY  Seeger and Kriegel's buddy tree: pages split at the
 *                first regular halving of their cell (axes in turn, inside
 *                a domain box) that separates their points, and every k-d
 *                leaf also keeps the minimum bounding box of the points
 *                below it, which stands in for empty space and prunes
 *                searches.
 *
 * ds_bd_tree_t, Ohsawa and Sakauchi's BD-tree: a binary tree whose nodes
 * split off a regular subinterval named by a DZ-expression (a prefix of the
 * interleaved coordinate bits): points matching it go inside, the rest
 * outside. Leaves are buckets; full buckets split off the sub-cell closest
 * to half their points.
 *
 * ds_gbd_tree_t, the generalised BD-tree for boxes: a balanced multiway
 * tree whose entries carry a DZ-expression and a minimum bounding box.
 * Boxes are routed by the DZ-expression of their centre, nodes split along
 * the DZ-expression that best halves their entries, and searches prune by
 * bounding boxes.
 *
 * ds_bv_tree_t, Freeston's BV-tree over nested DZ regions: when an index
 * node splits, entries whose regions enclose the split region are promoted
 * to the parent as guards instead of being split, so the tree is not
 * height-balanced and a search carries the guards met on its way down,
 * choosing at each level the best match among the node's entries and the
 * guards (backtracking over levels).
 *
 * The DZ-based structures and the buddy tree quantise each coordinate to
 * 30 bits inside a domain box; points outside fall into border cells.
 * Recursion is bounded by the tree height. Memory comes from the
 * ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_HB_MAX_DIM 8

/* item: a point (d doubles) or a box (2d doubles); false stops. */
typedef bool (*ds_hb_visit_func_t)(const double *item, size_t value,
                                   void *user);

/* ---- hB-tree and buddy tree ------------------------------------------------
 */

typedef enum ds_hb_mode { DS_HB_HOLEY = 0, DS_HB_BUDDY } ds_hb_mode_t;

typedef struct ds_hb_tree {
  size_t dim;
  size_t bucket;
  size_t fanout; /* k-d leaves per index node */
  ds_hb_mode_t mode;
  double domain[2 * DS_HB_MAX_DIM];
  /* k-d nodes */
  size_t *kaxis;  /* (size_t)-1 for a leaf */
  double *kvalue; /* low side: coordinate < value */
  size_t *klo;
  size_t *khi;
  size_t *ktarget;      /* leaf: page, index node, or (size_t)-1 (empty) */
  unsigned char *kpage; /* leaf targets a page */
  size_t *kowner;       /* index node */
  double *kmbb;         /* leaf: bounding box of points below (buddy) */
  size_t knodes;
  size_t kcapacity;
  size_t kfree;
  /* index nodes */
  size_t *iroot;
  size_t *ileaves;
  size_t *iparent; /* k-d leaf of the parent pointing here */
  size_t *ilevel;  /* 1: leaves point to pages */
  size_t inodes;
  size_t icapacity;
  size_t ifree;
  /* data pages */
  double **points;
  size_t **values;
  size_t *count;
  size_t *capacity;
  size_t *owner;
  unsigned *stamp;
  unsigned char *alive;
  size_t pages;
  size_t pcapacity;
  size_t pfree;
  size_t live_pages;
  unsigned epoch;
  size_t root;   /* index node */
  size_t height; /* index levels */
  size_t size;
  ds_context_t *context;
} ds_hb_tree_t;

/* bucket >= 2 (0: 16), fanout >= 4 (0: 32). domain is needed by
 * DS_HB_BUDDY and ignored (may be NULL) by DS_HB_HOLEY. */
ds_status_t ds_hb_tree_init(ds_hb_tree_t *t, size_t dim, ds_hb_mode_t mode,
                            const double *domain, size_t bucket, size_t fanout,
                            ds_context_t *context);
void ds_hb_tree_destroy(ds_hb_tree_t *t);
size_t ds_hb_tree_size(const ds_hb_tree_t *t);
size_t ds_hb_tree_height(const ds_hb_tree_t *t);
size_t ds_hb_tree_pages(const ds_hb_tree_t *t);
ds_status_t ds_hb_tree_insert(ds_hb_tree_t *t, const double *point,
                              size_t value);
ds_status_t ds_hb_tree_remove(ds_hb_tree_t *t, const double *point,
                              size_t value);
bool ds_hb_tree_contains(const ds_hb_tree_t *t, const double *point);
ds_status_t ds_hb_tree_query(ds_hb_tree_t *t, const double *box,
                             ds_hb_visit_func_t visit, void *user);
ds_status_t ds_hb_tree_nearest(ds_hb_tree_t *t, const double *point, size_t k,
                               size_t *out_values, double *out_dist,
                               size_t *out_found);
/* Every point routes to its page, counts and bounding boxes agree. */
bool ds_hb_tree_valid(const ds_hb_tree_t *t);

/* ---- BD-tree
 * ------------------------------------------------------------------------ */

typedef struct ds_bd_tree {
  size_t dim;
  size_t bucket;
  double domain[2 * DS_HB_MAX_DIM];
  /* nodes: internal (DZ split) or leaf (bucket) */
  size_t *dz_depth;  /* internal: length of the DZ-expression */
  unsigned long *dz; /* d per node: the DZ cell's quantised low corner */
  size_t *in;        /* internal: matching child; leaf: (size_t)-1 */
  size_t *out;       /* internal: other child */
  size_t *parent;
  double **points;
  size_t **values;
  size_t *count;
  size_t *capacity;
  size_t nodes;
  size_t node_capacity;
  size_t free_list;
  size_t root;
  size_t size;
  ds_context_t *context;
} ds_bd_tree_t;

ds_status_t ds_bd_tree_init(ds_bd_tree_t *t, size_t dim, const double *domain,
                            size_t bucket, ds_context_t *context);
void ds_bd_tree_destroy(ds_bd_tree_t *t);
size_t ds_bd_tree_size(const ds_bd_tree_t *t);
size_t ds_bd_tree_leaves(const ds_bd_tree_t *t);
ds_status_t ds_bd_tree_insert(ds_bd_tree_t *t, const double *point,
                              size_t value);
ds_status_t ds_bd_tree_remove(ds_bd_tree_t *t, const double *point,
                              size_t value);
ds_status_t ds_bd_tree_query(const ds_bd_tree_t *t, const double *box,
                             ds_hb_visit_func_t visit, void *user);
bool ds_bd_tree_valid(const ds_bd_tree_t *t);

/* ---- GBD-tree
 * ---------------------------------------------------------------------- */

typedef struct ds_gbd_tree {
  size_t dim;
  size_t fanout;
  double domain[2 * DS_HB_MAX_DIM];
  /* nodes */
  size_t *ncount;
  unsigned char *nleaf;
  size_t *nparent;
  size_t nodes;
  size_t ncapacity;
  size_t nfree;
  /* entries: fanout + 1 per node */
  size_t *edepth;     /* DZ length */
  unsigned long *edz; /* d per entry */
  double *ebox;       /* 2d per entry: object box or bounding box */
  size_t *eref;       /* child node or value */
  size_t root;
  size_t height;
  size_t size;
  ds_context_t *context;
} ds_gbd_tree_t;

/* fanout >= 4 (0: 16). */
ds_status_t ds_gbd_tree_init(ds_gbd_tree_t *t, size_t dim, const double *domain,
                             size_t fanout, ds_context_t *context);
void ds_gbd_tree_destroy(ds_gbd_tree_t *t);
size_t ds_gbd_tree_size(const ds_gbd_tree_t *t);
size_t ds_gbd_tree_height(const ds_gbd_tree_t *t);
ds_status_t ds_gbd_tree_insert(ds_gbd_tree_t *t, const double *box,
                               size_t value);
ds_status_t ds_gbd_tree_remove(ds_gbd_tree_t *t, const double *box,
                               size_t value);
ds_status_t ds_gbd_tree_query(const ds_gbd_tree_t *t, const double *box,
                              ds_hb_visit_func_t visit, void *user);
/* Balanced, bounding boxes exact, fill legal, count right. */
bool ds_gbd_tree_valid(const ds_gbd_tree_t *t);

/* ---- BV-tree
 * ------------------------------------------------------------------------- */

typedef struct ds_bv_tree {
  size_t dim;
  size_t bucket;
  size_t fanout;
  double domain[2 * DS_HB_MAX_DIM];
  /* index nodes: entries in per-node arrays */
  size_t *ilevel;
  size_t *iparent; /* node holding the entry that points here */
  size_t *icount;
  size_t *icapacity;
  size_t **edepth;     /* DZ length per entry */
  unsigned long **edz; /* d per entry */
  size_t **echild;     /* index node or page */
  size_t **elevel;     /* level of the child: 0 for pages */
  size_t inodes;
  size_t inode_capacity;
  /* pages */
  double **points;
  size_t **values;
  size_t *count;
  size_t *capacity;
  size_t pages;
  size_t page_capacity;
  size_t root;
  size_t size;
  size_t guards;   /* promoted entries */
  size_t *scratch; /* search state */
  size_t scratch_capacity;
  ds_context_t *context;
} ds_bv_tree_t;

/* bucket >= 2 (0: 16), fanout >= 4 (0: 16) counts a node's own entries;
 * guards come on top. */
ds_status_t ds_bv_tree_init(ds_bv_tree_t *t, size_t dim, const double *domain,
                            size_t bucket, size_t fanout,
                            ds_context_t *context);
void ds_bv_tree_destroy(ds_bv_tree_t *t);
size_t ds_bv_tree_size(const ds_bv_tree_t *t);
size_t ds_bv_tree_height(const ds_bv_tree_t *t);
size_t ds_bv_tree_guards(const ds_bv_tree_t *t);
ds_status_t ds_bv_tree_insert(ds_bv_tree_t *t, const double *point,
                              size_t value);
ds_status_t ds_bv_tree_remove(ds_bv_tree_t *t, const double *point,
                              size_t value);
bool ds_bv_tree_contains(ds_bv_tree_t *t, const double *point);
ds_status_t ds_bv_tree_query(const ds_bv_tree_t *t, const double *box,
                             ds_hb_visit_func_t visit, void *user);
/* Every point is found where a search for it ends. */
bool ds_bv_tree_valid(ds_bv_tree_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DS_HB_TREE_H */
