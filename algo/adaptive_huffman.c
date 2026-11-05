#include "adaptive_huffman.h"
#include <string.h>

#define AH_NONE 0xFFFFu
#define AH_ROOT (DS_ADAPTIVE_HUFFMAN_NODES - 1)
#define AH_NYT_SYM 256u
#define AH_INTERNAL 257u

static int ah_is_leaf(const ds_adaptive_huffman_t *h, unsigned p) {
  return h->symbol[p] != AH_INTERNAL;
}

void ds_adaptive_huffman_init(ds_adaptive_huffman_t *h,
                              ds_adaptive_huffman_policy_t policy) {
  unsigned i;
  for (i = 0; i < 256; i++) {
    h->leaf[i] = AH_NONE;
  }
  for (i = 0; i < DS_ADAPTIVE_HUFFMAN_NODES; i++) {
    h->weight[i] = 0;
    h->parent[i] = AH_NONE;
    h->left[i] = AH_NONE;
    h->right[i] = AH_NONE;
    h->symbol[i] = AH_INTERNAL;
  }
  h->symbol[AH_ROOT] = AH_NYT_SYM;
  h->nyt = AH_ROOT;
  h->lowest = AH_ROOT;
  h->policy =
      policy == DS_ADAPTIVE_VITTER ? DS_ADAPTIVE_VITTER : DS_ADAPTIVE_FGK;
}

/* Exchange the subtrees at node numbers i and j (neither an ancestor of
 * the other); parents stay with the numbers. */
static void ah_swap(ds_adaptive_huffman_t *h, unsigned i, unsigned j) {
  unsigned long w;
  unsigned short t;
  unsigned k;
  unsigned p;
  w = h->weight[i];
  h->weight[i] = h->weight[j];
  h->weight[j] = w;
  t = h->left[i];
  h->left[i] = h->left[j];
  h->left[j] = t;
  t = h->right[i];
  h->right[i] = h->right[j];
  h->right[j] = t;
  t = h->symbol[i];
  h->symbol[i] = h->symbol[j];
  h->symbol[j] = t;
  for (k = 0; k < 2; k++) {
    p = k == 0 ? i : j;
    if (h->symbol[p] == AH_INTERNAL) {
      h->parent[h->left[p]] = (unsigned short)p;
      h->parent[h->right[p]] = (unsigned short)p;
    } else if (h->symbol[p] == AH_NYT_SYM) {
      h->nyt = (unsigned short)p;
    } else {
      h->leaf[h->symbol[p]] = (unsigned short)p;
    }
  }
}

/* Split the NYT leaf: it becomes an internal node over a new NYT and a
 * new leaf for `symbol` (weight 0); returns the new leaf. */
static unsigned ah_split_nyt(ds_adaptive_huffman_t *h, unsigned symbol) {
  unsigned z = h->nyt;
  unsigned leafn = h->lowest - 1u;
  unsigned nytn = h->lowest - 2u;
  h->lowest = (unsigned short)nytn;
  h->symbol[z] = AH_INTERNAL;
  h->left[z] = (unsigned short)nytn;
  h->right[z] = (unsigned short)leafn;
  h->symbol[leafn] = (unsigned short)symbol;
  h->weight[leafn] = 0;
  h->parent[leafn] = (unsigned short)z;
  h->left[leafn] = AH_NONE;
  h->right[leafn] = AH_NONE;
  h->symbol[nytn] = AH_NYT_SYM;
  h->weight[nytn] = 0;
  h->parent[nytn] = (unsigned short)z;
  h->left[nytn] = AH_NONE;
  h->right[nytn] = AH_NONE;
  h->nyt = (unsigned short)nytn;
  h->leaf[symbol] = (unsigned short)leafn;
  return leafn;
}

static void ah_update_fgk(ds_adaptive_huffman_t *h, unsigned q) {
  unsigned lead;
  while (q != AH_NONE) {
    /* leader: highest number with the same weight */
    lead = q;
    while (lead < AH_ROOT && h->weight[lead + 1] == h->weight[q]) {
      lead++;
    }
    if (lead != q && lead != h->parent[q]) {
      ah_swap(h, q, lead);
      q = lead;
    }
    h->weight[q]++;
    q = h->parent[q];
  }
}

/* Vitter: move p to the top of its block (same weight and kind), then
 * past the next block (internal nodes of its weight for a leaf, leaves of
 * weight + 1 for an internal node), increment, and return the next node
 * to process. */
static unsigned ah_slide_and_increment(ds_adaptive_huffman_t *h, unsigned p) {
  unsigned long w = h->weight[p];
  int leaf = ah_is_leaf(h, p);
  unsigned fp;
  while (p < AH_ROOT && h->weight[p + 1] == w && ah_is_leaf(h, p + 1) == leaf &&
         p + 1 != h->parent[p]) {
    ah_swap(h, p, p + 1);
    p++;
  }
  fp = h->parent[p];
  if (leaf) {
    while (p < AH_ROOT && h->weight[p + 1] == w && !ah_is_leaf(h, p + 1) &&
           p + 1 != h->parent[p]) {
      ah_swap(h, p, p + 1);
      p++;
    }
  } else {
    while (p < AH_ROOT && h->weight[p + 1] == w + 1 && ah_is_leaf(h, p + 1)) {
      ah_swap(h, p, p + 1);
      p++;
    }
  }
  h->weight[p]++;
  return leaf ? h->parent[p] : fp;
}

static void ah_update(ds_adaptive_huffman_t *h, unsigned symbol) {
  unsigned q;
  unsigned leafinc = AH_NONE;
  if (h->policy == DS_ADAPTIVE_FGK) {
    q = h->leaf[symbol] == AH_NONE ? ah_split_nyt(h, symbol) : h->leaf[symbol];
    ah_update_fgk(h, q);
    return;
  }
  if (h->leaf[symbol] == AH_NONE) {
    leafinc = ah_split_nyt(h, symbol);
    q = h->parent[leafinc];
  } else {
    q = h->leaf[symbol];
    /* first to the top of its block, then test for the NYT sibling */
    while (q < AH_ROOT && h->weight[q + 1] == h->weight[q] &&
           ah_is_leaf(h, q + 1) && q + 1 != h->parent[q]) {
      ah_swap(h, q, q + 1);
      q++;
    }
    if (h->parent[q] == h->parent[h->nyt]) {
      leafinc = q;
      q = h->parent[q];
    }
  }
  while (q != AH_NONE) {
    q = ah_slide_and_increment(h, q);
  }
  if (leafinc != AH_NONE) {
    ah_slide_and_increment(h, leafinc);
  }
}

static ds_status_t ah_emit_path(const ds_adaptive_huffman_t *h,
                                ds_bit_writer_t *out, unsigned node) {
  unsigned char bits[DS_ADAPTIVE_HUFFMAN_NODES];
  unsigned n = 0;
  unsigned p;
  ds_status_t st = DS_OK;
  while (node != AH_ROOT) {
    p = h->parent[node];
    bits[n++] = (unsigned char)(h->right[p] == node ? 1 : 0);
    node = p;
  }
  while (n > 0 && st == DS_OK) {
    st = ds_bit_writer_put(out, bits[--n], 1);
  }
  return st;
}

ds_status_t ds_adaptive_huffman_encode(ds_adaptive_huffman_t *h,
                                       ds_bit_writer_t *out,
                                       unsigned char symbol) {
  ds_status_t st;
  if (h == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (h->leaf[symbol] != AH_NONE) {
    st = ah_emit_path(h, out, h->leaf[symbol]);
  } else {
    st = ah_emit_path(h, out, h->nyt);
    if (st == DS_OK) {
      st = ds_bit_writer_put(out, symbol, 8);
    }
  }
  if (st == DS_OK) {
    ah_update(h, symbol);
  }
  return st;
}

ds_status_t ds_adaptive_huffman_decode(ds_adaptive_huffman_t *h,
                                       ds_bit_reader_t *in,
                                       unsigned char *out_symbol) {
  unsigned node = AH_ROOT;
  unsigned long b;
  unsigned sym;
  if (h == NULL || in == NULL || out_symbol == NULL) {
    return DS_ERR_NULL;
  }
  while (!ah_is_leaf(h, node)) {
    if (ds_bit_reader_get(in, 1, &b) != DS_OK) {
      return node == AH_ROOT ? DS_NOT_FOUND : DS_ERR_INVALID;
    }
    node = b ? h->right[node] : h->left[node];
  }
  if (h->symbol[node] == AH_NYT_SYM) {
    if (ds_bit_reader_get(in, 8, &b) != DS_OK) {
      return DS_ERR_INVALID;
    }
    sym = (unsigned)b;
    if (h->leaf[sym] != AH_NONE) {
      return DS_ERR_INVALID;
    }
  } else {
    sym = h->symbol[node];
  }
  ah_update(h, sym);
  *out_symbol = (unsigned char)sym;
  return DS_OK;
}

bool ds_adaptive_huffman_valid(const ds_adaptive_huffman_t *h) {
  unsigned p;
  unsigned l;
  unsigned r;
  if (h == NULL) {
    return false;
  }
  for (p = h->lowest; p <= AH_ROOT; p++) {
    if (p > h->lowest && h->weight[p - 1] > h->weight[p]) {
      return false;
    }
    if (h->policy == DS_ADAPTIVE_VITTER && p > h->lowest &&
        h->weight[p - 1] == h->weight[p] && !ah_is_leaf(h, p - 1) &&
        ah_is_leaf(h, p)) {
      return false;
    }
    if (ah_is_leaf(h, p)) {
      if (h->symbol[p] == AH_NYT_SYM ? h->nyt != p
                                     : h->leaf[h->symbol[p]] != p) {
        return false;
      }
      continue;
    }
    l = h->left[p];
    r = h->right[p];
    if (l >= p || r >= p || h->parent[l] != p || h->parent[r] != p ||
        h->weight[p] != h->weight[l] + h->weight[r]) {
      return false;
    }
    /* siblings occupy consecutive numbers */
    if (l + 1 != r && r + 1 != l) {
      return false;
    }
  }
  return true;
}

size_t ds_adaptive_huffman_compress_bound(size_t n) {
  if (n > (((size_t)-1) - 64) / 8) {
    return (size_t)-1;
  }
  return 8 * n + 64;
}

ds_status_t ds_adaptive_huffman_compress(const unsigned char *in, size_t n,
                                         ds_adaptive_huffman_policy_t policy,
                                         unsigned char *out, size_t capacity,
                                         size_t *out_length) {
  ds_adaptive_huffman_t h;
  ds_bit_writer_t w;
  size_t i;
  ds_status_t st = DS_OK;
  if ((in == NULL && n > 0) || out == NULL || out_length == NULL) {
    return DS_ERR_NULL;
  }
  if (n > 0xFFFFFFFFUL) {
    return DS_ERR_OVERFLOW;
  }
  if (capacity < 5) {
    return DS_ERR_OVERFLOW;
  }
  out[0] = (unsigned char)((n >> 24) & 0xFFu);
  out[1] = (unsigned char)((n >> 16) & 0xFFu);
  out[2] = (unsigned char)((n >> 8) & 0xFFu);
  out[3] = (unsigned char)(n & 0xFFu);
  out[4] = (unsigned char)(policy == DS_ADAPTIVE_VITTER ? 1 : 0);
  ds_adaptive_huffman_init(&h, policy);
  ds_bit_writer_init(&w, out + 5, capacity - 5);
  for (i = 0; i < n && st == DS_OK; i++) {
    st = ds_adaptive_huffman_encode(&h, &w, in[i]);
  }
  if (st == DS_OK) {
    *out_length = 5 + ds_bit_writer_bytes(&w);
  }
  return st;
}

ds_status_t ds_adaptive_huffman_decompress(const unsigned char *in, size_t n,
                                           unsigned char *out, size_t capacity,
                                           size_t *out_length) {
  ds_adaptive_huffman_t h;
  ds_bit_reader_t r;
  size_t count;
  size_t i;
  ds_status_t st;
  if (in == NULL || out_length == NULL) {
    return DS_ERR_NULL;
  }
  if (n < 5 || in[4] > 1) {
    return DS_ERR_INVALID;
  }
  count = ((size_t)in[0] << 24) | ((size_t)in[1] << 16) | ((size_t)in[2] << 8) |
          (size_t)in[3];
  *out_length = count;
  if (count > capacity) {
    return DS_ERR_OVERFLOW;
  }
  if (count > 0 && out == NULL) {
    return DS_ERR_NULL;
  }
  ds_adaptive_huffman_init(&h, in[4] ? DS_ADAPTIVE_VITTER : DS_ADAPTIVE_FGK);
  ds_bit_reader_init(&r, in + 5, n - 5);
  for (i = 0; i < count; i++) {
    st = ds_adaptive_huffman_decode(&h, &r, out + i);
    if (st != DS_OK) {
      return DS_ERR_INVALID;
    }
  }
  return DS_OK;
}
