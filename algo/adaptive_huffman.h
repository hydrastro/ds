#ifndef DS_ALGO_ADAPTIVE_HUFFMAN_H
#define DS_ALGO_ADAPTIVE_HUFFMAN_H

/*
 * Adaptive (dynamic) Huffman coding of bytes: encoder and decoder keep
 * the same Huffman tree of the counts seen so far and update it after
 * every symbol, so no table is transmitted. New symbols are sent as the
 * code of the NYT ("not yet transmitted") leaf followed by the raw byte.
 *
 * Update policy (pluggable):
 *   DS_ADAPTIVE_FGK     Faller-Gallager-Knuth: exchange each node on the
 *                       path with the leader of its weight block, then
 *                       increment
 *   DS_ADAPTIVE_VITTER  Vitter's algorithm (Lambda): within a weight
 *                       block leaves precede internal nodes, kept by
 *                       sliding nodes past the following block; this
 *                       minimises the height and the sum of path lengths
 *                       among the Huffman trees of the counts and codes
 *                       within one bit per symbol of static Huffman
 *
 * The tree lives in fixed arrays inside the struct (no allocation). Nodes
 * are stored by their implicit number: weights never decrease with the
 * number and every parent is numbered above its children (the sibling
 * property). ds_adaptive_huffman_valid() checks these invariants.
 */

#include "../lib/status.h"
#include "prefix_code.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_ADAPTIVE_HUFFMAN_NODES 513 /* 256 leaves, NYT, 256 internal */

typedef enum ds_adaptive_huffman_policy {
  DS_ADAPTIVE_FGK = 0,
  DS_ADAPTIVE_VITTER = 1
} ds_adaptive_huffman_policy_t;

typedef struct ds_adaptive_huffman {
  unsigned long weight[DS_ADAPTIVE_HUFFMAN_NODES];
  unsigned short parent[DS_ADAPTIVE_HUFFMAN_NODES];
  unsigned short left[DS_ADAPTIVE_HUFFMAN_NODES];
  unsigned short right[DS_ADAPTIVE_HUFFMAN_NODES];
  unsigned short symbol[DS_ADAPTIVE_HUFFMAN_NODES];
  unsigned short leaf[256]; /* node of each symbol, or none */
  unsigned short nyt;
  unsigned short lowest; /* lowest node number in use */
  ds_adaptive_huffman_policy_t policy;
} ds_adaptive_huffman_t;

void ds_adaptive_huffman_init(ds_adaptive_huffman_t *h,
                              ds_adaptive_huffman_policy_t policy);
/* Code one byte and update the tree. */
ds_status_t ds_adaptive_huffman_encode(ds_adaptive_huffman_t *h,
                                       ds_bit_writer_t *out,
                                       unsigned char symbol);
/* DS_NOT_FOUND at the end of input, DS_ERR_INVALID on bad input. */
ds_status_t ds_adaptive_huffman_decode(ds_adaptive_huffman_t *h,
                                       ds_bit_reader_t *in,
                                       unsigned char *out_symbol);
/* Sibling property, weight sums and (Vitter) leaves-before-internal. */
bool ds_adaptive_huffman_valid(const ds_adaptive_huffman_t *h);

/* Whole buffers: the output stores the length (4 bytes) and the policy. */
size_t ds_adaptive_huffman_compress_bound(size_t n);
ds_status_t ds_adaptive_huffman_compress(const unsigned char *in, size_t n,
                                         ds_adaptive_huffman_policy_t policy,
                                         unsigned char *out, size_t capacity,
                                         size_t *out_length);
ds_status_t ds_adaptive_huffman_decompress(const unsigned char *in, size_t n,
                                           unsigned char *out, size_t capacity,
                                           size_t *out_length);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_ADAPTIVE_HUFFMAN_H */
