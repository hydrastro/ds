#ifndef DS_WAVELET_TREE_H
#define DS_WAVELET_TREE_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/structures/bitvector.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Wavelet tree over a sequence of non-negative integer symbols in [0, sigma).
 * Built once from a fixed sequence, it answers in O(log sigma):
 *   access(i)        - the symbol at position i
 *   rank(c, i)       - occurrences of symbol c in the prefix [0, i)
 *   select(c, k)     - position of the k-th occurrence of symbol c (0-based)
 *   quantile(l, r, k)- the k-th smallest symbol in the range [l, r)
 * The structure recursively splits the alphabet in half; each internal node
 * stores a rank/select bitvector marking which half each local symbol takes.
 */

typedef struct wavelet_node {
  ds_bitvector_t *bv;
  struct wavelet_node *left;
  struct wavelet_node *right;
  size_t lo; /* inclusive low symbol of this node's alphabet range */
  size_t hi; /* inclusive high symbol of this node's alphabet range */
} ds_wavelet_node_t;

typedef struct wavelet_tree {
  ds_wavelet_node_t *root;
  size_t length;         /* sequence length */
  size_t sigma;          /* alphabet size (symbols are in [0, sigma)) */
  ds_context_t *context; /* NULL for allocator-pair constructors */
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_wavelet_tree_t;

#define DS_WAVELET_NPOS ((size_t) - 1)

/* NULL when memory runs out, or without an allocator and a deallocator */
ds_wavelet_tree_t *ds_wavelet_tree_create(const size_t *sequence, size_t length,
                                          size_t sigma);
ds_wavelet_tree_t *ds_wavelet_tree_create_alloc(const size_t *sequence,
                                                size_t length, size_t sigma,
                                                void *(*allocator)(size_t),
                                                void (*deallocator)(void *));
size_t ds_wavelet_tree_access(ds_wavelet_tree_t *wt, size_t index);
size_t ds_wavelet_tree_rank(ds_wavelet_tree_t *wt, size_t symbol, size_t index);
size_t ds_wavelet_tree_select(ds_wavelet_tree_t *wt, size_t symbol, size_t k);
size_t ds_wavelet_tree_quantile(ds_wavelet_tree_t *wt, size_t left,
                                size_t right, size_t k);
size_t ds_wavelet_tree_length(ds_wavelet_tree_t *wt);
void ds_wavelet_tree_destroy(ds_wavelet_tree_t *wt);

/* Context-owned internal storage; NULL context chooses the default.
 * The context must outlive the container. On failure *out is NULL. */
ds_status_t ds_wavelet_tree_create_context(const size_t *sequence,
                                           size_t length, size_t sigma,
                                           ds_context_t *context,
                                           ds_wavelet_tree_t **out);

/* Checked queries preserve out on absence/error. Rank uses [0,count),
 * quantile uses [left,right); ordinals are zero-based. */
ds_status_t ds_wavelet_tree_access_ex(ds_wavelet_tree_t *tree, size_t index,
                                      size_t *out);
ds_status_t ds_wavelet_tree_rank_ex(ds_wavelet_tree_t *tree, size_t symbol,
                                    size_t count, size_t *out);
ds_status_t ds_wavelet_tree_select_ex(ds_wavelet_tree_t *tree, size_t symbol,
                                      size_t ordinal, size_t *out);
ds_status_t ds_wavelet_tree_quantile_ex(ds_wavelet_tree_t *tree, size_t left,
                                        size_t right, size_t ordinal,
                                        size_t *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_WAVELET_TREE_H */
