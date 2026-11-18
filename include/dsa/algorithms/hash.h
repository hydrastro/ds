#ifndef DS_ALGO_HASH_H
#define DS_ALGO_HASH_H

/*
 * Non-cryptographic hash functions and checksums.
 *
 * Every function returns a 32-bit value in an unsigned long and gives the
 * same result on every platform (bytes are read individually, so alignment
 * and endianness do not matter). None of them are suitable against an
 * adversary: use a keyed hash such as SipHash for hash tables exposed to
 * untrusted input, and a cryptographic hash where integrity matters.
 *
 *   ds_hash_fnv1a32     tiny and fast for short keys
 *   ds_hash_murmur3_32  MurmurHash3 x86_32, good distribution, seeded
 *   ds_hash_xxh32       xxHash32, fast on longer inputs, seeded
 *   ds_hash_mix32       integer finalizer (MurmurHash3 fmix32)
 *   ds_hash_combine32   mix a value into a running hash (boost style)
 *
 * Checksums take the previous value so data can be fed in chunks; start
 * with 0 for CRC-32/CRC-32C and 1 for Adler-32:
 *   ds_crc32 (IEEE 802.3, as zlib/PNG/Ethernet), ds_crc32c (Castagnoli,
 *   as iSCSI/ext4/SSE4.2), ds_adler32 (zlib), ds_fletcher16.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned long ds_hash_fnv1a32(const void *data, size_t length);
unsigned long ds_hash_murmur3_32(const void *data, size_t length,
                                 unsigned long seed);
unsigned long ds_hash_xxh32(const void *data, size_t length,
                            unsigned long seed);
unsigned long ds_hash_mix32(unsigned long value);
unsigned long ds_hash_combine32(unsigned long hash, unsigned long value);
unsigned long ds_hash_cstr(const char *text);

unsigned long ds_crc32(unsigned long crc, const void *data, size_t length);
unsigned long ds_crc32c(unsigned long crc, const void *data, size_t length);
unsigned long ds_adler32(unsigned long adler, const void *data, size_t length);
unsigned int ds_fletcher16(const void *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_HASH_H */
