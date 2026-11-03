#ifndef DS_ALGO_BITS_H
#define DS_ALGO_BITS_H

/*
 * Bit manipulation on unsigned long and size_t, portable to any width.
 * GCC and Clang builtins are used when available; the fallbacks are
 * branch-light loops. Counts and indices are in bits; clz/ctz of zero return
 * the width of unsigned long.
 *
 * The *32 functions operate on the low 32 bits. Morton (Z-order) codes
 * interleave two 16-bit coordinates into a 32-bit key, which turns 2D
 * proximity into 1D order (spatial indexing, cache-friendly layouts).
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned int ds_popcount(unsigned long x);
unsigned int ds_clz(unsigned long x);
unsigned int ds_ctz(unsigned long x);
bool ds_parity(unsigned long x);
unsigned long ds_lowest_set_bit(unsigned long x);

bool ds_is_pow2(size_t x);
size_t ds_next_pow2(size_t x);
unsigned int ds_floor_log2(size_t x);
unsigned int ds_ceil_log2(size_t x);

unsigned long ds_rotl32(unsigned long x, unsigned int r);
unsigned long ds_rotr32(unsigned long x, unsigned int r);
unsigned long ds_bit_reverse32(unsigned long x);
unsigned long ds_gray_encode(unsigned long x);
unsigned long ds_gray_decode(unsigned long gray);
unsigned long ds_morton2_encode(unsigned int x, unsigned int y);
void ds_morton2_decode(unsigned long code, unsigned int *out_x,
                       unsigned int *out_y);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_BITS_H */
