#ifndef DS_LD_SIGNATURE_H
#define DS_LD_SIGNATURE_H

/*
 * Linked data signatures (LDS): a digital signature, and the information
 * to verify it, for linked data, after the W3C Linked Data Signatures 1.0
 * draft.
 *
 * A dataset is a list of N-Quads statements. Signing canonicalises the
 * dataset and the proof options, hashes both with SHA-256, and signs the
 * 64-byte concatenation (options hash, then dataset hash) with a pluggable
 * signature suite; verification recomputes it. The default suite is
 * HMAC-SHA256 with a shared key; asymmetric suites plug in through the
 * sign and verify callbacks.
 *
 * Canonicalisation sorts and deduplicates the statements after relabelling
 * blank nodes canonically: every blank node gets a hash of the statements
 * it occurs in (itself as _:a, other blank nodes as _:z), refined with its
 * neighbours' hashes until the partition is stable, and is renamed
 * _:c14n0, _:c14n1, ... in hash order (blank nodes that remain
 * indistinguishable keep their relative input order). Isomorphic datasets
 * without such symmetric blank nodes canonicalise identically.
 *
 * Profile and limits: this is a colour-refinement canonicalisation, NOT
 * the W3C RDF Dataset Canonicalization algorithm (RDFC-1.0 / URDNA2015):
 * datasets whose blank nodes stay symmetric after refinement may
 * canonicalise differently depending on input order, and signatures are
 * not interoperable with other Linked Data Proof implementations. The
 * default suite is a shared-key MAC (HMAC-SHA256), i.e. an integrity
 * check between parties sharing the key, not a public-key signature.
 *
 * SHA-256 and HMAC-SHA256 are exposed for reuse. Memory comes from the
 * ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_SHA256_SIZE 32

typedef struct ds_sha256 {
  unsigned long h[8];
  unsigned char buf[64];
  size_t buf_len;
  unsigned long bytes_lo; /* message length, 32-bit halves */
  unsigned long bytes_hi;
} ds_sha256_t;

void ds_sha256_init(ds_sha256_t *s);
void ds_sha256_update(ds_sha256_t *s, const void *data, size_t len);
void ds_sha256_final(ds_sha256_t *s, unsigned char *out);
void ds_sha256(const void *data, size_t len, unsigned char *out);
void ds_hmac_sha256(const void *key, size_t key_len, const void *data,
                    size_t len, unsigned char *out);

/* Canonical form of the statements (each an N-Quads line without the
 * newline): the lines, relabelled, sorted, unique, each ending in '\n'.
 * *out is allocated from the context (free with ds_context_free). */
ds_status_t ds_lds_canonicalize(const char *const *quads, size_t n, char **out,
                                size_t *out_len, ds_context_t *context);

/* A signature suite: sign the to-be-signed bytes into sig (capacity in
 * *sig_len, actual length out); verify returns whether sig matches. */
typedef ds_status_t (*ds_lds_sign_func_t)(const unsigned char *tbs, size_t len,
                                          unsigned char *sig, size_t *sig_len,
                                          void *user);
typedef bool (*ds_lds_verify_func_t)(const unsigned char *tbs, size_t len,
                                     const unsigned char *sig, size_t sig_len,
                                     void *user);

typedef struct ds_lds_suite {
  ds_lds_sign_func_t sign;     /* NULL: HMAC-SHA256 with the key below */
  ds_lds_verify_func_t verify; /* NULL: HMAC-SHA256 with the key below */
  void *user;
  const unsigned char *key; /* for the default suite */
  size_t key_len;
} ds_lds_suite_t;

/* Proof options are name/value pairs (created, creator, nonce, domain,
 * ...), canonicalised as sorted "name value" lines. */
typedef struct ds_lds_option {
  const char *name;
  const char *value;
} ds_lds_option_t;

/* The 64 bytes that get signed. */
ds_status_t ds_lds_tbs(const char *const *quads, size_t n,
                       const ds_lds_option_t *options, size_t n_options,
                       unsigned char *out, ds_context_t *context);
ds_status_t ds_lds_sign(const char *const *quads, size_t n,
                        const ds_lds_option_t *options, size_t n_options,
                        const ds_lds_suite_t *suite, unsigned char *sig,
                        size_t *sig_len, ds_context_t *context);
/* DS_OK when the signature verifies, DS_NOT_FOUND when it does not. */
ds_status_t ds_lds_verify(const char *const *quads, size_t n,
                          const ds_lds_option_t *options, size_t n_options,
                          const ds_lds_suite_t *suite, const unsigned char *sig,
                          size_t sig_len, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_LD_SIGNATURE_H */
