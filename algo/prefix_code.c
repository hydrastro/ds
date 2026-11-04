#include "prefix_code.h"
#include <string.h>

#define PC_NONE (~(size_t)0)
#define PC_MAXLEN 255
#define PC_HUFF_LIMIT 24
#define PC_HEADER (4 + 256)

/* ---- bit I/O -------------------------------------------------------- */

void ds_bit_writer_init(ds_bit_writer_t *w, unsigned char *data,
                        size_t capacity) {
  w->data = data;
  w->capacity = capacity;
  w->bits = 0;
}

ds_status_t ds_bit_writer_put(ds_bit_writer_t *w, unsigned long value,
                              unsigned count) {
  unsigned i;
  size_t byte;
  if (count > 32) {
    return DS_ERR_INVALID;
  }
  if ((w->bits + count + 7) / 8 > w->capacity) {
    return DS_ERR_OVERFLOW;
  }
  for (i = count; i-- > 0;) {
    byte = w->bits / 8;
    if (w->bits % 8 == 0) {
      w->data[byte] = 0;
    }
    if ((value >> i) & 1UL) {
      w->data[byte] =
          (unsigned char)(w->data[byte] | (0x80u >> (unsigned)(w->bits % 8)));
    }
    w->bits++;
  }
  return DS_OK;
}

size_t ds_bit_writer_bytes(const ds_bit_writer_t *w) {
  return (w->bits + 7) / 8;
}

void ds_bit_reader_init(ds_bit_reader_t *r, const unsigned char *data,
                        size_t bytes) {
  r->data = data;
  r->bit_count = bytes * 8;
  r->position = 0;
}

ds_status_t ds_bit_reader_get(ds_bit_reader_t *r, unsigned count,
                              unsigned long *out_value) {
  unsigned long v = 0;
  unsigned i;
  if (count > 32) {
    return DS_ERR_INVALID;
  }
  if (r->position + count > r->bit_count) {
    return DS_NOT_FOUND;
  }
  for (i = 0; i < count; i++) {
    v = (v << 1) |
        (unsigned long)((r->data[r->position / 8] >> (7 - r->position % 8)) &
                        1u);
    r->position++;
  }
  *out_value = v;
  return DS_OK;
}

/* ---- Huffman lengths ------------------------------------------------ */

typedef struct pc_item {
  double weight;
  size_t id;
} pc_item_t;

static int pc_less(const pc_item_t *a, const pc_item_t *b) {
  if (a->weight < b->weight) {
    return 1;
  }
  if (b->weight < a->weight) {
    return 0;
  }
  return a->id < b->id;
}

static void pc_down(pc_item_t *h, size_t n, size_t i) {
  size_t c;
  pc_item_t t;
  for (;;) {
    c = 2 * i + 1;
    if (c >= n) {
      return;
    }
    if (c + 1 < n && pc_less(&h[c + 1], &h[c])) {
      c++;
    }
    if (!pc_less(&h[c], &h[i])) {
      return;
    }
    t = h[i];
    h[i] = h[c];
    h[c] = t;
    i = c;
  }
}

static void pc_up(pc_item_t *h, size_t i) {
  pc_item_t t;
  while (i > 0 && pc_less(&h[i], &h[(i - 1) / 2])) {
    t = h[i];
    h[i] = h[(i - 1) / 2];
    h[(i - 1) / 2] = t;
    i = (i - 1) / 2;
  }
}

ds_status_t ds_huffman_lengths(const unsigned long *freqs, size_t n,
                               unsigned arity, unsigned char *out_lengths,
                               ds_context_t *context) {
  pc_item_t *heap;
  size_t *parent;
  size_t *depth;
  size_t used = 0;
  size_t dummies;
  size_t total;
  size_t hn = 0;
  size_t next;
  size_t i;
  size_t k;
  double sum;
  pc_item_t top;
  if (n > 0 && (freqs == NULL || out_lengths == NULL)) {
    return DS_ERR_NULL;
  }
  if (arity < 2 || arity > 256) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    out_lengths[i] = 0;
    if (freqs[i] > 0) {
      used++;
    }
  }
  if (used == 0) {
    return DS_OK;
  }
  if (used == 1) {
    for (i = 0; i < n; i++) {
      if (freqs[i] > 0) {
        out_lengths[i] = 1;
      }
    }
    return DS_OK;
  }
  dummies = (arity - 1 - (used - 1) % (arity - 1)) % (arity - 1);
  total = 2 * (used + dummies); /* leaves + internal nodes, generously */
  heap = (pc_item_t *)ds_context_alloc(context,
                                       (used + dummies) * sizeof(pc_item_t));
  parent = (size_t *)ds_context_alloc(context, 2 * total * sizeof(size_t));
  if (heap == NULL || parent == NULL) {
    ds_context_free(context, heap);
    ds_context_free(context, parent);
    return DS_ERR_ALLOC;
  }
  depth = parent + total;
  /* ids: 0 .. n - 1 symbols, then dummies and internal nodes from n */
  /* map ids into [0, total) compactly: leaves first */
  next = 0;
  for (i = 0; i < n; i++) {
    if (freqs[i] > 0) {
      heap[hn].weight = (double)freqs[i];
      heap[hn].id = next++;
      hn++;
    }
  }
  for (i = 0; i < dummies; i++) {
    heap[hn].weight = 0.0;
    heap[hn].id = next++;
    hn++;
  }
  for (i = hn / 2; i-- > 0;) {
    pc_down(heap, hn, i);
  }
  while (hn > 1) {
    sum = 0.0;
    for (k = 0; k < arity && hn > 0; k++) {
      top = heap[0];
      heap[0] = heap[--hn];
      pc_down(heap, hn, 0);
      sum += top.weight;
      parent[top.id] = next;
    }
    heap[hn].weight = sum;
    heap[hn].id = next++;
    pc_up(heap, hn);
    hn++;
  }
  /* the root is the last node; parents always have larger ids */
  depth[next - 1] = 0;
  for (i = next - 1; i-- > 0;) {
    depth[i] = depth[parent[i]] + 1;
  }
  k = 0;
  for (i = 0; i < n; i++) {
    if (freqs[i] > 0) {
      if (depth[k] > PC_MAXLEN) {
        ds_context_free(context, heap);
        ds_context_free(context, parent);
        return DS_ERR_OVERFLOW;
      }
      out_lengths[i] = (unsigned char)depth[k];
      k++;
    }
  }
  ds_context_free(context, heap);
  ds_context_free(context, parent);
  return DS_OK;
}

ds_status_t ds_huffman_limit_lengths(unsigned char *lengths, size_t n,
                                     unsigned max_length,
                                     ds_context_t *context) {
  unsigned char *old;
  size_t count[PC_MAXLEN + 2];
  size_t used = 0;
  size_t i;
  size_t j;
  size_t len;
  size_t maxseen = 0;
  if (n > 0 && lengths == NULL) {
    return DS_ERR_NULL;
  }
  if (max_length < 1 || max_length > PC_MAXLEN) {
    return DS_ERR_INVALID;
  }
  memset(count, 0, sizeof(count));
  for (i = 0; i < n; i++) {
    if (lengths[i] > 0) {
      count[lengths[i]]++;
      used++;
      if (lengths[i] > maxseen) {
        maxseen = lengths[i];
      }
    }
  }
  if (maxseen <= max_length) {
    return DS_OK;
  }
  if (max_length < sizeof(size_t) * 8 && used > ((size_t)1 << max_length)) {
    return DS_ERR_INVALID;
  }
  /* move pairs of the longest codes up, splitting a shorter code */
  for (i = maxseen; i > max_length; i--) {
    while (count[i] > 0) {
      j = i - 2;
      while (j > 0 && count[j] == 0) {
        j--;
      }
      if (j == 0) {
        return DS_ERR_INVALID;
      }
      count[i] -= 2;
      count[i - 1] += 1;
      count[j + 1] += 2;
      count[j] -= 1;
    }
  }
  /* reassign: symbols sorted by old length keep their rank */
  old = (unsigned char *)ds_context_alloc(context, n);
  if (old == NULL) {
    return DS_ERR_ALLOC;
  }
  memcpy(old, lengths, n);
  len = 1;
  for (j = 1; j <= maxseen; j++) {
    for (i = 0; i < n; i++) {
      if (old[i] != j) {
        continue;
      }
      while (count[len] == 0) {
        len++;
      }
      count[len]--;
      lengths[i] = (unsigned char)len;
    }
  }
  ds_context_free(context, old);
  return DS_OK;
}

ds_status_t ds_shannon_fano_lengths(const unsigned long *freqs, size_t n,
                                    unsigned char *out_lengths,
                                    ds_context_t *context) {
  size_t *order;
  double *pre;
  size_t *stack;
  size_t used = 0;
  size_t top;
  size_t lo;
  size_t hi;
  size_t d;
  size_t i;
  size_t j;
  size_t k;
  size_t best;
  double diff;
  double bd;
  size_t t;
  if (n > 0 && (freqs == NULL || out_lengths == NULL)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n; i++) {
    out_lengths[i] = 0;
    if (freqs[i] > 0) {
      used++;
    }
  }
  if (used == 0) {
    return DS_OK;
  }
  order = (size_t *)ds_context_alloc(context, (4 * used + 3) * sizeof(size_t));
  pre = (double *)ds_context_alloc(context, (used + 1) * sizeof(double));
  if (order == NULL || pre == NULL) {
    ds_context_free(context, order);
    ds_context_free(context, pre);
    return DS_ERR_ALLOC;
  }
  stack = order + used;
  k = 0;
  for (i = 0; i < n; i++) {
    if (freqs[i] > 0) {
      order[k++] = i;
    }
  }
  /* insertion sort by decreasing frequency, ties by symbol */
  for (i = 1; i < used; i++) {
    t = order[i];
    for (j = i; j > 0 && freqs[order[j - 1]] < freqs[t]; j--) {
      order[j] = order[j - 1];
    }
    order[j] = t;
  }
  pre[0] = 0.0;
  for (i = 0; i < used; i++) {
    pre[i + 1] = pre[i] + (double)freqs[order[i]];
  }
  stack[0] = 0;
  stack[1] = used;
  stack[2] = 0;
  top = 1;
  while (top > 0) {
    top--;
    lo = stack[3 * top];
    hi = stack[3 * top + 1];
    d = stack[3 * top + 2];
    if (hi - lo == 1) {
      if (d > PC_MAXLEN) {
        ds_context_free(context, order);
        ds_context_free(context, pre);
        return DS_ERR_OVERFLOW;
      }
      out_lengths[order[lo]] = (unsigned char)(d == 0 ? 1 : d);
      continue;
    }
    best = lo + 1;
    bd = -1.0;
    for (k = lo + 1; k < hi; k++) {
      diff = (pre[k] - pre[lo]) - (pre[hi] - pre[k]);
      if (diff < 0) {
        diff = -diff;
      }
      if (bd < 0 || diff < bd) {
        bd = diff;
        best = k;
      }
    }
    stack[3 * top] = lo;
    stack[3 * top + 1] = best;
    stack[3 * top + 2] = d + 1;
    top++;
    stack[3 * top] = best;
    stack[3 * top + 1] = hi;
    stack[3 * top + 2] = d + 1;
    top++;
  }
  ds_context_free(context, order);
  ds_context_free(context, pre);
  return DS_OK;
}

ds_status_t ds_hu_tucker_lengths(const unsigned long *freqs, size_t n,
                                 unsigned char *out_lengths,
                                 ds_context_t *context) {
  /* Garsia-Wachs: list of (weight, node); nodes 0..n-1 leaves. */
  double *w;
  size_t *node;
  size_t *left;
  size_t *right;
  size_t *depth;
  size_t *stack;
  size_t t = n; /* list length */
  size_t next = n;
  size_t i;
  size_t j;
  size_t top;
  size_t x;
  double y;
  size_t ny;
  if (n > 0 && (freqs == NULL || out_lengths == NULL)) {
    return DS_ERR_NULL;
  }
  if (n == 0) {
    return DS_OK;
  }
  if (n == 1) {
    out_lengths[0] = 1;
    return DS_OK;
  }
  w = (double *)ds_context_alloc(context, n * sizeof(double));
  node = (size_t *)ds_context_alloc(context, (n + 5 * 2 * n) * sizeof(size_t));
  if (w == NULL || node == NULL) {
    ds_context_free(context, w);
    ds_context_free(context, node);
    return DS_ERR_ALLOC;
  }
  left = node + n;
  right = left + 2 * n;
  depth = right + 2 * n;
  stack = depth + 2 * n;
  for (i = 0; i < n; i++) {
    w[i] = (double)freqs[i];
    node[i] = i;
  }
  while (t > 1) {
    /* smallest i >= 1 with w[i - 1] <= w[i + 1] (w[t] = infinity) */
    for (i = 1; i + 1 < t && w[i - 1] > w[i + 1]; i++) {
    }
    y = w[i - 1] + w[i];
    ny = next++;
    left[ny] = node[i - 1];
    right[ny] = node[i];
    /* remove positions i - 1 and i */
    memmove(w + i - 1, w + i + 1, (t - i - 1) * sizeof(double));
    memmove(node + i - 1, node + i + 1, (t - i - 1) * sizeof(size_t));
    t -= 2;
    /* insert after the largest j < i - 1 with w[j] >= y (w[-1] = inf) */
    j = i - 1;
    while (j > 0 && w[j - 1] < y) {
      j--;
    }
    memmove(w + j + 1, w + j, (t - j) * sizeof(double));
    memmove(node + j + 1, node + j, (t - j) * sizeof(size_t));
    w[j] = y;
    node[j] = ny;
    t++;
  }
  /* leaf depths in the Garsia-Wachs tree */
  top = 0;
  stack[top++] = node[0];
  depth[node[0]] = 0;
  while (top > 0) {
    x = stack[--top];
    if (x < n) {
      if (depth[x] > PC_MAXLEN) {
        ds_context_free(context, w);
        ds_context_free(context, node);
        return DS_ERR_OVERFLOW;
      }
      out_lengths[x] = (unsigned char)depth[x];
      continue;
    }
    depth[left[x]] = depth[x] + 1;
    depth[right[x]] = depth[x] + 1;
    stack[top++] = left[x];
    stack[top++] = right[x];
  }
  ds_context_free(context, w);
  ds_context_free(context, node);
  return DS_OK;
}

ds_status_t ds_canonical_codes(const unsigned char *lengths, size_t n,
                               unsigned long *out_codes) {
  unsigned long count[34];
  unsigned long next[34];
  unsigned long code = 0;
  size_t i;
  unsigned b;
  if (n > 0 && (lengths == NULL || out_codes == NULL)) {
    return DS_ERR_NULL;
  }
  memset(count, 0, sizeof(count));
  for (i = 0; i < n; i++) {
    if (lengths[i] > 32) {
      return DS_ERR_INVALID;
    }
    count[lengths[i]]++;
  }
  if (ds_prefix_code_check(lengths, n) == DS_PREFIX_CODE_OVERFULL) {
    return DS_ERR_INVALID;
  }
  count[0] = 0;
  for (b = 1; b <= 32; b++) {
    code = (code + count[b - 1]) << 1;
    next[b] = code;
  }
  for (i = 0; i < n; i++) {
    if (lengths[i] > 0) {
      out_codes[i] = next[lengths[i]]++;
    } else {
      out_codes[i] = 0;
    }
  }
  return DS_OK;
}

ds_status_t ds_alphabetic_codes(const unsigned char *lengths, size_t n,
                                unsigned long *out_codes) {
  unsigned long c = 0;
  size_t i;
  size_t prev = 0;
  if (n > 0 && (lengths == NULL || out_codes == NULL)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n; i++) {
    if (lengths[i] == 0 || lengths[i] > 32) {
      return DS_ERR_INVALID;
    }
  }
  for (i = 0; i < n; i++) {
    if (i > 0) {
      c++;
      if (lengths[i] >= lengths[prev]) {
        c <<= (unsigned)(lengths[i] - lengths[prev]);
      } else {
        c >>= (unsigned)(lengths[prev] - lengths[i]);
      }
    }
    out_codes[i] = c;
    prev = i;
  }
  return DS_OK;
}

ds_prefix_code_kind_t ds_prefix_code_check(const unsigned char *lengths,
                                           size_t n) {
  size_t count[PC_MAXLEN + 1];
  size_t remaining = 0;
  size_t i;
  size_t avail = 1; /* free nodes at the current depth */
  int capped = 0;
  memset(count, 0, sizeof(count));
  for (i = 0; i < n; i++) {
    if (lengths[i] > 0) {
      count[lengths[i]]++;
      remaining++;
    }
  }
  for (i = 1; i <= PC_MAXLEN; i++) {
    if (!capped) {
      avail *= 2;
    }
    if (avail < count[i] && !capped) {
      return DS_PREFIX_CODE_OVERFULL;
    }
    if (!capped) {
      avail -= count[i];
    }
    remaining -= count[i];
    if (!capped && avail > remaining) {
      capped = 1; /* can never reach zero again */
    }
  }
  return !capped && avail == 0 ? DS_PREFIX_CODE_COMPLETE
                               : DS_PREFIX_CODE_INCOMPLETE;
}

/* ---- coding tree ---------------------------------------------------- */

static ds_status_t ct_new_node(ds_coding_tree_t *t, size_t *out) {
  if (t->nodes == t->capacity) {
    return DS_ERR_INTERNAL;
  }
  t->child[2 * t->nodes] = PC_NONE;
  t->child[2 * t->nodes + 1] = PC_NONE;
  t->symbol[t->nodes] = PC_NONE;
  *out = t->nodes++;
  return DS_OK;
}

ds_status_t ds_coding_tree_build(ds_coding_tree_t *tree,
                                 const unsigned long *codes,
                                 const unsigned char *lengths, size_t n,
                                 ds_context_t *context) {
  size_t cap = 1;
  size_t i;
  size_t cur;
  size_t nx;
  unsigned b;
  unsigned bit;
  if (tree == NULL || (n > 0 && (codes == NULL || lengths == NULL))) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n; i++) {
    if (lengths[i] > 32) {
      return DS_ERR_INVALID;
    }
    cap += lengths[i];
  }
  tree->child = (size_t *)ds_context_alloc(context, 2 * cap * sizeof(size_t));
  tree->symbol = (size_t *)ds_context_alloc(context, cap * sizeof(size_t));
  tree->capacity = cap;
  tree->nodes = 0;
  tree->context = context;
  if (tree->child == NULL || tree->symbol == NULL) {
    ds_coding_tree_destroy(tree);
    return DS_ERR_ALLOC;
  }
  ct_new_node(tree, &cur);
  for (i = 0; i < n; i++) {
    if (lengths[i] == 0) {
      continue;
    }
    cur = 0;
    for (b = lengths[i]; b-- > 0;) {
      if (tree->symbol[cur] != PC_NONE) {
        ds_coding_tree_destroy(tree);
        return DS_ERR_INVALID; /* passes through another codeword */
      }
      bit = (unsigned)((codes[i] >> b) & 1UL);
      nx = tree->child[2 * cur + bit];
      if (nx == PC_NONE) {
        ct_new_node(tree, &nx);
        tree->child[2 * cur + bit] = nx;
      }
      cur = nx;
    }
    if (tree->symbol[cur] != PC_NONE || tree->child[2 * cur] != PC_NONE ||
        tree->child[2 * cur + 1] != PC_NONE) {
      ds_coding_tree_destroy(tree);
      return DS_ERR_INVALID;
    }
    tree->symbol[cur] = i;
  }
  return DS_OK;
}

void ds_coding_tree_destroy(ds_coding_tree_t *tree) {
  if (tree == NULL) {
    return;
  }
  ds_context_free(tree->context, tree->child);
  ds_context_free(tree->context, tree->symbol);
  tree->child = NULL;
  tree->symbol = NULL;
  tree->nodes = 0;
}

ds_status_t ds_coding_tree_decode(const ds_coding_tree_t *tree,
                                  ds_bit_reader_t *reader, size_t *out_symbol) {
  size_t cur = 0;
  unsigned long bit;
  ds_status_t st;
  if (tree == NULL || reader == NULL || out_symbol == NULL) {
    return DS_ERR_NULL;
  }
  if (tree->nodes == 0) {
    return DS_ERR_INVALID;
  }
  while (tree->symbol[cur] == PC_NONE) {
    st = ds_bit_reader_get(reader, 1, &bit);
    if (st != DS_OK) {
      return cur == 0 ? DS_NOT_FOUND : DS_ERR_INVALID;
    }
    cur = tree->child[2 * cur + bit];
    if (cur == PC_NONE) {
      return DS_ERR_INVALID;
    }
  }
  *out_symbol = tree->symbol[cur];
  return DS_OK;
}

/* ---- static Huffman compression ------------------------------------- */

size_t ds_huffman_compress_bound(size_t n) {
  if (n > (((size_t)-1) - PC_HEADER - 8) / 3) {
    return (size_t)-1;
  }
  return PC_HEADER + 3 * n + 8;
}

ds_status_t ds_huffman_compress(const unsigned char *in, size_t n,
                                unsigned char *out, size_t capacity,
                                size_t *out_length, ds_context_t *context) {
  unsigned long freq[256];
  unsigned char len[256];
  unsigned long code[256];
  ds_bit_writer_t w;
  size_t i;
  ds_status_t st;
  if ((in == NULL && n > 0) || out == NULL || out_length == NULL) {
    return DS_ERR_NULL;
  }
  if (n > 0xFFFFFFFFUL) {
    return DS_ERR_OVERFLOW;
  }
  if (capacity < PC_HEADER) {
    return DS_ERR_OVERFLOW;
  }
  memset(freq, 0, sizeof(freq));
  for (i = 0; i < n; i++) {
    freq[in[i]]++;
  }
  st = ds_huffman_lengths(freq, 256, 2, len, context);
  if (st == DS_OK) {
    st = ds_huffman_limit_lengths(len, 256, PC_HUFF_LIMIT, context);
  }
  if (st == DS_OK) {
    st = ds_canonical_codes(len, 256, code);
  }
  if (st != DS_OK) {
    return st;
  }
  out[0] = (unsigned char)((n >> 24) & 0xFFu);
  out[1] = (unsigned char)((n >> 16) & 0xFFu);
  out[2] = (unsigned char)((n >> 8) & 0xFFu);
  out[3] = (unsigned char)(n & 0xFFu);
  memcpy(out + 4, len, 256);
  ds_bit_writer_init(&w, out + PC_HEADER, capacity - PC_HEADER);
  for (i = 0; i < n; i++) {
    st = ds_bit_writer_put(&w, code[in[i]], len[in[i]]);
    if (st != DS_OK) {
      return st;
    }
  }
  *out_length = PC_HEADER + ds_bit_writer_bytes(&w);
  return DS_OK;
}

ds_status_t ds_huffman_decompress(const unsigned char *in, size_t n,
                                  unsigned char *out, size_t capacity,
                                  size_t *out_length, ds_context_t *context) {
  unsigned char len[256];
  unsigned long code[256];
  ds_coding_tree_t tree;
  ds_bit_reader_t r;
  size_t count;
  size_t i;
  size_t sym;
  ds_status_t st;
  if (in == NULL || out_length == NULL) {
    return DS_ERR_NULL;
  }
  if (n < PC_HEADER) {
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
  memcpy(len, in + 4, 256);
  for (i = 0; i < 256; i++) {
    if (len[i] > PC_HUFF_LIMIT) {
      return DS_ERR_INVALID;
    }
  }
  st = ds_canonical_codes(len, 256, code);
  if (st != DS_OK) {
    return st;
  }
  st = ds_coding_tree_build(&tree, code, len, 256, context);
  if (st != DS_OK) {
    return st;
  }
  ds_bit_reader_init(&r, in + PC_HEADER, n - PC_HEADER);
  for (i = 0; i < count; i++) {
    st = ds_coding_tree_decode(&tree, &r, &sym);
    if (st != DS_OK) {
      st = DS_ERR_INVALID;
      break;
    }
    out[i] = (unsigned char)sym;
  }
  ds_coding_tree_destroy(&tree);
  return st;
}
