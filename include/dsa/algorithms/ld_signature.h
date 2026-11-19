#ifndef DS_LD_SIGNATURE_H
#define DS_LD_SIGNATURE_H

/*
 * Linked data signatures: a digital signature, and the information to
 * verify it, for linked data (W3C Linked Data Signatures 1.0, and the
 * hashing step of the Data Integrity cryptosuites over RDF, such as
 * eddsa-rdfc-2022 and ecdsa-rdfc-2019).
 *
 * Signing canonicalizes the document and the proof options with RDF
 * Dataset Canonicalization (RDFC-1.0, dsa/algorithms/rdf_canon.h), hashes each
 * canonical form, and signs the concatenation (proof options hash, then
 * document hash) with a pluggable signature suite; verification
 * recomputes it. The data can come in two ways:
 *
 * - As RDF datasets (dsa/algorithms/rdf_canon.h) for both the document and the
 * proof options: ds_lds_hash_data computes the hash data of Data Integrity,
 *   H(c14n(proof options)) || H(c14n(document)), with any
 * dsa/algorithms/sha2.h digest (SHA-256 by default, SHA-384 for P-384 suites).
 * Given the same RDF it matches other implementations; turning JSON-LD into
 * RDF is up to the caller. ds_lds_sign_data and ds_lds_verify_data then sign
 * or check those bytes.
 * - As N-Quads statements (one per string) with proof options as
 *   name/value pairs: ds_lds_tbs, ds_lds_sign and ds_lds_verify. The
 *   statements are canonicalized with RDFC-1.0 too, but the options are
 *   hashed as sorted "name value" lines, a local convention, so these
 *   signatures only verify here.
 *
 * Suites: sign and verify callbacks receive the hash data, so any
 * signature scheme plugs in; without callbacks the suite is HMAC-SHA256
 * with a shared key, an integrity check between parties that share the
 * key rather than a public-key signature. ds_lds_ed25519_suite signs with
 * Ed25519 (dsa/algorithms/ed25519.h), the scheme of the eddsa-rdfc-2022
 * cryptosuite: with ds_lds_hash_data over SHA-256 it gives that suite's
 * signatures, whose proofValue is the multibase (base58-btc) text of the 64
 * bytes (ds_lds_multibase_encode), and whose verification method names a
 * Multikey (ds_lds_ed25519_multikey: "z6Mk...").
 *
 * Canonicalization errors pass through: DS_ERR_INVALID for statements
 * that are not N-Quads, DS_ERR_RANGE for poison datasets (see
 * dsa/algorithms/rdf_canon.h). SHA-256 and HMAC-SHA256 are declared in
 * dsa/algorithms/sha2.h, which this header includes. Memory comes from the
 * ds_context_t.
 */

#include <dsa/algorithms/rdf_canon.h>
#include <dsa/algorithms/sha2.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RDFC-1.0 canonical form of the statements (each one N-Quads statement,
 * with or without its newline): the distinct quads relabelled, sorted,
 * each ending in '\n'. *out is allocated from the context (free with
 * ds_context_free). */
ds_status_t ds_lds_canonicalize(const char *const *quads, size_t n, char **out,
                                size_t *out_len, ds_context_t *context);

/* A signature suite: sign the hash data into sig (capacity in *sig_len,
 * actual length out); verify returns whether sig matches. */
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

/* Ed25519 as a suite: seed (32 bytes; NULL for a key that only
 * verifies) and public key (32 bytes). The suite points at key, which
 * must outlive it. Signing needs room for DS_ED25519_SIGNATURE_SIZE (64)
 * bytes and gives DS_ERR_RANGE otherwise, DS_ERR_UNSUPPORTED without a
 * seed. */
typedef struct ds_lds_ed25519_key {
  const unsigned char *seed;
  const unsigned char *public_key;
} ds_lds_ed25519_key_t;
void ds_lds_ed25519_suite(ds_lds_suite_t *suite, ds_lds_ed25519_key_t *key);

/* Multibase base58-btc text: 'z', then the bytes in Bitcoin's base58
 * alphabet, each leading zero byte as '1'. out has room for *out_len
 * characters and the NUL; *out_len gets the text's length, or with
 * DS_ERR_RANGE the room it needs (2 + len * 138 / 100 + 1 always
 * suffices). */
ds_status_t ds_lds_multibase_encode(const unsigned char *data, size_t len,
                                    char *out, size_t *out_len);
/* the bytes of len characters of base58-btc multibase text; out has room
 * for *out_len bytes and *out_len gets the count (DS_ERR_RANGE: too
 * small, with the room needed); DS_ERR_INVALID for text that is not
 * 'z' and base58 digits */
ds_status_t ds_lds_multibase_decode(const char *text, size_t len,
                                    unsigned char *out, size_t *out_len);
/* An Ed25519 public key as a Multikey: the multibase text of the
 * multicodec prefix 0xed 0x01 and the key, 48 characters ("z6Mk..."), in
 * out (room for 49 with the NUL); and back (DS_ERR_INVALID for text that
 * is not an Ed25519 Multikey). */
#define DS_LDS_ED25519_MULTIKEY_LEN 48
ds_status_t ds_lds_ed25519_multikey(const unsigned char *public_key,
                                    char *out);
ds_status_t ds_lds_ed25519_from_multikey(const char *text, size_t len,
                                         unsigned char *public_key);

/* Proof options are name/value pairs (created, creator, nonce, domain,
 * ...), canonicalised as sorted "name value" lines. */
typedef struct ds_lds_option {
  const char *name;
  const char *value;
} ds_lds_option_t;

/* the hash data of Data Integrity for RDF: digest (NULL: SHA-256) of the
 * RDFC-1.0 canonical proof options, then of the canonical document; out
 * holds 2 * digest size bytes (at most 2 * DS_DIGEST_MAX_SIZE), *out_len
 * gets the length. c14n: canonicalization options (NULL: defaults). */
ds_status_t ds_lds_hash_data(const ds_rdf_dataset_t *document,
                             const ds_rdf_dataset_t *proof,
                             const ds_digest_ops_t *digest,
                             const ds_rdfc_options_t *c14n, unsigned char *out,
                             size_t *out_len);
/* sign or check any hash data with a suite; verify gives DS_OK when the
 * signature matches and DS_NOT_FOUND when it does not */
ds_status_t ds_lds_sign_data(const unsigned char *data, size_t len,
                             const ds_lds_suite_t *suite, unsigned char *sig,
                             size_t *sig_len);
ds_status_t ds_lds_verify_data(const unsigned char *data, size_t len,
                               const ds_lds_suite_t *suite,
                               const unsigned char *sig, size_t sig_len);

/* The 64 bytes that get signed for N-Quads statements and name/value
 * options: SHA-256 of the options lines, then of the canonical
 * statements. */
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
                          const ds_lds_suite_t *suite,
                          const unsigned char *sig, size_t sig_len,
                          ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_LD_SIGNATURE_H */
