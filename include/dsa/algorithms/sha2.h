#ifndef DS_SHA2_H
#define DS_SHA2_H

/*
 * SHA-2 message digests (FIPS 180-4): SHA-256, SHA-384 and SHA-512, and
 * HMAC (RFC 2104) over each. Strict C89: the 64-bit words of SHA-384 and
 * SHA-512 are kept as pairs of 32-bit halves, so no 64-bit integer type
 * is needed.
 *
 * Each digest streams: init, update any number of times, then final,
 * which leaves the state to be initialised again. The one-shot functions
 * hash a single buffer.
 *
 * ds_digest_ops_t puts a digest behind function pointers, for algorithms
 * that take the hash as a parameter (RDF dataset canonicalization, linked
 * data signatures): the caller provides state_size bytes of state, aligned
 * as malloc would align them. ds_digest_sha256(), ds_digest_sha384() and
 * ds_digest_sha512() describe the three digests here; any other digest
 * plugs in the same way.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_SHA256_SIZE 32
#define DS_SHA384_SIZE 48
#define DS_SHA512_SIZE 64
/* the largest digest here, for buffers that hold any of them */
#define DS_DIGEST_MAX_SIZE 64

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

/* SHA-384 is SHA-512 with other initial values, cut to 48 bytes */
typedef struct ds_sha512 {
  unsigned long h[16]; /* eight 64-bit words, high half first */
  unsigned char buf[128];
  size_t buf_len;
  unsigned long bytes_lo; /* message length, 32-bit halves */
  unsigned long bytes_hi;
  size_t size; /* bytes final writes: 48 or 64 */
} ds_sha512_t;

void ds_sha384_init(ds_sha512_t *s);
void ds_sha512_init(ds_sha512_t *s);
void ds_sha512_update(ds_sha512_t *s, const void *data, size_t len);
/* writes s->size bytes */
void ds_sha512_final(ds_sha512_t *s, unsigned char *out);
void ds_sha384(const void *data, size_t len, unsigned char *out);
void ds_sha512(const void *data, size_t len, unsigned char *out);
void ds_hmac_sha384(const void *key, size_t key_len, const void *data,
                    size_t len, unsigned char *out);
void ds_hmac_sha512(const void *key, size_t key_len, const void *data,
                    size_t len, unsigned char *out);

typedef struct ds_digest_ops {
  const char *name;  /* "SHA-256", "SHA-384", "SHA-512" */
  size_t size;       /* digest bytes, at most DS_DIGEST_MAX_SIZE */
  size_t block;      /* input block bytes */
  size_t state_size; /* bytes of state */
  void (*init)(void *state);
  void (*update)(void *state, const void *data, size_t len);
  void (*final)(void *state, unsigned char *out);
} ds_digest_ops_t;

const ds_digest_ops_t *ds_digest_sha256(void);
const ds_digest_ops_t *ds_digest_sha384(void);
const ds_digest_ops_t *ds_digest_sha512(void);

/* the digest as 2 * size lowercase hexadecimal digits and a NUL */
void ds_digest_hex(const unsigned char *digest, size_t size, char *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_SHA2_H */
