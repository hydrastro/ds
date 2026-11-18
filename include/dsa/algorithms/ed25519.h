#ifndef DS_ED25519_H
#define DS_ED25519_H

/*
 * Ed25519 signatures (RFC 8032: "pure" EdDSA over edwards25519 with
 * SHA-512) and X25519 key agreement (RFC 7748), in strict C89 with no
 * 64-bit integer type: field elements are 32 limbs of 8 bits in unsigned
 * long, so every product and sum fits in 32 bits.
 *
 * Keys: the private key is a 32-byte seed (random bytes from the caller's
 * source); ds_ed25519_public_key derives the 32-byte public key. Signing
 * is deterministic (RFC 8032), so it needs no randomness.
 *
 * Verification follows RFC 8032's checks: S must be below the group
 * order, and A and R must be canonical encodings of curve points (y below
 * p, no negative zero). It uses the cofactorless equation [S]B = R + [k]A
 * compared by encoding, as RFC 8032 section 5.1.7 permits and most
 * libraries do; batch verification is not offered.
 *
 * Secrets: key derivation, signing and X25519 run in constant time with
 * respect to the seed, the scalar and the nonce (no branches or table
 * indices depend on them; swaps and selections use masks), as far as the
 * compiler keeps the code's shape. Verification handles public data only
 * and is not constant time.
 *
 * Speed: a signature or a public key costs one scalar multiplication, a
 * verification two: a few milliseconds each with this portable
 * arithmetic (sign 4 ms, verify 8 ms, X25519 2 ms at -O2 on a current
 * x86-64). Nothing allocates.
 */

#include <dsa/core/status.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_ED25519_SEED_SIZE 32
#define DS_ED25519_PUBLIC_SIZE 32
#define DS_ED25519_SIGNATURE_SIZE 64
#define DS_X25519_SIZE 32

/* the public key of a seed */
ds_status_t ds_ed25519_public_key(const unsigned char *seed,
                                  unsigned char *public_key);
/* sign len bytes of msg with seed, whose public key is public_key (NULL:
 * derived here, at the cost of a second scalar multiplication); sig gets
 * DS_ED25519_SIGNATURE_SIZE bytes */
ds_status_t ds_ed25519_sign(const unsigned char *seed,
                            const unsigned char *public_key, const void *msg,
                            size_t len, unsigned char *sig);
/* DS_OK when sig is a valid signature of msg under public_key,
 * DS_NOT_FOUND when it is not (including malformed keys and signatures) */
ds_status_t ds_ed25519_verify(const unsigned char *public_key, const void *msg,
                              size_t len, const unsigned char *sig);

/* X25519: out = the u-coordinate of scalar * point (both 32 bytes, scalar
 * clamped as RFC 7748 says, the point's top bit ignored).
 * DS_ERR_INVALID when the result is zero, as for a point of small order:
 * the shared secret would carry no secret. */
ds_status_t ds_x25519(const unsigned char *scalar, const unsigned char *point,
                      unsigned char *out);
/* the public key of scalar: scalar * 9 */
ds_status_t ds_x25519_public_key(const unsigned char *scalar,
                                 unsigned char *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_ED25519_H */
