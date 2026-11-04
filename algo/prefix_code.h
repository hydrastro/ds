#ifndef DS_ALGO_PREFIX_CODE_H
#define DS_ALGO_PREFIX_CODE_H

/*
 * Prefix codes: construction, coding trees and static Huffman coding.
 *
 * Code construction from symbol frequencies (code lengths out, codes
 * from the lengths):
 *   ds_huffman_lengths        Huffman coding, binary or k-ary (zero-weight
 *                             dummy symbols pad the k-ary tree)
 *   ds_huffman_limit_lengths  caps the longest code at max_length while
 *                             keeping Kraft's inequality (the JPEG Annex K
 *                             adjustment)
 *   ds_shannon_fano_lengths   Shannon-Fano coding: sort by frequency and
 *                             split into halves of nearly equal weight
 *   ds_hu_tucker_lengths      order-preserving (alphabetic) Huffman coding
 *                             by the Garsia-Wachs algorithm: codewords keep
 *                             the symbols' order
 *   ds_canonical_codes        canonical codes for lengths (binary)
 *   ds_alphabetic_codes       order-preserving codes for alphabetic lengths
 *   ds_prefix_code_check      Kraft sum test (complete, valid, invalid)
 *
 * ds_coding_tree_t is the binary tree of any prefix code (codes given
 * explicitly); it decodes bit by bit and rejects codes that are not
 * prefix-free.
 *
 * ds_bit_writer_t / ds_bit_reader_t write and read bits MSB-first in a
 * caller buffer (no allocation; DS_ERR_OVERFLOW when it is full).
 *
 * ds_huffman_compress / ds_huffman_decompress: static Huffman coding of
 * bytes; the output stores the length (4 bytes), the 256 code lengths (one
 * byte each) and the canonical codes, lengths capped at 24 bits.
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_bit_writer {
  unsigned char *data;
  size_t capacity; /* bytes */
  size_t bits;     /* bits written */
} ds_bit_writer_t;

typedef struct ds_bit_reader {
  const unsigned char *data;
  size_t bit_count; /* readable bits */
  size_t position;
} ds_bit_reader_t;

void ds_bit_writer_init(ds_bit_writer_t *w, unsigned char *data,
                        size_t capacity);
/* The low `count` (<= 32) bits of value, most significant first. */
ds_status_t ds_bit_writer_put(ds_bit_writer_t *w, unsigned long value,
                              unsigned count);
size_t ds_bit_writer_bytes(const ds_bit_writer_t *w);
void ds_bit_reader_init(ds_bit_reader_t *r, const unsigned char *data,
                        size_t bytes);
/* DS_NOT_FOUND past the end. */
ds_status_t ds_bit_reader_get(ds_bit_reader_t *r, unsigned count,
                              unsigned long *out_value);

/* lengths[i] = 0 for symbols of frequency 0. With one used symbol its
 * length is 1. arity in [2, 256]. Lengths can exceed 32 for very skewed
 * frequencies; use ds_huffman_limit_lengths when codes must fit a word. */
ds_status_t ds_huffman_lengths(const unsigned long *freqs, size_t n,
                               unsigned arity, unsigned char *out_lengths,
                               ds_context_t *context);
/* Binary codes only. DS_ERR_INVALID if max_length is too small for the
 * number of used symbols. Needs n bytes from the context when it has to
 * change anything. */
ds_status_t ds_huffman_limit_lengths(unsigned char *lengths, size_t n,
                                     unsigned max_length,
                                     ds_context_t *context);
ds_status_t ds_shannon_fano_lengths(const unsigned long *freqs, size_t n,
                                    unsigned char *out_lengths,
                                    ds_context_t *context);
/* Every symbol gets a codeword (zero frequencies included), codewords in
 * symbol order. */
ds_status_t ds_hu_tucker_lengths(const unsigned long *freqs, size_t n,
                                 unsigned char *out_lengths,
                                 ds_context_t *context);
/* Lengths <= 32. Symbols of length 0 get no code. */
ds_status_t ds_canonical_codes(const unsigned char *lengths, size_t n,
                               unsigned long *out_codes);
ds_status_t ds_alphabetic_codes(const unsigned char *lengths, size_t n,
                                unsigned long *out_codes);

typedef enum ds_prefix_code_kind {
  DS_PREFIX_CODE_COMPLETE = 0, /* Kraft sum exactly 1 */
  DS_PREFIX_CODE_INCOMPLETE = 1,
  DS_PREFIX_CODE_OVERFULL = 2 /* no prefix code has these lengths */
} ds_prefix_code_kind_t;
ds_prefix_code_kind_t ds_prefix_code_check(const unsigned char *lengths,
                                           size_t n);

typedef struct ds_coding_tree {
  size_t *child;  /* 2 per node; ~0 = none */
  size_t *symbol; /* per node; ~0 for internal nodes */
  size_t nodes;
  size_t capacity;
  ds_context_t *context;
} ds_coding_tree_t;

/* codes[i] of lengths[i] bits (length 0: no code). DS_ERR_INVALID if the
 * code is not prefix-free. */
ds_status_t ds_coding_tree_build(ds_coding_tree_t *tree,
                                 const unsigned long *codes,
                                 const unsigned char *lengths, size_t n,
                                 ds_context_t *context);
void ds_coding_tree_destroy(ds_coding_tree_t *tree);
/* Next symbol from the reader: DS_NOT_FOUND at the end of input,
 * DS_ERR_INVALID on a bit sequence that leads nowhere. */
ds_status_t ds_coding_tree_decode(const ds_coding_tree_t *tree,
                                  ds_bit_reader_t *reader, size_t *out_symbol);

/* Bound on the compressed size of n bytes. */
size_t ds_huffman_compress_bound(size_t n);
ds_status_t ds_huffman_compress(const unsigned char *in, size_t n,
                                unsigned char *out, size_t capacity,
                                size_t *out_length, ds_context_t *context);
/* The decompressed length is stored: *out_length receives it even when
 * DS_ERR_OVERFLOW says capacity is too small. */
ds_status_t ds_huffman_decompress(const unsigned char *in, size_t n,
                                  unsigned char *out, size_t capacity,
                                  size_t *out_length, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_PREFIX_CODE_H */
