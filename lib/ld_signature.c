#include "ld_signature.h"
#include <stdlib.h>
#include <string.h>

#define LD_M32 0xFFFFFFFFUL
#define LD_ROTR(x, n) ((((x) >> (n)) | ((x) << (32 - (n)))) & LD_M32)

/* ---- SHA-256
 * ------------------------------------------------------------------ */

static const unsigned long ld_k[64] = {
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

static void ld_block(ds_sha256_t *s, const unsigned char *p) {
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
        LD_ROTR(w[i - 15], 7) ^ LD_ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
    unsigned long s1 =
        LD_ROTR(w[i - 2], 17) ^ LD_ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = (w[i - 16] + s0 + w[i - 7] + s1) & LD_M32;
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
    unsigned long s1 = LD_ROTR(e, 6) ^ LD_ROTR(e, 11) ^ LD_ROTR(e, 25);
    unsigned long ch = (e & f) ^ (~e & LD_M32 & g);
    unsigned long t1 = (h + s1 + ch + ld_k[i] + w[i]) & LD_M32;
    unsigned long s0 = LD_ROTR(a, 2) ^ LD_ROTR(a, 13) ^ LD_ROTR(a, 22);
    unsigned long mj = (a & b) ^ (a & c) ^ (b & c);
    unsigned long t2 = (s0 + mj) & LD_M32;
    h = g;
    g = f;
    f = e;
    e = (d + t1) & LD_M32;
    d = c;
    c = b;
    b = a;
    a = (t1 + t2) & LD_M32;
  }
  s->h[0] = (s->h[0] + a) & LD_M32;
  s->h[1] = (s->h[1] + b) & LD_M32;
  s->h[2] = (s->h[2] + c) & LD_M32;
  s->h[3] = (s->h[3] + d) & LD_M32;
  s->h[4] = (s->h[4] + e) & LD_M32;
  s->h[5] = (s->h[5] + f) & LD_M32;
  s->h[6] = (s->h[6] + g) & LD_M32;
  s->h[7] = (s->h[7] + h) & LD_M32;
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
    s->bytes_lo = (s->bytes_lo + (unsigned long)take) & LD_M32;
    if (s->bytes_lo < old) {
      s->bytes_hi = (s->bytes_hi + 1UL) & LD_M32;
    }
    if (s->buf_len == 64) {
      ld_block(s, s->buf);
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
  bits_hi = ((s->bytes_hi << 3) | (s->bytes_lo >> 29)) & LD_M32;
  bits_lo = (s->bytes_lo << 3) & LD_M32;
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

/* ---- canonicalisation
 * -------------------------------------------------------------- */

typedef struct ld_buf {
  char *p;
  size_t len;
  size_t cap;
  ds_context_t *ctx;
  ds_status_t st;
} ld_buf_t;

static void ld_put(ld_buf_t *b, const char *s, size_t n) {
  if (b->st != DS_OK) {
    return;
  }
  if (b->len + n + 1 > b->cap) {
    size_t nc = b->cap < 64 ? 128 : b->cap;
    void *q;
    while (nc < b->len + n + 1) {
      nc *= 2;
    }
    q = ds_context_realloc(b->ctx, b->p, nc);
    if (q == NULL) {
      b->st = DS_ERR_ALLOC;
      return;
    }
    b->p = (char *)q;
    b->cap = nc;
  }
  memcpy(b->p + b->len, s, n);
  b->len += n;
  b->p[b->len] = '\0';
}

/* a statement as text pieces and blank node references */
typedef struct ld_piece {
  size_t start; /* text: [start, end) of the line; blank: end = -1 */
  size_t end;
  size_t blank;
} ld_piece_t;

typedef struct ld_stmt {
  const char *line;
  ld_piece_t *pieces;
  size_t n;
} ld_stmt_t;

static int ld_cmp_str(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static const char ld_hex[] = "0123456789abcdef";

/* render statement s: blank `self` as _:a, others by name[] (or _:z) */
static void ld_render(ld_buf_t *b, const ld_stmt_t *s, size_t self,
                      const unsigned char *const *colors, char **names) {
  size_t i;
  for (i = 0; i < s->n; i++) {
    const ld_piece_t *p = &s->pieces[i];
    if (p->end != (size_t)-1) {
      ld_put(b, s->line + p->start, p->end - p->start);
    } else if (names != NULL) {
      ld_put(b, names[p->blank], strlen(names[p->blank]));
    } else if (p->blank == self) {
      ld_put(b, "_:a", 3);
    } else if (colors == NULL) {
      ld_put(b, "_:z", 3);
    } else {
      char hx[2 + 2 * DS_SHA256_SIZE];
      size_t j;
      hx[0] = '_';
      hx[1] = ':';
      for (j = 0; j < DS_SHA256_SIZE; j++) {
        hx[2 + 2 * j] = ld_hex[colors[p->blank][j] >> 4];
        hx[3 + 2 * j] = ld_hex[colors[p->blank][j] & 15];
      }
      ld_put(b, hx, sizeof(hx));
    }
  }
}

typedef struct ld_order {
  const unsigned char *color;
  size_t id;
} ld_order_t;

static int ld_cmp_order(const void *a, const void *b) {
  const ld_order_t *x = (const ld_order_t *)a;
  const ld_order_t *y = (const ld_order_t *)b;
  int c = memcmp(x->color, y->color, DS_SHA256_SIZE);
  if (c != 0) {
    return c;
  }
  return (x->id > y->id) - (x->id < y->id);
}

/* split statement text into pieces, registering blank node labels */
static ds_status_t ld_split(ld_stmt_t *st_, const char *s, const char ***labels,
                            size_t **label_len, size_t *nb, size_t *bcap,
                            ds_context_t *ctx) {
  size_t len = strlen(s);
  size_t pos = 0;
  size_t text = 0;
  size_t cap = 4;
  char quote = 0;
  st_->line = s;
  st_->n = 0;
  st_->pieces = (ld_piece_t *)ds_context_alloc(ctx, cap * sizeof(ld_piece_t));
  if (st_->pieces == NULL) {
    return DS_ERR_ALLOC;
  }
  for (;;) {
    size_t e;
    size_t id;
    if (pos < len) {
      char ch = s[pos];
      if (quote != 0) {
        if (ch == '\\' && pos + 1 < len) {
          pos += 2;
        } else {
          quote = ch == quote ? 0 : quote;
          pos++;
        }
        continue;
      }
      if (ch == '"' || ch == '<') {
        quote = ch == '"' ? '"' : '>';
        pos++;
        continue;
      }
      if (!(ch == '_' && pos + 1 < len && s[pos + 1] == ':' &&
            (pos == 0 || s[pos - 1] == ' ' || s[pos - 1] == '\t'))) {
        pos++;
        continue;
      }
    }
    if (st_->n + 2 > cap) {
      void *q;
      cap *= 2;
      q = ds_context_realloc(ctx, st_->pieces, cap * sizeof(ld_piece_t));
      if (q == NULL) {
        return DS_ERR_ALLOC;
      }
      st_->pieces = (ld_piece_t *)q;
    }
    if (pos > text) {
      st_->pieces[st_->n].start = text;
      st_->pieces[st_->n].end = pos;
      st_->pieces[st_->n].blank = 0;
      st_->n++;
    }
    if (pos >= len) {
      return DS_OK;
    }
    for (e = pos + 2; e < len && s[e] != ' ' && s[e] != '\t'; e++) {
    }
    for (id = 0; id < *nb; id++) {
      if ((*label_len)[id] == e - pos &&
          memcmp((*labels)[id], s + pos, e - pos) == 0) {
        break;
      }
    }
    if (id == *nb) {
      if (*nb == *bcap) {
        size_t nc = *bcap < 4 ? 8 : *bcap * 2;
        void *q =
            ds_context_realloc(ctx, (void *)*labels, nc * sizeof(const char *));
        if (q == NULL) {
          return DS_ERR_ALLOC;
        }
        *labels = (const char **)q;
        q = ds_context_realloc(ctx, *label_len, nc * sizeof(size_t));
        if (q == NULL) {
          return DS_ERR_ALLOC;
        }
        *label_len = (size_t *)q;
        *bcap = nc;
      }
      (*labels)[*nb] = s + pos;
      (*label_len)[*nb] = e - pos;
      (*nb)++;
    }
    st_->pieces[st_->n].start = pos;
    st_->pieces[st_->n].end = (size_t)-1;
    st_->pieces[st_->n].blank = id;
    st_->n++;
    pos = e;
    text = e;
  }
}

/* the sorted renderings of the statements of blank b, hashed */
static ds_status_t ld_color(const ld_stmt_t *stmts, const size_t *occ,
                            size_t occ_n, size_t b,
                            const unsigned char *const *colors,
                            const unsigned char *prev, unsigned char *out,
                            ds_context_t *ctx) {
  char **r;
  ld_buf_t buf;
  size_t i;
  ds_sha256_t h;
  ds_status_t st = DS_OK;
  r = (char **)ds_context_calloc(ctx, occ_n + 1, sizeof(char *));
  if (r == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < occ_n && st == DS_OK; i++) {
    memset(&buf, 0, sizeof(buf));
    buf.ctx = ctx;
    ld_put(&buf, "", 0);
    ld_render(&buf, &stmts[occ[i]], b, colors, NULL);
    st = buf.st;
    r[i] = buf.p;
  }
  if (st == DS_OK) {
    qsort(r, occ_n, sizeof(char *), ld_cmp_str);
    ds_sha256_init(&h);
    if (prev != NULL) {
      ds_sha256_update(&h, prev, DS_SHA256_SIZE);
    }
    for (i = 0; i < occ_n; i++) {
      ds_sha256_update(&h, r[i], strlen(r[i]));
      ds_sha256_update(&h, "\n", 1);
    }
    ds_sha256_final(&h, out);
  }
  for (i = 0; i < occ_n; i++) {
    ds_context_free(ctx, r[i]);
  }
  ds_context_free(ctx, r);
  return st;
}

static size_t ld_distinct(const unsigned char *colors, size_t nb,
                          ld_order_t *ord) {
  size_t i;
  size_t k = 0;
  for (i = 0; i < nb; i++) {
    ord[i].color = colors + i * DS_SHA256_SIZE;
    ord[i].id = i;
  }
  qsort(ord, nb, sizeof(ld_order_t), ld_cmp_order);
  for (i = 0; i < nb; i++) {
    k += i == 0 || memcmp(ord[i].color, ord[i - 1].color, DS_SHA256_SIZE) != 0;
  }
  return k;
}

ds_status_t ds_lds_canonicalize(const char *const *quads, size_t n, char **out,
                                size_t *out_len, ds_context_t *context) {
  ld_stmt_t *stmts = NULL;
  const char **labels = NULL;
  size_t *label_len = NULL;
  size_t nb = 0;
  size_t bcap = 0;
  unsigned char *color = NULL;
  unsigned char *next = NULL;
  const unsigned char **cptr = NULL;
  size_t *occ = NULL;
  size_t *occ_start = NULL;
  ld_order_t *ord = NULL;
  char **names = NULL;
  char **lines = NULL;
  const char **uniq = NULL;
  ld_buf_t res;
  size_t i;
  size_t j;
  ds_status_t st = DS_OK;
  if (out == NULL || out_len == NULL || (n > 0 && quads == NULL)) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  *out_len = 0;
  memset(&res, 0, sizeof(res));
  res.ctx = context;
  for (i = 0; i < n; i++) {
    if (quads[i] == NULL) {
      return DS_ERR_NULL;
    }
  }
  /* identical statements count once */
  uniq = (const char **)ds_context_alloc(context, (n + 1) * sizeof(char *));
  stmts = (ld_stmt_t *)ds_context_calloc(context, n + 1, sizeof(ld_stmt_t));
  if (uniq == NULL || stmts == NULL) {
    ds_context_free(context, (void *)uniq);
    ds_context_free(context, stmts);
    return DS_ERR_ALLOC;
  }
  memcpy((void *)uniq, quads, n * sizeof(char *));
  qsort((void *)uniq, n, sizeof(char *), ld_cmp_str);
  j = 0;
  for (i = 0; i < n; i++) {
    if (j == 0 || strcmp(uniq[i], uniq[j - 1]) != 0) {
      uniq[j++] = uniq[i];
    }
  }
  n = j;
  for (i = 0; i < n && st == DS_OK; i++) {
    st = ld_split(&stmts[i], uniq[i], &labels, &label_len, &nb, &bcap, context);
  }
  if (st != DS_OK) {
    goto done;
  }
  /* statements per blank node (CSR) */
  occ_start = (size_t *)ds_context_calloc(context, nb + 2, sizeof(size_t));
  color = (unsigned char *)ds_context_alloc(context, (nb + 1) * DS_SHA256_SIZE);
  next = (unsigned char *)ds_context_alloc(context, (nb + 1) * DS_SHA256_SIZE);
  cptr = (const unsigned char **)ds_context_alloc(
      context, (nb + 1) * sizeof(const unsigned char *));
  ord = (ld_order_t *)ds_context_alloc(context, (nb + 1) * sizeof(ld_order_t));
  names = (char **)ds_context_calloc(context, nb + 1, sizeof(char *));
  lines = (char **)ds_context_calloc(context, n + 1, sizeof(char *));
  if (occ_start == NULL || color == NULL || next == NULL || cptr == NULL ||
      ord == NULL || names == NULL || lines == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (i = 0; i < n; i++) {
    for (j = 0; j < stmts[i].n; j++) {
      if (stmts[i].pieces[j].end == (size_t)-1) {
        occ_start[stmts[i].pieces[j].blank + 1]++;
      }
    }
  }
  for (i = 0; i < nb; i++) {
    occ_start[i + 1] += occ_start[i];
  }
  occ =
      (size_t *)ds_context_alloc(context, (occ_start[nb] + 1) * sizeof(size_t));
  if (occ == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  {
    size_t *fill =
        (size_t *)ds_context_alloc(context, (nb + 1) * sizeof(size_t));
    if (fill == NULL) {
      st = DS_ERR_ALLOC;
      goto done;
    }
    memcpy(fill, occ_start, (nb + 1) * sizeof(size_t));
    for (i = 0; i < n; i++) {
      for (j = 0; j < stmts[i].n; j++) {
        size_t bl = stmts[i].pieces[j].blank;
        if (stmts[i].pieces[j].end == (size_t)-1 &&
            (fill[bl] == occ_start[bl] || occ[fill[bl] - 1] != i)) {
          occ[fill[bl]++] = i;
        }
      }
    }
    /* blank i's statements: occ[occ_start[i] .. fill[i]) (a statement
     * naming it twice is listed once) */
    for (i = 0; i < nb && st == DS_OK; i++) {
      st = ld_color(stmts, occ + occ_start[i], fill[i] - occ_start[i], i, NULL,
                    NULL, color + i * DS_SHA256_SIZE, context);
    }
    /* refine until the partition stops splitting */
    if (st == DS_OK && nb > 1) {
      size_t classes = ld_distinct(color, nb, ord);
      size_t round;
      for (round = 0; round < nb && classes < nb && st == DS_OK; round++) {
        size_t now;
        for (i = 0; i < nb; i++) {
          cptr[i] = color + i * DS_SHA256_SIZE;
        }
        for (i = 0; i < nb && st == DS_OK; i++) {
          st = ld_color(stmts, occ + occ_start[i], fill[i] - occ_start[i], i,
                        cptr, color + i * DS_SHA256_SIZE,
                        next + i * DS_SHA256_SIZE, context);
        }
        memcpy(color, next, nb * DS_SHA256_SIZE);
        now = ld_distinct(color, nb, ord);
        if (now <= classes) {
          break;
        }
        classes = now;
      }
    }
    ds_context_free(context, fill);
  }
  if (st != DS_OK) {
    goto done;
  }
  /* canonical names in color order */
  if (nb > 0) {
    ld_distinct(color, nb, ord);
  }
  for (i = 0; i < nb && st == DS_OK; i++) {
    char tmp[32];
    size_t k = i;
    size_t len = 0;
    char digits[24];
    size_t nd = 0;
    do {
      digits[nd++] = (char)('0' + k % 10);
      k /= 10;
    } while (k > 0);
    memcpy(tmp, "_:c14n", 6);
    len = 6;
    while (nd > 0) {
      tmp[len++] = digits[--nd];
    }
    tmp[len] = '\0';
    names[ord[i].id] = (char *)ds_context_alloc(context, len + 1);
    if (names[ord[i].id] == NULL) {
      st = DS_ERR_ALLOC;
      break;
    }
    memcpy(names[ord[i].id], tmp, len + 1);
  }
  /* render, sort, deduplicate */
  for (i = 0; i < n && st == DS_OK; i++) {
    ld_buf_t b;
    memset(&b, 0, sizeof(b));
    b.ctx = context;
    ld_put(&b, "", 0);
    ld_render(&b, &stmts[i], (size_t)-1, NULL, names);
    st = b.st;
    lines[i] = b.p;
  }
  if (st == DS_OK) {
    qsort(lines, n, sizeof(char *), ld_cmp_str);
    ld_put(&res, "", 0);
    for (i = 0; i < n; i++) {
      if (i > 0 && strcmp(lines[i], lines[i - 1]) == 0) {
        continue;
      }
      ld_put(&res, lines[i], strlen(lines[i]));
      ld_put(&res, "\n", 1);
    }
    st = res.st;
  }
done:
  if (st == DS_OK) {
    *out = res.p;
    *out_len = res.len;
  } else {
    ds_context_free(context, res.p);
  }
  if (stmts != NULL) {
    for (i = 0; i < n; i++) {
      ds_context_free(context, stmts[i].pieces);
    }
  }
  if (names != NULL) {
    for (i = 0; i < nb; i++) {
      ds_context_free(context, names[i]);
    }
  }
  if (lines != NULL) {
    for (i = 0; i < n; i++) {
      ds_context_free(context, lines[i]);
    }
  }
  ds_context_free(context, stmts);
  ds_context_free(context, (void *)uniq);
  ds_context_free(context, (void *)labels);
  ds_context_free(context, label_len);
  ds_context_free(context, color);
  ds_context_free(context, next);
  ds_context_free(context, (void *)cptr);
  ds_context_free(context, occ);
  ds_context_free(context, occ_start);
  ds_context_free(context, ord);
  ds_context_free(context, names);
  ds_context_free(context, lines);
  return st;
}

/* ---- signing
 * ------------------------------------------------------------------- */

static ds_status_t ld_options_hash(const ds_lds_option_t *options, size_t n,
                                   unsigned char *out, ds_context_t *ctx) {
  char **l;
  size_t i;
  ds_sha256_t h;
  ds_status_t st = DS_OK;
  if (n > 0 && options == NULL) {
    return DS_ERR_NULL;
  }
  l = (char **)ds_context_calloc(ctx, n + 1, sizeof(char *));
  if (l == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n && st == DS_OK; i++) {
    ld_buf_t b;
    if (options[i].name == NULL || options[i].value == NULL) {
      st = DS_ERR_NULL;
      break;
    }
    memset(&b, 0, sizeof(b));
    b.ctx = ctx;
    ld_put(&b, options[i].name, strlen(options[i].name));
    ld_put(&b, " ", 1);
    ld_put(&b, options[i].value, strlen(options[i].value));
    st = b.st;
    l[i] = b.p;
  }
  if (st == DS_OK) {
    qsort(l, n, sizeof(char *), ld_cmp_str);
    ds_sha256_init(&h);
    for (i = 0; i < n; i++) {
      ds_sha256_update(&h, l[i], strlen(l[i]));
      ds_sha256_update(&h, "\n", 1);
    }
    ds_sha256_final(&h, out);
  }
  for (i = 0; i < n; i++) {
    ds_context_free(ctx, l[i]);
  }
  ds_context_free(ctx, l);
  return st;
}

ds_status_t ds_lds_tbs(const char *const *quads, size_t n,
                       const ds_lds_option_t *options, size_t n_options,
                       unsigned char *out, ds_context_t *context) {
  char *c14n;
  size_t len;
  ds_status_t st;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  st = ld_options_hash(options, n_options, out, context);
  if (st != DS_OK) {
    return st;
  }
  st = ds_lds_canonicalize(quads, n, &c14n, &len, context);
  if (st != DS_OK) {
    return st;
  }
  ds_sha256(c14n, len, out + DS_SHA256_SIZE);
  ds_context_free(context, c14n);
  return DS_OK;
}

ds_status_t ds_lds_sign(const char *const *quads, size_t n,
                        const ds_lds_option_t *options, size_t n_options,
                        const ds_lds_suite_t *suite, unsigned char *sig,
                        size_t *sig_len, ds_context_t *context) {
  unsigned char tbs[2 * DS_SHA256_SIZE];
  ds_status_t st;
  if (suite == NULL || sig == NULL || sig_len == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_lds_tbs(quads, n, options, n_options, tbs, context);
  if (st != DS_OK) {
    return st;
  }
  if (suite->sign != NULL) {
    return suite->sign(tbs, sizeof(tbs), sig, sig_len, suite->user);
  }
  if (*sig_len < DS_SHA256_SIZE) {
    return DS_ERR_RANGE;
  }
  ds_hmac_sha256(suite->key, suite->key_len, tbs, sizeof(tbs), sig);
  *sig_len = DS_SHA256_SIZE;
  return DS_OK;
}

ds_status_t ds_lds_verify(const char *const *quads, size_t n,
                          const ds_lds_option_t *options, size_t n_options,
                          const ds_lds_suite_t *suite, const unsigned char *sig,
                          size_t sig_len, ds_context_t *context) {
  unsigned char tbs[2 * DS_SHA256_SIZE];
  unsigned char mac[DS_SHA256_SIZE];
  unsigned diff = 0;
  size_t i;
  ds_status_t st;
  if (suite == NULL || sig == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_lds_tbs(quads, n, options, n_options, tbs, context);
  if (st != DS_OK) {
    return st;
  }
  if (suite->verify != NULL) {
    return suite->verify(tbs, sizeof(tbs), sig, sig_len, suite->user)
               ? DS_OK
               : DS_NOT_FOUND;
  }
  if (sig_len != DS_SHA256_SIZE) {
    return DS_NOT_FOUND;
  }
  ds_hmac_sha256(suite->key, suite->key_len, tbs, sizeof(tbs), mac);
  for (i = 0; i < DS_SHA256_SIZE; i++) {
    diff |= (unsigned)(mac[i] ^ sig[i]);
  }
  return diff == 0 ? DS_OK : DS_NOT_FOUND;
}
