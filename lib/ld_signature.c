#include "ld_signature.h"
#include <stdlib.h>
#include <string.h>

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
