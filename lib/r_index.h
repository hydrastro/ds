#ifndef DS_R_INDEX_H
#define DS_R_INDEX_H

/*
 * The r-index (Gagie, Navarro and Prezza, 2018): a full-text index whose
 * size grows with the number r of runs in the Burrows-Wheeler transform
 * rather than with the text length, which suits repetitive collections
 * (versions of a document, genomes of one species) where r is far below n.
 *
 * Counting is backward search over the run-length BWT (Makinen and
 * Navarro's RLFM): the rank of a symbol before a row comes from the run
 * that holds the row (a binary search over run starts) and the runs of
 * that symbol before it (a binary search over the symbol's runs, with
 * their cumulative lengths). Locating uses the toehold lemma: the backward
 * search carries the suffix array value of its range's last row, taking a
 * stored value at a run end whenever the prepended symbol differs from the
 * last row's; the other occurrences follow by phi(SA[i]) = SA[i - 1],
 * which needs only suffix array values at run boundaries: phi(x) =
 * phi(x') + (x - x') for the predecessor x' of x among the values at run
 * starts.
 *
 * Space: 6r + 1 words and 2r bytes (run starts and symbols, each symbol's
 * runs with cumulative lengths, run-end samples, and the phi table sorted
 * by text position) plus fixed tables. count is O(m log r) for m pattern
 * bytes; locate adds O(log r) per occurrence. There is no extraction:
 * that needs further samples, which a plain FM-index (lib/fm_index)
 * keeps.
 *
 * Construction builds the full suffix array (by default with SA-IS from
 * lib/suffix_array; any lib/fm_index builder fits) and keeps only the run
 * data, so it needs n + 1 words while running. The text may hold any byte
 * values. Memory comes from the context.
 */

#include "context.h"
#include "fm_index.h"
#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_rindex {
  size_t n;    /* text length: n + 1 rows with the end marker */
  size_t runs; /* r */
  /* symbols: byte + 1, and 0 for the end marker */
  size_t C[258];       /* rows starting with a symbol below c */
  size_t *start;       /* runs + 1: each run's first row, then n + 1 */
  unsigned short *sym; /* runs: each run's symbol */
  size_t sym_off[258]; /* symbol c's runs: list[sym_off[c] .. sym_off[c+1]) */
  size_t *list;        /* runs: run numbers grouped by symbol, increasing */
  size_t *cum;         /* runs: rows of the symbol in its earlier runs */
  size_t *end_sa;      /* runs: suffix array value at each run's last row */
  size_t *phi_pos;     /* runs: values at run starts, increasing */
  size_t *phi_val;     /* runs: the value at the row before each */
  size_t last_sa;      /* the suffix array value of the last row */
  ds_context_t *context;
} ds_rindex_t;

/* builder NULL: ds_sa_build (SA-IS) */
ds_status_t ds_rindex_build(ds_rindex_t *r, const unsigned char *text, size_t n,
                            ds_fm_sa_builder_t builder, ds_context_t *context);
void ds_rindex_destroy(ds_rindex_t *r);

/* occurrences of p (m bytes); out_sp/out_ep (optional) get the row range
 * [sp, ep). The empty pattern occurs at every position 0 .. n. */
size_t ds_rindex_count(const ds_rindex_t *r, const unsigned char *p, size_t m,
                       size_t *out_sp, size_t *out_ep);
/* up to capacity starting positions (in suffix order, from the last row
 * back) into out; *out_total gets the number of occurrences */
ds_status_t ds_rindex_locate(const ds_rindex_t *r, const unsigned char *p,
                             size_t m, size_t *out, size_t capacity,
                             size_t *out_total);

size_t ds_rindex_runs(const ds_rindex_t *r);
size_t ds_rindex_bytes(const ds_rindex_t *r);
/* the BWT at a row: its byte, or -1 for the end marker */
int ds_rindex_bwt(const ds_rindex_t *r, size_t row);
/* the row of the suffix one position earlier in the text (LF) */
size_t ds_rindex_lf(const ds_rindex_t *r, size_t row);
/* phi: the suffix array value of the row before the one holding text
 * position x (any x but the marker's, n) */
size_t ds_rindex_phi(const ds_rindex_t *r, size_t x);

#ifdef __cplusplus
}
#endif

#endif /* DS_R_INDEX_H */
