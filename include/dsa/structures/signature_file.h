#ifndef DS_SIGNATURE_FILE_H
#define DS_SIGNATURE_FILE_H

/*
 * Superimposed coding and signature files (Mooers; Roberts; Faloutsos):
 * every word of a record is hashed to k positions in a codeword of m bits
 * (positions can repeat), and the record's signature is the OR of its
 * codewords. A query's signature is built the same way; a record can only
 * contain all the query words if its signature covers the query
 * signature, so the scan yields every match plus a few false drops, which
 * the caller verifies against the records.
 *
 * Words are byte strings; the k bit positions come from a seeded hash
 * (pluggable; one seed per position). Signatures are stored row by
 * row, m bits per record, and scanned sequentially.
 *
 * Concurrency: a signature file may be used by one thread at a time.
 */

#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hash of a word for the given seed in 0..k-1; seeds should give independent
 * probes. The built-in hash costs O(word length + k) per codeword. */
typedef size_t (*ds_signature_hash_func_t)(const unsigned char *word,
                                           size_t len, size_t seed, void *user);

typedef struct ds_signature_file {
  unsigned char *rows; /* records x row_bytes */
  size_t records;
  size_t capacity;
  size_t m;
  size_t k;
  size_t row_bytes;
  ds_signature_hash_func_t
      hash; /* init installs the built-in when passed NULL */
  void *user;
  ds_context_t *context;
} ds_signature_file_t;

/* m bits per signature (>= 8), k bits per codeword (1..m). */
ds_status_t ds_signature_file_init(ds_signature_file_t *f, size_t m, size_t k,
                                   ds_signature_hash_func_t hash, void *user,
                                   ds_context_t *context);
void ds_signature_file_destroy(ds_signature_file_t *f);
size_t ds_signature_file_records(const ds_signature_file_t *f);
/* Superimposes the codeword of word onto sig (row_bytes bytes). */
void ds_signature_superimpose(const ds_signature_file_t *f,
                              const unsigned char *word, size_t len,
                              unsigned char *sig);
/* Appends a record of `count` words; its index is the record number. */
ds_status_t ds_signature_file_add(ds_signature_file_t *f,
                                  const unsigned char *const *words,
                                  const size_t *lens, size_t count,
                                  size_t *out_record);
/* Records whose signatures cover the query's (candidates, in order). */
ds_status_t ds_signature_file_query(const ds_signature_file_t *f,
                                    const unsigned char *const *words,
                                    const size_t *lens, size_t count,
                                    bool (*visit)(size_t record, void *user),
                                    void *user);
/* Fraction of the record's bits that are set (ideal: about one half). */
double ds_signature_file_density(const ds_signature_file_t *f, size_t record);

#ifdef __cplusplus
}
#endif

#endif /* DS_SIGNATURE_FILE_H */
