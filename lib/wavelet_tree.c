#include "wavelet_tree.h"
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
  node = (ds_wavelet_node_t *)wt->allocator(sizeof(ds_wavelet_node_t));
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
  node->bv = ds_bitvector_create_alloc(length, wt->allocator, wt->deallocator);
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
  left_seq =
      left_len > 0 ? (size_t *)wt->allocator(left_len * sizeof(size_t)) : NULL;
  right_seq = right_len > 0
                  ? (size_t *)wt->allocator(right_len * sizeof(size_t))
                  : NULL;
  if (ds_bitvector_build_ex(node->bv) != DS_OK ||
      (left_len > 0 && left_seq == NULL) ||
      (right_len > 0 && right_seq == NULL)) {
    if (left_seq != NULL) {
      wt->deallocator(left_seq);
    }
    if (right_seq != NULL) {
      wt->deallocator(right_seq);
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
    wt->deallocator(left_seq);
  }
  if (node->left != NULL) {
    node->right = wavelet_build(wt, right_seq, right_len, mid + 1, hi);
  }
  if (right_seq != NULL) {
    wt->deallocator(right_seq);
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

ds_wavelet_tree_t *ds_wavelet_tree_create_alloc(const size_t *sequence,
                                                size_t length, size_t sigma,
                                                void *(*allocator)(size_t),
                                                void (*deallocator)(void *)) {
  ds_wavelet_tree_t *wt;
  size_t *work;
  size_t i;
  if (allocator == NULL || deallocator == NULL ||
      (length > 0 && sequence == NULL) ||
      length > (size_t)-1 / sizeof(size_t)) {
    return NULL;
  }
  wt = (ds_wavelet_tree_t *)allocator(sizeof(ds_wavelet_tree_t));
  if (wt == NULL) {
    return NULL;
  }
  wt->allocator = allocator;
  wt->deallocator = deallocator;
  wt->length = length;
  wt->sigma = sigma < 1 ? 1 : sigma;
  work = length > 0 ? (size_t *)allocator(length * sizeof(size_t)) : NULL;
  if (length > 0 && work == NULL) {
    deallocator(wt);
    return NULL;
  }
  for (i = 0; i < length; i++) {
    work[i] = sequence[i];
  }
  wt->root = wavelet_build(wt, work, length, 0, wt->sigma - 1);
  if (work != NULL) {
    deallocator(work);
  }
  if (wt->root == NULL) {
    deallocator(wt);
    return NULL;
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
