#include <dsa/algorithms/ed25519.h>
#include <dsa/algorithms/sha2.h>
#include <string.h>

/* ---- the field GF(2^255 - 19) ----
 *
 * An element is 32 limbs of radix 2^8, little-endian, in unsigned long. A
 * carried element has every limb below 2^8 except the lowest, below 2^9;
 * every operation takes carried elements and returns one, so a product of
 * two limbs stays below 2^18, a column of 32 products below 2^23, and the
 * column folded down by 2^256 = 38 below 2^29. */

typedef unsigned long fe[32];

typedef struct ge {
  fe x;
  fe y;
  fe z;
  fe t;
} ge_t; /* extended coordinates: x = X/Z, y = Y/Z, xy = T/Z */

static const fe fe_zero = {0};
static const fe fe_one = {1};
static const fe fe_d = {0xa3, 0x78, 0x59, 0x13, 0xca, 0x4d, 0xeb, 0x75,
                        0xab, 0xd8, 0x41, 0x41, 0x4d, 0x0a, 0x70, 0x00,
                        0x98, 0xe8, 0x79, 0x77, 0x79, 0x40, 0xc7, 0x8c,
                        0x73, 0xfe, 0x6f, 0x2b, 0xee, 0x6c, 0x03, 0x52};
static const fe fe_d2 = {0x59, 0xf1, 0xb2, 0x26, 0x94, 0x9b, 0xd6, 0xeb,
                         0x56, 0xb1, 0x83, 0x82, 0x9a, 0x14, 0xe0, 0x00,
                         0x30, 0xd1, 0xf3, 0xee, 0xf2, 0x80, 0x8e, 0x19,
                         0xe7, 0xfc, 0xdf, 0x56, 0xdc, 0xd9, 0x06, 0x24};
static const fe fe_sqrtm1 = {0xb0, 0xa0, 0x0e, 0x4a, 0x27, 0x1b, 0xee, 0xc4,
                             0x78, 0xe4, 0x2f, 0xad, 0x06, 0x18, 0x43, 0x2f,
                             0xa7, 0xd7, 0xfb, 0x3d, 0x99, 0x00, 0x4d, 0x2b,
                             0x0b, 0xdf, 0xc1, 0x4f, 0x80, 0x24, 0x83, 0x2b};
static const fe fe_base_x = {0x1a, 0xd5, 0x25, 0x8f, 0x60, 0x2d, 0x56, 0xc9,
                             0xb2, 0xa7, 0x25, 0x95, 0x60, 0xc7, 0x2c, 0x69,
                             0x5c, 0xdc, 0xd6, 0xfd, 0x31, 0xe2, 0xa4, 0xc0,
                             0xfe, 0x53, 0x6e, 0xcd, 0xd3, 0x36, 0x69, 0x21};
static const fe fe_base_y = {0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
                             0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
                             0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
                             0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66};
static const fe fe_a24 = {0x41, 0xdb, 0x01}; /* (486662 - 2) / 4 = 121665 */

/* the group order L = 2^252 + 27742317777372353535851937790883648493 */
static const unsigned char sc_order[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
    0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};

static void wipe(void *p, size_t n) {
  volatile unsigned char *v = (volatile unsigned char *)p;
  while (n-- > 0) {
    *v++ = 0;
  }
}

static void fe_copy(fe o, const fe a) {
  size_t i;
  for (i = 0; i < 32; i++) {
    o[i] = a[i];
  }
}

/* two carry passes: limbs below 2^8, the lowest below 2^9; what leaves
 * the top limb is a multiple of 2^256 = 38 */
static void fe_carry(fe o) {
  int pass;
  size_t i;
  for (pass = 0; pass < 2; pass++) {
    unsigned long c;
    for (i = 0; i < 31; i++) {
      o[i + 1] += o[i] >> 8;
      o[i] &= 0xFFUL;
    }
    c = o[31] >> 8;
    o[31] &= 0xFFUL;
    o[0] += 38UL * c;
  }
}

static void fe_add(fe o, const fe a, const fe b) {
  size_t i;
  for (i = 0; i < 32; i++) {
    o[i] = a[i] + b[i];
  }
  fe_carry(o);
}

/* a - b, with a zero of p's multiples whose limbs are all at least 872
 * added first, so no limb goes below zero */
static void fe_sub(fe o, const fe a, const fe b) {
  size_t i;
  o[0] = a[0] + 872UL - b[0];
  for (i = 1; i < 32; i++) {
    o[i] = a[i] + 1020UL - b[i];
  }
  fe_carry(o);
}

static void fe_mul(fe o, const fe a, const fe b) {
  unsigned long t[63];
  size_t i;
  size_t j;
  for (i = 0; i < 63; i++) {
    t[i] = 0;
  }
  for (i = 0; i < 32; i++) {
    for (j = 0; j < 32; j++) {
      t[i + j] += a[i] * b[j];
    }
  }
  for (i = 0; i < 31; i++) {
    t[i] += 38UL * t[i + 32];
  }
  for (i = 0; i < 32; i++) {
    o[i] = t[i];
  }
  fe_carry(o);
}

static void fe_sq(fe o, const fe a) { fe_mul(o, a, a); }

/* p and q swap when b is 1, stay when it is 0, without a branch */
static void fe_cswap(fe p, fe q, unsigned long b) {
  unsigned long mask = 0UL - b;
  size_t i;
  for (i = 0; i < 32; i++) {
    unsigned long t = mask & (p[i] ^ q[i]);
    p[i] ^= t;
    q[i] ^= t;
  }
}

/* the canonical 32 bytes: fully carried, then p subtracted while it fits,
 * by masks */
static void fe_pack(unsigned char *out, const fe a) {
  fe t;
  unsigned long m[32];
  size_t i;
  int round;
  fe_copy(t, a);
  fe_carry(t); /* every limb below 2^8: the value is below 2^256 = 2p + 38 */
  for (round = 0; round < 2; round++) {
    unsigned long borrow = 0;
    unsigned long take;
    for (i = 0; i < 32; i++) {
      unsigned long pi = i == 0 ? 0xEDUL : (i == 31 ? 0x7FUL : 0xFFUL);
      unsigned long v = t[i] - pi - borrow;
      m[i] = v & 0xFFUL;
      borrow = (v >> 8) & 1UL;
    }
    take = borrow - 1UL; /* all ones when t >= p */
    for (i = 0; i < 32; i++) {
      t[i] = (m[i] & take) | (t[i] & ~take);
    }
  }
  for (i = 0; i < 32; i++) {
    out[i] = (unsigned char)t[i];
  }
}

static void fe_unpack(fe o, const unsigned char *in) {
  size_t i;
  for (i = 0; i < 32; i++) {
    o[i] = in[i];
  }
  o[31] &= 0x7FUL;
}

/* public data only: these branch */
static int fe_equal(const fe a, const fe b) {
  unsigned char x[32];
  unsigned char y[32];
  fe_pack(x, a);
  fe_pack(y, b);
  return memcmp(x, y, 32) == 0;
}

static int fe_parity(const fe a) {
  unsigned char x[32];
  fe_pack(x, a);
  return x[0] & 1;
}

static int fe_is_zero(const fe a) { return fe_equal(a, fe_zero); }

/* a^(p - 2) = 1/a: the exponent 2^255 - 21 has every bit set but bits 2
 * and 4 */
static void fe_inv(fe o, const fe a) {
  fe c;
  int i;
  fe_copy(c, a);
  for (i = 253; i >= 0; i--) {
    fe_sq(c, c);
    if (i != 2 && i != 4) {
      fe_mul(c, c, a);
    }
  }
  fe_copy(o, c);
}

/* a^((p - 5) / 8) = a^(2^252 - 3), for square roots */
static void fe_pow2523(fe o, const fe a) {
  fe c;
  int i;
  fe_copy(c, a);
  for (i = 250; i >= 0; i--) {
    fe_sq(c, c);
    if (i != 1) {
      fe_mul(c, c, a);
    }
  }
  fe_copy(o, c);
}

/* ---- the curve: -x^2 + y^2 = 1 + d x^2 y^2 ---- */

/* p += q; the unified formula also doubles (q may be p) */
static void ge_add(ge_t *p, const ge_t *q) {
  fe a;
  fe b;
  fe c;
  fe d;
  fe t;
  fe e;
  fe f;
  fe g;
  fe h;
  fe_sub(a, p->y, p->x);
  fe_sub(t, q->y, q->x);
  fe_mul(a, a, t);
  fe_add(b, p->x, p->y);
  fe_add(t, q->x, q->y);
  fe_mul(b, b, t);
  fe_mul(c, p->t, q->t);
  fe_mul(c, c, fe_d2);
  fe_mul(d, p->z, q->z);
  fe_add(d, d, d);
  fe_sub(e, b, a);
  fe_sub(f, d, c);
  fe_add(g, d, c);
  fe_add(h, b, a);
  fe_mul(p->x, e, f);
  fe_mul(p->y, h, g);
  fe_mul(p->z, g, f);
  fe_mul(p->t, e, h);
}

static void ge_cswap(ge_t *p, ge_t *q, unsigned long b) {
  fe_cswap(p->x, q->x, b);
  fe_cswap(p->y, q->y, b);
  fe_cswap(p->z, q->z, b);
  fe_cswap(p->t, q->t, b);
}

static void ge_pack(unsigned char *out, const ge_t *p) {
  fe zi;
  fe x;
  fe y;
  fe_inv(zi, p->z);
  fe_mul(x, p->x, zi);
  fe_mul(y, p->y, zi);
  fe_pack(out, y);
  out[31] = (unsigned char)(out[31] | (fe_parity(x) << 7));
}

/* p = s * q for a 32-byte little-endian s, by a ladder: the same
 * additions for every bit, swapped by mask (q is consumed) */
static void ge_scalarmult(ge_t *p, ge_t *q, const unsigned char *s) {
  int i;
  fe_copy(p->x, fe_zero);
  fe_copy(p->y, fe_one);
  fe_copy(p->z, fe_one);
  fe_copy(p->t, fe_zero);
  for (i = 255; i >= 0; i--) {
    unsigned long b = (unsigned long)((s[i >> 3] >> (i & 7)) & 1);
    ge_cswap(p, q, b);
    ge_add(q, p);
    ge_add(p, p);
    ge_cswap(p, q, b);
  }
}

static void ge_scalarmult_base(ge_t *p, const unsigned char *s) {
  ge_t q;
  fe_copy(q.x, fe_base_x);
  fe_copy(q.y, fe_base_y);
  fe_copy(q.z, fe_one);
  fe_mul(q.t, fe_base_x, fe_base_y);
  ge_scalarmult(p, &q, s);
}

/* decode a point and negate it (verification wants -A); 0 on success,
 * -1 for an encoding RFC 8032 rejects: y not below p, no square root, or
 * x = 0 with the sign bit set */
static int ge_unpack_neg(ge_t *r, const unsigned char *s) {
  fe t;
  fe chk;
  fe num;
  fe den;
  fe den2;
  fe den4;
  fe den6;
  int sign = s[31] >> 7;
  size_t i;
  if ((s[31] & 0x7F) == 0x7F && s[0] >= 0xED) {
    for (i = 1; i < 31 && s[i] == 0xFF; i++) {
    }
    if (i == 31) {
      return -1; /* y >= p */
    }
  }
  fe_copy(r->z, fe_one);
  fe_unpack(r->y, s);
  fe_sq(num, r->y);
  fe_mul(den, num, fe_d);
  fe_sub(num, num, r->z); /* y^2 - 1 */
  fe_add(den, r->z, den); /* d y^2 + 1 */
  /* x = num den^3 (num den^7)^((p - 5) / 8) */
  fe_sq(den2, den);
  fe_sq(den4, den2);
  fe_mul(den6, den4, den2);
  fe_mul(t, den6, num);
  fe_mul(t, t, den);
  fe_pow2523(t, t);
  fe_mul(t, t, num);
  fe_mul(t, t, den);
  fe_mul(t, t, den);
  fe_mul(r->x, t, den);
  fe_sq(chk, r->x);
  fe_mul(chk, chk, den);
  if (!fe_equal(chk, num)) {
    fe_mul(r->x, r->x, fe_sqrtm1);
  }
  fe_sq(chk, r->x);
  fe_mul(chk, chk, den);
  if (!fe_equal(chk, num)) {
    return -1;
  }
  if (sign && fe_is_zero(r->x)) {
    return -1;
  }
  if (fe_parity(r->x) == sign) {
    fe_sub(r->x, fe_zero, r->x);
  }
  fe_mul(r->t, r->x, r->y);
  return 0;
}

/* ---- scalars modulo L ---- */

/* x mod L for a 64-byte little-endian x: bit by bit from the top,
 * doubling and subtracting L by mask, in constant time */
static void sc_reduce(unsigned char *out, const unsigned char *x) {
  unsigned long r[33];
  unsigned long d[33];
  size_t i;
  int bit;
  for (i = 0; i < 33; i++) {
    r[i] = 0;
  }
  for (bit = 511; bit >= 0; bit--) {
    unsigned long carry = (unsigned long)((x[bit >> 3] >> (bit & 7)) & 1);
    unsigned long borrow = 0;
    unsigned long take;
    for (i = 0; i < 33; i++) {
      unsigned long v = (r[i] << 1) | carry;
      carry = v >> 8;
      r[i] = v & 0xFFUL;
    }
    for (i = 0; i < 33; i++) {
      unsigned long li = i < 32 ? sc_order[i] : 0UL;
      unsigned long v = r[i] - li - borrow;
      d[i] = v & 0xFFUL;
      borrow = (v >> 8) & 1UL;
    }
    take = borrow - 1UL; /* all ones when r >= L */
    for (i = 0; i < 33; i++) {
      r[i] = (d[i] & take) | (r[i] & ~take);
    }
  }
  for (i = 0; i < 32; i++) {
    out[i] = (unsigned char)r[i];
  }
  wipe(r, sizeof(r));
  wipe(d, sizeof(d));
}

/* (a b + c) mod L */
static void sc_muladd(unsigned char *out, const unsigned char *a,
                      const unsigned char *b, const unsigned char *c) {
  unsigned long t[64];
  unsigned char x[64];
  size_t i;
  size_t j;
  for (i = 0; i < 64; i++) {
    t[i] = i < 32 ? (unsigned long)c[i] : 0UL;
  }
  for (i = 0; i < 32; i++) {
    for (j = 0; j < 32; j++) {
      t[i + j] += (unsigned long)a[i] * (unsigned long)b[j];
    }
  }
  for (i = 0; i < 63; i++) {
    t[i + 1] += t[i] >> 8;
    x[i] = (unsigned char)(t[i] & 0xFFUL);
  }
  x[63] = (unsigned char)(t[63] & 0xFFUL);
  sc_reduce(out, x);
  wipe(t, sizeof(t));
  wipe(x, sizeof(x));
}

static int sc_below_order(const unsigned char *s) {
  int i;
  for (i = 31; i >= 0; i--) {
    if (s[i] != sc_order[i]) {
      return s[i] < sc_order[i];
    }
  }
  return 0;
}

/* ---- Ed25519 ---- */

/* the secret scalar and the nonce prefix: SHA-512 of the seed, clamped */
static void ed_expand(const unsigned char *seed, unsigned char *az) {
  ds_sha512(seed, DS_ED25519_SEED_SIZE, az);
  az[0] &= 248;
  az[31] &= 127;
  az[31] |= 64;
}

ds_status_t ds_ed25519_public_key(const unsigned char *seed,
                                  unsigned char *public_key) {
  unsigned char az[64];
  ge_t p;
  if (seed == NULL || public_key == NULL) {
    return DS_ERR_NULL;
  }
  ed_expand(seed, az);
  ge_scalarmult_base(&p, az);
  ge_pack(public_key, &p);
  wipe(az, sizeof(az));
  wipe(&p, sizeof(p));
  return DS_OK;
}

ds_status_t ds_ed25519_sign(const unsigned char *seed,
                            const unsigned char *public_key, const void *msg,
                            size_t len, unsigned char *sig) {
  unsigned char az[64];
  unsigned char nonce[64];
  unsigned char r[32];
  unsigned char k[64];
  unsigned char kr[32];
  unsigned char pk[32];
  ds_sha512_t h;
  ge_t p;
  if (seed == NULL || sig == NULL || (msg == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  ed_expand(seed, az);
  if (public_key == NULL) {
    ge_scalarmult_base(&p, az);
    ge_pack(pk, &p);
    public_key = pk;
  }
  /* r = H(prefix || M) mod L, R = rB */
  ds_sha512_init(&h);
  ds_sha512_update(&h, az + 32, 32);
  ds_sha512_update(&h, msg, len);
  ds_sha512_final(&h, nonce);
  sc_reduce(r, nonce);
  ge_scalarmult_base(&p, r);
  ge_pack(sig, &p);
  /* S = (H(R || A || M) a + r) mod L */
  ds_sha512_init(&h);
  ds_sha512_update(&h, sig, 32);
  ds_sha512_update(&h, public_key, 32);
  ds_sha512_update(&h, msg, len);
  ds_sha512_final(&h, k);
  sc_reduce(kr, k);
  sc_muladd(sig + 32, kr, az, r);
  wipe(az, sizeof(az));
  wipe(nonce, sizeof(nonce));
  wipe(r, sizeof(r));
  wipe(&p, sizeof(p));
  wipe(&h, sizeof(h));
  return DS_OK;
}

ds_status_t ds_ed25519_verify(const unsigned char *public_key, const void *msg,
                              size_t len, const unsigned char *sig) {
  unsigned char k[64];
  unsigned char kr[32];
  unsigned char check[32];
  ds_sha512_t h;
  ge_t a;
  ge_t p;
  ge_t q;
  if (public_key == NULL || sig == NULL || (msg == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  if (!sc_below_order(sig + 32) || ge_unpack_neg(&a, public_key) != 0 ||
      ge_unpack_neg(&q, sig) != 0) { /* R must decode as well */
    return DS_NOT_FOUND;
  }
  ds_sha512_init(&h);
  ds_sha512_update(&h, sig, 32);
  ds_sha512_update(&h, public_key, 32);
  ds_sha512_update(&h, msg, len);
  ds_sha512_final(&h, k);
  sc_reduce(kr, k);
  ge_scalarmult(&p, &a, kr);        /* -kA */
  ge_scalarmult_base(&q, sig + 32); /* SB */
  ge_add(&p, &q);
  ge_pack(check, &p);
  return memcmp(check, sig, 32) == 0 ? DS_OK : DS_NOT_FOUND;
}

/* ---- X25519 ---- */

ds_status_t ds_x25519(const unsigned char *scalar, const unsigned char *point,
                      unsigned char *out) {
  unsigned char z[32];
  fe x;
  fe a;
  fe b;
  fe c;
  fe d;
  fe e;
  fe f;
  unsigned long any = 0;
  int i;
  if (scalar == NULL || point == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  memcpy(z, scalar, 32);
  z[31] = (unsigned char)((z[31] & 127) | 64);
  z[0] &= 248;
  fe_unpack(x, point);
  fe_copy(a, fe_one);
  fe_copy(b, x);
  fe_copy(c, fe_zero);
  fe_copy(d, fe_one);
  /* the Montgomery ladder: (a : c) = kP, (b : d) = (k + 1)P */
  for (i = 254; i >= 0; i--) {
    unsigned long bit = (unsigned long)((z[i >> 3] >> (i & 7)) & 1);
    fe_cswap(a, b, bit);
    fe_cswap(c, d, bit);
    fe_add(e, a, c);
    fe_sub(a, a, c);
    fe_add(c, b, d);
    fe_sub(b, b, d);
    fe_sq(d, e);
    fe_sq(f, a);
    fe_mul(a, c, a);
    fe_mul(c, b, e);
    fe_add(e, a, c);
    fe_sub(a, a, c);
    fe_sq(b, a);
    fe_sub(c, d, f);
    fe_mul(a, c, fe_a24);
    fe_add(a, a, d);
    fe_mul(c, c, a);
    fe_mul(a, d, f);
    fe_mul(d, b, x);
    fe_sq(b, e);
    fe_cswap(a, b, bit);
    fe_cswap(c, d, bit);
  }
  fe_inv(c, c);
  fe_mul(a, a, c);
  fe_pack(out, a);
  for (i = 0; i < 32; i++) {
    any |= out[i];
  }
  wipe(z, sizeof(z));
  wipe(a, sizeof(a));
  wipe(b, sizeof(b));
  wipe(c, sizeof(c));
  wipe(d, sizeof(d));
  wipe(e, sizeof(e));
  wipe(f, sizeof(f));
  return any == 0 ? DS_ERR_INVALID : DS_OK;
}

ds_status_t ds_x25519_public_key(const unsigned char *scalar,
                                 unsigned char *out) {
  static const unsigned char nine[32] = {9};
  return ds_x25519(scalar, nine, out);
}
