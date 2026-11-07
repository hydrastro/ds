#include "space_order.h"
#include <string.h>

#define SO_ULBITS (sizeof(unsigned long) * 8)
#define SO_MAXD 32

static unsigned long so_gray(unsigned long v) { return v ^ (v >> 1); }

static unsigned long so_gray_inverse(unsigned long g) {
  unsigned long v = g;
  unsigned s;
  for (s = 1; s < SO_ULBITS; s <<= 1) {
    v ^= v >> s;
  }
  return v;
}

static unsigned long so_interleave(unsigned long x, unsigned long y,
                                   unsigned bits) {
  unsigned long r = 0;
  unsigned i;
  for (i = 0; i < bits; i++) {
    r |= ((x >> i) & 1UL) << (2 * i);
    r |= ((y >> i) & 1UL) << (2 * i + 1);
  }
  return r;
}

static void so_deinterleave(unsigned long r, unsigned bits, unsigned long *x,
                            unsigned long *y) {
  unsigned i;
  *x = *y = 0;
  for (i = 0; i < bits; i++) {
    *x |= ((r >> (2 * i)) & 1UL) << i;
    *y |= ((r >> (2 * i + 1)) & 1UL) << i;
  }
}

/* Hilbert curve on a 2^bits square (the classic d2xy / xy2d) */
static void so_rot(unsigned long n, unsigned long *x, unsigned long *y,
                   unsigned long rx, unsigned long ry) {
  if (ry == 0) {
    if (rx == 1) {
      *x = n - 1 - *x;
      *y = n - 1 - *y;
    }
    {
      unsigned long t = *x;
      *x = *y;
      *y = t;
    }
  }
}

static unsigned long so_hilbert_xy2d(unsigned bits, unsigned long x,
                                     unsigned long y) {
  unsigned long n = 1UL << bits;
  unsigned long d = 0;
  unsigned long s;
  for (s = n / 2; s > 0; s /= 2) {
    unsigned long rx = (x & s) > 0 ? 1UL : 0UL;
    unsigned long ry = (y & s) > 0 ? 1UL : 0UL;
    d += s * s * ((3UL * rx) ^ ry);
    so_rot(n, &x, &y, rx, ry);
  }
  return d;
}

static void so_hilbert_d2xy(unsigned bits, unsigned long d, unsigned long *x,
                            unsigned long *y) {
  unsigned long n = 1UL << bits;
  unsigned long s;
  unsigned long t = d;
  *x = *y = 0;
  for (s = 1; s < n; s *= 2) {
    unsigned long rx = 1UL & (t / 2);
    unsigned long ry = 1UL & (t ^ rx);
    so_rot(s, x, y, rx, ry);
    *x += s * rx;
    *y += s * ry;
    t /= 4;
  }
}

/* U order digits: (x bit, y bit) -> 0 (0,0), 1 (0,1), 2 (1,1), 3 (1,0) */
static const unsigned so_u_digit[2][2] = {{0, 1}, {3, 2}};
static const unsigned so_u_x[4] = {0, 0, 1, 1};
static const unsigned so_u_y[4] = {0, 1, 1, 0};

ds_status_t ds_space_order_encode(ds_space_order_t order, unsigned long param,
                                  long x, long y, unsigned long *out) {
  unsigned long ux;
  unsigned long uy;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  if (order == DS_ORDER_SPIRAL) {
    /* ring r = max(|x|, |y|); positions of ring r start at (2r - 1)^2 */
    long r = (x < 0 ? -x : x) > (y < 0 ? -y : y) ? (x < 0 ? -x : x)
                                                 : (y < 0 ? -y : y);
    unsigned long ur = (unsigned long)r;
    unsigned long base;
    unsigned long off;
    if (r == 0) {
      *out = 0;
      return DS_OK;
    }
    if (ur > 32767UL) {
      return DS_ERR_OVERFLOW;
    }
    base = (2 * ur - 1) * (2 * ur - 1);
    /* start just above (r, -r), go up the right side, left along the top,
     * down the left side, right along the bottom */
    if (x == r && y > -r) {
      off = (unsigned long)(y + r) - 1;
    } else if (y == r) {
      off = 2 * ur - 1 + (unsigned long)(r - x);
    } else if (x == -r) {
      off = 4 * ur - 1 + (unsigned long)(r - y);
    } else {
      off = 6 * ur - 1 + (unsigned long)(x + r);
    }
    *out = base + off;
    return DS_OK;
  }
  if (x < 0 || y < 0) {
    return DS_ERR_RANGE;
  }
  ux = (unsigned long)x;
  uy = (unsigned long)y;
  switch (order) {
  case DS_ORDER_ROW:
  case DS_ORDER_ROW_PRIME:
    if (param == 0 || ux >= param) {
      return DS_ERR_RANGE;
    }
    if (uy > (~0UL - param) / param) {
      return DS_ERR_OVERFLOW;
    }
    *out = uy * param +
           (order == DS_ORDER_ROW_PRIME && (uy & 1UL) ? param - 1 - ux : ux);
    return DS_OK;
  case DS_ORDER_CANTOR: {
    unsigned long s = ux + uy;
    if (s < ux || s > 65535UL) {
      return DS_ERR_OVERFLOW;
    }
    *out = s * (s + 1) / 2 + uy;
    return DS_OK;
  }
  case DS_ORDER_MORTON:
  case DS_ORDER_HILBERT:
  case DS_ORDER_GRAY:
  case DS_ORDER_DOUBLE_GRAY:
  case DS_ORDER_U: {
    unsigned bits = (unsigned)param;
    if (param == 0 || 2 * param > SO_ULBITS) {
      return DS_ERR_INVALID;
    }
    if ((bits < SO_ULBITS && (ux >> bits) != 0) ||
        (bits < SO_ULBITS && (uy >> bits) != 0)) {
      return DS_ERR_RANGE;
    }
    if (order == DS_ORDER_MORTON) {
      *out = so_interleave(ux, uy, bits);
    } else if (order == DS_ORDER_HILBERT) {
      *out = so_hilbert_xy2d(bits, ux, uy);
    } else if (order == DS_ORDER_GRAY) {
      *out = so_gray_inverse(so_interleave(ux, uy, bits));
    } else if (order == DS_ORDER_DOUBLE_GRAY) {
      *out = so_gray_inverse(so_interleave(so_gray(ux), so_gray(uy), bits));
    } else {
      unsigned long r = 0;
      unsigned i;
      for (i = 0; i < bits; i++) {
        unsigned bx = (unsigned)((ux >> i) & 1UL);
        unsigned by = (unsigned)((uy >> i) & 1UL);
        r |= (unsigned long)so_u_digit[bx][by] << (2 * i);
      }
      *out = r;
    }
    return DS_OK;
  }
  case DS_ORDER_SPIRAL:
  default:
    return DS_ERR_INVALID;
  }
}

ds_status_t ds_space_order_decode(ds_space_order_t order, unsigned long param,
                                  unsigned long position, long *out_x,
                                  long *out_y) {
  unsigned long ux = 0;
  unsigned long uy = 0;
  if (out_x == NULL || out_y == NULL) {
    return DS_ERR_NULL;
  }
  switch (order) {
  case DS_ORDER_SPIRAL: {
    unsigned long r;
    unsigned long base;
    unsigned long off;
    long sr;
    if (position == 0) {
      *out_x = *out_y = 0;
      return DS_OK;
    }
    if (position >= 65535UL * 65535UL) {
      return DS_ERR_OVERFLOW;
    }
    r = 1;
    while ((2 * r + 1) * (2 * r + 1) <= position) {
      r++;
    }
    base = (2 * r - 1) * (2 * r - 1);
    off = position - base;
    sr = (long)r;
    if (off < 2 * r) {
      *out_x = sr;
      *out_y = (long)off + 1 - sr;
    } else if (off < 4 * r) {
      *out_x = sr - (long)(off - (2 * r - 1));
      *out_y = sr;
    } else if (off < 6 * r) {
      *out_x = -sr;
      *out_y = sr - (long)(off - (4 * r - 1));
    } else {
      *out_x = (long)(off - (6 * r - 1)) - sr;
      *out_y = -sr;
    }
    return DS_OK;
  }
  case DS_ORDER_ROW:
  case DS_ORDER_ROW_PRIME:
    if (param == 0) {
      return DS_ERR_INVALID;
    }
    uy = position / param;
    ux = position % param;
    if (order == DS_ORDER_ROW_PRIME && (uy & 1UL)) {
      ux = param - 1 - ux;
    }
    break;
  case DS_ORDER_CANTOR: {
    unsigned long s = 0;
    while ((s + 1) * (s + 2) / 2 <= position) {
      s++;
    }
    uy = position - s * (s + 1) / 2;
    ux = s - uy;
    break;
  }
  case DS_ORDER_MORTON:
  case DS_ORDER_HILBERT:
  case DS_ORDER_GRAY:
  case DS_ORDER_DOUBLE_GRAY:
  case DS_ORDER_U: {
    unsigned bits = (unsigned)param;
    if (param == 0 || 2 * param > SO_ULBITS) {
      return DS_ERR_INVALID;
    }
    if (2 * bits < SO_ULBITS && (position >> (2 * bits)) != 0) {
      return DS_ERR_RANGE;
    }
    if (order == DS_ORDER_MORTON) {
      so_deinterleave(position, bits, &ux, &uy);
    } else if (order == DS_ORDER_HILBERT) {
      so_hilbert_d2xy(bits, position, &ux, &uy);
    } else if (order == DS_ORDER_GRAY) {
      so_deinterleave(so_gray(position), bits, &ux, &uy);
    } else if (order == DS_ORDER_DOUBLE_GRAY) {
      so_deinterleave(so_gray(position), bits, &ux, &uy);
      ux = so_gray_inverse(ux);
      uy = so_gray_inverse(uy);
    } else {
      unsigned i;
      for (i = 0; i < bits; i++) {
        unsigned dg = (unsigned)((position >> (2 * i)) & 3UL);
        ux |= (unsigned long)so_u_x[dg] << i;
        uy |= (unsigned long)so_u_y[dg] << i;
      }
    }
    break;
  }
  default:
    return DS_ERR_INVALID;
  }
  if (ux > (unsigned long)((~0UL) >> 1) || uy > (unsigned long)((~0UL) >> 1)) {
    return DS_ERR_OVERFLOW;
  }
  *out_x = (long)ux;
  *out_y = (long)uy;
  return DS_OK;
}

/* ---- k dimensions
 * ------------------------------------------------------------ */

static ds_status_t so_nd_check(size_t dims, unsigned bits, size_t words) {
  if (dims == 0 || dims > SO_MAXD || bits == 0 || bits > 32 ||
      dims * bits > 32 * words) {
    return DS_ERR_INVALID;
  }
  return DS_OK;
}

/* bit i of the position (0 = least significant) in a word array, most
 * significant word first */
static void so_set_bit(unsigned long *w, size_t words, size_t i) {
  w[words - 1 - i / 32] |= 1UL << (i % 32);
}

static unsigned so_get_bit(const unsigned long *w, size_t words, size_t i) {
  return (unsigned)((w[words - 1 - i / 32] >> (i % 32)) & 1UL);
}

ds_status_t ds_morton_encode_nd(const unsigned long *coords, size_t dims,
                                unsigned bits, unsigned long *out,
                                size_t words) {
  size_t b;
  size_t a;
  ds_status_t st;
  if (coords == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  st = so_nd_check(dims, bits, words);
  if (st != DS_OK) {
    return st;
  }
  memset(out, 0, words * sizeof(unsigned long));
  for (b = 0; b < bits; b++) {
    for (a = 0; a < dims; a++) {
      if ((coords[a] >> b) & 1UL) {
        so_set_bit(out, words, b * dims + a);
      }
    }
  }
  return DS_OK;
}

ds_status_t ds_morton_decode_nd(const unsigned long *position, size_t words,
                                size_t dims, unsigned bits,
                                unsigned long *out_coords) {
  size_t b;
  size_t a;
  ds_status_t st;
  if (position == NULL || out_coords == NULL) {
    return DS_ERR_NULL;
  }
  st = so_nd_check(dims, bits, words);
  if (st != DS_OK) {
    return st;
  }
  for (a = 0; a < dims; a++) {
    out_coords[a] = 0;
  }
  for (b = 0; b < bits; b++) {
    for (a = 0; a < dims; a++) {
      if (so_get_bit(position, words, b * dims + a)) {
        out_coords[a] |= 1UL << b;
      }
    }
  }
  return DS_OK;
}

/* Skilling, "Programming the Hilbert curve" (2004): coordinates <->
 * transposed Hilbert index, in place */
static void so_axes_to_transpose(unsigned long *x, unsigned bits, size_t n) {
  unsigned long m = 1UL << (bits - 1);
  unsigned long q;
  unsigned long p;
  unsigned long t;
  size_t i;
  for (q = m; q > 1; q >>= 1) {
    p = q - 1;
    for (i = 0; i < n; i++) {
      if (x[i] & q) {
        x[0] ^= p;
      } else {
        t = (x[0] ^ x[i]) & p;
        x[0] ^= t;
        x[i] ^= t;
      }
    }
  }
  for (i = 1; i < n; i++) {
    x[i] ^= x[i - 1];
  }
  t = 0;
  for (q = m; q > 1; q >>= 1) {
    if (x[n - 1] & q) {
      t ^= q - 1;
    }
  }
  for (i = 0; i < n; i++) {
    x[i] ^= t;
  }
}

static void so_transpose_to_axes(unsigned long *x, unsigned bits, size_t n) {
  unsigned long nb = 2UL << (bits - 1);
  unsigned long q;
  unsigned long p;
  unsigned long t;
  size_t i;
  t = x[n - 1] >> 1;
  for (i = n - 1; i > 0; i--) {
    x[i] ^= x[i - 1];
  }
  x[0] ^= t;
  for (q = 2; q != nb; q <<= 1) {
    p = q - 1;
    for (i = n; i-- > 0;) {
      if (x[i] & q) {
        x[0] ^= p;
      } else {
        t = (x[0] ^ x[i]) & p;
        x[0] ^= t;
        x[i] ^= t;
      }
    }
  }
}

ds_status_t ds_hilbert_encode_nd(const unsigned long *coords, size_t dims,
                                 unsigned bits, unsigned long *out,
                                 size_t words) {
  unsigned long x[SO_MAXD];
  size_t b;
  size_t a;
  ds_status_t st;
  if (coords == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  st = so_nd_check(dims, bits, words);
  if (st != DS_OK) {
    return st;
  }
  for (a = 0; a < dims; a++) {
    x[a] = coords[a] & (bits == 32 ? 0xFFFFFFFFUL : (1UL << bits) - 1UL);
  }
  so_axes_to_transpose(x, bits, dims);
  /* the transposed form interleaves: bit b of x[a] is index bit
   * b * dims + (dims - 1 - a) */
  memset(out, 0, words * sizeof(unsigned long));
  for (b = 0; b < bits; b++) {
    for (a = 0; a < dims; a++) {
      if ((x[a] >> b) & 1UL) {
        so_set_bit(out, words, b * dims + (dims - 1 - a));
      }
    }
  }
  return DS_OK;
}

ds_status_t ds_hilbert_decode_nd(const unsigned long *position, size_t words,
                                 size_t dims, unsigned bits,
                                 unsigned long *out_coords) {
  size_t b;
  size_t a;
  ds_status_t st;
  if (position == NULL || out_coords == NULL) {
    return DS_ERR_NULL;
  }
  st = so_nd_check(dims, bits, words);
  if (st != DS_OK) {
    return st;
  }
  for (a = 0; a < dims; a++) {
    out_coords[a] = 0;
  }
  for (b = 0; b < bits; b++) {
    for (a = 0; a < dims; a++) {
      if (so_get_bit(position, words, b * dims + (dims - 1 - a))) {
        out_coords[a] |= 1UL << b;
      }
    }
  }
  so_transpose_to_axes(out_coords, bits, dims);
  return DS_OK;
}
