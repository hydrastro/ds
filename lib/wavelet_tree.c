#include "wavelet_tree.h"
#include <stdlib.h>

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
  node = (ds_wavelet_node_t *)wt->allocator(sizeof(ds_wavelet_node_t));
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
  node->bv = ds_bitvector_create_alloc(length, wt->allocator, wt->deallocator);
  for (i = 0; i < length; i++) {
    if (seq[i] > mid) {
      ds_bitvector_set(node->bv, i, true);
      right_len += 1;
    } else {
      left_len += 1;
    }
  }
  ds_bitvector_build(node->bv);
  /* partition into child subsequences (stable order preserved) */
  left_seq =
      left_len > 0 ? (size_t *)wt->allocator(left_len * sizeof(size_t)) : NULL;
  right_seq = right_len > 0
                  ? (size_t *)wt->allocator(right_len * sizeof(size_t))
                  : NULL;
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
  if (left_len > 0) {
    node->left = wavelet_build(wt, left_seq, left_len, lo, mid);
    wt->deallocator(left_seq);
  } else {
    node->left = wavelet_build(wt, NULL, 0, lo, mid);
  }
  if (right_len > 0) {
    node->right = wavelet_build(wt, right_seq, right_len, mid + 1, hi);
    wt->deallocator(right_seq);
  } else {
    node->right = wavelet_build(wt, NULL, 0, mid + 1, hi);
  }
  return node;
}

ds_wavelet_tree_t *ds_wavelet_tree_create(const size_t *sequence, size_t length,
                                          size_t sigma) {
  return ds_wavelet_tree_create_alloc(sequence, length, sigma, malloc, free);
}

ds_wavelet_tree_t *ds_wavelet_tree_create_alloc(const size_t *sequence,
                                                size_t length, size_t sigma,
                                                void *(*allocator)(size_t),
                                                void (*deallocator)(void *)) {
  ds_wavelet_tree_t *wt =
      (ds_wavelet_tree_t *)allocator(sizeof(ds_wavelet_tree_t));
  size_t *work;
  size_t i;
  wt->allocator = allocator;
  wt->deallocator = deallocator;
  wt->length = length;
  wt->sigma = sigma < 1 ? 1 : sigma;
  work = length > 0 ? (size_t *)allocator(length * sizeof(size_t)) : NULL;
  for (i = 0; i < length; i++) {
    work[i] = sequence[i];
  }
  wt->root = wavelet_build(wt, work, length, 0, wt->sigma - 1);
  if (work != NULL) {
    deallocator(work);
  }
  return wt;
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
  wt->deallocator(node);
}

void ds_wavelet_tree_destroy(ds_wavelet_tree_t *wt) {
  wavelet_destroy_node(wt, wt->root);
  wt->deallocator(wt);
}
