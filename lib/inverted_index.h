#ifndef DS_INVERTED_INDEX_H
#define DS_INVERTED_INDEX_H

/*
 * Inverted indexes over a growing document collection.
 *
 * Terms are byte strings, interned in a dictionary (term ids in order of
 * first appearance). Documents are added in increasing id order as token
 * sequences (or as text through a tokenizer); each term keeps a posting
 * list of gaps, frequencies and position gaps, compressed by a codec
 * chosen at init (ds_invidx_options_t.codec):
 *
 *   ds_invidx_codec_vbyte()    variable byte (the default): 7 bits per
 *                              byte, byte aligned, fast
 *   ds_invidx_codec_gamma()    Elias gamma: 2 log2(v + 1) + 1 bits, good
 *                              for small numbers (frequencies, dense gaps)
 *   ds_invidx_codec_delta()    Elias delta: log2 v + 2 log2 log2 v + O(1)
 *                              bits, better for large gaps
 *   ds_invidx_codec_rice(k)    Golomb-Rice with parameter 2^k: v >> k in
 *                              unary, then k bits; optimal for geometric
 *                              gaps of mean about 2^k ln 2
 *
 * A codec is an ops table (bits, put, get over a bit stream), so others
 * can be supplied. The granularity is chosen at init:
 *
 *   DS_INVIDX_DOCUMENT  inverted file index: per term, the documents and
 *                       the term frequency in each
 *   DS_INVIDX_POSITIONAL full inverted index: also every position, for
 *                       phrase queries
 *   DS_INVIDX_BLOCK     block addressing index: postings name blocks of
 *                       block_size consecutive documents only (a much
 *                       smaller index); queries return candidate blocks to
 *                       be verified by scanning their documents
 *
 * With keep_forward the index also stores the forward index: per
 * document, its distinct terms with frequencies.
 *
 * Queries: posting cursors (documents or blocks, frequencies, positions),
 * boolean queries (all of "must", at least one of "should" if any, none
 * of "must not") by merging, phrase queries on positional indexes, and
 * Okapi BM25 ranking (top k) on document and positional indexes. Results
 * are allocated from the index's context and released with
 * ds_invidx_result_free.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_INVIDX_NONE (~(size_t)0)

typedef enum ds_invidx_mode {
  DS_INVIDX_DOCUMENT,
  DS_INVIDX_POSITIONAL,
  DS_INVIDX_BLOCK
} ds_invidx_mode_t;

typedef struct ds_invidx_term {
  const void *text;
  size_t length;
} ds_invidx_term_t;

/* Find the next token at or after *cursor: set *start, *length, advance
 * *cursor past it; false when there are no more tokens. */
typedef bool (*ds_invidx_tokenizer_fn)(const char *text, size_t length,
                                       size_t *cursor, size_t *start,
                                       size_t *token_length, void *user);

/* An integer codec over bit streams (bits are filled from the least
 * significant bit of each byte up). put writes v at bit *pos of a buffer
 * that is zero from *pos on and has room for bits(v) more bits, advancing
 * *pos; get reads one value back. Values are below SIZE_MAX. */
typedef struct ds_invidx_codec {
  const char *name;
  size_t (*bits)(size_t v, unsigned param);
  void (*put)(unsigned char *buf, size_t *pos, size_t v, unsigned param);
  size_t (*get)(const unsigned char *buf, size_t *pos, unsigned param);
  unsigned param;
} ds_invidx_codec_t;

const ds_invidx_codec_t *ds_invidx_codec_vbyte(void);
const ds_invidx_codec_t *ds_invidx_codec_gamma(void);
const ds_invidx_codec_t *ds_invidx_codec_delta(void);
/* k from 0 to 31 (NULL otherwise) */
const ds_invidx_codec_t *ds_invidx_codec_rice(unsigned k);

typedef struct ds_invidx_posting_list {
  unsigned char *bytes;
  size_t length; /* bytes in use */
  size_t capacity;
  size_t bits; /* bits in use */
  size_t df;   /* documents (or blocks) */
  size_t cf;   /* occurrences */
  size_t last; /* last document / block id written */
} ds_invidx_posting_list_t;

typedef struct ds_inverted_index {
  ds_invidx_mode_t mode;
  size_t block_size;
  bool keep_forward;
  const ds_invidx_codec_t *codec;
  /* dictionary */
  unsigned char *text;
  size_t text_used;
  size_t text_capacity;
  size_t *term_off;
  size_t *term_len;
  ds_invidx_posting_list_t *postings;
  size_t terms;
  size_t term_capacity;
  size_t *slot;
  size_t slots;
  /* documents in addition order */
  size_t *doc_id;
  size_t *doc_len;
  size_t *fwd_off; /* into fwd (keep_forward) */
  size_t docs;
  size_t doc_capacity;
  unsigned char *fwd;
  size_t fwd_used;
  size_t fwd_capacity;
  size_t total_tokens;
  /* scratch */
  size_t *scratch;
  size_t scratch_capacity;
  ds_context_t *context;
} ds_inverted_index_t;

typedef struct ds_invidx_cursor {
  const ds_inverted_index_t *index;
  size_t term; /* DS_INVIDX_NONE: empty */
  size_t at;   /* bit offset in the posting list */
  size_t doc;  /* document id, or block number in block mode */
  size_t freq; /* occurrences in the document (0 in block mode) */
  size_t positions_left;
  size_t position;
  bool started;
} ds_invidx_cursor_t;

typedef struct ds_invidx_result {
  size_t *docs;   /* document ids (block numbers in block mode) */
  double *scores; /* BM25 only, else NULL */
  size_t count;
} ds_invidx_result_t;

ds_status_t ds_invidx_init(ds_inverted_index_t *idx, ds_invidx_mode_t mode,
                           size_t block_size, bool keep_forward,
                           ds_context_t *context);
/* the same with every choice in one place (NULL: document mode, vbyte) */
typedef struct ds_invidx_options {
  ds_invidx_mode_t mode;
  size_t block_size; /* block mode */
  bool keep_forward;
  const ds_invidx_codec_t *codec; /* NULL: ds_invidx_codec_vbyte() */
} ds_invidx_options_t;
ds_status_t ds_invidx_init_opts(ds_inverted_index_t *idx,
                                const ds_invidx_options_t *options,
                                ds_context_t *context);
void ds_invidx_destroy(ds_inverted_index_t *idx);
/* tokens in order (position = index); doc_id must exceed the last one
 * (DS_ERR_STATE) and be below SIZE_MAX (DS_ERR_RANGE) */
ds_status_t ds_invidx_add(ds_inverted_index_t *idx, size_t doc_id,
                          const ds_invidx_term_t *tokens, size_t count);
/* tokenizer NULL: maximal runs of ASCII letters and digits; fold_case
 * lowercases ASCII letters */
ds_status_t ds_invidx_add_text(ds_inverted_index_t *idx, size_t doc_id,
                               const char *text, size_t length,
                               ds_invidx_tokenizer_fn tokenizer, void *user,
                               bool fold_case);

size_t ds_invidx_terms(const ds_inverted_index_t *idx);
size_t ds_invidx_documents(const ds_inverted_index_t *idx);
/* DS_INVIDX_NONE when absent */
size_t ds_invidx_term_id(const ds_inverted_index_t *idx, const void *text,
                         size_t length);
ds_status_t ds_invidx_term_text(const ds_inverted_index_t *idx, size_t term,
                                const void **text, size_t *length);
size_t ds_invidx_document_frequency(const ds_inverted_index_t *idx,
                                    const void *text, size_t length);
/* compressed size of all posting lists, in bytes */
size_t ds_invidx_postings_bytes(const ds_inverted_index_t *idx);
/* documents (ids, in order) covered by a block */
ds_status_t ds_invidx_block_documents(const ds_inverted_index_t *idx,
                                      size_t block, const size_t **ids,
                                      size_t *count);
/* forward index: the distinct term ids of a document with frequencies,
 * ascending by term id; *count is the full number (DS_ERR_RANGE if it did
 * not fit). DS_ERR_STATE without keep_forward, DS_ERR_RANGE for an
 * unknown document. */
ds_status_t ds_invidx_document_terms(const ds_inverted_index_t *idx,
                                     size_t doc_id, size_t *terms,
                                     size_t *freqs, size_t capacity,
                                     size_t *count);

void ds_invidx_cursor_open(const ds_inverted_index_t *idx, const void *text,
                           size_t length, ds_invidx_cursor_t *cursor);
bool ds_invidx_cursor_next(ds_invidx_cursor_t *cursor);
/* positional mode: the next position of the current document */
bool ds_invidx_cursor_next_position(ds_invidx_cursor_t *cursor,
                                    size_t *position);

ds_status_t
ds_invidx_boolean(const ds_inverted_index_t *idx, const ds_invidx_term_t *must,
                  size_t must_count, const ds_invidx_term_t *should,
                  size_t should_count, const ds_invidx_term_t *must_not,
                  size_t must_not_count, ds_invidx_result_t *result);
/* documents containing the terms at consecutive positions */
ds_status_t ds_invidx_phrase(const ds_inverted_index_t *idx,
                             const ds_invidx_term_t *terms, size_t count,
                             ds_invidx_result_t *result);
/* top_k documents by Okapi BM25 (k1, b; e.g. 1.2 and 0.75), best first,
 * ties by smaller id */
ds_status_t ds_invidx_bm25(const ds_inverted_index_t *idx,
                           const ds_invidx_term_t *terms, size_t count,
                           double k1, double b, size_t top_k,
                           ds_invidx_result_t *result);
void ds_invidx_result_free(const ds_inverted_index_t *idx,
                           ds_invidx_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* DS_INVERTED_INDEX_H */
