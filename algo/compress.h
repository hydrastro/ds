#ifndef DS_ALGO_COMPRESS_H
#define DS_ALGO_COMPRESS_H

/*
 * Compression schemes over byte buffers (bits written with the bit I/O of
 * prefix_code.h, into caller buffers: DS_ERR_OVERFLOW when they are too
 * small).
 *
 *   arithmetic coding   Witten-Neal-Cleary integer coder (16-bit code
 *                       values, totals below 2^14, so every product fits
 *                       in 32 bits) driven by a pluggable model; the
 *                       ready-made adaptive order-0 model keeps counts in
 *                       a Fenwick tree and halves them when they get large
 *   LZW                 Lempel-Ziv-Welch with variable code width (9 up to
 *                       max_bits); space-constrained LZW by a dictionary
 *                       cap with a pluggable policy once it is full:
 *                       freeze it, or reset it with a CLEAR code
 *   move-to-front       the transform and its inverse
 *   BSTW                Bentley-Sleator-Tarjan-Wei: each byte is coded by
 *                       its position in a move-to-front list (Elias gamma),
 *                       first occurrences by an escape and the raw byte
 *   star encoding       reversible text transform replacing dictionary words
 *                       by codewords of '*' and a few letters, to make text
 *                       more compressible for a later stage
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include "prefix_code.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- arithmetic coding ---------------------------------------------- */

#define DS_ARITH_MAX_TOTAL 16383UL

/* Cumulative frequencies: symbol s occupies [low, high) of [0, total),
 * total <= DS_ARITH_MAX_TOTAL. find maps a target in [0, total) to its
 * symbol. update adapts after each coded symbol (may be NULL). */
typedef struct ds_arith_model {
  void (*range)(size_t symbol, unsigned long *low, unsigned long *high,
                void *state);
  size_t (*find)(unsigned long target, unsigned long *low, unsigned long *high,
                 void *state);
  unsigned long (*total)(void *state);
  void (*update)(size_t symbol, void *state);
  void *state;
} ds_arith_model_t;

typedef struct ds_arith_encoder {
  ds_bit_writer_t *out;
  unsigned long low;
  unsigned long high;
  unsigned long pending;
} ds_arith_encoder_t;

typedef struct ds_arith_decoder {
  ds_bit_reader_t *in;
  unsigned long low;
  unsigned long high;
  unsigned long value;
} ds_arith_decoder_t;

void ds_arith_encoder_init(ds_arith_encoder_t *e, ds_bit_writer_t *out);
ds_status_t ds_arith_encode(ds_arith_encoder_t *e,
                            const ds_arith_model_t *model, size_t symbol);
ds_status_t ds_arith_encoder_finish(ds_arith_encoder_t *e);
/* Reads past the end of input as zero bits. */
void ds_arith_decoder_init(ds_arith_decoder_t *d, ds_bit_reader_t *in);
ds_status_t ds_arith_decode(ds_arith_decoder_t *d,
                            const ds_arith_model_t *model, size_t *out_symbol);

/* Adaptive order-0 model over `symbols` (<= 4096) symbols, all starting
 * at count 1. */
typedef struct ds_arith_adaptive_model {
  unsigned long *tree; /* Fenwick tree of counts */
  size_t symbols;
  unsigned long total;
  size_t top_bit;
  ds_context_t *context;
} ds_arith_adaptive_model_t;

ds_status_t ds_arith_adaptive_model_init(ds_arith_adaptive_model_t *m,
                                         size_t symbols, ds_context_t *context);
void ds_arith_adaptive_model_destroy(ds_arith_adaptive_model_t *m);
/* Fill a ds_arith_model_t that uses m. */
void ds_arith_adaptive_model_bind(ds_arith_adaptive_model_t *m,
                                  ds_arith_model_t *out);

/* Bytes with the adaptive model; the length is stored (4 bytes). */
size_t ds_arith_compress_bound(size_t n);
ds_status_t ds_arith_compress(const unsigned char *in, size_t n,
                              unsigned char *out, size_t capacity,
                              size_t *out_length, ds_context_t *context);
ds_status_t ds_arith_decompress(const unsigned char *in, size_t n,
                                unsigned char *out, size_t capacity,
                                size_t *out_length, ds_context_t *context);

/* ---- LZW ------------------------------------------------------------ */

typedef enum ds_lzw_full_policy {
  DS_LZW_FREEZE = 0,
  DS_LZW_RESET = 1
} ds_lzw_full_policy_t;

/* max_bits in [9, 16]; the dictionary holds at most 2^max_bits codes. */
size_t ds_lzw_compress_bound(size_t n);
ds_status_t ds_lzw_compress(const unsigned char *in, size_t n,
                            unsigned max_bits, ds_lzw_full_policy_t policy,
                            unsigned char *out, size_t capacity,
                            size_t *out_length, ds_context_t *context);
/* Settings are read from the stream. */
ds_status_t ds_lzw_decompress(const unsigned char *in, size_t n,
                              unsigned char *out, size_t capacity,
                              size_t *out_length, ds_context_t *context);

/* ---- move-to-front and BSTW ----------------------------------------- */

/* out may equal in. */
void ds_move_to_front_encode(const unsigned char *in, size_t n,
                             unsigned char *out);
void ds_move_to_front_decode(const unsigned char *in, size_t n,
                             unsigned char *out);
size_t ds_bstw_compress_bound(size_t n);
ds_status_t ds_bstw_compress(const unsigned char *in, size_t n,
                             unsigned char *out, size_t capacity,
                             size_t *out_length);
ds_status_t ds_bstw_decompress(const unsigned char *in, size_t n,
                               unsigned char *out, size_t capacity,
                               size_t *out_length);

/* ---- star encoding -------------------------------------------------- */

/* Dictionary of words made of letters (A-Z, a-z), most frequent first;
 * within one length the i-th word becomes '*' repeated, its last digits
 * replaced by i in base 52 (the most frequent word of each length is all
 * stars). Words are maximal letter runs of the text; '*' and '\\' in the
 * text are escaped with '\\'. The length needed goes to *out_length even
 * on DS_ERR_OVERFLOW. Words that are not letters only, or repeated, give
 * DS_ERR_INVALID. */
ds_status_t ds_star_encode(const unsigned char *text, size_t n,
                           const unsigned char *const *words,
                           const size_t *lens, size_t count, unsigned char *out,
                           size_t capacity, size_t *out_length,
                           ds_context_t *context);
ds_status_t ds_star_decode(const unsigned char *in, size_t n,
                           const unsigned char *const *words,
                           const size_t *lens, size_t count, unsigned char *out,
                           size_t capacity, size_t *out_length,
                           ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_COMPRESS_H */
