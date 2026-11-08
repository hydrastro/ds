#ifndef DS_FM_INDEX_H
#define DS_FM_INDEX_H

/*
 * FM-index (Ferragina & Manzini, 2000): a compressed full-text index over
 * the Burrows-Wheeler transform of a byte string T (n bytes; any byte
 * value, a unique smallest end marker is added internally).
 *
 *   count(P)          backward search, O(|P|) rank queries
 *   locate(P)         plus O(s) LF steps per occurrence, with one suffix
 *                     array entry kept per s text positions
 *   extract(i, len)   O(len + s') LF steps, with the inverse suffix array
 *                     kept every s' positions
 *   backward_step     one step of backward search on a row range, for
 *                     callers driving their own search (approximate
 *                     matching, bidirectional schemes)
 *
 * The rank structure over the BWT is pluggable (ds_fm_occ_ops_t):
 *
 *   ds_fm_occ_table()        sampled occurrence counts (two-level, per
 *                            symbol) and the BWT: fastest rank,
 *                            about n * (2 + sigma / 32) bytes
 *   ds_fm_occ_wavelet()      wavelet matrix: ceil(log2 sigma) bits per
 *                            symbol plus rank directories, O(log sigma)
 *                            rank and access
 *
 * The suffix array is built by the configured builder: any function with
 * the signature of algo/stringology's ds_suffix_array (sorting suffixes
 * so that a proper prefix comes first) fits; the default is an internal
 * O(n log n) prefix-doubling radix sort. Memory comes from the context.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef ds_status_t (*ds_fm_sa_builder_t)(const unsigned char *s, size_t n,
                                          size_t *out_sa,
                                          ds_context_t *context);

typedef struct ds_fm_occ_ops {
  const char *name;
  /* bwt holds n symbols in [0, sigma), sigma <= 65536 */
  ds_status_t (*build)(void **state, const unsigned short *bwt, size_t n,
                       unsigned sigma, ds_context_t *context);
  /* occurrences of c in bwt[0, i) */
  size_t (*rank)(const void *state, unsigned c, size_t i);
  unsigned (*access)(const void *state, size_t i);
  size_t (*bytes)(const void *state);
  void (*destroy)(void *state, ds_context_t *context);
} ds_fm_occ_ops_t;

const ds_fm_occ_ops_t *ds_fm_occ_table(void);
const ds_fm_occ_ops_t *ds_fm_occ_wavelet(void);

/* bit vector with a rank directory (also used by the wavelet backend) */
typedef struct ds_fm_bits {
  unsigned long *words;
  size_t *blocks; /* ones before each 8-word block */
  size_t n;
} ds_fm_bits_t;

typedef struct ds_fm_config {
  const ds_fm_occ_ops_t *occ;    /* NULL: ds_fm_occ_table() */
  size_t sa_sample;              /* 0: 32 */
  size_t isa_sample;             /* 0: sa_sample */
  ds_fm_sa_builder_t sa_builder; /* NULL: internal */
  ds_context_t *context;
} ds_fm_config_t;

typedef struct ds_fm_index {
  size_t n;       /* text length (rows: n + 1) */
  size_t primary; /* row of the whole text (its BWT symbol is the marker) */
  unsigned sigma; /* symbols in use, end marker included */
  size_t C[258];  /* C[c]: rows starting with a symbol below c */
  unsigned short map[256]; /* byte -> symbol (0: absent) */
  unsigned char unmap[257];
  const ds_fm_occ_ops_t *occ;
  void *occ_state;
  ds_fm_bits_t sampled; /* rows whose suffix array entry is kept */
  size_t *sa_samples;   /* in row order */
  size_t *isa_samples;  /* row of text position k * isa_sample */
  size_t sa_sample;
  size_t isa_sample;
  ds_context_t *context;
} ds_fm_index_t;

void ds_fm_config_init(ds_fm_config_t *config);
ds_status_t ds_fm_index_build(ds_fm_index_t *fm, const unsigned char *text,
                              size_t n, const ds_fm_config_t *config);
void ds_fm_index_destroy(ds_fm_index_t *fm);

/* refine the row range [*sp, *ep) by prepending byte c; start from
 * [0, n + 1). Returns false (range emptied) when no row survives. */
bool ds_fm_index_backward_step(const ds_fm_index_t *fm, unsigned char c,
                               size_t *sp, size_t *ep);
/* occurrences of p; out_sp/out_ep (optional) receive the row range */
size_t ds_fm_index_count(const ds_fm_index_t *fm, const unsigned char *p,
                         size_t m, size_t *out_sp, size_t *out_ep);
/* text position of the suffix at row */
size_t ds_fm_index_locate_row(const ds_fm_index_t *fm, size_t row);
/* up to capacity starting positions (unsorted) into out; *out_total gets
 * the number of occurrences */
ds_status_t ds_fm_index_locate(const ds_fm_index_t *fm, const unsigned char *p,
                               size_t m, size_t *out, size_t capacity,
                               size_t *out_total);
/* text[start, start + len) into out */
ds_status_t ds_fm_index_extract(const ds_fm_index_t *fm, size_t start,
                                size_t len, unsigned char *out);
/* the BWT of text + end marker (n + 1 bytes; the marker row holds
 * *out_primary and an arbitrary byte) */
ds_status_t ds_fm_index_bwt(const ds_fm_index_t *fm, unsigned char *out,
                            size_t *out_primary);
size_t ds_fm_index_bytes(const ds_fm_index_t *fm);

#ifdef __cplusplus
}
#endif

#endif /* DS_FM_INDEX_H */
