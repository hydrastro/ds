#include <dsa/algorithms/sha2.h>
#include <string.h>

#define SH_M32 0xFFFFFFFFUL
#define SH_ROTR(x, n) ((((x) >> (n)) | ((x) << (32 - (n)))) & SH_M32)

/* ---- SHA-256 */

static const unsigned long sh_k256[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL,
    0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL, 0xd807aa98UL, 0x12835b01UL,
    0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL,
    0xc19bf174UL, 0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL, 0x983e5152UL,
    0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL,
    0x06ca6351UL, 0x14292967UL, 0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL,
    0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL,
    0xd6990624UL, 0xf40e3585UL, 0x106aa070UL, 0x19a4c116UL, 0x1e376c08UL,
    0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL,
    0x682e6ff3UL, 0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL};

static void sh256_block(ds_sha256_t *s, const unsigned char *p) {
  unsigned long w[64];
  unsigned long a;
  unsigned long b;
  unsigned long c;
  unsigned long d;
  unsigned long e;
  unsigned long f;
  unsigned long g;
  unsigned long h;
  size_t i;
  for (i = 0; i < 16; i++) {
    w[i] = ((unsigned long)p[4 * i] << 24) |
           ((unsigned long)p[4 * i + 1] << 16) |
           ((unsigned long)p[4 * i + 2] << 8) | (unsigned long)p[4 * i + 3];
  }
  for (i = 16; i < 64; i++) {
    unsigned long s0 =
        SH_ROTR(w[i - 15], 7) ^ SH_ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
    unsigned long s1 =
        SH_ROTR(w[i - 2], 17) ^ SH_ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = (w[i - 16] + s0 + w[i - 7] + s1) & SH_M32;
  }
  a = s->h[0];
  b = s->h[1];
  c = s->h[2];
  d = s->h[3];
  e = s->h[4];
  f = s->h[5];
  g = s->h[6];
  h = s->h[7];
  for (i = 0; i < 64; i++) {
    unsigned long s1 = SH_ROTR(e, 6) ^ SH_ROTR(e, 11) ^ SH_ROTR(e, 25);
    unsigned long ch = (e & f) ^ (~e & SH_M32 & g);
    unsigned long t1 = (h + s1 + ch + sh_k256[i] + w[i]) & SH_M32;
    unsigned long s0 = SH_ROTR(a, 2) ^ SH_ROTR(a, 13) ^ SH_ROTR(a, 22);
    unsigned long mj = (a & b) ^ (a & c) ^ (b & c);
    unsigned long t2 = (s0 + mj) & SH_M32;
    h = g;
    g = f;
    f = e;
    e = (d + t1) & SH_M32;
    d = c;
    c = b;
    b = a;
    a = (t1 + t2) & SH_M32;
  }
  s->h[0] = (s->h[0] + a) & SH_M32;
  s->h[1] = (s->h[1] + b) & SH_M32;
  s->h[2] = (s->h[2] + c) & SH_M32;
  s->h[3] = (s->h[3] + d) & SH_M32;
  s->h[4] = (s->h[4] + e) & SH_M32;
  s->h[5] = (s->h[5] + f) & SH_M32;
  s->h[6] = (s->h[6] + g) & SH_M32;
  s->h[7] = (s->h[7] + h) & SH_M32;
}

void ds_sha256_init(ds_sha256_t *s) {
  if (s == NULL) {
    return;
  }
  s->h[0] = 0x6a09e667UL;
  s->h[1] = 0xbb67ae85UL;
  s->h[2] = 0x3c6ef372UL;
  s->h[3] = 0xa54ff53aUL;
  s->h[4] = 0x510e527fUL;
  s->h[5] = 0x9b05688cUL;
  s->h[6] = 0x1f83d9abUL;
  s->h[7] = 0x5be0cd19UL;
  s->buf_len = 0;
  s->bytes_lo = s->bytes_hi = 0;
}

void ds_sha256_update(ds_sha256_t *s, const void *data, size_t len) {
  const unsigned char *p = (const unsigned char *)data;
  if (s == NULL || (len > 0 && data == NULL)) {
    return;
  }
  while (len > 0) {
    size_t take = 64 - s->buf_len < len ? 64 - s->buf_len : len;
    unsigned long old = s->bytes_lo;
    memcpy(s->buf + s->buf_len, p, take);
    s->buf_len += take;
    p += take;
    len -= take;
    s->bytes_lo = (s->bytes_lo + (unsigned long)take) & SH_M32;
    if (s->bytes_lo < old) {
      s->bytes_hi = (s->bytes_hi + 1UL) & SH_M32;
    }
    if (s->buf_len == 64) {
      sh256_block(s, s->buf);
      s->buf_len = 0;
    }
  }
}

void ds_sha256_final(ds_sha256_t *s, unsigned char *out) {
  unsigned char pad[72];
  unsigned long bits_hi;
  unsigned long bits_lo;
  size_t padlen;
  size_t i;
  if (s == NULL || out == NULL) {
    return;
  }
  bits_hi = ((s->bytes_hi << 3) | (s->bytes_lo >> 29)) & SH_M32;
  bits_lo = (s->bytes_lo << 3) & SH_M32;
  padlen = s->buf_len < 56 ? 56 - s->buf_len : 120 - s->buf_len;
  memset(pad, 0, sizeof(pad));
  pad[0] = 0x80;
  for (i = 0; i < 4; i++) {
    pad[padlen + i] = (unsigned char)((bits_hi >> (24 - 8 * i)) & 0xFFUL);
    pad[padlen + 4 + i] = (unsigned char)((bits_lo >> (24 - 8 * i)) & 0xFFUL);
  }
  {
    /* the length bytes must not count themselves */
    unsigned long lo = s->bytes_lo;
    unsigned long hi = s->bytes_hi;
    ds_sha256_update(s, pad, padlen + 8);
    s->bytes_lo = lo;
    s->bytes_hi = hi;
  }
  for (i = 0; i < 8; i++) {
    out[4 * i] = (unsigned char)((s->h[i] >> 24) & 0xFFUL);
    out[4 * i + 1] = (unsigned char)((s->h[i] >> 16) & 0xFFUL);
    out[4 * i + 2] = (unsigned char)((s->h[i] >> 8) & 0xFFUL);
    out[4 * i + 3] = (unsigned char)(s->h[i] & 0xFFUL);
  }
}

void ds_sha256(const void *data, size_t len, unsigned char *out) {
  ds_sha256_t s;
  ds_sha256_init(&s);
  ds_sha256_update(&s, data, len);
  ds_sha256_final(&s, out);
}

void ds_hmac_sha256(const void *key, size_t key_len, const void *data,
                    size_t len, unsigned char *out) {
  unsigned char k[64];
  unsigned char inner[DS_SHA256_SIZE];
  unsigned char pad[64];
  ds_sha256_t s;
  size_t i;
  if (out == NULL) {
    return;
  }
  memset(k, 0, sizeof(k));
  if (key_len > 64) {
    ds_sha256(key, key_len, k);
  } else if (key_len > 0) {
    memcpy(k, key, key_len);
  }
  for (i = 0; i < 64; i++) {
    pad[i] = (unsigned char)(k[i] ^ 0x36);
  }
  ds_sha256_init(&s);
  ds_sha256_update(&s, pad, 64);
  ds_sha256_update(&s, data, len);
  ds_sha256_final(&s, inner);
  for (i = 0; i < 64; i++) {
    pad[i] = (unsigned char)(k[i] ^ 0x5c);
  }
  ds_sha256_init(&s);
  ds_sha256_update(&s, pad, 64);
  ds_sha256_update(&s, inner, sizeof(inner));
  ds_sha256_final(&s, out);
}

static const unsigned long sh_k512[160] = {
    0x428a2f98UL, 0xd728ae22UL, 0x71374491UL, 0x23ef65cdUL, 0xb5c0fbcfUL,
    0xec4d3b2fUL, 0xe9b5dba5UL, 0x8189dbbcUL, 0x3956c25bUL, 0xf348b538UL,
    0x59f111f1UL, 0xb605d019UL, 0x923f82a4UL, 0xaf194f9bUL, 0xab1c5ed5UL,
    0xda6d8118UL, 0xd807aa98UL, 0xa3030242UL, 0x12835b01UL, 0x45706fbeUL,
    0x243185beUL, 0x4ee4b28cUL, 0x550c7dc3UL, 0xd5ffb4e2UL, 0x72be5d74UL,
    0xf27b896fUL, 0x80deb1feUL, 0x3b1696b1UL, 0x9bdc06a7UL, 0x25c71235UL,
    0xc19bf174UL, 0xcf692694UL, 0xe49b69c1UL, 0x9ef14ad2UL, 0xefbe4786UL,
    0x384f25e3UL, 0x0fc19dc6UL, 0x8b8cd5b5UL, 0x240ca1ccUL, 0x77ac9c65UL,
    0x2de92c6fUL, 0x592b0275UL, 0x4a7484aaUL, 0x6ea6e483UL, 0x5cb0a9dcUL,
    0xbd41fbd4UL, 0x76f988daUL, 0x831153b5UL, 0x983e5152UL, 0xee66dfabUL,
    0xa831c66dUL, 0x2db43210UL, 0xb00327c8UL, 0x98fb213fUL, 0xbf597fc7UL,
    0xbeef0ee4UL, 0xc6e00bf3UL, 0x3da88fc2UL, 0xd5a79147UL, 0x930aa725UL,
    0x06ca6351UL, 0xe003826fUL, 0x14292967UL, 0x0a0e6e70UL, 0x27b70a85UL,
    0x46d22ffcUL, 0x2e1b2138UL, 0x5c26c926UL, 0x4d2c6dfcUL, 0x5ac42aedUL,
    0x53380d13UL, 0x9d95b3dfUL, 0x650a7354UL, 0x8baf63deUL, 0x766a0abbUL,
    0x3c77b2a8UL, 0x81c2c92eUL, 0x47edaee6UL, 0x92722c85UL, 0x1482353bUL,
    0xa2bfe8a1UL, 0x4cf10364UL, 0xa81a664bUL, 0xbc423001UL, 0xc24b8b70UL,
    0xd0f89791UL, 0xc76c51a3UL, 0x0654be30UL, 0xd192e819UL, 0xd6ef5218UL,
    0xd6990624UL, 0x5565a910UL, 0xf40e3585UL, 0x5771202aUL, 0x106aa070UL,
    0x32bbd1b8UL, 0x19a4c116UL, 0xb8d2d0c8UL, 0x1e376c08UL, 0x5141ab53UL,
    0x2748774cUL, 0xdf8eeb99UL, 0x34b0bcb5UL, 0xe19b48a8UL, 0x391c0cb3UL,
    0xc5c95a63UL, 0x4ed8aa4aUL, 0xe3418acbUL, 0x5b9cca4fUL, 0x7763e373UL,
    0x682e6ff3UL, 0xd6b2b8a3UL, 0x748f82eeUL, 0x5defb2fcUL, 0x78a5636fUL,
    0x43172f60UL, 0x84c87814UL, 0xa1f0ab72UL, 0x8cc70208UL, 0x1a6439ecUL,
    0x90befffaUL, 0x23631e28UL, 0xa4506cebUL, 0xde82bde9UL, 0xbef9a3f7UL,
    0xb2c67915UL, 0xc67178f2UL, 0xe372532bUL, 0xca273eceUL, 0xea26619cUL,
    0xd186b8c7UL, 0x21c0c207UL, 0xeada7dd6UL, 0xcde0eb1eUL, 0xf57d4f7fUL,
    0xee6ed178UL, 0x06f067aaUL, 0x72176fbaUL, 0x0a637dc5UL, 0xa2c898a6UL,
    0x113f9804UL, 0xbef90daeUL, 0x1b710b35UL, 0x131c471bUL, 0x28db77f5UL,
    0x23047d84UL, 0x32caab7bUL, 0x40c72493UL, 0x3c9ebe0aUL, 0x15c9bebcUL,
    0x431d67c4UL, 0x9c100d4cUL, 0x4cc5d4beUL, 0xcb3e42b6UL, 0x597f299cUL,
    0xfc657e2aUL, 0x5fcb6fabUL, 0x3ad6faecUL, 0x6c44198cUL, 0x4a475817UL};
static const unsigned long sh_h512[16] = {
    0x6a09e667UL, 0xf3bcc908UL, 0xbb67ae85UL, 0x84caa73bUL,
    0x3c6ef372UL, 0xfe94f82bUL, 0xa54ff53aUL, 0x5f1d36f1UL,
    0x510e527fUL, 0xade682d1UL, 0x9b05688cUL, 0x2b3e6c1fUL,
    0x1f83d9abUL, 0xfb41bd6bUL, 0x5be0cd19UL, 0x137e2179UL};
static const unsigned long sh_h384[16] = {
    0xcbbb9d5dUL, 0xc1059ed8UL, 0x629a292aUL, 0x367cd507UL,
    0x9159015aUL, 0x3070dd17UL, 0x152fecd8UL, 0xf70e5939UL,
    0x67332667UL, 0xffc00b31UL, 0x8eb44a87UL, 0x68581511UL,
    0xdb0c2e0dUL, 0x64f98fa7UL, 0x47b5481dUL, 0xbefa4fa4UL};

/* ---- SHA-384 and SHA-512: a 64-bit word is a pair (high, low) of 32-bit
 * halves, held in unsigned longs and masked to 32 bits */

/* (hi, lo) = (ah, al) rotated right by n, 0 < n < 64 */
static void sh_rotr64(unsigned long ah, unsigned long al, unsigned n,
                      unsigned long *hi, unsigned long *lo) {
  if (n >= 32) {
    unsigned long t = ah;
    ah = al;
    al = t;
    n -= 32;
  }
  if (n == 0) {
    *hi = ah;
    *lo = al;
    return;
  }
  *hi = ((ah >> n) | (al << (32 - n))) & SH_M32;
  *lo = ((al >> n) | (ah << (32 - n))) & SH_M32;
}

/* (hi, lo) = (ah, al) shifted right by n, 0 < n < 32 */
static void sh_shr64(unsigned long ah, unsigned long al, unsigned n,
                     unsigned long *hi, unsigned long *lo) {
  *hi = ah >> n;
  *lo = ((al >> n) | (ah << (32 - n))) & SH_M32;
}

/* (*hi, *lo) += (bh, bl) modulo 2^64 */
static void sh_add64(unsigned long *hi, unsigned long *lo, unsigned long bh,
                     unsigned long bl) {
  unsigned long l = (*lo + bl) & SH_M32;
  *hi = (*hi + bh + (l < *lo ? 1UL : 0UL)) & SH_M32;
  *lo = l;
}

/* x = rotr(a, r1) ^ rotr(a, r2) ^ (r3 < 0 ? shr(a, -r3) : rotr(a, r3)) */
static void sh_mix64(unsigned long ah, unsigned long al, unsigned r1,
                     unsigned r2, int r3, unsigned long *hi,
                     unsigned long *lo) {
  unsigned long h1;
  unsigned long l1;
  unsigned long h2;
  unsigned long l2;
  unsigned long h3;
  unsigned long l3;
  sh_rotr64(ah, al, r1, &h1, &l1);
  sh_rotr64(ah, al, r2, &h2, &l2);
  if (r3 < 0) {
    sh_shr64(ah, al, (unsigned)-r3, &h3, &l3);
  } else {
    sh_rotr64(ah, al, (unsigned)r3, &h3, &l3);
  }
  *hi = h1 ^ h2 ^ h3;
  *lo = l1 ^ l2 ^ l3;
}

static void sh512_block(ds_sha512_t *s, const unsigned char *p) {
  unsigned long w[160];
  unsigned long v[16]; /* a .. h, high halves at even indexes */
  size_t i;
  for (i = 0; i < 32; i++) {
    w[i] = ((unsigned long)p[4 * i] << 24) |
           ((unsigned long)p[4 * i + 1] << 16) |
           ((unsigned long)p[4 * i + 2] << 8) | (unsigned long)p[4 * i + 3];
  }
  for (i = 16; i < 80; i++) {
    unsigned long h0;
    unsigned long l0;
    unsigned long h1;
    unsigned long l1;
    sh_mix64(w[2 * (i - 15)], w[2 * (i - 15) + 1], 1, 8, -7, &h0, &l0);
    sh_mix64(w[2 * (i - 2)], w[2 * (i - 2) + 1], 19, 61, -6, &h1, &l1);
    sh_add64(&h0, &l0, h1, l1);
    sh_add64(&h0, &l0, w[2 * (i - 7)], w[2 * (i - 7) + 1]);
    sh_add64(&h0, &l0, w[2 * (i - 16)], w[2 * (i - 16) + 1]);
    w[2 * i] = h0;
    w[2 * i + 1] = l0;
  }
  memcpy(v, s->h, sizeof(v));
  for (i = 0; i < 80; i++) {
    unsigned long t1h = v[14];
    unsigned long t1l = v[15];
    unsigned long t2h;
    unsigned long t2l;
    unsigned long xh;
    unsigned long xl;
    /* t1 = h + S1(e) + Ch(e, f, g) + k[i] + w[i] */
    sh_mix64(v[8], v[9], 14, 18, 41, &xh, &xl);
    sh_add64(&t1h, &t1l, xh, xl);
    sh_add64(&t1h, &t1l, (v[8] & v[10]) ^ (~v[8] & SH_M32 & v[12]),
             (v[9] & v[11]) ^ (~v[9] & SH_M32 & v[13]));
    sh_add64(&t1h, &t1l, sh_k512[2 * i], sh_k512[2 * i + 1]);
    sh_add64(&t1h, &t1l, w[2 * i], w[2 * i + 1]);
    /* t2 = S0(a) + Maj(a, b, c) */
    sh_mix64(v[0], v[1], 28, 34, 39, &t2h, &t2l);
    sh_add64(&t2h, &t2l, (v[0] & v[2]) ^ (v[0] & v[4]) ^ (v[2] & v[4]),
             (v[1] & v[3]) ^ (v[1] & v[5]) ^ (v[3] & v[5]));
    memmove(v + 2, v, 14 * sizeof(unsigned long));
    sh_add64(&v[8], &v[9], t1h, t1l); /* e = d + t1 */
    v[0] = t1h;
    v[1] = t1l;
    sh_add64(&v[0], &v[1], t2h, t2l); /* a = t1 + t2 */
  }
  for (i = 0; i < 8; i++) {
    sh_add64(&s->h[2 * i], &s->h[2 * i + 1], v[2 * i], v[2 * i + 1]);
  }
}

static void sh512_start(ds_sha512_t *s, const unsigned long *iv, size_t size) {
  if (s == NULL) {
    return;
  }
  memcpy(s->h, iv, sizeof(s->h));
  s->buf_len = 0;
  s->bytes_lo = s->bytes_hi = 0;
  s->size = size;
}

void ds_sha384_init(ds_sha512_t *s) {
  sh512_start(s, sh_h384, DS_SHA384_SIZE);
}

void ds_sha512_init(ds_sha512_t *s) {
  sh512_start(s, sh_h512, DS_SHA512_SIZE);
}

void ds_sha512_update(ds_sha512_t *s, const void *data, size_t len) {
  const unsigned char *p = (const unsigned char *)data;
  if (s == NULL || (len > 0 && data == NULL)) {
    return;
  }
  while (len > 0) {
    size_t take = 128 - s->buf_len < len ? 128 - s->buf_len : len;
    unsigned long old = s->bytes_lo;
    memcpy(s->buf + s->buf_len, p, take);
    s->buf_len += take;
    p += take;
    len -= take;
    s->bytes_lo = (s->bytes_lo + (unsigned long)take) & SH_M32;
    if (s->bytes_lo < old) {
      s->bytes_hi = (s->bytes_hi + 1UL) & SH_M32;
    }
    if (s->buf_len == 128) {
      sh512_block(s, s->buf);
      s->buf_len = 0;
    }
  }
}

void ds_sha512_final(ds_sha512_t *s, unsigned char *out) {
  unsigned char pad[144];
  unsigned long len[4]; /* bit length, 128 bits, most significant first */
  size_t padlen;
  size_t i;
  if (s == NULL || out == NULL) {
    return;
  }
  len[0] = 0;
  len[1] = (s->bytes_hi >> 29) & 7UL;
  len[2] = ((s->bytes_hi << 3) | (s->bytes_lo >> 29)) & SH_M32;
  len[3] = (s->bytes_lo << 3) & SH_M32;
  padlen = s->buf_len < 112 ? 112 - s->buf_len : 240 - s->buf_len;
  memset(pad, 0, sizeof(pad));
  pad[0] = 0x80;
  for (i = 0; i < 16; i++) {
    pad[padlen + i] =
        (unsigned char)((len[i / 4] >> (24 - 8 * (i % 4))) & 0xFFUL);
  }
  {
    /* the length bytes must not count themselves */
    unsigned long lo = s->bytes_lo;
    unsigned long hi = s->bytes_hi;
    ds_sha512_update(s, pad, padlen + 16);
    s->bytes_lo = lo;
    s->bytes_hi = hi;
  }
  for (i = 0; i < s->size; i++) {
    out[i] = (unsigned char)((s->h[i / 4] >> (24 - 8 * (i % 4))) & 0xFFUL);
  }
}

void ds_sha384(const void *data, size_t len, unsigned char *out) {
  ds_sha512_t s;
  ds_sha384_init(&s);
  ds_sha512_update(&s, data, len);
  ds_sha512_final(&s, out);
}

void ds_sha512(const void *data, size_t len, unsigned char *out) {
  ds_sha512_t s;
  ds_sha512_init(&s);
  ds_sha512_update(&s, data, len);
  ds_sha512_final(&s, out);
}

static void sh_hmac512(const unsigned long *iv, size_t size, const void *key,
                       size_t key_len, const void *data, size_t len,
                       unsigned char *out) {
  unsigned char k[128];
  unsigned char inner[DS_SHA512_SIZE];
  unsigned char pad[128];
  ds_sha512_t s;
  size_t i;
  if (out == NULL) {
    return;
  }
  memset(k, 0, sizeof(k));
  if (key_len > 128) {
    sh512_start(&s, iv, size);
    ds_sha512_update(&s, key, key_len);
    ds_sha512_final(&s, k);
  } else if (key_len > 0) {
    memcpy(k, key, key_len);
  }
  for (i = 0; i < 128; i++) {
    pad[i] = (unsigned char)(k[i] ^ 0x36);
  }
  sh512_start(&s, iv, size);
  ds_sha512_update(&s, pad, 128);
  ds_sha512_update(&s, data, len);
  ds_sha512_final(&s, inner);
  for (i = 0; i < 128; i++) {
    pad[i] = (unsigned char)(k[i] ^ 0x5c);
  }
  sh512_start(&s, iv, size);
  ds_sha512_update(&s, pad, 128);
  ds_sha512_update(&s, inner, size);
  ds_sha512_final(&s, out);
}

void ds_hmac_sha384(const void *key, size_t key_len, const void *data,
                    size_t len, unsigned char *out) {
  sh_hmac512(sh_h384, DS_SHA384_SIZE, key, key_len, data, len, out);
}

void ds_hmac_sha512(const void *key, size_t key_len, const void *data,
                    size_t len, unsigned char *out) {
  sh_hmac512(sh_h512, DS_SHA512_SIZE, key, key_len, data, len, out);
}

/* ---- digests behind function pointers */

static void sh_256_init(void *s) { ds_sha256_init((ds_sha256_t *)s); }
static void sh_256_update(void *s, const void *d, size_t n) {
  ds_sha256_update((ds_sha256_t *)s, d, n);
}
static void sh_256_final(void *s, unsigned char *out) {
  ds_sha256_final((ds_sha256_t *)s, out);
}
static void sh_384_init(void *s) { ds_sha384_init((ds_sha512_t *)s); }
static void sh_512_init(void *s) { ds_sha512_init((ds_sha512_t *)s); }
static void sh_512_update(void *s, const void *d, size_t n) {
  ds_sha512_update((ds_sha512_t *)s, d, n);
}
static void sh_512_final(void *s, unsigned char *out) {
  ds_sha512_final((ds_sha512_t *)s, out);
}

static const ds_digest_ops_t sh_ops256 = {
    "SHA-256",   DS_SHA256_SIZE, 64,          sizeof(ds_sha256_t),
    sh_256_init, sh_256_update,  sh_256_final};
static const ds_digest_ops_t sh_ops384 = {
    "SHA-384",   DS_SHA384_SIZE, 128,         sizeof(ds_sha512_t),
    sh_384_init, sh_512_update,  sh_512_final};
static const ds_digest_ops_t sh_ops512 = {
    "SHA-512",   DS_SHA512_SIZE, 128,         sizeof(ds_sha512_t),
    sh_512_init, sh_512_update,  sh_512_final};

const ds_digest_ops_t *ds_digest_sha256(void) { return &sh_ops256; }
const ds_digest_ops_t *ds_digest_sha384(void) { return &sh_ops384; }
const ds_digest_ops_t *ds_digest_sha512(void) { return &sh_ops512; }

void ds_digest_hex(const unsigned char *digest, size_t size, char *out) {
  static const char hex[] = "0123456789abcdef";
  size_t i;
  if (out == NULL) {
    return;
  }
  for (i = 0; i < size; i++) {
    out[2 * i] = hex[digest[i] >> 4];
    out[2 * i + 1] = hex[digest[i] & 15];
  }
  out[2 * size] = '\0';
}
