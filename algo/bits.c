#include "bits.h"
#include <limits.h>

#define BITS_ULONG ((unsigned int)(CHAR_BIT * sizeof(unsigned long)))
#define BITS_SIZE ((unsigned int)(CHAR_BIT * sizeof(size_t)))
#define M32 0xFFFFFFFFUL

#if defined(__GNUC__) || defined(__clang__)
#define BITS_HAVE_BUILTINS 1
#else
#define BITS_HAVE_BUILTINS 0
#endif

unsigned int ds_popcount(unsigned long x) {
#if BITS_HAVE_BUILTINS
  return (unsigned int)__builtin_popcountl(x);
#else
  unsigned int n = 0;
  while (x != 0) {
    x &= x - 1;
    n++;
  }
  return n;
#endif
}

unsigned int ds_clz(unsigned long x) {
  if (x == 0) {
    return BITS_ULONG;
  }
#if BITS_HAVE_BUILTINS
  return (unsigned int)__builtin_clzl(x);
#else
  {
    unsigned int n = 0;
    unsigned long top = 1UL << (BITS_ULONG - 1);
    while ((x & top) == 0) {
      x <<= 1;
      n++;
    }
    return n;
  }
#endif
}

unsigned int ds_ctz(unsigned long x) {
  if (x == 0) {
    return BITS_ULONG;
  }
#if BITS_HAVE_BUILTINS
  return (unsigned int)__builtin_ctzl(x);
#else
  {
    unsigned int n = 0;
    while ((x & 1UL) == 0) {
      x >>= 1;
      n++;
    }
    return n;
  }
#endif
}

bool ds_parity(unsigned long x) {
  return (ds_popcount(x) & 1U) != 0;
}

unsigned long ds_lowest_set_bit(unsigned long x) {
  return x & (~x + 1UL);
}

bool ds_is_pow2(size_t x) {
  return x != 0 && (x & (x - 1)) == 0;
}

size_t ds_next_pow2(size_t x) {
  size_t p = 1;
  if (x <= 1) {
    return 1;
  }
  if (x > ((size_t)1 << (BITS_SIZE - 1))) {
    return 0; /* would overflow */
  }
  while (p < x) {
    p <<= 1;
  }
  return p;
}

unsigned int ds_floor_log2(size_t x) {
  unsigned int r = 0;
  while (x > 1) {
    x >>= 1;
    r++;
  }
  return r;
}

unsigned int ds_ceil_log2(size_t x) {
  return x <= 1 ? 0U : ds_floor_log2(x - 1) + 1U;
}

unsigned long ds_rotl32(unsigned long x, unsigned int r) {
  x &= M32;
  r &= 31U;
  if (r == 0) {
    return x;
  }
  return ((x << r) | (x >> (32U - r))) & M32;
}

unsigned long ds_rotr32(unsigned long x, unsigned int r) {
  return ds_rotl32(x, (32U - (r & 31U)) & 31U);
}

unsigned long ds_bit_reverse32(unsigned long x) {
  x &= M32;
  x = ((x >> 1) & 0x55555555UL) | ((x & 0x55555555UL) << 1);
  x = ((x >> 2) & 0x33333333UL) | ((x & 0x33333333UL) << 2);
  x = ((x >> 4) & 0x0F0F0F0FUL) | ((x & 0x0F0F0F0FUL) << 4);
  x = ((x >> 8) & 0x00FF00FFUL) | ((x & 0x00FF00FFUL) << 8);
  x = ((x >> 16) & 0x0000FFFFUL) | ((x & 0x0000FFFFUL) << 16);
  return x & M32;
}

unsigned long ds_gray_encode(unsigned long x) {
  return x ^ (x >> 1);
}

unsigned long ds_gray_decode(unsigned long gray) {
  unsigned long x = gray;
  unsigned int shift;
  for (shift = 1; shift < BITS_ULONG; shift <<= 1) {
    x ^= x >> shift;
  }
  return x;
}

static unsigned long bits_spread16(unsigned long v) {
  v &= 0xFFFFUL;
  v = (v | (v << 8)) & 0x00FF00FFUL;
  v = (v | (v << 4)) & 0x0F0F0F0FUL;
  v = (v | (v << 2)) & 0x33333333UL;
  v = (v | (v << 1)) & 0x55555555UL;
  return v;
}

static unsigned int bits_compact16(unsigned long v) {
  v &= 0x55555555UL;
  v = (v | (v >> 1)) & 0x33333333UL;
  v = (v | (v >> 2)) & 0x0F0F0F0FUL;
  v = (v | (v >> 4)) & 0x00FF00FFUL;
  v = (v | (v >> 8)) & 0x0000FFFFUL;
  return (unsigned int)v;
}

unsigned long ds_morton2_encode(unsigned int x, unsigned int y) {
  return bits_spread16(x) | (bits_spread16(y) << 1);
}

void ds_morton2_decode(unsigned long code, unsigned int *out_x,
                       unsigned int *out_y) {
  if (out_x != NULL) {
    *out_x = bits_compact16(code);
  }
  if (out_y != NULL) {
    *out_y = bits_compact16(code >> 1);
  }
}
