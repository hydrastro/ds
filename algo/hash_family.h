#ifndef DS_ALGO_HASH_FAMILY_H
#define DS_ALGO_HASH_FAMILY_H

/*
 * Hash function families and incremental hashes (all results 32-bit,
 * platform independent).
 *
 * Classic constructions:
 *   ds_hash_pearson()          Pearson's 8-bit table hash (1990) over a
 *                              permutation of 0..255 (the default table or
 *                              your own); ds_hash_pearson_wide() chains it
 *                              to 1..4 bytes of output.
 *   ds_hash_multiplicative()   Knuth's multiplication method: the top
 *                              `bits` bits of key * floor(2^32 / phi).
 *   ds_hash_order_preserving() monotone map of [low, high] onto buckets
 *                              (a "linear hash": order is kept, so range
 *                              scans stay sequential).
 *
 * Universal families (Carter and Wegman): drawing a member at random gives
 * collision probability about 1/m for any two fixed keys, which defeats
 * inputs chosen against a fixed hash. Members are drawn with any
 * ds_random_func_t (e.g. ds_rng_callback()):
 *   ds_universal_hash_t   ((a x + b) mod p) mod m, p = 2^31 - 1
 *   ds_multiply_shift_t   (a x mod 2^32) >> (32 - bits), a odd
 *                         (Dietzfelbinger et al.), 2-universal-ish and fast
 *   ds_tabulation_hash_t  simple tabulation (Zobrist; Patrascu-Thorup):
 *                         XOR of four random tables indexed by the key's
 *                         bytes, 3-independent
 *   ds_twisted_tabulation_hash_t
 *                         twisted tabulation (Patrascu-Thorup): the tables
 *                         of characters 2..c also yield a twister, XORed
 *                         into the first character before its lookup,
 *                         which gives Chernoff-style concentration
 *
 * Incremental (rolling) hashes for sliding windows, the basis of
 * Rabin-Karp and content-defined chunking:
 *   ds_rolling_hash_t     polynomial hash mod 2^31 - 1: push a byte,
 *                         roll (drop the oldest byte, add a new one) in O(1)
 *   ds_buzhash_t          cyclic polynomial (buzhash): rotations and XORs
 *                         of a random byte table, no multiplications
 */

#include "elem.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_HASH_MERSENNE31 0x7FFFFFFFUL

unsigned int ds_hash_pearson(const void *data, size_t length,
                             const unsigned char *table);
/* `bytes` in 1..4 output bytes; table NULL for the default permutation. */
unsigned long ds_hash_pearson_wide(const void *data, size_t length,
                                   unsigned int bytes,
                                   const unsigned char *table);
/* Fill table with a random permutation of 0..255. */
void ds_hash_pearson_table(unsigned char table[256], ds_random_func_t random,
                           void *random_state);
unsigned long ds_hash_multiplicative(unsigned long key, unsigned int bits);
unsigned long ds_hash_order_preserving(unsigned long key, unsigned long low,
                                       unsigned long high,
                                       unsigned long buckets);

typedef struct ds_universal_hash {
  unsigned long a; /* 1 .. p - 1 */
  unsigned long b; /* 0 .. p - 1 */
  unsigned long m; /* range */
} ds_universal_hash_t;

void ds_universal_hash_init(ds_universal_hash_t *h, unsigned long m,
                            ds_random_func_t random, void *random_state);
/* key is reduced mod p first. Result in [0, m). */
unsigned long ds_universal_hash(const ds_universal_hash_t *h,
                                unsigned long key);

typedef struct ds_multiply_shift {
  unsigned long a; /* odd */
  unsigned int bits;
} ds_multiply_shift_t;

void ds_multiply_shift_init(ds_multiply_shift_t *h, unsigned int bits,
                            ds_random_func_t random, void *random_state);
/* The low 32 bits of key are hashed. Result in [0, 2^bits). */
unsigned long ds_multiply_shift(const ds_multiply_shift_t *h,
                                unsigned long key);

typedef struct ds_tabulation_hash {
  unsigned long table[4][256];
} ds_tabulation_hash_t;

void ds_tabulation_hash_init(ds_tabulation_hash_t *h, ds_random_func_t random,
                             void *random_state);
unsigned long ds_tabulation_hash(const ds_tabulation_hash_t *h,
                                 unsigned long key);
/* Byte strings: tables are reused cyclically and the position is mixed in.
 */
unsigned long ds_tabulation_hash_bytes(const ds_tabulation_hash_t *h,
                                       const void *data, size_t length);

typedef struct ds_twisted_tabulation_hash {
  unsigned long table[4][256]; /* hash parts (table 0: the first byte) */
  unsigned char twist[4][256]; /* twisters of tables 1..3 */
} ds_twisted_tabulation_hash_t;

void ds_twisted_tabulation_hash_init(ds_twisted_tabulation_hash_t *h,
                                     ds_random_func_t random,
                                     void *random_state);
/* (t, r) = XOR of T_i[x_i] over bytes 1..3; h = r ^ T_0[x_0 ^ t]. */
unsigned long ds_twisted_tabulation_hash(const ds_twisted_tabulation_hash_t *h,
                                         unsigned long key);
/* Byte strings: the twister of bytes 1..n-1 (tables reused cyclically,
 * position mixed in) twists the first byte. */
unsigned long
ds_twisted_tabulation_hash_bytes(const ds_twisted_tabulation_hash_t *h,
                                 const void *data, size_t length);

typedef struct ds_rolling_hash {
  unsigned long base;  /* < p */
  unsigned long value; /* hash of the current window */
  unsigned long power; /* base^(window - 1) mod p */
  size_t window;
} ds_rolling_hash_t;

/* base 0 picks 256. */
void ds_rolling_hash_init(ds_rolling_hash_t *h, unsigned long base);
/* Append a byte (the window grows by one). */
void ds_rolling_hash_push(ds_rolling_hash_t *h, unsigned char in);
/* Drop `out` (the oldest byte) and append `in`: the window size stays. */
void ds_rolling_hash_roll(ds_rolling_hash_t *h, unsigned char out,
                          unsigned char in);
/* Hash of a whole buffer with the same base (equals pushing each byte). */
unsigned long ds_rolling_hash_of(unsigned long base, const void *data,
                                 size_t length);

typedef struct ds_buzhash {
  unsigned long table[256];
  unsigned long value;
  size_t window;
} ds_buzhash_t;

void ds_buzhash_init(ds_buzhash_t *h, ds_random_func_t random,
                     void *random_state);
void ds_buzhash_reset(ds_buzhash_t *h);
void ds_buzhash_push(ds_buzhash_t *h, unsigned char in);
void ds_buzhash_roll(ds_buzhash_t *h, unsigned char out, unsigned char in);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_HASH_FAMILY_H */
