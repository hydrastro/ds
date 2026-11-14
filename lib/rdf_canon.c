#include "rdf_canon.h"
#include <stdlib.h>
#include <string.h>

#define RC_NONE ((size_t) - 1)

static const char rc_xsd_string[] = "http://www.w3.org/2001/XMLSchema#string";
static const char rc_lang_string[] =
    "http://www.w3.org/1999/02/22-rdf-syntax-ns#langString";

/* ---- growing byte buffers */

typedef struct rc_buf {
  char *p;
  size_t len;
  size_t cap;
} rc_buf_t;

static ds_status_t rc_reserve(rc_buf_t *b, size_t extra, ds_context_t *ctx) {
  if (extra > (size_t)-1 / 2 - b->len) {
    return DS_ERR_OVERFLOW;
  }
  if (b->len + extra + 1 > b->cap) {
    size_t nc = b->cap < 64 ? 64 : b->cap;
    void *q;
    while (nc < b->len + extra + 1) {
      nc *= 2;
    }
    q = ds_context_realloc(ctx, b->p, nc);
    if (q == NULL) {
      return DS_ERR_ALLOC;
    }
    b->p = (char *)q;
    b->cap = nc;
  }
  return DS_OK;
}

static ds_status_t rc_put(rc_buf_t *b, const void *s, size_t n,
                          ds_context_t *ctx) {
  ds_status_t st = rc_reserve(b, n, ctx);
  if (st != DS_OK) {
    return st;
  }
  if (n > 0) {
    memcpy(b->p + b->len, s, n);
  }
  b->len += n;
  b->p[b->len] = '\0';
  return DS_OK;
}

static ds_status_t rc_puts(rc_buf_t *b, const char *s, ds_context_t *ctx) {
  return rc_put(b, s, strlen(s), ctx);
}

/* writes prefix and the decimal digits of v, and a NUL; returns the
 * length (out needs strlen(prefix) + 3 * sizeof(size_t) + 1 bytes) */
static size_t rc_format_id(char *out, const char *prefix, size_t v) {
  char d[3 * sizeof(size_t) + 1];
  size_t n = 0;
  size_t plen = strlen(prefix);
  do {
    d[sizeof(d) - 1 - n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v > 0);
  memcpy(out, prefix, plen);
  memcpy(out + plen, d + sizeof(d) - n, n);
  out[plen + n] = '\0';
  return plen + n;
}

/* prefix followed by the decimal digits of v */
static ds_status_t rc_put_id(rc_buf_t *b, const char *prefix, size_t v,
                             ds_context_t *ctx) {
  char d[8 + 3 * sizeof(size_t) + 1];
  return rc_put(b, d, rc_format_id(d, prefix, v), ctx);
}

static void rc_buf_free(rc_buf_t *b, ds_context_t *ctx) {
  ds_context_free(ctx, b->p);
  b->p = NULL;
  b->len = b->cap = 0;
}

/* -1, 0 or 1 as a is below, equal to or above b in code point order */
static int rc_cmp_bytes(const char *a, size_t an, const char *b, size_t bn) {
  int c = memcmp(a, b, an < bn ? an : bn);
  if (c != 0) {
    return c < 0 ? -1 : 1;
  }
  return (an > bn) - (an < bn);
}

/* ---- characters */

/* decodes the UTF-8 character at s[*i] (*i < n); 0 if malformed */
static int rc_utf8(const char *s_, size_t n, size_t *i, unsigned long *cp) {
  const unsigned char *s = (const unsigned char *)s_;
  unsigned long c = s[*i];
  unsigned long min;
  size_t need;
  size_t k;
  if (c < 0x80) {
    *cp = c;
    (*i)++;
    return 1;
  }
  if (c >= 0xC2 && c <= 0xDF) {
    need = 1;
    c &= 0x1F;
    min = 0x80;
  } else if (c >= 0xE0 && c <= 0xEF) {
    need = 2;
    c &= 0x0F;
    min = 0x800;
  } else if (c >= 0xF0 && c <= 0xF4) {
    need = 3;
    c &= 0x07;
    min = 0x10000;
  } else {
    return 0;
  }
  if (n - *i <= need) {
    return 0;
  }
  for (k = 1; k <= need; k++) {
    unsigned long b = s[*i + k];
    if ((b & 0xC0) != 0x80) {
      return 0;
    }
    c = (c << 6) | (b & 0x3F);
  }
  if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
    return 0;
  }
  *cp = c;
  *i += need + 1;
  return 1;
}

static size_t rc_encode(unsigned long cp, char *out) {
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

static int rc_alpha(unsigned long c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int rc_digit(unsigned long c) { return c >= '0' && c <= '9'; }

static int rc_pn_base(unsigned long c) {
  return rc_alpha(c) || (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) ||
         (c >= 0xF8 && c <= 0x2FF) || (c >= 0x370 && c <= 0x37D) ||
         (c >= 0x37F && c <= 0x1FFF) || (c >= 0x200C && c <= 0x200D) ||
         (c >= 0x2070 && c <= 0x218F) || (c >= 0x2C00 && c <= 0x2FEF) ||
         (c >= 0x3001 && c <= 0xD7FF) || (c >= 0xF900 && c <= 0xFDCF) ||
         (c >= 0xFDF0 && c <= 0xFFFD) || (c >= 0x10000 && c <= 0xEFFFF);
}

static int rc_pn_u(unsigned long c) {
  return rc_pn_base(c) || c == '_' || c == ':';
}

static int rc_pn(unsigned long c) {
  return rc_pn_u(c) || c == '-' || rc_digit(c) || c == 0xB7 ||
         (c >= 0x300 && c <= 0x36F) || (c >= 0x203F && c <= 0x2040);
}

/* a character N-Quads can write unescaped in an IRI */
static int rc_iri_char(unsigned long c) {
  return c > 0x20 && c != '<' && c != '>' && c != '"' && c != '{' && c != '}' &&
         c != '|' && c != '^' && c != '`' && c != '\\';
}

static int rc_iri_ok(const char *s, size_t n) {
  size_t i;
  unsigned long c;
  if (n == 0 || !rc_alpha((unsigned char)s[0])) {
    return 0;
  }
  for (i = 1; i < n && s[i] != ':'; i++) {
    c = (unsigned char)s[i];
    if (!rc_alpha(c) && !rc_digit(c) && c != '+' && c != '-' && c != '.') {
      return 0;
    }
  }
  if (i == n) {
    return 0;
  }
  i = 0;
  while (i < n) {
    if (!rc_utf8(s, n, &i, &c) || !rc_iri_char(c)) {
      return 0;
    }
  }
  return 1;
}

static int rc_label_ok(const char *s, size_t n) {
  size_t i = 0;
  unsigned long c;
  int dot = 0;
  if (n == 0 || !rc_utf8(s, n, &i, &c) || !(rc_pn_u(c) || rc_digit(c))) {
    return 0;
  }
  while (i < n) {
    if (!rc_utf8(s, n, &i, &c)) {
      return 0;
    }
    if (c == '.') {
      dot = 1;
    } else if (rc_pn(c)) {
      dot = 0;
    } else {
      return 0;
    }
  }
  return !dot;
}

static int rc_lang_ok(const char *s) {
  size_t i = 0;
  size_t run = 0;
  int first = 1;
  for (;; i++) {
    unsigned long c = (unsigned char)s[i];
    if (c == '\0' || c == '-') {
      if (run == 0) {
        return 0;
      }
      if (c == '\0') {
        return 1;
      }
      run = 0;
      first = 0;
    } else if (rc_alpha(c) || (!first && rc_digit(c))) {
      run++;
    } else {
      return 0;
    }
  }
}

static int rc_text_ok(const char *s, size_t n) {
  size_t i = 0;
  unsigned long c;
  while (i < n) {
    if (!rc_utf8(s, n, &i, &c)) {
      return 0;
    }
  }
  return 1;
}

/* ---- terms */

void ds_rdf_iri(ds_rdf_term_t *term, const char *iri) {
  if (term == NULL) {
    return;
  }
  term->kind = DS_RDF_IRI;
  term->value = iri;
  term->length = iri != NULL ? strlen(iri) : 0;
  term->datatype = NULL;
  term->language = NULL;
}

void ds_rdf_blank(ds_rdf_term_t *term, const char *label) {
  if (term == NULL) {
    return;
  }
  ds_rdf_iri(term, label);
  term->kind = DS_RDF_BLANK;
}

void ds_rdf_literal(ds_rdf_term_t *term, const char *lexical,
                    const char *datatype, const char *language) {
  if (term == NULL) {
    return;
  }
  ds_rdf_iri(term, lexical);
  term->kind = DS_RDF_LITERAL;
  term->datatype = datatype;
  term->language = language;
}

void ds_rdf_default_graph(ds_rdf_term_t *term) {
  if (term == NULL) {
    return;
  }
  ds_rdf_iri(term, "");
  term->kind = DS_RDF_DEFAULT_GRAPH;
}

static int rc_str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) {
    return a == b;
  }
  return strcmp(a, b) == 0;
}

bool ds_rdf_term_equal(const ds_rdf_term_t *a, const ds_rdf_term_t *b) {
  if (a == NULL || b == NULL || a->kind != b->kind) {
    return false;
  }
  if (a->kind == DS_RDF_DEFAULT_GRAPH) {
    return true;
  }
  return a->length == b->length &&
         (a->length == 0 || memcmp(a->value, b->value, a->length) == 0) &&
         rc_str_eq(a->datatype, b->datatype) &&
         rc_str_eq(a->language, b->language);
}

/* order of terms for finding duplicates: any total order will do */
static int rc_term_cmp(const ds_rdf_term_t *a, const ds_rdf_term_t *b) {
  int c;
  if (a->kind != b->kind) {
    return a->kind < b->kind ? -1 : 1;
  }
  if (a->kind == DS_RDF_DEFAULT_GRAPH) {
    return 0;
  }
  c = rc_cmp_bytes(a->value, a->length, b->value, b->length);
  if (c != 0) {
    return c;
  }
  if (!rc_str_eq(a->datatype, b->datatype)) {
    if (a->datatype == NULL || b->datatype == NULL) {
      return a->datatype == NULL ? -1 : 1;
    }
    return strcmp(a->datatype, b->datatype) < 0 ? -1 : 1;
  }
  if (!rc_str_eq(a->language, b->language)) {
    if (a->language == NULL || b->language == NULL) {
      return a->language == NULL ? -1 : 1;
    }
    return strcmp(a->language, b->language) < 0 ? -1 : 1;
  }
  return 0;
}

static int rc_quad_cmp(const ds_rdf_quad_t *a, const ds_rdf_quad_t *b) {
  int c = rc_term_cmp(&a->subject, &b->subject);
  if (c == 0) {
    c = rc_term_cmp(&a->predicate, &b->predicate);
  }
  if (c == 0) {
    c = rc_term_cmp(&a->object, &b->object);
  }
  if (c == 0) {
    c = rc_term_cmp(&a->graph, &b->graph);
  }
  return c;
}

/* ---- datasets */

void ds_rdf_dataset_init(ds_rdf_dataset_t *dataset, ds_context_t *context) {
  if (dataset == NULL) {
    return;
  }
  dataset->quads = NULL;
  dataset->count = 0;
  dataset->capacity = 0;
  dataset->chunks = NULL;
  dataset->context = context;
}

static void rc_free_chunks(ds_rdf_dataset_t *d, ds_rdf_chunk_t *stop) {
  while (d->chunks != stop) {
    ds_rdf_chunk_t *next = d->chunks->next;
    ds_context_free(d->context, d->chunks);
    d->chunks = next;
  }
}

void ds_rdf_dataset_clear(ds_rdf_dataset_t *dataset) {
  if (dataset == NULL) {
    return;
  }
  rc_free_chunks(dataset, NULL);
  dataset->count = 0;
}

void ds_rdf_dataset_destroy(ds_rdf_dataset_t *dataset) {
  if (dataset == NULL) {
    return;
  }
  ds_rdf_dataset_clear(dataset);
  ds_context_free(dataset->context, dataset->quads);
  dataset->quads = NULL;
  dataset->capacity = 0;
}

/* a copy of s[0 .. n) and a NUL in the dataset's storage */
static char *rc_store(ds_rdf_dataset_t *d, const char *s, size_t n) {
  ds_rdf_chunk_t *c = d->chunks;
  char *p;
  if (n > (size_t)-1 / 2) {
    return NULL;
  }
  if (c == NULL || c->size - c->used < n + 1) {
    size_t size = n + 1 > 4000 ? n + 1 : 4000;
    c = (ds_rdf_chunk_t *)ds_context_alloc(d->context,
                                           sizeof(ds_rdf_chunk_t) + size);
    if (c == NULL) {
      return NULL;
    }
    c->next = d->chunks;
    c->used = 0;
    c->size = size;
    d->chunks = c;
  }
  p = (char *)(c + 1) + c->used;
  if (n > 0) {
    memcpy(p, s, n);
  }
  p[n] = '\0';
  c->used += n + 1;
  return p;
}

typedef struct rc_mark {
  ds_rdf_chunk_t *head;
  size_t used;
  size_t count;
} rc_mark_t;

static void rc_mark(const ds_rdf_dataset_t *d, rc_mark_t *m) {
  m->head = d->chunks;
  m->used = d->chunks != NULL ? d->chunks->used : 0;
  m->count = d->count;
}

static void rc_release(ds_rdf_dataset_t *d, const rc_mark_t *m) {
  rc_free_chunks(d, m->head);
  if (d->chunks != NULL) {
    d->chunks->used = m->used;
  }
  d->count = m->count;
}

/* checks a term for its position (0 subject .. 3 graph) */
static int rc_term_ok(const ds_rdf_term_t *t, int position) {
  if (t->kind == DS_RDF_DEFAULT_GRAPH) {
    return position == 3;
  }
  if (t->value == NULL) {
    return 0;
  }
  switch (t->kind) {
  case DS_RDF_IRI:
    return rc_iri_ok(t->value, t->length);
  case DS_RDF_BLANK:
    return position != 1 && rc_label_ok(t->value, t->length);
  case DS_RDF_LITERAL:
    if (position != 2 || !rc_text_ok(t->value, t->length)) {
      return 0;
    }
    if (t->language != NULL) {
      return rc_lang_ok(t->language) &&
             (t->datatype == NULL || strcmp(t->datatype, rc_lang_string) == 0);
    }
    return t->datatype == NULL || rc_iri_ok(t->datatype, strlen(t->datatype));
  case DS_RDF_DEFAULT_GRAPH:
  default:
    return 0;
  }
}

static ds_status_t rc_copy_term(ds_rdf_dataset_t *d, const ds_rdf_term_t *t,
                                ds_rdf_term_t *out) {
  out->kind = t->kind;
  out->datatype = NULL;
  out->language = NULL;
  if (t->kind == DS_RDF_DEFAULT_GRAPH) {
    out->value = rc_store(d, "", 0);
    out->length = 0;
    return out->value != NULL ? DS_OK : DS_ERR_ALLOC;
  }
  out->value = rc_store(d, t->value, t->length);
  out->length = t->length;
  if (out->value == NULL) {
    return DS_ERR_ALLOC;
  }
  if (t->kind == DS_RDF_LITERAL) {
    if (t->language != NULL) {
      out->language = rc_store(d, t->language, strlen(t->language));
      if (out->language == NULL) {
        return DS_ERR_ALLOC;
      }
    } else if (t->datatype != NULL && strcmp(t->datatype, rc_xsd_string) != 0) {
      out->datatype = rc_store(d, t->datatype, strlen(t->datatype));
      if (out->datatype == NULL) {
        return DS_ERR_ALLOC;
      }
    }
  }
  return DS_OK;
}

static ds_status_t rc_add(ds_rdf_dataset_t *d, const ds_rdf_quad_t *q) {
  ds_rdf_quad_t c;
  ds_status_t st;
  if (!rc_term_ok(&q->subject, 0) || !rc_term_ok(&q->predicate, 1) ||
      !rc_term_ok(&q->object, 2) || !rc_term_ok(&q->graph, 3) ||
      q->subject.kind == DS_RDF_LITERAL || q->predicate.kind != DS_RDF_IRI ||
      q->graph.kind == DS_RDF_LITERAL) {
    return DS_ERR_INVALID;
  }
  if (d->count == d->capacity) {
    size_t nc = d->capacity < 8 ? 16 : d->capacity * 2;
    void *p;
    if (nc > (size_t)-1 / sizeof(ds_rdf_quad_t)) {
      return DS_ERR_OVERFLOW;
    }
    p = ds_context_realloc(d->context, d->quads, nc * sizeof(ds_rdf_quad_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    d->quads = (ds_rdf_quad_t *)p;
    d->capacity = nc;
  }
  st = rc_copy_term(d, &q->subject, &c.subject);
  if (st == DS_OK) {
    st = rc_copy_term(d, &q->predicate, &c.predicate);
  }
  if (st == DS_OK) {
    st = rc_copy_term(d, &q->object, &c.object);
  }
  if (st == DS_OK) {
    st = rc_copy_term(d, &q->graph, &c.graph);
  }
  if (st == DS_OK) {
    d->quads[d->count++] = c;
  }
  return st;
}

ds_status_t ds_rdf_dataset_add(ds_rdf_dataset_t *dataset,
                               const ds_rdf_quad_t *quad) {
  rc_mark_t m;
  ds_status_t st;
  if (dataset == NULL || quad == NULL) {
    return DS_ERR_NULL;
  }
  rc_mark(dataset, &m);
  st = rc_add(dataset, quad);
  if (st != DS_OK) {
    rc_release(dataset, &m);
  }
  return st;
}

/* ---- N-Quads */

typedef struct rc_parser {
  const char *s;
  size_t n;
  size_t i;
  rc_buf_t v[4]; /* values of subject .. graph */
  rc_buf_t dt;   /* the object's datatype */
  rc_buf_t lang; /* the object's language */
  ds_context_t *ctx;
} rc_parser_t;

static int rc_hex(int c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

/* one \uXXXX or \UXXXXXXXX escape at s[i] = '\\', as it stands */
static int rc_uchar_raw(rc_parser_t *p, unsigned long *cp) {
  size_t digits;
  size_t k;
  unsigned long c = 0;
  if (p->i + 1 >= p->n) {
    return 0;
  }
  digits = p->s[p->i + 1] == 'u' ? 4 : p->s[p->i + 1] == 'U' ? 8 : 0;
  if (digits == 0 || p->n - p->i < digits + 2) {
    return 0;
  }
  for (k = 0; k < digits; k++) {
    int h = rc_hex(p->s[p->i + 2 + k]);
    if (h < 0) {
      return 0;
    }
    c = (c << 4) | (unsigned long)h;
  }
  p->i += digits + 2;
  *cp = c;
  return 1;
}

/* a UCHAR: a high surrogate must be followed by an escaped low surrogate,
 * and the pair makes one character */
static int rc_uchar(rc_parser_t *p, unsigned long *cp) {
  unsigned long c;
  if (!rc_uchar_raw(p, &c)) {
    return 0;
  }
  if (c >= 0xD800 && c <= 0xDBFF) {
    unsigned long lo;
    if (p->i >= p->n || p->s[p->i] != '\\' || !rc_uchar_raw(p, &lo) ||
        lo < 0xDC00 || lo > 0xDFFF) {
      return 0;
    }
    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
  }
  if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
    return 0;
  }
  *cp = c;
  return 1;
}

static ds_status_t rc_put_cp(rc_buf_t *b, unsigned long cp, ds_context_t *ctx) {
  char u[4];
  return rc_put(b, u, rc_encode(cp, u), ctx);
}

static void rc_ws(rc_parser_t *p) {
  while (p->i < p->n && (p->s[p->i] == ' ' || p->s[p->i] == '\t')) {
    p->i++;
  }
}

/* IRIREF at '<' into b; DS_ERR_INVALID on a syntax error */
static ds_status_t rc_iriref(rc_parser_t *p, rc_buf_t *b) {
  ds_status_t st = DS_OK;
  b->len = 0;
  st = rc_reserve(b, 0, p->ctx);
  if (st != DS_OK) {
    return st;
  }
  b->p[0] = '\0';
  p->i++;
  while (p->i < p->n && p->s[p->i] != '>') {
    unsigned long c;
    if (p->s[p->i] == '\\') {
      if (!rc_uchar(p, &c) || !rc_iri_char(c)) {
        return DS_ERR_INVALID;
      }
    } else if (!rc_utf8(p->s, p->n, &p->i, &c) || !rc_iri_char(c)) {
      return DS_ERR_INVALID;
    }
    st = rc_put_cp(b, c, p->ctx);
    if (st != DS_OK) {
      return st;
    }
  }
  if (p->i >= p->n) {
    return DS_ERR_INVALID;
  }
  p->i++;
  return rc_iri_ok(b->p, b->len) ? DS_OK : DS_ERR_INVALID;
}

/* BLANK_NODE_LABEL at '_' into b */
static ds_status_t rc_label(rc_parser_t *p, rc_buf_t *b) {
  size_t start;
  size_t end;
  unsigned long c;
  if (p->n - p->i < 3 || p->s[p->i + 1] != ':') {
    return DS_ERR_INVALID;
  }
  p->i += 2;
  start = p->i;
  if (!rc_utf8(p->s, p->n, &p->i, &c) || !(rc_pn_u(c) || rc_digit(c))) {
    return DS_ERR_INVALID;
  }
  end = p->i;
  while (p->i < p->n) {
    size_t at = p->i;
    if (!rc_utf8(p->s, p->n, &p->i, &c) || !(c == '.' || rc_pn(c))) {
      p->i = at;
      break;
    }
    if (c != '.') {
      end = p->i;
    }
  }
  p->i = end; /* a label does not end with '.' */
  b->len = 0;
  return rc_put(b, p->s + start, end - start, p->ctx);
}

/* STRING_LITERAL_QUOTE at '"', then a language tag or a datatype */
static ds_status_t rc_literal(rc_parser_t *p) {
  ds_status_t st;
  rc_buf_t *b = &p->v[2];
  b->len = 0;
  p->dt.len = 0;
  p->lang.len = 0;
  st = rc_reserve(b, 0, p->ctx);
  if (st != DS_OK) {
    return st;
  }
  b->p[0] = '\0';
  p->i++;
  for (;;) {
    unsigned long c;
    char ch;
    if (p->i >= p->n) {
      return DS_ERR_INVALID;
    }
    ch = p->s[p->i];
    if (ch == '"') {
      p->i++;
      break;
    }
    if (ch == '\n' || ch == '\r') {
      return DS_ERR_INVALID;
    }
    if (ch == '\\') {
      char e = p->i + 1 < p->n ? p->s[p->i + 1] : '\0';
      if (e == 'u' || e == 'U') {
        if (!rc_uchar(p, &c)) {
          return DS_ERR_INVALID;
        }
      } else {
        c = e == 't'    ? 9UL
            : e == 'b'  ? 8UL
            : e == 'n'  ? 10UL
            : e == 'r'  ? 13UL
            : e == 'f'  ? 12UL
            : e == '"'  ? 34UL
            : e == '\'' ? 39UL
            : e == '\\' ? 92UL
                        : 0x110000UL;
        if (c == 0x110000UL) {
          return DS_ERR_INVALID;
        }
        p->i += 2;
      }
    } else if (!rc_utf8(p->s, p->n, &p->i, &c)) {
      return DS_ERR_INVALID;
    }
    st = rc_put_cp(b, c, p->ctx);
    if (st != DS_OK) {
      return st;
    }
  }
  if (p->i < p->n && p->s[p->i] == '@') {
    size_t start = ++p->i;
    while (p->i < p->n &&
           (rc_alpha((unsigned char)p->s[p->i]) ||
            ((rc_digit((unsigned char)p->s[p->i]) || p->s[p->i] == '-') &&
             p->i > start))) {
      p->i++;
    }
    st = rc_put(&p->lang, p->s + start, p->i - start, p->ctx);
    if (st != DS_OK) {
      return st;
    }
    return rc_lang_ok(p->lang.p) ? DS_OK : DS_ERR_INVALID;
  }
  if (p->n - p->i >= 2 && p->s[p->i] == '^' && p->s[p->i + 1] == '^') {
    p->i += 2;
    if (p->i >= p->n || p->s[p->i] != '<') {
      return DS_ERR_INVALID;
    }
    return rc_iriref(p, &p->dt);
  }
  return DS_OK;
}

/* a subject, object or graph term (position 0, 2 or 3) */
static ds_status_t rc_term(rc_parser_t *p, int position, ds_rdf_term_t *t) {
  ds_status_t st;
  char c = p->i < p->n ? p->s[p->i] : '\0';
  t->datatype = NULL;
  t->language = NULL;
  if (c == '<') {
    t->kind = DS_RDF_IRI;
    st = rc_iriref(p, &p->v[position]);
  } else if (c == '_') {
    t->kind = DS_RDF_BLANK;
    st = rc_label(p, &p->v[position]);
  } else if (c == '"' && position == 2) {
    t->kind = DS_RDF_LITERAL;
    st = rc_literal(p);
    if (st == DS_OK) {
      t->datatype = p->dt.len > 0 ? p->dt.p : NULL;
      t->language = p->lang.len > 0 ? p->lang.p : NULL;
    }
  } else {
    return DS_ERR_INVALID;
  }
  if (st == DS_OK) {
    t->value = p->v[position].p;
    t->length = p->v[position].len;
  }
  return st;
}

static ds_status_t rc_statement(rc_parser_t *p, ds_rdf_dataset_t *d) {
  ds_rdf_quad_t q;
  ds_status_t st;
  st = rc_term(p, 0, &q.subject);
  if (st != DS_OK) {
    return st;
  }
  rc_ws(p);
  if (p->i >= p->n || p->s[p->i] != '<') {
    return DS_ERR_INVALID;
  }
  st = rc_iriref(p, &p->v[1]);
  if (st != DS_OK) {
    return st;
  }
  q.predicate.kind = DS_RDF_IRI;
  q.predicate.value = p->v[1].p;
  q.predicate.length = p->v[1].len;
  q.predicate.datatype = q.predicate.language = NULL;
  rc_ws(p);
  st = rc_term(p, 2, &q.object);
  if (st != DS_OK) {
    return st;
  }
  rc_ws(p);
  if (p->i < p->n && p->s[p->i] != '.') {
    st = rc_term(p, 3, &q.graph);
    if (st != DS_OK) {
      return st;
    }
    rc_ws(p);
  } else {
    ds_rdf_default_graph(&q.graph);
  }
  if (p->i >= p->n || p->s[p->i] != '.') {
    return DS_ERR_INVALID;
  }
  p->i++;
  rc_ws(p);
  if (p->i < p->n && p->s[p->i] == '#') {
    while (p->i < p->n && p->s[p->i] != '\n' && p->s[p->i] != '\r') {
      p->i++;
    }
  }
  if (p->i < p->n && p->s[p->i] != '\n' && p->s[p->i] != '\r') {
    return DS_ERR_INVALID;
  }
  return rc_add(d, &q);
}

ds_status_t ds_rdf_parse_nquads(ds_rdf_dataset_t *dataset, const char *text,
                                size_t length, size_t *error_line) {
  rc_parser_t p;
  rc_mark_t m;
  size_t line = 1;
  size_t k;
  ds_status_t st = DS_OK;
  if (error_line != NULL) {
    *error_line = 0;
  }
  if (dataset == NULL || (text == NULL && length > 0)) {
    return DS_ERR_NULL;
  }
  memset(&p, 0, sizeof(p));
  p.s = text;
  p.n = length;
  p.ctx = dataset->context;
  rc_mark(dataset, &m);
  while (p.i < p.n && st == DS_OK) {
    char c;
    rc_ws(&p);
    if (p.i >= p.n) {
      break;
    }
    c = p.s[p.i];
    if (c == '\n' || c == '\r') {
      p.i++;
      if (c == '\r' && p.i < p.n && p.s[p.i] == '\n') {
        p.i++;
      }
      line++;
    } else if (c == '#') {
      while (p.i < p.n && p.s[p.i] != '\n' && p.s[p.i] != '\r') {
        p.i++;
      }
    } else {
      st = rc_statement(&p, dataset);
    }
  }
  for (k = 0; k < 4; k++) {
    rc_buf_free(&p.v[k], p.ctx);
  }
  rc_buf_free(&p.dt, p.ctx);
  rc_buf_free(&p.lang, p.ctx);
  if (st != DS_OK) {
    rc_release(dataset, &m);
    if (error_line != NULL && st == DS_ERR_INVALID) {
      *error_line = line;
    }
  }
  return st;
}

/* the lexical form between quotes, escaped as canonical N-Quads requires */
static ds_status_t rc_put_lexical(rc_buf_t *b, const char *s, size_t n,
                                  ds_context_t *ctx) {
  static const char hex[] = "0123456789ABCDEF";
  size_t i = 0;
  size_t run = 0;
  ds_status_t st = DS_OK;
  while (i < n && st == DS_OK) {
    unsigned char c = (unsigned char)s[i];
    char esc[7];
    size_t elen = 2;
    size_t skip = 1;
    esc[0] = '\\';
    if (c == 8 || c == 9 || c == 10 || c == 12 || c == 13 || c == '"' ||
        c == '\\') {
      esc[1] = c == 8    ? 'b'
               : c == 9  ? 't'
               : c == 10 ? 'n'
               : c == 12 ? 'f'
               : c == 13 ? 'r'
                         : (char)c;
    } else if (c < 0x20 || c == 0x7F) {
      esc[1] = 'u';
      esc[2] = '0';
      esc[3] = '0';
      esc[4] = hex[c >> 4];
      esc[5] = hex[c & 15];
      elen = 6;
    } else if (c == 0xEF && n - i >= 3 && (unsigned char)s[i + 1] == 0xBF &&
               ((unsigned char)s[i + 2] == 0xBE ||
                (unsigned char)s[i + 2] == 0xBF)) {
      /* U+FFFE and U+FFFF are not XML 1.1 characters */
      memcpy(esc + 1, (unsigned char)s[i + 2] == 0xBE ? "uFFFE" : "uFFFF", 5);
      elen = 6;
      skip = 3;
    } else {
      i++;
      continue;
    }
    st = rc_put(b, s + run, i - run, ctx);
    if (st == DS_OK) {
      st = rc_put(b, esc, elen, ctx);
    }
    i += skip;
    run = i;
  }
  if (st == DS_OK) {
    st = rc_put(b, s + run, n - run, ctx);
  }
  return st;
}

/* a term in canonical N-Quads form (nothing for the default graph) */
static ds_status_t rc_put_term(rc_buf_t *b, const ds_rdf_term_t *t,
                               ds_context_t *ctx) {
  ds_status_t st = DS_OK;
  switch (t->kind) {
  case DS_RDF_IRI:
    st = rc_puts(b, "<", ctx);
    if (st == DS_OK) {
      st = rc_put(b, t->value, t->length, ctx);
    }
    if (st == DS_OK) {
      st = rc_puts(b, ">", ctx);
    }
    break;
  case DS_RDF_BLANK:
    st = rc_puts(b, "_:", ctx);
    if (st == DS_OK) {
      st = rc_put(b, t->value, t->length, ctx);
    }
    break;
  case DS_RDF_LITERAL:
    st = rc_puts(b, "\"", ctx);
    if (st == DS_OK) {
      st = rc_put_lexical(b, t->value, t->length, ctx);
    }
    if (st == DS_OK) {
      st = rc_puts(b, "\"", ctx);
    }
    if (st == DS_OK && t->language != NULL) {
      st = rc_puts(b, "@", ctx);
      if (st == DS_OK) {
        st = rc_puts(b, t->language, ctx);
      }
    } else if (st == DS_OK && t->datatype != NULL) {
      st = rc_puts(b, "^^<", ctx);
      if (st == DS_OK) {
        st = rc_puts(b, t->datatype, ctx);
      }
      if (st == DS_OK) {
        st = rc_puts(b, ">", ctx);
      }
    }
    break;
  case DS_RDF_DEFAULT_GRAPH:
  default:
    break;
  }
  return st;
}

ds_status_t ds_rdf_dataset_nquads(const ds_rdf_dataset_t *dataset, char **out,
                                  size_t *out_length) {
  rc_buf_t b;
  size_t i;
  ds_status_t st = DS_OK;
  if (dataset == NULL || out == NULL || out_length == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  *out_length = 0;
  memset(&b, 0, sizeof(b));
  st = rc_reserve(&b, 0, dataset->context);
  if (st == DS_OK) {
    b.p[0] = '\0';
  }
  for (i = 0; i < dataset->count && st == DS_OK; i++) {
    const ds_rdf_quad_t *q = &dataset->quads[i];
    st = rc_put_term(&b, &q->subject, dataset->context);
    if (st == DS_OK) {
      st = rc_puts(&b, " ", dataset->context);
    }
    if (st == DS_OK) {
      st = rc_put_term(&b, &q->predicate, dataset->context);
    }
    if (st == DS_OK) {
      st = rc_puts(&b, " ", dataset->context);
    }
    if (st == DS_OK) {
      st = rc_put_term(&b, &q->object, dataset->context);
    }
    if (st == DS_OK && q->graph.kind != DS_RDF_DEFAULT_GRAPH) {
      st = rc_puts(&b, " ", dataset->context);
      if (st == DS_OK) {
        st = rc_put_term(&b, &q->graph, dataset->context);
      }
    }
    if (st == DS_OK) {
      st = rc_puts(&b, " .\n", dataset->context);
    }
  }
  if (st != DS_OK) {
    rc_buf_free(&b, dataset->context);
    return st;
  }
  *out = b.p;
  *out_length = b.len;
  return DS_OK;
}

/* ---- canonicalization (RDFC-1.0) */

typedef struct rc_rel {
  unsigned char h[DS_DIGEST_MAX_SIZE]; /* the related hash */
  size_t hsize;
  size_t rank; /* of the related blank node, in input label order */
} rc_rel_t;

/* a run of Hash N-Degree Quads on the explicit stack */
typedef struct rc_frame {
  size_t node;
  size_t ilen; /* the frame's issuer is the shared issuer's first ilen */
  rc_rel_t *rel;
  size_t nrel;
  size_t relcap;
  size_t g; /* current group of related blank nodes: rel[g .. gend) */
  size_t gend;
  size_t *perm; /* ranks, permuted lexicographically */
  size_t *rl;   /* recursion list */
  size_t nrl;
  size_t ri;
  size_t *cseq; /* the chosen issuer, beyond ilen */
  size_t ncseq;
  size_t cseqcap;
  rc_buf_t data;
  rc_buf_t path;
  rc_buf_t chosen;
  int has_chosen;
  int state;
  unsigned char result[DS_DIGEST_MAX_SIZE];
} rc_frame_t;

enum { RC_ENTER, RC_GROUP, RC_PERM, RC_REC, RC_AFTER, RC_DONE, RC_NEXT };

typedef struct rc_state {
  const ds_rdf_dataset_t *d;
  ds_context_t *ctx;
  const ds_digest_ops_t *dg;
  void *hs; /* digest state */
  size_t hsize;
  /* distinct quads in input order: blank node of each component (or
   * RC_NONE) and the canonical text of the others */
  size_t nq;
  size_t *qb; /* 4 per quad */
  size_t *qt; /* 8 per quad: offset and length in text */
  rc_buf_t text;
  /* blank nodes in order of first appearance */
  size_t nb;
  const char **label;
  size_t *label_len;
  size_t *rank;
  size_t *by_rank;
  size_t *mstart; /* quads mentioning b: mq[mstart[b] .. mstart[b + 1]) */
  size_t *mq;
  unsigned char *hf; /* first-degree hashes */
  size_t *canon;     /* canonical number or RC_NONE */
  size_t *canon_order;
  size_t ncanon;
  /* the shared temporary issuer: seq[0 .. seqlen), pos[b] = index + 1 */
  size_t *seq;
  size_t seqlen;
  size_t *pos;
  /* limits */
  size_t *calls;
  size_t total;
  size_t most;
  size_t max_node;
  size_t max_total;
  rc_frame_t *frames;
  size_t nframes;
  size_t top;
  rc_buf_t line;
} rc_state_t;

static void rc_hash(rc_state_t *s, const void *data, size_t len,
                    unsigned char *out) {
  s->dg->init(s->hs);
  s->dg->update(s->hs, data != NULL ? data : "", len);
  s->dg->final(s->hs, out);
}

static ds_status_t rc_put_hex(rc_state_t *s, rc_buf_t *b,
                              const unsigned char *h) {
  char hex[2 * DS_DIGEST_MAX_SIZE + 1];
  ds_digest_hex(h, s->hsize, hex);
  return rc_put(b, hex, 2 * s->hsize, s->ctx);
}

/* text of component c of distinct quad q, empty for the default graph */
static const char *rc_text(const rc_state_t *s, size_t q, int c, size_t *len) {
  *len = s->qt[8 * q + 2 * (size_t)c + 1];
  return s->text.p + s->qt[8 * q + 2 * (size_t)c];
}

/* quad q in canonical N-Quads form, blank nodes named by mode: ref as _:a
 * and others as _:z (ref != RC_NONE), or canonical identifiers */
static ds_status_t rc_render(rc_state_t *s, rc_buf_t *b, size_t q, size_t ref) {
  int c;
  ds_status_t st = DS_OK;
  for (c = 0; c < 4 && st == DS_OK; c++) {
    size_t bl = s->qb[4 * q + (size_t)c];
    size_t len;
    const char *t = rc_text(s, q, c, &len);
    if (bl == RC_NONE && len == 0) {
      continue; /* the default graph */
    }
    if (c > 0) {
      st = rc_puts(b, " ", s->ctx);
    }
    if (st != DS_OK) {
      break;
    }
    if (bl == RC_NONE) {
      st = rc_put(b, t, len, s->ctx);
    } else if (ref != RC_NONE) {
      st = rc_puts(b, bl == ref ? "_:a" : "_:z", s->ctx);
    } else {
      st = rc_put_id(b, "_:c14n", s->canon[bl], s->ctx);
    }
  }
  if (st == DS_OK) {
    st = rc_puts(b, " .\n", s->ctx);
  }
  return st;
}

typedef struct rc_line {
  const char *p;
  size_t len;
} rc_line_t;

static int rc_line_cmp(const void *a, const void *b) {
  const rc_line_t *x = (const rc_line_t *)a;
  const rc_line_t *y = (const rc_line_t *)b;
  return rc_cmp_bytes(x->p, x->len, y->p, y->len);
}

/* renders quads qs[0 .. n) into s->line and sorts them into lines */
static ds_status_t rc_sorted_lines(rc_state_t *s, const size_t *qs, size_t n,
                                   size_t ref, rc_line_t *lines) {
  size_t i;
  ds_status_t st = DS_OK;
  s->line.len = 0;
  for (i = 0; i < n && st == DS_OK; i++) {
    lines[i].len = s->line.len;
    st = rc_render(s, &s->line, qs[i], ref);
  }
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < n; i++) {
    size_t start = lines[i].len;
    size_t end = i + 1 < n ? lines[i + 1].len : s->line.len;
    lines[i].p = s->line.p + start;
    lines[i].len = end - start;
  }
  qsort(lines, n, sizeof(rc_line_t), rc_line_cmp);
  return DS_OK;
}

/* Hash First Degree Quads of every blank node */
static ds_status_t rc_first_degree(rc_state_t *s) {
  size_t most = 0;
  size_t b;
  rc_line_t *lines;
  ds_status_t st = DS_OK;
  for (b = 0; b < s->nb; b++) {
    size_t m = s->mstart[b + 1] - s->mstart[b];
    most = m > most ? m : most;
  }
  lines = (rc_line_t *)ds_context_alloc(s->ctx, (most + 1) * sizeof(rc_line_t));
  if (lines == NULL) {
    return DS_ERR_ALLOC;
  }
  for (b = 0; b < s->nb && st == DS_OK; b++) {
    size_t m = s->mstart[b + 1] - s->mstart[b];
    size_t i;
    st = rc_sorted_lines(s, s->mq + s->mstart[b], m, b, lines);
    if (st != DS_OK) {
      break;
    }
    s->dg->init(s->hs);
    for (i = 0; i < m; i++) {
      s->dg->update(s->hs, lines[i].p, lines[i].len);
    }
    s->dg->final(s->hs, s->hf + b * s->hsize);
  }
  ds_context_free(s->ctx, lines);
  return st;
}

static void rc_issue(rc_state_t *s, size_t b) {
  if (s->canon[b] == RC_NONE) {
    s->canon[b] = s->ncanon;
    s->canon_order[s->ncanon++] = b;
  }
}

static void rc_push(rc_state_t *s, size_t b) {
  s->seq[s->seqlen++] = b;
  s->pos[b] = s->seqlen;
}

static void rc_truncate(rc_state_t *s, size_t len) {
  while (s->seqlen > len) {
    s->pos[s->seq[--s->seqlen]] = 0;
  }
}

/* Hash Related Blank Node */
static ds_status_t rc_related(rc_state_t *s, size_t related, size_t q, int c,
                              unsigned char *out) {
  static const char where[4] = {'s', 'p', 'o', 'g'};
  ds_status_t st;
  s->line.len = 0;
  st = rc_put(&s->line, &where[c], 1, s->ctx);
  if (st == DS_OK && c != 3) {
    size_t len;
    const char *t = rc_text(s, q, 1, &len);
    st = rc_put(&s->line, t, len, s->ctx); /* <predicate> */
  }
  if (st == DS_OK) {
    if (s->canon[related] != RC_NONE) {
      st = rc_put_id(&s->line, "_:c14n", s->canon[related], s->ctx);
    } else if (s->pos[related] != 0) {
      st = rc_put_id(&s->line, "_:b", s->pos[related] - 1, s->ctx);
    } else {
      st = rc_put_hex(s, &s->line, s->hf + related * s->hsize);
    }
  }
  if (st == DS_OK) {
    rc_hash(s, s->line.p, s->line.len, out);
  }
  return st;
}

static int rc_rel_cmp(const void *a, const void *b) {
  const rc_rel_t *x = (const rc_rel_t *)a;
  const rc_rel_t *y = (const rc_rel_t *)b;
  int c = memcmp(x->h, y->h, x->hsize);
  if (c != 0) {
    return c;
  }
  return (x->rank > y->rank) - (x->rank < y->rank);
}

static ds_status_t rc_grow(void **p, size_t *cap, size_t need, size_t elem,
                           ds_context_t *ctx) {
  size_t nc;
  void *q;
  if (need <= *cap) {
    return DS_OK;
  }
  nc = *cap < 8 ? 8 : *cap;
  while (nc < need) {
    nc *= 2;
  }
  if (nc > (size_t)-1 / elem) {
    return DS_ERR_OVERFLOW;
  }
  q = ds_context_realloc(ctx, *p, nc * elem);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *p = q;
  *cap = nc;
  return DS_OK;
}

/* the related hashes of a frame's node, sorted (Hn in hash order) */
static ds_status_t rc_build_related(rc_state_t *s, rc_frame_t *f) {
  size_t i;
  size_t need = 3 * (s->mstart[f->node + 1] - s->mstart[f->node]);
  size_t cap = f->relcap;
  void *p = f->rel;
  ds_status_t st = rc_grow(&p, &cap, need + 1, sizeof(rc_rel_t), s->ctx);
  f->rel = (rc_rel_t *)p;
  if (st == DS_OK && cap != f->relcap) {
    void *q = ds_context_realloc(s->ctx, f->perm, cap * sizeof(size_t));
    if (q == NULL) {
      return DS_ERR_ALLOC;
    }
    f->perm = (size_t *)q;
    q = ds_context_realloc(s->ctx, f->rl, cap * sizeof(size_t));
    if (q == NULL) {
      return DS_ERR_ALLOC;
    }
    f->rl = (size_t *)q;
    f->relcap = cap;
  }
  if (st != DS_OK) {
    return st;
  }
  f->nrel = 0;
  for (i = s->mstart[f->node]; i < s->mstart[f->node + 1] && st == DS_OK; i++) {
    size_t q = s->mq[i];
    int c;
    for (c = 0; c < 4 && st == DS_OK; c++) {
      size_t bl = s->qb[4 * q + (size_t)c];
      if (bl != RC_NONE && bl != f->node) {
        rc_rel_t *r = &f->rel[f->nrel++];
        st = rc_related(s, bl, q, c, r->h);
        r->hsize = s->hsize;
        r->rank = s->rank[bl];
      }
    }
  }
  if (st == DS_OK) {
    qsort(f->rel, f->nrel, sizeof(rc_rel_t), rc_rel_cmp);
  }
  return st;
}

static int rc_next_perm(size_t *a, size_t k) {
  size_t i;
  size_t j;
  if (k < 2) {
    return 0;
  }
  i = k - 1;
  while (i > 0 && a[i - 1] >= a[i]) {
    i--;
  }
  if (i == 0) {
    return 0;
  }
  j = k - 1;
  while (a[j] <= a[i - 1]) {
    j--;
  }
  {
    size_t t = a[i - 1];
    a[i - 1] = a[j];
    a[j] = t;
  }
  for (j = k - 1; i < j; i++, j--) {
    size_t t = a[i];
    a[i] = a[j];
    a[j] = t;
  }
  return 1;
}

/* true when no permutation follows a (non-increasing order) */
static int rc_last_perm(const size_t *a, size_t k) {
  size_t i;
  for (i = 1; i < k; i++) {
    if (a[i - 1] < a[i]) {
      return 0;
    }
  }
  return 1;
}

/* path > chosen in code point order */
static int rc_beyond(const rc_frame_t *f) {
  return f->has_chosen &&
         rc_cmp_bytes(f->path.p, f->path.len, f->chosen.p, f->chosen.len) > 0;
}

/* Hash N-Degree Quads of node n, whose temporary identifier the shared
 * issuer already holds; the issuer ends as the result's issuer */
static ds_status_t rc_ndegree(rc_state_t *s, size_t n, unsigned char *out) {
  ds_status_t st = DS_OK;
  if (s->nframes == 0) {
    void *p = ds_context_calloc(s->ctx, 4, sizeof(rc_frame_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    s->frames = (rc_frame_t *)p;
    s->nframes = 4;
  }
  s->top = 1;
  s->frames[0].node = n;
  s->frames[0].ilen = s->seqlen;
  s->frames[0].state = RC_ENTER;
  while (st == DS_OK) {
    rc_frame_t *f = &s->frames[s->top - 1];
    size_t k = f->gend - f->g;
    size_t j;
    switch (f->state) {
    case RC_ENTER:
      if (++s->calls[f->node] > s->max_node ||
          (s->max_total != 0 && s->total >= s->max_total)) {
        return DS_ERR_RANGE;
      }
      s->total++;
      if (s->calls[f->node] > s->most) {
        s->most = s->calls[f->node];
      }
      st = rc_build_related(s, f);
      f->data.len = 0;
      f->g = 0;
      f->state = RC_GROUP;
      break;
    case RC_GROUP:
      if (f->g == f->nrel) {
        rc_hash(s, f->data.p, f->data.len, f->result);
        if (--s->top == 0) {
          memcpy(out, f->result, s->hsize);
          return DS_OK;
        }
        break;
      }
      for (f->gend = f->g + 1;
           f->gend < f->nrel &&
           memcmp(f->rel[f->gend].h, f->rel[f->g].h, s->hsize) == 0;
           f->gend++) {
      }
      st = rc_put_hex(s, &f->data, f->rel[f->g].h);
      for (j = f->g; j < f->gend; j++) {
        f->perm[j - f->g] = f->rel[j].rank;
      }
      f->has_chosen = 0;
      f->state = RC_PERM;
      break;
    case RC_PERM:
      rc_truncate(s, f->ilen);
      f->path.len = 0;
      f->nrl = 0;
      f->state = RC_REC;
      f->ri = 0;
      for (j = 0; j < k && st == DS_OK; j++) {
        size_t r = s->by_rank[f->perm[j]];
        if (s->canon[r] != RC_NONE) {
          st = rc_put_id(&f->path, "_:c14n", s->canon[r], s->ctx);
        } else {
          if (s->pos[r] == 0) {
            f->rl[f->nrl++] = r;
            rc_push(s, r);
          }
          st = rc_put_id(&f->path, "_:b", s->pos[r] - 1, s->ctx);
        }
        if (rc_beyond(f)) {
          f->state = RC_NEXT;
          break;
        }
      }
      break;
    case RC_REC:
      if (f->ri == f->nrl) {
        f->state = RC_DONE;
        break;
      }
      f->state = RC_AFTER;
      if (s->top == s->nframes) {
        void *p = ds_context_realloc(s->ctx, s->frames,
                                     2 * s->nframes * sizeof(rc_frame_t));
        if (p == NULL) {
          return DS_ERR_ALLOC;
        }
        s->frames = (rc_frame_t *)p;
        memset(s->frames + s->nframes, 0, s->nframes * sizeof(rc_frame_t));
        s->nframes *= 2;
        f = &s->frames[s->top - 1];
      }
      s->frames[s->top].node = f->rl[f->ri];
      s->frames[s->top].ilen = s->seqlen;
      s->frames[s->top].state = RC_ENTER;
      s->top++;
      break;
    case RC_AFTER:
      st = rc_put_id(&f->path, "_:b", s->pos[f->rl[f->ri]] - 1, s->ctx);
      if (st == DS_OK) {
        st = rc_puts(&f->path, "<", s->ctx);
      }
      if (st == DS_OK) {
        st = rc_put_hex(s, &f->path, s->frames[s->top].result);
      }
      if (st == DS_OK) {
        st = rc_puts(&f->path, ">", s->ctx);
      }
      f->ri++;
      f->state = rc_beyond(f) ? RC_NEXT : RC_REC;
      break;
    case RC_DONE:
      if (!f->has_chosen || rc_cmp_bytes(f->path.p, f->path.len, f->chosen.p,
                                         f->chosen.len) < 0) {
        void *p = f->cseq;
        size_t cap = f->cseqcap;
        f->chosen.len = 0;
        st = rc_put(&f->chosen, f->path.p, f->path.len, s->ctx);
        if (st == DS_OK && rc_last_perm(f->perm, k)) {
          /* the shared issuer already is the chosen issuer: no copy, so
           * a long gossip path through single permutations stays linear */
          f->has_chosen = 2;
        } else if (st == DS_OK) {
          st = rc_grow(&p, &cap, s->seqlen - f->ilen + 1, sizeof(size_t),
                       s->ctx);
          f->cseq = (size_t *)p;
          f->cseqcap = cap;
          if (st == DS_OK) {
            f->ncseq = s->seqlen - f->ilen;
            memcpy(f->cseq, s->seq + f->ilen, f->ncseq * sizeof(size_t));
            f->has_chosen = 1;
          }
        }
      }
      f->state = RC_NEXT;
      break;
    default: /* RC_NEXT */
      if (rc_next_perm(f->perm, k)) {
        f->state = RC_PERM;
        break;
      }
      st = rc_put(&f->data, f->chosen.p, f->chosen.len, s->ctx);
      if (f->has_chosen != 2) {
        rc_truncate(s, f->ilen);
        for (j = 0; j < f->ncseq; j++) {
          rc_push(s, f->cseq[j]);
        }
      }
      f->ilen = s->seqlen;
      f->g = f->gend;
      f->state = RC_GROUP;
      break;
    }
  }
  return st;
}

/* one connected group of blank nodes sharing a first-degree hash: the
 * smallest result's hash, position and issuer */
typedef struct rc_comp {
  unsigned char h[DS_DIGEST_MAX_SIZE];
  size_t hsize;
  size_t position;
  size_t off; /* issuer in the component list */
  size_t size;
} rc_comp_t;

static int rc_comp_cmp(const void *a, const void *b) {
  const rc_comp_t *x = (const rc_comp_t *)a;
  const rc_comp_t *y = (const rc_comp_t *)b;
  int c = memcmp(x->h, y->h, x->hsize);
  if (c != 0) {
    return c;
  }
  return (x->position > y->position) - (x->position < y->position);
}

typedef struct rc_bhash {
  const unsigned char *h;
  size_t hsize;
  size_t b;
} rc_bhash_t;

static int rc_bhash_cmp(const void *a, const void *b) {
  const rc_bhash_t *x = (const rc_bhash_t *)a;
  const rc_bhash_t *y = (const rc_bhash_t *)b;
  int c = memcmp(x->h, y->h, x->hsize);
  if (c != 0) {
    return c;
  }
  return (x->b > y->b) - (x->b < y->b);
}

/* steps 4 and 5: canonical identifiers for every blank node */
static ds_status_t rc_issue_all(rc_state_t *s) {
  rc_bhash_t *order;
  rc_comp_t *comp = NULL;
  size_t *stamp = NULL;
  size_t *cof = NULL;
  size_t *clist = NULL;
  size_t clen = 0;
  size_t g;
  size_t pass = 0;
  ds_status_t st = DS_OK;
  order =
      (rc_bhash_t *)ds_context_alloc(s->ctx, (s->nb + 1) * sizeof(rc_bhash_t));
  comp = (rc_comp_t *)ds_context_alloc(s->ctx, (s->nb + 1) * sizeof(rc_comp_t));
  stamp = (size_t *)ds_context_calloc(s->ctx, s->nb + 1, sizeof(size_t));
  cof = (size_t *)ds_context_alloc(s->ctx, (s->nb + 1) * sizeof(size_t));
  clist = (size_t *)ds_context_alloc(s->ctx, (s->nb + 1) * sizeof(size_t));
  if (order == NULL || comp == NULL || stamp == NULL || cof == NULL ||
      clist == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (g = 0; g < s->nb; g++) {
    order[g].h = s->hf + g * s->hsize;
    order[g].hsize = s->hsize;
    order[g].b = g;
  }
  qsort(order, s->nb, sizeof(rc_bhash_t), rc_bhash_cmp);
  /* unique hashes first, in hash order */
  for (g = 0; g < s->nb;) {
    size_t e = g + 1;
    while (e < s->nb && memcmp(order[e].h, order[g].h, s->hsize) == 0) {
      e++;
    }
    if (e == g + 1) {
      rc_issue(s, order[g].b);
    }
    g = e;
  }
  /* the default limit: as many runs per blank node as there are blank
   * nodes sharing first-degree hashes, and at least 64 */
  if (s->max_node == 0) {
    s->max_node = s->nb - s->ncanon > 64 ? s->nb - s->ncanon : 64;
  }
  for (g = 0; g < s->nb && st == DS_OK;) {
    size_t e = g + 1;
    size_t ncomp = 0;
    size_t i;
    while (e < s->nb && memcmp(order[e].h, order[g].h, s->hsize) == 0) {
      e++;
    }
    if (e == g + 1) {
      g = e;
      continue;
    }
    pass++;
    clen = 0;
    for (i = g; i < e && st == DS_OK; i++) {
      size_t n = order[i].b;
      unsigned char h[DS_DIGEST_MAX_SIZE];
      if (s->canon[n] != RC_NONE) {
        continue;
      }
      rc_truncate(s, 0);
      rc_push(s, n);
      st = rc_ndegree(s, n, h);
      if (st != DS_OK) {
        break;
      }
      if (stamp[n] == pass) {
        rc_comp_t *c = &comp[cof[n]];
        if (c->size != s->seqlen) {
          st = DS_ERR_INTERNAL;
        } else if (memcmp(h, c->h, s->hsize) < 0) {
          memcpy(c->h, h, s->hsize);
          c->position = i;
          memcpy(clist + c->off, s->seq, s->seqlen * sizeof(size_t));
        }
      } else {
        rc_comp_t *c = &comp[ncomp];
        size_t j;
        memcpy(c->h, h, s->hsize);
        c->hsize = s->hsize;
        c->position = i;
        c->off = clen;
        c->size = s->seqlen;
        if (clen + s->seqlen > s->nb) {
          st = DS_ERR_INTERNAL;
          break;
        }
        memcpy(clist + clen, s->seq, s->seqlen * sizeof(size_t));
        clen += s->seqlen;
        for (j = 0; j < s->seqlen; j++) {
          stamp[s->seq[j]] = pass;
          cof[s->seq[j]] = ncomp;
        }
        ncomp++;
      }
    }
    if (st == DS_OK) {
      qsort(comp, ncomp, sizeof(rc_comp_t), rc_comp_cmp);
      for (i = 0; i < ncomp; i++) {
        size_t j;
        for (j = 0; j < comp[i].size; j++) {
          rc_issue(s, clist[comp[i].off + j]);
        }
      }
    }
    g = e;
  }
  rc_truncate(s, 0);
done:
  ds_context_free(s->ctx, order);
  ds_context_free(s->ctx, comp);
  ds_context_free(s->ctx, stamp);
  ds_context_free(s->ctx, cof);
  ds_context_free(s->ctx, clist);
  return st;
}

typedef struct rc_qref {
  const ds_rdf_quad_t *q;
} rc_qref_t;

static int rc_qref_cmp(const void *a, const void *b) {
  const rc_qref_t *x = (const rc_qref_t *)a;
  const rc_qref_t *y = (const rc_qref_t *)b;
  int c = rc_quad_cmp(x->q, y->q);
  if (c != 0) {
    return c;
  }
  return (x->q > y->q) - (x->q < y->q);
}

static unsigned long rc_fnv(const char *s, size_t n) {
  unsigned long h = 2166136261UL;
  size_t i;
  for (i = 0; i < n; i++) {
    h = ((h ^ (unsigned char)s[i]) * 16777619UL) & 0xFFFFFFFFUL;
  }
  return h;
}

typedef struct rc_lref {
  const char *p;
  size_t len;
  size_t b;
} rc_lref_t;

static int rc_lref_cmp(const void *a, const void *b) {
  const rc_lref_t *x = (const rc_lref_t *)a;
  const rc_lref_t *y = (const rc_lref_t *)b;
  return rc_cmp_bytes(x->p, x->len, y->p, y->len);
}

/* distinct quads, blank node numbering, component texts, mentions */
static ds_status_t rc_prepare(rc_state_t *s) {
  const ds_rdf_dataset_t *d = s->d;
  rc_qref_t *refs = NULL;
  size_t *keep = NULL;
  size_t *table = NULL;
  rc_lref_t *byl = NULL;
  size_t tcap = 16;
  size_t i;
  size_t k;
  ds_status_t st = DS_OK;
  refs =
      (rc_qref_t *)ds_context_alloc(s->ctx, (d->count + 1) * sizeof(rc_qref_t));
  keep = (size_t *)ds_context_alloc(s->ctx, (d->count + 1) * sizeof(size_t));
  if (refs == NULL || keep == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  /* the first of equal quads stays, in input order */
  for (i = 0; i < d->count; i++) {
    refs[i].q = &d->quads[i];
  }
  qsort(refs, d->count, sizeof(rc_qref_t), rc_qref_cmp);
  for (i = 0; i < d->count; i++) {
    keep[i] = 0;
  }
  for (i = 0; i < d->count; i++) {
    if (i == 0 || rc_quad_cmp(refs[i - 1].q, refs[i].q) != 0) {
      keep[(size_t)(refs[i].q - d->quads)] = 1;
    }
  }
  s->nq = 0;
  for (i = 0; i < d->count; i++) {
    if (keep[i]) {
      keep[s->nq++] = i;
    }
  }
  s->qb = (size_t *)ds_context_alloc(s->ctx, (4 * s->nq + 1) * sizeof(size_t));
  s->qt = (size_t *)ds_context_alloc(s->ctx, (8 * s->nq + 1) * sizeof(size_t));
  while (tcap < 6 * s->nq) {
    tcap *= 2;
  }
  table = (size_t *)ds_context_alloc(s->ctx, tcap * sizeof(size_t));
  s->label =
      (const char **)ds_context_alloc(s->ctx, (3 * s->nq + 1) * sizeof(char *));
  s->label_len =
      (size_t *)ds_context_alloc(s->ctx, (3 * s->nq + 1) * sizeof(size_t));
  if (s->qb == NULL || s->qt == NULL || table == NULL || s->label == NULL ||
      s->label_len == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (i = 0; i < tcap; i++) {
    table[i] = RC_NONE;
  }
  s->nb = 0;
  s->text.len = 0;
  st = rc_reserve(&s->text, 0, s->ctx);
  for (i = 0; i < s->nq && st == DS_OK; i++) {
    const ds_rdf_quad_t *q = &d->quads[keep[i]];
    const ds_rdf_term_t *t[4];
    int c;
    t[0] = &q->subject;
    t[1] = &q->predicate;
    t[2] = &q->object;
    t[3] = &q->graph;
    for (c = 0; c < 4 && st == DS_OK; c++) {
      size_t at = 4 * i + (size_t)c;
      s->qb[at] = RC_NONE;
      s->qt[2 * at] = s->text.len;
      if (t[c]->kind == DS_RDF_BLANK) {
        size_t h = (size_t)rc_fnv(t[c]->value, t[c]->length) & (tcap - 1);
        while (table[h] != RC_NONE &&
               (s->label_len[table[h]] != t[c]->length ||
                memcmp(s->label[table[h]], t[c]->value, t[c]->length) != 0)) {
          h = (h + 1) & (tcap - 1);
        }
        if (table[h] == RC_NONE) {
          table[h] = s->nb;
          s->label[s->nb] = t[c]->value;
          s->label_len[s->nb] = t[c]->length;
          s->nb++;
        }
        s->qb[at] = table[h];
      } else {
        st = rc_put_term(&s->text, t[c], s->ctx);
      }
      s->qt[2 * at + 1] = s->text.len - s->qt[2 * at];
    }
  }
  if (st != DS_OK) {
    goto done;
  }
  /* labels in order; mentions */
  s->rank = (size_t *)ds_context_alloc(s->ctx, (s->nb + 1) * sizeof(size_t));
  s->by_rank = (size_t *)ds_context_alloc(s->ctx, (s->nb + 1) * sizeof(size_t));
  s->mstart = (size_t *)ds_context_calloc(s->ctx, s->nb + 2, sizeof(size_t));
  byl = (rc_lref_t *)ds_context_alloc(s->ctx, (s->nb + 1) * sizeof(rc_lref_t));
  if (s->rank == NULL || s->by_rank == NULL || s->mstart == NULL ||
      byl == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (i = 0; i < s->nb; i++) {
    byl[i].p = s->label[i];
    byl[i].len = s->label_len[i];
    byl[i].b = i;
  }
  qsort(byl, s->nb, sizeof(rc_lref_t), rc_lref_cmp);
  for (i = 0; i < s->nb; i++) {
    s->rank[byl[i].b] = i;
    s->by_rank[i] = byl[i].b;
  }
  for (k = 0; k < 2; k++) {
    if (k == 1) {
      for (i = 0; i < s->nb; i++) {
        s->mstart[i + 1] += s->mstart[i];
      }
      s->mq = (size_t *)ds_context_alloc(s->ctx, (s->mstart[s->nb] + 1) *
                                                     sizeof(size_t));
      if (s->mq == NULL) {
        st = DS_ERR_ALLOC;
        goto done;
      }
      for (i = s->nb; i > 0; i--) {
        s->mstart[i] = s->mstart[i - 1];
      }
      s->mstart[0] = 0;
    }
    for (i = 0; i < s->nq; i++) {
      int c;
      for (c = 0; c < 4; c++) {
        size_t bl = s->qb[4 * i + (size_t)c];
        int seen = 0;
        int e;
        for (e = 0; e < c; e++) {
          seen |= s->qb[4 * i + (size_t)e] == bl;
        }
        if (bl == RC_NONE || seen) {
          continue;
        }
        if (k == 0) {
          s->mstart[bl + 1]++;
        } else {
          s->mq[s->mstart[bl + 1]++] = i;
        }
      }
    }
  }
done:
  ds_context_free(s->ctx, refs);
  ds_context_free(s->ctx, keep);
  ds_context_free(s->ctx, table);
  ds_context_free(s->ctx, byl);
  return st;
}

static void rc_state_free(rc_state_t *s) {
  size_t i;
  for (i = 0; i < s->nframes; i++) {
    rc_frame_t *f = &s->frames[i];
    ds_context_free(s->ctx, f->rel);
    ds_context_free(s->ctx, f->perm);
    ds_context_free(s->ctx, f->rl);
    ds_context_free(s->ctx, f->cseq);
    rc_buf_free(&f->data, s->ctx);
    rc_buf_free(&f->path, s->ctx);
    rc_buf_free(&f->chosen, s->ctx);
  }
  ds_context_free(s->ctx, s->frames);
  ds_context_free(s->ctx, s->hs);
  ds_context_free(s->ctx, s->qb);
  ds_context_free(s->ctx, s->qt);
  rc_buf_free(&s->text, s->ctx);
  ds_context_free(s->ctx, (void *)s->label);
  ds_context_free(s->ctx, s->label_len);
  ds_context_free(s->ctx, s->rank);
  ds_context_free(s->ctx, s->by_rank);
  ds_context_free(s->ctx, s->mstart);
  ds_context_free(s->ctx, s->mq);
  ds_context_free(s->ctx, s->hf);
  ds_context_free(s->ctx, s->canon);
  ds_context_free(s->ctx, s->canon_order);
  ds_context_free(s->ctx, s->seq);
  ds_context_free(s->ctx, s->pos);
  ds_context_free(s->ctx, s->calls);
  rc_buf_free(&s->line, s->ctx);
}

void ds_rdfc_options_init(ds_rdfc_options_t *options) {
  if (options == NULL) {
    return;
  }
  options->digest = NULL;
  options->max_node_calls = 0;
  options->max_calls = 0;
}

static void rc_result_clear(ds_rdfc_result_t *r, ds_context_t *ctx) {
  r->nquads = NULL;
  r->length = 0;
  r->quads = 0;
  r->labels = NULL;
  r->label_count = 0;
  r->by_input = NULL;
  r->calls = 0;
  r->max_node_calls = 0;
  r->strings = NULL;
  r->context = ctx;
}

/* the result: serialized canonical form and the identifiers map */
static ds_status_t rc_finish(rc_state_t *s, ds_rdfc_result_t *r) {
  rc_line_t *lines = NULL;
  size_t *all = NULL;
  rc_buf_t out;
  size_t i;
  size_t bytes = 0;
  ds_status_t st = DS_OK;
  memset(&out, 0, sizeof(out));
  lines =
      (rc_line_t *)ds_context_alloc(s->ctx, (s->nq + 1) * sizeof(rc_line_t));
  all = (size_t *)ds_context_alloc(
      s->ctx, ((s->nq > s->nb ? s->nq : s->nb) + 1) * sizeof(size_t));
  if (lines == NULL || all == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (i = 0; i < s->nq; i++) {
    all[i] = i;
  }
  st = rc_sorted_lines(s, all, s->nq, RC_NONE, lines);
  if (st == DS_OK) {
    st = rc_reserve(&out, s->line.len, s->ctx);
  }
  for (i = 0; i < s->nq && st == DS_OK; i++) {
    st = rc_put(&out, lines[i].p, lines[i].len, s->ctx);
  }
  if (st == DS_OK && out.p == NULL) {
    st = rc_reserve(&out, 0, s->ctx);
    if (st == DS_OK) {
      out.p[0] = '\0';
    }
  }
  if (st != DS_OK) {
    goto done;
  }
  /* labels: "input\0c14nK\0" each */
  for (i = 0; i < s->nb; i++) {
    bytes += s->label_len[i] + 1 + 4 + 3 * sizeof(size_t) + 1;
  }
  r->strings = (char *)ds_context_alloc(s->ctx, bytes + 1);
  r->labels = (ds_rdfc_label_t *)ds_context_alloc(
      s->ctx, (s->nb + 1) * sizeof(ds_rdfc_label_t));
  r->by_input =
      (size_t *)ds_context_alloc(s->ctx, (s->nb + 1) * sizeof(size_t));
  if (r->strings == NULL || r->labels == NULL || r->by_input == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  {
    char *p = r->strings;
    for (i = 0; i < s->ncanon; i++) {
      size_t b = s->canon_order[i];
      memcpy(p, s->label[b], s->label_len[b]);
      p[s->label_len[b]] = '\0';
      r->labels[i].input = p;
      p += s->label_len[b] + 1;
      r->labels[i].canonical = p;
      p += rc_format_id(p, "c14n", i) + 1;
      all[s->rank[b]] = i; /* labels in input label order */
    }
    for (i = 0; i < s->ncanon; i++) {
      r->by_input[i] = all[i];
    }
  }
done:
  ds_context_free(s->ctx, lines);
  ds_context_free(s->ctx, all);
  if (st != DS_OK) {
    rc_buf_free(&out, s->ctx);
    ds_context_free(s->ctx, r->strings);
    ds_context_free(s->ctx, r->labels);
    ds_context_free(s->ctx, r->by_input);
    rc_result_clear(r, s->ctx);
    return st;
  }
  r->nquads = out.p;
  r->length = out.len;
  r->quads = s->nq;
  r->label_count = s->ncanon;
  r->calls = s->total;
  r->max_node_calls = s->most;
  return DS_OK;
}

ds_status_t ds_rdfc_canonicalize(const ds_rdf_dataset_t *dataset,
                                 const ds_rdfc_options_t *options,
                                 ds_rdfc_result_t *result) {
  rc_state_t s;
  ds_rdfc_options_t defaults;
  ds_status_t st;
  size_t i;
  if (dataset == NULL || result == NULL) {
    return DS_ERR_NULL;
  }
  rc_result_clear(result, dataset->context);
  if (options == NULL) {
    ds_rdfc_options_init(&defaults);
    options = &defaults;
  }
  memset(&s, 0, sizeof(s));
  s.d = dataset;
  s.ctx = dataset->context;
  s.dg = options->digest != NULL ? options->digest : ds_digest_sha256();
  s.hsize = s.dg->size;
  if (s.hsize == 0 || s.hsize > DS_DIGEST_MAX_SIZE || s.dg->init == NULL ||
      s.dg->update == NULL || s.dg->final == NULL) {
    return DS_ERR_INVALID;
  }
  s.max_node = options->max_node_calls;
  s.max_total = options->max_calls;
  s.hs = ds_context_alloc(s.ctx, s.dg->state_size + 1);
  if (s.hs == NULL) {
    return DS_ERR_ALLOC;
  }
  st = rc_prepare(&s);
  if (st == DS_OK) {
    s.hf = (unsigned char *)ds_context_alloc(s.ctx, (s.nb + 1) * s.hsize);
    s.canon = (size_t *)ds_context_alloc(s.ctx, (s.nb + 1) * sizeof(size_t));
    s.canon_order =
        (size_t *)ds_context_alloc(s.ctx, (s.nb + 1) * sizeof(size_t));
    s.seq = (size_t *)ds_context_alloc(s.ctx, (s.nb + 1) * sizeof(size_t));
    s.pos = (size_t *)ds_context_calloc(s.ctx, s.nb + 1, sizeof(size_t));
    s.calls = (size_t *)ds_context_calloc(s.ctx, s.nb + 1, sizeof(size_t));
    if (s.hf == NULL || s.canon == NULL || s.canon_order == NULL ||
        s.seq == NULL || s.pos == NULL || s.calls == NULL) {
      st = DS_ERR_ALLOC;
    }
  }
  if (st == DS_OK) {
    for (i = 0; i < s.nb; i++) {
      s.canon[i] = RC_NONE;
    }
    st = rc_first_degree(&s);
  }
  if (st == DS_OK) {
    st = rc_issue_all(&s);
  }
  if (st == DS_OK && s.ncanon != s.nb) {
    st = DS_ERR_INTERNAL;
  }
  if (st == DS_OK) {
    st = rc_finish(&s, result);
  }
  if (st != DS_OK) {
    result->calls = s.total;
    result->max_node_calls = s.most;
  }
  rc_state_free(&s);
  return st;
}

ds_status_t ds_rdfc_canonicalize_nquads(const char *text, size_t length,
                                        const ds_rdfc_options_t *options,
                                        ds_rdfc_result_t *result,
                                        size_t *error_line,
                                        ds_context_t *context) {
  ds_rdf_dataset_t d;
  ds_status_t st;
  if (result == NULL) {
    return DS_ERR_NULL;
  }
  rc_result_clear(result, context);
  ds_rdf_dataset_init(&d, context);
  st = ds_rdf_parse_nquads(&d, text, length, error_line);
  if (st == DS_OK) {
    st = ds_rdfc_canonicalize(&d, options, result);
  }
  ds_rdf_dataset_destroy(&d);
  return st;
}

const char *ds_rdfc_label_of(const ds_rdfc_result_t *result,
                             const char *input) {
  size_t lo = 0;
  size_t hi;
  if (result == NULL || input == NULL || result->by_input == NULL) {
    return NULL;
  }
  hi = result->label_count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    const ds_rdfc_label_t *l = &result->labels[result->by_input[mid]];
    int c = strcmp(l->input, input);
    if (c == 0) {
      return l->canonical;
    }
    if (c < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return NULL;
}

void ds_rdfc_result_destroy(ds_rdfc_result_t *result) {
  if (result == NULL) {
    return;
  }
  ds_context_free(result->context, result->nquads);
  ds_context_free(result->context, result->labels);
  ds_context_free(result->context, result->by_input);
  ds_context_free(result->context, result->strings);
  rc_result_clear(result, result->context);
}
