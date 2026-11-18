#include "wavelet_tree.h"
#include "legacy_alloc.h"
#include <stdlib.h>

static void wavelet_destroy_node(ds_wavelet_tree_t *wt,
                                 ds_wavelet_node_t *node);

/* the subtree over symbols lo..hi of seq; NULL when memory runs out, with
 * everything it took released. The recursion is log2(sigma) deep. */
static ds_wavelet_node_t *wavelet_build(ds_wavelet_tree_t *wt, size_t *seq,
                                        size_t length, size_t lo, size_t hi) {
  ds_wavelet_node_t *node;
  size_t mid;
  size_t i;
  size_t left_len = 0;
  size_t right_len = 0;
  size_t *left_seq;
  size_t *right_seq;
  size_t li;
  size_t ri;
  node = (ds_wavelet_node_t *)DS_LEGACY_ALLOC(wt, sizeof(ds_wavelet_node_t));
  if (node == NULL) {
    return NULL;
  }
  node->left = NULL;
  node->right = NULL;
  node->lo = lo;
  node->hi = hi;
  node->bv = NULL;
  if (lo == hi) {
    /* leaf: a run of a single symbol, no bitvector needed */
    return node;
  }
  mid = lo + (hi - lo) / 2;
  if (wt->context != NULL) {
    if (ds_bitvector_create_context(length, wt->context, &node->bv) != DS_OK)
      node->bv = NULL;
  } else
    node->bv =
        ds_bitvector_create_alloc(length, wt->allocator, wt->deallocator);
  if (node->bv == NULL) {
    wavelet_destroy_node(wt, node);
    return NULL;
  }
  for (i = 0; i < length; i++) {
    if (seq[i] > mid) {
      ds_bitvector_set(node->bv, i, true);
      right_len += 1;
    } else {
      left_len += 1;
    }
  }
  /* partition into child subsequences (stable order preserved) */
  left_seq = left_len > 0
                 ? (size_t *)DS_LEGACY_ALLOC(wt, left_len * sizeof(size_t))
                 : NULL;
  right_seq = right_len > 0
                  ? (size_t *)DS_LEGACY_ALLOC(wt, right_len * sizeof(size_t))
                  : NULL;
  if (ds_bitvector_build_ex(node->bv) != DS_OK ||
      (left_len > 0 && left_seq == NULL) ||
      (right_len > 0 && right_seq == NULL)) {
    if (left_seq != NULL) {
      DS_LEGACY_FREE(wt, left_seq);
    }
    if (right_seq != NULL) {
      DS_LEGACY_FREE(wt, right_seq);
    }
    wavelet_destroy_node(wt, node);
    return NULL;
  }
  li = 0;
  ri = 0;
  for (i = 0; i < length; i++) {
    if (seq[i] > mid) {
      right_seq[ri] = seq[i];
      ri += 1;
    } else {
      left_seq[li] = seq[i];
      li += 1;
    }
  }
  node->left = wavelet_build(wt, left_seq, left_len, lo, mid);
  if (left_seq != NULL) {
    DS_LEGACY_FREE(wt, left_seq);
  }
  if (node->left != NULL) {
    node->right = wavelet_build(wt, right_seq, right_len, mid + 1, hi);
  }
  if (right_seq != NULL) {
    DS_LEGACY_FREE(wt, right_seq);
  }
  if (node->left == NULL || node->right == NULL) {
    wavelet_destroy_node(wt, node);
    return NULL;
  }
  return node;
}

ds_wavelet_tree_t *ds_wavelet_tree_create(const size_t *sequence, size_t length,
                                          size_t sigma) {
  return ds_wavelet_tree_create_alloc(sequence, length, sigma, malloc, free);
}

static ds_wavelet_tree_t *
wavelet_tree_create_internal(const size_t *sequence, size_t length,
                             size_t sigma, void *(*allocator)(size_t),
                             void (*deallocator)(void *),
                             ds_context_t *context) {
  ds_wavelet_tree_t *wt;
  size_t *work;
  size_t i;
  if (allocator == NULL || deallocator == NULL ||
      (length > 0 && sequence == NULL) ||
      length > (size_t)-1 / sizeof(size_t)) {
    return NULL;
  }
  for (i = 0; i < length; i++)
    if (sequence[i] >= (sigma > 0 ? sigma : 1))
      return NULL;
  wt = (ds_wavelet_tree_t *)DS_LEGACY_ALLOCATE(context, allocator,
                                               sizeof(ds_wavelet_tree_t));
  if (wt == NULL) {
    return NULL;
  }
  wt->context = context;
  wt->allocator = allocator;
  wt->deallocator = deallocator;
  wt->length = length;
  wt->sigma = sigma < 1 ? 1 : sigma;
  work = length > 0 ? (size_t *)DS_LEGACY_ALLOCATE(context, allocator,
                                                   length * sizeof(size_t))
                    : NULL;
  if (length > 0 && work == NULL) {
    DS_LEGACY_RELEASE(context, deallocator, wt);
    return NULL;
  }
  for (i = 0; i < length; i++) {
    work[i] = sequence[i];
  }
  wt->root = wavelet_build(wt, work, length, 0, wt->sigma - 1);
  if (work != NULL) {
    DS_LEGACY_RELEASE(context, deallocator, work);
  }
  if (wt->root == NULL) {
    DS_LEGACY_RELEASE(context, deallocator, wt);
    return NULL;
  }
  return wt;
}
ds_wavelet_tree_t *ds_wavelet_tree_create_alloc(const size_t *sequence,
                                                size_t length, size_t sigma,
                                                void *(*allocator)(size_t),
                                                void (*deallocator)(void *)) {
  return wavelet_tree_create_internal(sequence, length, sigma, allocator,
                                      deallocator, NULL);
}
ds_status_t ds_wavelet_tree_create_context(const size_t *sequence,
                                           size_t length, size_t sigma,
                                           ds_context_t *context,
                                           ds_wavelet_tree_t **out) {
  size_t i;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (length > 0 && sequence == NULL)
    return DS_ERR_NULL;
  if (length > (size_t)-1 / sizeof(size_t))
    return DS_ERR_OVERFLOW;
  if (sigma == 0 && length > 0)
    return DS_ERR_INVALID;
  for (i = 0; i < length; i++)
    if (sequence[i] >= sigma)
      return DS_ERR_RANGE;
  *out = wavelet_tree_create_internal(sequence, length, sigma, malloc, free,
                                      context != NULL ? context
                                                      : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

static size_t wavelet_access_nolock(ds_wavelet_tree_t *wt, size_t index) {
  ds_wavelet_node_t *node = wt->root;
  while (node->lo != node->hi) {
    if (ds_bitvector_get(node->bv, index)) {
      index = ds_bitvector_rank1(node->bv, index);
      node = node->right;
    } else {
      index = ds_bitvector_rank0(node->bv, index);
      node = node->left;
    }
  }
  return node->lo;
}

size_t ds_wavelet_tree_access(ds_wavelet_tree_t *wt, size_t index) {
  size_t result;
  if (index >= wt->length) {
    result = DS_WAVELET_NPOS;
  } else {
    result = wavelet_access_nolock(wt, index);
  }
  return result;
}

size_t ds_wavelet_tree_rank(ds_wavelet_tree_t *wt, size_t symbol,
                            size_t index) {
  ds_wavelet_node_t *node;
  size_t result;
  if (symbol >= wt->sigma || index > wt->length) {
    result = 0;
  } else {
    node = wt->root;
    while (node->lo != node->hi) {
      size_t mid = node->lo + (node->hi - node->lo) / 2;
      if (symbol > mid) {
        index = ds_bitvector_rank1(node->bv, index);
        node = node->right;
      } else {
        index = ds_bitvector_rank0(node->bv, index);
        node = node->left;
      }
    }
    result = index;
  }
  return result;
}

size_t ds_wavelet_tree_select(ds_wavelet_tree_t *wt, size_t symbol, size_t k) {
  ds_wavelet_node_t *path[8 * sizeof(size_t) + 2];
  size_t depth = 0;
  ds_wavelet_node_t *node;
  size_t result;
  if (symbol >= wt->sigma) {
    return DS_WAVELET_NPOS;
  }
  /* descend to the leaf for `symbol`, recording the path */
  node = wt->root;
  while (node->lo != node->hi) {
    size_t mid = node->lo + (node->hi - node->lo) / 2;
    path[depth] = node;
    depth += 1;
    node = (symbol > mid) ? node->right : node->left;
  }
  /* climb back up, mapping the k-th position down-to-up */
  result = k;
  while (depth > 0) {
    ds_wavelet_node_t *parent;
    size_t mid;
    depth -= 1;
    parent = path[depth];
    mid = parent->lo + (parent->hi - parent->lo) / 2;
    if (symbol > mid) {
      result = ds_bitvector_select1(parent->bv, result);
    } else {
      result = ds_bitvector_select0(parent->bv, result);
    }
    if (result == DS_BITVECTOR_NPOS) {
      break;
    }
  }
  return result == DS_BITVECTOR_NPOS ? DS_WAVELET_NPOS : result;
}

size_t ds_wavelet_tree_quantile(ds_wavelet_tree_t *wt, size_t left,
                                size_t right, size_t k) {
  ds_wavelet_node_t *node;
  size_t result;
  if (left >= right || right > wt->length || k >= (right - left)) {
    return DS_WAVELET_NPOS;
  }
  node = wt->root;
  while (node->lo != node->hi) {
    size_t left_zeros_before = ds_bitvector_rank0(node->bv, left);
    size_t left_zeros_in =
        ds_bitvector_rank0(node->bv, right) - left_zeros_before;
    if (k < left_zeros_in) {
      left = left_zeros_before;
      right = left_zeros_before + left_zeros_in;
      node = node->left;
    } else {
      size_t ones_before = ds_bitvector_rank1(node->bv, left);
      size_t ones_in = ds_bitvector_rank1(node->bv, right) - ones_before;
      k -= left_zeros_in;
      left = ones_before;
      right = ones_before + ones_in;
      node = node->right;
    }
  }
  result = node->lo;
  return result;
}

size_t ds_wavelet_tree_length(ds_wavelet_tree_t *wt) {
  size_t result;
  result = wt->length;
  return result;
}

static void wavelet_destroy_node(ds_wavelet_tree_t *wt,
                                 ds_wavelet_node_t *node) {
  if (node == NULL) {
    return;
  }
  wavelet_destroy_node(wt, node->left);
  wavelet_destroy_node(wt, node->right);
  if (node->bv != NULL) {
    ds_bitvector_destroy(node->bv);
  }
  DS_LEGACY_FREE(wt, node);
}

void ds_wavelet_tree_destroy(ds_wavelet_tree_t *wt) {
  wavelet_destroy_node(wt, wt->root);
  DS_LEGACY_FREE(wt, wt);
}

ds_status_t ds_wavelet_tree_access_ex(ds_wavelet_tree_t *tree, size_t index,
                                      size_t *out) {
  if (tree == NULL || out == NULL)
    return DS_ERR_NULL;
  if (index >= tree->length)
    return DS_ERR_RANGE;
  *out = ds_wavelet_tree_access(tree, index);
  return DS_OK;
}
ds_status_t ds_wavelet_tree_rank_ex(ds_wavelet_tree_t *tree, size_t symbol,
                                    size_t count, size_t *out) {
  if (tree == NULL || out == NULL)
    return DS_ERR_NULL;
  if (count > tree->length || symbol >= tree->sigma)
    return DS_ERR_RANGE;
  *out = ds_wavelet_tree_rank(tree, symbol, count);
  return DS_OK;
}
ds_status_t ds_wavelet_tree_select_ex(ds_wavelet_tree_t *tree, size_t symbol,
                                      size_t ordinal, size_t *out) {
  size_t position;
  if (tree == NULL || out == NULL)
    return DS_ERR_NULL;
  if (symbol >= tree->sigma)
    return DS_ERR_RANGE;
  position = ds_wavelet_tree_select(tree, symbol, ordinal);
  if (position == DS_WAVELET_NPOS)
    return DS_NOT_FOUND;
  *out = position;
  return DS_OK;
}
ds_status_t ds_wavelet_tree_quantile_ex(ds_wavelet_tree_t *tree, size_t left,
                                        size_t right, size_t ordinal,
                                        size_t *out) {
  if (tree == NULL || out == NULL)
    return DS_ERR_NULL;
  if (left > right || right > tree->length || ordinal >= right - left)
    return DS_ERR_RANGE;
  *out = ds_wavelet_tree_quantile(tree, left, right, ordinal);
  return DS_OK;
}
