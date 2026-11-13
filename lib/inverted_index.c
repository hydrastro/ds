#include "inverted_index.h"
#include "map_iface.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define II_NONE DS_INVIDX_NONE

static ds_status_t ii_reserve(ds_context_t *ctx, void **p, size_t *cap,
                              size_t need, size_t elem) {
  size_t nc;
  void *q;
  if (need <= *cap) {
    return DS_OK;
  }
  nc = *cap < 8 ? 16 : *cap;
  while (nc < need) {
    if (nc > (size_t)-1 / 2 / elem) {
      return DS_ERR_OVERFLOW;
    }
    nc *= 2;
  }
  q = ds_context_realloc(ctx, *p, nc * elem);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *p = q;
  *cap = nc;
  return DS_OK;
}

static ds_status_t ii_put_varint(ds_context_t *ctx, unsigned char **buf,
                                 size_t *len, size_t *cap, size_t v) {
  ds_status_t st = ii_reserve(ctx, (void **)buf, cap, *len + 10, 1);
  if (st != DS_OK) {
    return st;
  }
  while (v >= 0x80) {
    (*buf)[(*len)++] = (unsigned char)(v | 0x80);
    v >>= 7;
  }
  (*buf)[(*len)++] = (unsigned char)v;
  return DS_OK;
}

static size_t ii_get_varint(const unsigned char *buf, size_t *at) {
  size_t v = 0;
  unsigned shift = 0;
  unsigned char c;
  do {
    c = buf[(*at)++];
    v |= (size_t)(c & 0x7F) << shift;
    shift += 7;
  } while ((c & 0x80) != 0);
  return v;
}

/* ---- posting codecs over bit streams ---- */

/* n bits of v (n < II_W) at bit *pos, least significant first */
static void ii_put_bits(unsigned char *buf, size_t *pos, size_t v, unsigned n) {
  unsigned done = 0;
  while (done < n) {
    unsigned off = (unsigned)(*pos & 7u);
    unsigned take = 8 - off < n - done ? 8 - off : n - done;
    size_t chunk = (v >> done) & (((size_t)1 << take) - 1);
    buf[*pos >> 3] = (unsigned char)(buf[*pos >> 3] | (chunk << off));
    *pos += take;
    done += take;
  }
}

static size_t ii_get_bits(const unsigned char *buf, size_t *pos, unsigned n) {
  size_t v = 0;
  unsigned done = 0;
  while (done < n) {
    unsigned off = (unsigned)(*pos & 7u);
    unsigned take = 8 - off < n - done ? 8 - off : n - done;
    size_t chunk = ((size_t)buf[*pos >> 3] >> off) & (((size_t)1 << take) - 1);
    v |= chunk << done;
    *pos += take;
    done += take;
  }
  return v;
}

/* q zero bits then a one */
static void ii_put_unary(unsigned char *buf, size_t *pos, size_t q) {
  *pos += q; /* the buffer is zero there already */
  ii_put_bits(buf, pos, 1, 1);
}

static size_t ii_get_unary(const unsigned char *buf, size_t *pos) {
  size_t zeros = 0;
  for (;;) {
    unsigned off = (unsigned)(*pos & 7u);
    unsigned b = (unsigned)buf[*pos >> 3] >> off;
    if (b != 0) {
      unsigned t = 0;
      while ((b & 1u) == 0) {
        b >>= 1;
        t++;
      }
      *pos += t + 1;
      return zeros + t;
    }
    zeros += 8 - off;
    *pos += 8 - off;
  }
}

static unsigned ii_bitlen(size_t v) {
  unsigned n = 0;
  while (v != 0) {
    v >>= 1;
    n++;
  }
  return n;
}

static size_t ii_vb_bits(size_t v, unsigned param) {
  size_t n = 8;
  (void)param;
  while (v >= 0x80) {
    v >>= 7;
    n += 8;
  }
  return n;
}

static void ii_vb_put(unsigned char *buf, size_t *pos, size_t v,
                      unsigned param) {
  (void)param;
  while (v >= 0x80) {
    ii_put_bits(buf, pos, (v & 0x7Fu) | 0x80u, 8);
    v >>= 7;
  }
  ii_put_bits(buf, pos, v, 8);
}

static size_t ii_vb_get(const unsigned char *buf, size_t *pos, unsigned param) {
  size_t v = 0;
  unsigned shift = 0;
  size_t c;
  (void)param;
  do {
    c = ii_get_bits(buf, pos, 8);
    v |= (c & 0x7Fu) << shift;
    shift += 7;
  } while ((c & 0x80u) != 0);
  return v;
}

/* gamma of N = v + 1 >= 1: L - 1 zeros, a one, then N's low L - 1 bits */
static size_t ii_gamma_bits(size_t v, unsigned param) {
  (void)param;
  return 2 * (size_t)ii_bitlen(v + 1) - 1;
}

static void ii_gamma_put_n(unsigned char *buf, size_t *pos, size_t n) {
  unsigned l = ii_bitlen(n);
  ii_put_unary(buf, pos, l - 1);
  ii_put_bits(buf, pos, n, l - 1);
}

static size_t ii_gamma_get_n(const unsigned char *buf, size_t *pos) {
  size_t l = ii_get_unary(buf, pos);
  return ((size_t)1 << l) | ii_get_bits(buf, pos, (unsigned)l);
}

static void ii_gamma_put(unsigned char *buf, size_t *pos, size_t v,
                         unsigned param) {
  (void)param;
  ii_gamma_put_n(buf, pos, v + 1);
}

static size_t ii_gamma_get(const unsigned char *buf, size_t *pos,
                           unsigned param) {
  (void)param;
  return ii_gamma_get_n(buf, pos) - 1;
}

/* delta of N = v + 1: gamma of its length L, then its low L - 1 bits */
static size_t ii_delta_bits(size_t v, unsigned param) {
  unsigned l = ii_bitlen(v + 1);
  (void)param;
  return 2 * (size_t)ii_bitlen(l) - 1 + l - 1;
}

static void ii_delta_put(unsigned char *buf, size_t *pos, size_t v,
                         unsigned param) {
  size_t n = v + 1;
  unsigned l = ii_bitlen(n);
  (void)param;
  ii_gamma_put_n(buf, pos, l);
  ii_put_bits(buf, pos, n, l - 1);
}

static size_t ii_delta_get(const unsigned char *buf, size_t *pos,
                           unsigned param) {
  size_t l = ii_gamma_get_n(buf, pos);
  (void)param;
  return (((size_t)1 << (l - 1)) | ii_get_bits(buf, pos, (unsigned)(l - 1))) -
         1;
}

/* Rice: v >> k in unary, then the low k bits */
static size_t ii_rice_bits(size_t v, unsigned k) { return (v >> k) + 1 + k; }

static void ii_rice_put(unsigned char *buf, size_t *pos, size_t v, unsigned k) {
  ii_put_unary(buf, pos, v >> k);
  ii_put_bits(buf, pos, v, k);
}

static size_t ii_rice_get(const unsigned char *buf, size_t *pos, unsigned k) {
  size_t q = ii_get_unary(buf, pos);
  return (q << k) | ii_get_bits(buf, pos, k);
}

static const ds_invidx_codec_t ii_codec_vbyte = {"vbyte", ii_vb_bits, ii_vb_put,
                                                 ii_vb_get, 0};
static const ds_invidx_codec_t ii_codec_gamma = {"gamma", ii_gamma_bits,
                                                 ii_gamma_put, ii_gamma_get, 0};
static const ds_invidx_codec_t ii_codec_delta = {"delta", ii_delta_bits,
                                                 ii_delta_put, ii_delta_get, 0};
#define II_RICE(k)                                                             \
  { "rice", ii_rice_bits, ii_rice_put, ii_rice_get, k }
static const ds_invidx_codec_t ii_codec_rice[32] = {
    II_RICE(0),  II_RICE(1),  II_RICE(2),  II_RICE(3),  II_RICE(4),
    II_RICE(5),  II_RICE(6),  II_RICE(7),  II_RICE(8),  II_RICE(9),
    II_RICE(10), II_RICE(11), II_RICE(12), II_RICE(13), II_RICE(14),
    II_RICE(15), II_RICE(16), II_RICE(17), II_RICE(18), II_RICE(19),
    II_RICE(20), II_RICE(21), II_RICE(22), II_RICE(23), II_RICE(24),
    II_RICE(25), II_RICE(26), II_RICE(27), II_RICE(28), II_RICE(29),
    II_RICE(30), II_RICE(31)};
#undef II_RICE

const ds_invidx_codec_t *ds_invidx_codec_vbyte(void) { return &ii_codec_vbyte; }
const ds_invidx_codec_t *ds_invidx_codec_gamma(void) { return &ii_codec_gamma; }
const ds_invidx_codec_t *ds_invidx_codec_delta(void) { return &ii_codec_delta; }
const ds_invidx_codec_t *ds_invidx_codec_rice(unsigned k) {
  return k < 32 ? &ii_codec_rice[k] : NULL;
}

/* append v to a posting list through the index's codec */
static ds_status_t ii_post(const ds_inverted_index_t *idx,
                           ds_invidx_posting_list_t *pl, size_t v) {
  size_t need = idx->codec->bits(v, idx->codec->param);
  size_t bytes;
  size_t old = pl->capacity;
  ds_status_t st;
  if (need > (size_t)-1 - pl->bits - 15) {
    return DS_ERR_OVERFLOW;
  }
  bytes = (pl->bits + need + 7) / 8 + 1;
  st = ii_reserve(idx->context, (void **)&pl->bytes, &pl->capacity, bytes, 1);
  if (st != DS_OK) {
    return st;
  }
  if (pl->capacity > old) {
    memset(pl->bytes + old, 0, pl->capacity - old); /* writers OR bits in */
  }
  idx->codec->put(pl->bytes, &pl->bits, v, idx->codec->param);
  pl->length = (pl->bits + 7) / 8;
  return DS_OK;
}

static size_t ii_unpost(const ds_inverted_index_t *idx,
                        const ds_invidx_posting_list_t *pl, size_t *at) {
  return idx->codec->get(pl->bytes, at, idx->codec->param);
}

/* before a list's next posting: record a skip point every interval */
static ds_status_t ii_skip_point(const ds_inverted_index_t *idx,
                                 ds_invidx_posting_list_t *pl) {
  ds_status_t st;
  if (pl->df == 0 || pl->df % idx->skip_interval != 0) {
    return DS_OK;
  }
  st = ii_reserve(idx->context, (void **)&pl->skips, &pl->skip_capacity,
                  pl->skip_count + 1, sizeof(ds_invidx_skip_t));
  if (st != DS_OK) {
    return st;
  }
  pl->skips[pl->skip_count].doc = pl->last;
  pl->skips[pl->skip_count].bit = pl->bits;
  pl->skip_count++;
  return DS_OK;
}

static size_t ii_hash(const unsigned char *s, size_t n) {
  unsigned long h = 2166136261ul;
  size_t i;
  for (i = 0; i < n; i++) {
    h = (h ^ s[i]) * 16777619ul;
  }
  h ^= h >> 16;
  return (size_t)h;
}

/* byte order, a prefix before its extensions */
static int ii_text_cmp(const unsigned char *a, size_t na,
                       const unsigned char *b, size_t nb) {
  size_t n = na < nb ? na : nb;
  int c = n > 0 ? memcmp(a, b, n) : 0;
  if (c != 0) {
    return c;
  }
  return na < nb ? -1 : na > nb ? 1 : 0;
}

static bool ii_text_eq(const unsigned char *a, size_t na,
                       const unsigned char *b, size_t nb) {
  return na == nb && (na == 0 || memcmp(a, b, na) == 0);
}

/* ---- dictionary: linear probing ---- */

typedef struct ii_entry {
  const unsigned char *text;
  size_t length;
  size_t term; /* II_NONE: empty slot */
} ii_entry_t;

typedef struct ii_open {
  ii_entry_t *slot;
  size_t slots; /* 0 or a power of two */
  size_t count;
  ds_context_t *context;
} ii_open_t;

static ds_status_t ii_open_init(void *d, const void *param,
                                ds_context_t *context) {
  ii_open_t *o = (ii_open_t *)d;
  (void)param;
  memset(o, 0, sizeof(*o));
  o->context = context;
  return DS_OK;
}

static void ii_open_destroy(void *d) {
  ii_open_t *o = (ii_open_t *)d;
  ds_context_free(o->context, o->slot);
  o->slot = NULL;
}

static size_t ii_open_find(const void *d, const void *text, size_t length) {
  const ii_open_t *o = (const ii_open_t *)d;
  const unsigned char *t = (const unsigned char *)text;
  size_t at;
  if (o->slots == 0) {
    return II_NONE;
  }
  at = ii_hash(t, length) & (o->slots - 1);
  while (o->slot[at].term != II_NONE) {
    if (ii_text_eq(o->slot[at].text, o->slot[at].length, t, length)) {
      return o->slot[at].term;
    }
    at = (at + 1) & (o->slots - 1);
  }
  return II_NONE;
}

static void ii_open_place(ii_entry_t *slot, size_t slots, const ii_entry_t *e) {
  size_t at = ii_hash(e->text, e->length) & (slots - 1);
  while (slot[at].term != II_NONE) {
    at = (at + 1) & (slots - 1);
  }
  slot[at] = *e;
}

static ds_status_t ii_open_insert(void *d, const void *text, size_t length,
                                  size_t term) {
  ii_open_t *o = (ii_open_t *)d;
  ii_entry_t e;
  if (2 * (o->count + 1) > o->slots) {
    size_t slots = o->slots < 16 ? 32 : o->slots * 2;
    ii_entry_t *s;
    size_t i;
    if (o->slots > (size_t)-1 / 4 / sizeof(ii_entry_t)) {
      return DS_ERR_OVERFLOW;
    }
    s = (ii_entry_t *)ds_context_alloc(o->context, slots * sizeof(ii_entry_t));
    if (s == NULL) {
      return DS_ERR_ALLOC;
    }
    for (i = 0; i < slots; i++) {
      s[i].term = II_NONE;
    }
    for (i = 0; i < o->slots; i++) {
      if (o->slot[i].term != II_NONE) {
        ii_open_place(s, slots, &o->slot[i]);
      }
    }
    ds_context_free(o->context, o->slot);
    o->slot = s;
    o->slots = slots;
  }
  e.text = (const unsigned char *)text;
  e.length = length;
  e.term = term;
  ii_open_place(o->slot, o->slots, &e);
  o->count++;
  return DS_OK;
}

/* backward-shift deletion: no tombstones, probes stay short */
static void ii_open_remove(void *d, const void *text, size_t length,
                           size_t term) {
  ii_open_t *o = (ii_open_t *)d;
  size_t mask;
  size_t i;
  size_t j;
  if (o->slots == 0) {
    return;
  }
  mask = o->slots - 1;
  i = ii_hash((const unsigned char *)text, length) & mask;
  while (o->slot[i].term != term) {
    if (o->slot[i].term == II_NONE) {
      return;
    }
    i = (i + 1) & mask;
  }
  j = i;
  for (;;) {
    size_t home;
    j = (j + 1) & mask;
    if (o->slot[j].term == II_NONE) {
      break;
    }
    home = ii_hash(o->slot[j].text, o->slot[j].length) & mask;
    /* move j back to i unless its home lies cyclically in (i, j] */
    if (i <= j ? (home <= i || home > j) : (home <= i && home > j)) {
      o->slot[i] = o->slot[j];
      i = j;
    }
  }
  o->slot[i].term = II_NONE;
  o->count--;
}

static const ds_invidx_dict_ops_t ii_dict_open = {"open addressing",
                                                  0u,
                                                  sizeof(ii_open_t),
                                                  ii_open_init,
                                                  ii_open_destroy,
                                                  ii_open_find,
                                                  ii_open_insert,
                                                  ii_open_remove,
                                                  NULL,
                                                  NULL};

const ds_invidx_dict_ops_t *ds_invidx_dict_open(void) { return &ii_dict_open; }

/* ---- dictionary: the map capability ---- */

typedef struct ii_key {
  const unsigned char *text;
  size_t length;
} ii_key_t;

typedef struct ii_mapdict {
  ds_map_t map;
} ii_mapdict_t;

static unsigned long ii_key_hash(const void *key, void *user) {
  const ii_key_t *k = (const ii_key_t *)key;
  (void)user;
  return (unsigned long)ii_hash(k->text, k->length);
}

static bool ii_key_equal(const void *a, const void *b, void *user) {
  const ii_key_t *x = (const ii_key_t *)a;
  const ii_key_t *y = (const ii_key_t *)b;
  (void)user;
  return ii_text_eq(x->text, x->length, y->text, y->length);
}

static ds_status_t ii_map_init(void *d, const void *param,
                               ds_context_t *context) {
  ii_mapdict_t *m = (ii_mapdict_t *)d;
  return ds_map_init(&m->map, (const ds_map_ops_t *)param, sizeof(ii_key_t),
                     sizeof(size_t), ii_key_hash, ii_key_equal, NULL, context);
}

static void ii_map_destroy(void *d) {
  ds_map_destroy(&((ii_mapdict_t *)d)->map);
}

static size_t ii_map_find(const void *d, const void *text, size_t length) {
  const ii_mapdict_t *m = (const ii_mapdict_t *)d;
  ii_key_t k;
  const void *v;
  size_t term;
  k.text = (const unsigned char *)text;
  k.length = length;
  v = ds_map_get(&m->map, &k);
  if (v == NULL) {
    return II_NONE;
  }
  memcpy(&term, v, sizeof(term)); /* values need not be aligned */
  return term;
}

static ds_status_t ii_map_insert(void *d, const void *text, size_t length,
                                 size_t term) {
  ii_mapdict_t *m = (ii_mapdict_t *)d;
  ii_key_t k;
  ds_status_t st;
  k.text = (const unsigned char *)text;
  k.length = length;
  st = ds_map_put(&m->map, &k, &term);
  return st == DS_EXISTS ? DS_ERR_STATE : st;
}

static void ii_map_remove(void *d, const void *text, size_t length,
                          size_t term) {
  ii_mapdict_t *m = (ii_mapdict_t *)d;
  ii_key_t k;
  (void)term;
  k.text = (const unsigned char *)text;
  k.length = length;
  (void)ds_map_remove(&m->map, &k, NULL);
}

static const ds_invidx_dict_ops_t ii_dict_map = {"map capability",
                                                 0u,
                                                 sizeof(ii_mapdict_t),
                                                 ii_map_init,
                                                 ii_map_destroy,
                                                 ii_map_find,
                                                 ii_map_insert,
                                                 ii_map_remove,
                                                 NULL,
                                                 NULL};

const ds_invidx_dict_ops_t *ds_invidx_dict_map(void) { return &ii_dict_map; }

/* ---- dictionary: sorted, a large and a small sorted array ---- */

typedef struct ii_sorted {
  ii_entry_t *main;
  size_t main_n;
  size_t main_cap;
  ii_entry_t *side; /* at most about sqrt(main_n) entries */
  size_t side_n;
  size_t side_cap;
  ds_context_t *context;
} ii_sorted_t;

static ds_status_t ii_sorted_init(void *d, const void *param,
                                  ds_context_t *context) {
  ii_sorted_t *o = (ii_sorted_t *)d;
  (void)param;
  memset(o, 0, sizeof(*o));
  o->context = context;
  return DS_OK;
}

static void ii_sorted_destroy(void *d) {
  ii_sorted_t *o = (ii_sorted_t *)d;
  ds_context_free(o->context, o->main);
  ds_context_free(o->context, o->side);
  o->main = NULL;
  o->side = NULL;
}

/* the first entry >= text */
static size_t ii_lower(const ii_entry_t *e, size_t n, const unsigned char *t,
                       size_t length) {
  size_t lo = 0;
  size_t hi = n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (ii_text_cmp(e[mid].text, e[mid].length, t, length) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

static size_t ii_sorted_find(const void *d, const void *text, size_t length) {
  const ii_sorted_t *o = (const ii_sorted_t *)d;
  const unsigned char *t = (const unsigned char *)text;
  size_t i = ii_lower(o->main, o->main_n, t, length);
  if (i < o->main_n &&
      ii_text_eq(o->main[i].text, o->main[i].length, t, length)) {
    return o->main[i].term;
  }
  i = ii_lower(o->side, o->side_n, t, length);
  if (i < o->side_n &&
      ii_text_eq(o->side[i].text, o->side[i].length, t, length)) {
    return o->side[i].term;
  }
  return II_NONE;
}

/* merge the small array into the large one, from the back */
static void ii_sorted_merge(ii_sorted_t *o) {
  size_t i = o->main_n;
  size_t j = o->side_n;
  size_t k = o->main_n + o->side_n;
  while (j > 0) {
    if (i > 0 && ii_text_cmp(o->main[i - 1].text, o->main[i - 1].length,
                             o->side[j - 1].text, o->side[j - 1].length) > 0) {
      o->main[--k] = o->main[--i];
    } else {
      o->main[--k] = o->side[--j];
    }
  }
  o->main_n += o->side_n;
  o->side_n = 0;
}

static ds_status_t ii_sorted_insert(void *d, const void *text, size_t length,
                                    size_t term) {
  ii_sorted_t *o = (ii_sorted_t *)d;
  const unsigned char *t = (const unsigned char *)text;
  size_t limit = 32;
  size_t i;
  ds_status_t st = ii_reserve(o->context, (void **)&o->side, &o->side_cap,
                              o->side_n + 1, sizeof(ii_entry_t));
  if (st != DS_OK) {
    return st;
  }
  i = ii_lower(o->side, o->side_n, t, length);
  memmove(o->side + i + 1, o->side + i, (o->side_n - i) * sizeof(ii_entry_t));
  o->side[i].text = t;
  o->side[i].length = length;
  o->side[i].term = term;
  o->side_n++;
  while (limit < o->main_n / limit) {
    limit *= 2;
  }
  /* a failed merge leaves the term in the small array, still found */
  if (o->side_n > limit &&
      ii_reserve(o->context, (void **)&o->main, &o->main_cap,
                 o->main_n + o->side_n, sizeof(ii_entry_t)) == DS_OK) {
    ii_sorted_merge(o);
  }
  return DS_OK;
}

static void ii_sorted_remove(void *d, const void *text, size_t length,
                             size_t term) {
  ii_sorted_t *o = (ii_sorted_t *)d;
  const unsigned char *t = (const unsigned char *)text;
  size_t i = ii_lower(o->side, o->side_n, t, length);
  if (i < o->side_n && o->side[i].term == term) {
    memmove(o->side + i, o->side + i + 1,
            (o->side_n - i - 1) * sizeof(ii_entry_t));
    o->side_n--;
    return;
  }
  i = ii_lower(o->main, o->main_n, t, length);
  if (i < o->main_n && o->main[i].term == term) {
    memmove(o->main + i, o->main + i + 1,
            (o->main_n - i - 1) * sizeof(ii_entry_t));
    o->main_n--;
  }
}

static void ii_sorted_seek(const void *d, const void *text, size_t length,
                           ds_invidx_dict_cursor_t *c) {
  const ii_sorted_t *o = (const ii_sorted_t *)d;
  const unsigned char *t = (const unsigned char *)text;
  memset(c, 0, sizeof(*c));
  c->state[0] = ii_lower(o->main, o->main_n, t, length);
  c->state[1] = ii_lower(o->side, o->side_n, t, length);
}

static size_t ii_sorted_next(const void *d, ds_invidx_dict_cursor_t *c) {
  const ii_sorted_t *o = (const ii_sorted_t *)d;
  size_t i = c->state[0];
  size_t j = c->state[1];
  if (i < o->main_n &&
      (j >= o->side_n || ii_text_cmp(o->main[i].text, o->main[i].length,
                                     o->side[j].text, o->side[j].length) < 0)) {
    c->state[0]++;
    return o->main[i].term;
  }
  if (j < o->side_n) {
    c->state[1]++;
    return o->side[j].term;
  }
  return II_NONE;
}

static const ds_invidx_dict_ops_t ii_dict_sorted = {
    "sorted",         DS_INVIDX_DICT_ORDERED, sizeof(ii_sorted_t),
    ii_sorted_init,   ii_sorted_destroy,      ii_sorted_find,
    ii_sorted_insert, ii_sorted_remove,       ii_sorted_seek,
    ii_sorted_next};

const ds_invidx_dict_ops_t *ds_invidx_dict_sorted(void) {
  return &ii_dict_sorted;
}

/* ---- term bytes, in chunks that never move ---- */

static const unsigned char ii_empty[1] = {0};

typedef struct ii_chunk_mark {
  size_t count;
  size_t used;
  size_t size;
} ii_chunk_mark_t;

static void ii_chunk_mark(const ds_inverted_index_t *idx, ii_chunk_mark_t *m) {
  m->count = idx->chunk_count;
  m->used = idx->chunk_used;
  m->size = idx->chunk_size;
}

/* give back the chunks and bytes taken since the mark */
static void ii_chunk_rewind(ds_inverted_index_t *idx,
                            const ii_chunk_mark_t *m) {
  while (idx->chunk_count > m->count) {
    ds_context_free(idx->context, idx->chunks[--idx->chunk_count]);
  }
  idx->chunk_used = m->used;
  idx->chunk_size = m->size;
}

static ds_status_t ii_store_text(ds_inverted_index_t *idx, const void *text,
                                 size_t length, const unsigned char **out) {
  unsigned char *p;
  if (length == 0) {
    *out = ii_empty;
    return DS_OK;
  }
  if (idx->chunk_count == 0 || idx->chunk_size - idx->chunk_used < length) {
    size_t size = idx->chunk_size < 256     ? 256
                  : idx->chunk_size > 32768 ? 65536
                                            : 2 * idx->chunk_size;
    unsigned char *chunk;
    ds_status_t st;
    if (size < length) {
      size = length;
    }
    st = ii_reserve(idx->context, (void **)&idx->chunks, &idx->chunk_capacity,
                    idx->chunk_count + 1, sizeof(unsigned char *));
    if (st != DS_OK) {
      return st;
    }
    chunk = (unsigned char *)ds_context_alloc(idx->context, size);
    if (chunk == NULL) {
      return DS_ERR_ALLOC;
    }
    idx->chunks[idx->chunk_count++] = chunk;
    idx->chunk_size = size;
    idx->chunk_used = 0;
  }
  p = idx->chunks[idx->chunk_count - 1] + idx->chunk_used;
  memcpy(p, text, length);
  idx->chunk_used += length;
  *out = p;
  return DS_OK;
}

ds_status_t ds_invidx_init(ds_inverted_index_t *idx, ds_invidx_mode_t mode,
                           size_t block_size, bool keep_forward,
                           ds_context_t *context) {
  ds_invidx_options_t o;
  memset(&o, 0, sizeof(o));
  o.mode = mode;
  o.block_size = block_size;
  o.keep_forward = keep_forward;
  return ds_invidx_init_opts(idx, &o, context);
}

ds_status_t ds_invidx_init_opts(ds_inverted_index_t *idx,
                                const ds_invidx_options_t *options,
                                ds_context_t *context) {
  ds_invidx_options_t def;
  const ds_invidx_dict_ops_t *dict;
  ds_invidx_mode_t mode;
  size_t block_size;
  bool keep_forward;
  ds_status_t st;
  if (idx == NULL) {
    return DS_ERR_NULL;
  }
  if (options == NULL) {
    memset(&def, 0, sizeof(def));
    def.mode = DS_INVIDX_DOCUMENT;
    options = &def;
  }
  mode = options->mode;
  block_size = options->block_size;
  keep_forward = options->keep_forward;
  memset(idx, 0, sizeof(*idx));
  if (options->codec != NULL &&
      (options->codec->bits == NULL || options->codec->put == NULL ||
       options->codec->get == NULL)) {
    return DS_ERR_INVALID;
  }
  dict = options->dictionary != NULL ? options->dictionary : &ii_dict_open;
  if (dict->init == NULL || dict->destroy == NULL || dict->find == NULL ||
      dict->insert == NULL || dict->remove == NULL ||
      ((dict->traits & DS_INVIDX_DICT_ORDERED) != 0 &&
       (dict->seek == NULL || dict->next == NULL))) {
    return DS_ERR_INVALID;
  }
  idx->codec = options->codec != NULL ? options->codec : &ii_codec_vbyte;
  if ((mode != DS_INVIDX_DOCUMENT && mode != DS_INVIDX_POSITIONAL &&
       mode != DS_INVIDX_BLOCK) ||
      (mode == DS_INVIDX_BLOCK && block_size == 0)) {
    return DS_ERR_INVALID;
  }
  idx->dict = ds_context_alloc(
      context, dict->instance_size > 0 ? dict->instance_size : 1);
  if (idx->dict == NULL) {
    return DS_ERR_ALLOC;
  }
  st = dict->init(idx->dict, options->dictionary_param, context);
  if (st != DS_OK) {
    ds_context_free(context, idx->dict);
    idx->dict = NULL;
    return st;
  }
  if (options->intersection != DS_INVIDX_INTERSECT_AUTO &&
      options->intersection != DS_INVIDX_INTERSECT_MERGE &&
      options->intersection != DS_INVIDX_INTERSECT_SKIP) {
    dict->destroy(idx->dict);
    ds_context_free(context, idx->dict);
    idx->dict = NULL;
    return DS_ERR_INVALID;
  }
  idx->skip_interval = options->skip_interval != 0 ? options->skip_interval
                                                   : DS_INVIDX_SKIP_DEFAULT;
  idx->intersection = options->intersection;
  idx->dict_ops = dict;
  idx->mode = mode;
  idx->block_size = mode == DS_INVIDX_BLOCK ? block_size : 1;
  idx->keep_forward = keep_forward;
  idx->context = context;
  return DS_OK;
}

void ds_invidx_destroy(ds_inverted_index_t *idx) {
  size_t t;
  if (idx == NULL) {
    return;
  }
  for (t = 0; t < idx->terms; t++) {
    ds_context_free(idx->context, idx->postings[t].bytes);
    ds_context_free(idx->context, idx->postings[t].skips);
  }
  if (idx->dict_ops != NULL) {
    idx->dict_ops->destroy(idx->dict);
    ds_context_free(idx->context, idx->dict);
  }
  for (t = 0; t < idx->chunk_count; t++) {
    ds_context_free(idx->context, idx->chunks[t]);
  }
  ds_context_free(idx->context, idx->chunks);
  ds_context_free(idx->context, (void *)idx->term_text);
  ds_context_free(idx->context, idx->term_len);
  ds_context_free(idx->context, idx->postings);
  ds_context_free(idx->context, idx->doc_id);
  ds_context_free(idx->context, idx->doc_len);
  ds_context_free(idx->context, idx->fwd_off);
  ds_context_free(idx->context, idx->fwd);
  ds_context_free(idx->context, idx->scratch);
  ds_context_free(idx->context, idx->deleted);
  memset(idx, 0, sizeof(*idx));
}

size_t ds_invidx_term_id(const ds_inverted_index_t *idx, const void *text,
                         size_t length) {
  if (idx == NULL || idx->dict_ops == NULL || (length > 0 && text == NULL)) {
    return II_NONE;
  }
  return idx->dict_ops->find(idx->dict, length > 0 ? text : ii_empty, length);
}

/* the term with these bytes, created when absent (its bytes and the
 * dictionary entry are given back by the caller's rollback on failure) */
static ds_status_t ii_intern(ds_inverted_index_t *idx, const void *text,
                             size_t length, size_t *out) {
  size_t t = ds_invidx_term_id(idx, text, length);
  size_t cap;
  const unsigned char *stored;
  ds_status_t st;
  if (t != II_NONE) {
    *out = t;
    return DS_OK;
  }
  if (idx->terms == II_NONE - 1) {
    return DS_ERR_OVERFLOW;
  }
  cap = idx->term_capacity;
  st = ii_reserve(idx->context, (void **)&idx->term_text, &cap, idx->terms + 1,
                  sizeof(const unsigned char *));
  if (st == DS_OK) {
    cap = idx->term_capacity;
    st = ii_reserve(idx->context, (void **)&idx->term_len, &cap, idx->terms + 1,
                    sizeof(size_t));
  }
  if (st == DS_OK) {
    cap = idx->term_capacity;
    st = ii_reserve(idx->context, (void **)&idx->postings, &cap, idx->terms + 1,
                    sizeof(ds_invidx_posting_list_t));
  }
  if (st != DS_OK) {
    return st;
  }
  idx->term_capacity = cap;
  st = ii_store_text(idx, text, length, &stored);
  if (st == DS_OK) {
    st = idx->dict_ops->insert(idx->dict, stored, length, idx->terms);
  }
  if (st != DS_OK) {
    return st;
  }
  t = idx->terms++;
  idx->term_text[t] = stored;
  idx->term_len[t] = length;
  memset(&idx->postings[t], 0, sizeof(ds_invidx_posting_list_t));
  *out = t;
  return DS_OK;
}

typedef struct ii_pair {
  size_t term;
  size_t pos;
} ii_pair_t;

static int ii_pair_cmp(const void *x, const void *y) {
  const ii_pair_t *a = (const ii_pair_t *)x;
  const ii_pair_t *b = (const ii_pair_t *)y;
  if (a->term != b->term) {
    return a->term < b->term ? -1 : 1;
  }
  return a->pos < b->pos ? -1 : a->pos > b->pos ? 1 : 0;
}

ds_status_t ds_invidx_add(ds_inverted_index_t *idx, size_t doc_id,
                          const ds_invidx_term_t *tokens, size_t count) {
  ii_pair_t *pairs = NULL;
  ds_invidx_posting_list_t *saved = NULL;
  ii_chunk_mark_t mark;
  size_t old_terms;
  size_t old_fwd;
  size_t block;
  size_t i;
  size_t runs = 0;
  size_t cap;
  ds_status_t st = DS_OK;
  if (idx == NULL || (count > 0 && tokens == NULL)) {
    return DS_ERR_NULL;
  }
  if (idx->docs > 0 && doc_id <= idx->doc_id[idx->docs - 1]) {
    return DS_ERR_STATE;
  }
  if (doc_id == (size_t)-1) {
    return DS_ERR_RANGE; /* codecs take values below SIZE_MAX */
  }
  if (count > (size_t)-1 / sizeof(ii_pair_t)) {
    return DS_ERR_OVERFLOW;
  }
  cap = idx->doc_capacity;
  st = ii_reserve(idx->context, (void **)&idx->doc_id, &cap, idx->docs + 1,
                  sizeof(size_t));
  if (st == DS_OK) {
    cap = idx->doc_capacity;
    st = ii_reserve(idx->context, (void **)&idx->doc_len, &cap, idx->docs + 1,
                    sizeof(size_t));
  }
  if (st == DS_OK) {
    cap = idx->doc_capacity;
    st = ii_reserve(idx->context, (void **)&idx->fwd_off, &cap, idx->docs + 1,
                    sizeof(size_t));
  }
  if (st != DS_OK) {
    return st;
  }
  idx->doc_capacity = cap;
  old_terms = idx->terms;
  old_fwd = idx->fwd_used;
  ii_chunk_mark(idx, &mark);
  pairs = (ii_pair_t *)ds_context_alloc(idx->context,
                                        (count + 1) * sizeof(ii_pair_t));
  if (pairs == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < count && st == DS_OK; i++) {
    if (tokens[i].length > 0 && tokens[i].text == NULL) {
      st = DS_ERR_NULL;
      break;
    }
    st = ii_intern(idx, tokens[i].text, tokens[i].length, &pairs[i].term);
    pairs[i].pos = i;
  }
  if (st != DS_OK) {
    goto fail;
  }
  qsort(pairs, count, sizeof(ii_pair_t), ii_pair_cmp);
  for (i = 0; i < count; i++) {
    if (i == 0 || pairs[i].term != pairs[i - 1].term) {
      runs++;
    }
  }
  /* save the touched lists so a failure can be rolled back */
  saved = (ds_invidx_posting_list_t *)ds_context_alloc(
      idx->context, (runs + 1) * sizeof(ds_invidx_posting_list_t));
  if (saved == NULL) {
    st = DS_ERR_ALLOC;
    goto fail;
  }
  block = idx->docs / idx->block_size;
  if (idx->keep_forward) {
    st = ii_put_varint(idx->context, &idx->fwd, &idx->fwd_used,
                       &idx->fwd_capacity, runs);
  }
  runs = 0;
  for (i = 0; st == DS_OK && i < count;) {
    size_t t = pairs[i].term;
    size_t j = i;
    ds_invidx_posting_list_t *pl = &idx->postings[t];
    while (j < count && pairs[j].term == t) {
      j++;
    }
    saved[runs++] = *pl;
    if (idx->mode == DS_INVIDX_BLOCK) {
      if (pl->df == 0 || pl->last != block) {
        st = ii_skip_point(idx, pl);
        if (st == DS_OK) {
          st = ii_post(idx, pl, block - (pl->df ? pl->last : 0));
        }
        pl->df++;
        pl->last = block;
      }
    } else {
      st = ii_skip_point(idx, pl);
      if (st == DS_OK) {
        st = ii_post(idx, pl, doc_id - (pl->df ? pl->last : 0));
      }
      if (st == DS_OK) {
        st = ii_post(idx, pl, j - i);
      }
      if (idx->mode == DS_INVIDX_POSITIONAL) {
        size_t k;
        size_t prev = 0;
        for (k = i; st == DS_OK && k < j; k++) {
          st = ii_post(idx, pl, pairs[k].pos - prev);
          prev = pairs[k].pos;
        }
      }
      pl->df++;
      pl->last = doc_id;
    }
    pl->cf += j - i;
    if (st == DS_OK && idx->keep_forward) {
      size_t prev_term = i == 0 ? 0 : pairs[i - 1].term;
      st = ii_put_varint(idx->context, &idx->fwd, &idx->fwd_used,
                         &idx->fwd_capacity, t - prev_term);
      if (st == DS_OK) {
        st = ii_put_varint(idx->context, &idx->fwd, &idx->fwd_used,
                           &idx->fwd_capacity, j - i);
      }
    }
    i = j;
  }
  if (st != DS_OK) {
    goto fail;
  }
  idx->doc_id[idx->docs] = doc_id;
  idx->doc_len[idx->docs] = count;
  idx->fwd_off[idx->docs] = old_fwd;
  idx->docs++;
  idx->total_tokens += count;
  ds_context_free(idx->context, pairs);
  ds_context_free(idx->context, saved);
  return DS_OK;
fail:
  if (saved != NULL) {
    size_t r = 0;
    for (i = 0; i < count && r < runs; i++) {
      if (i == 0 || pairs[i].term != pairs[i - 1].term) {
        ds_invidx_posting_list_t *pl = &idx->postings[pairs[i].term];
        unsigned char *bytes = pl->bytes;
        size_t capacity = pl->capacity;
        ds_invidx_skip_t *skips = pl->skips;
        size_t skip_capacity = pl->skip_capacity;
        *pl = saved[r++];
        pl->bytes = bytes; /* keep the (possibly moved) buffers */
        pl->capacity = capacity;
        pl->skips = skips;
        pl->skip_capacity = skip_capacity;
        /* clear what the failed add wrote, for the next writer */
        if (pl->bytes != NULL) {
          size_t whole = (pl->bits + 7) / 8;
          if (pl->bits % 8 != 0) {
            pl->bytes[pl->bits / 8] &=
                (unsigned char)((1u << (pl->bits % 8)) - 1u);
          }
          memset(pl->bytes + whole, 0, pl->capacity - whole);
        }
      }
    }
  }
  /* forget the new terms, newest first, before their bytes go */
  while (idx->terms > old_terms) {
    size_t t = --idx->terms;
    idx->dict_ops->remove(idx->dict, idx->term_text[t], idx->term_len[t], t);
    ds_context_free(idx->context, idx->postings[t].bytes);
    ds_context_free(idx->context, idx->postings[t].skips);
  }
  ii_chunk_rewind(idx, &mark);
  idx->fwd_used = old_fwd;
  ds_context_free(idx->context, pairs);
  ds_context_free(idx->context, saved);
  return st;
}

static bool ii_alnum(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}

static bool ii_default_tokenizer(const char *text, size_t length,
                                 size_t *cursor, size_t *start,
                                 size_t *token_length, void *user) {
  size_t i = *cursor;
  (void)user;
  while (i < length && !ii_alnum(text[i])) {
    i++;
  }
  if (i == length) {
    *cursor = i;
    return false;
  }
  *start = i;
  while (i < length && ii_alnum(text[i])) {
    i++;
  }
  *token_length = i - *start;
  *cursor = i;
  return true;
}

ds_status_t ds_invidx_add_text(ds_inverted_index_t *idx, size_t doc_id,
                               const char *text, size_t length,
                               ds_invidx_tokenizer_fn tokenizer, void *user,
                               bool fold_case) {
  char *folded = NULL;
  const char *src = text;
  ds_invidx_term_t *tokens = NULL;
  size_t n = 0;
  size_t cap = 0;
  size_t cursor = 0;
  size_t start;
  size_t tlen;
  ds_status_t st = DS_OK;
  if (idx == NULL || (length > 0 && text == NULL)) {
    return DS_ERR_NULL;
  }
  if (tokenizer == NULL) {
    tokenizer = ii_default_tokenizer;
  }
  if (fold_case && length > 0) {
    size_t i;
    folded = (char *)ds_context_alloc(idx->context, length);
    if (folded == NULL) {
      return DS_ERR_ALLOC;
    }
    for (i = 0; i < length; i++) {
      char c = text[i];
      folded[i] = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
    }
    src = folded;
  }
  while (st == DS_OK && tokenizer(text, length, &cursor, &start, &tlen, user)) {
    if (start > length || tlen > length - start) {
      st = DS_ERR_CALLBACK;
      break;
    }
    st = ii_reserve(idx->context, (void **)&tokens, &cap, n + 1,
                    sizeof(ds_invidx_term_t));
    if (st == DS_OK) {
      tokens[n].text = src + start;
      tokens[n].length = tlen;
      n++;
    }
  }
  if (st == DS_OK) {
    st = ds_invidx_add(idx, doc_id, tokens, n);
  }
  ds_context_free(idx->context, tokens);
  ds_context_free(idx->context, folded);
  return st;
}

size_t ds_invidx_terms(const ds_inverted_index_t *idx) {
  return idx == NULL ? 0 : idx->terms;
}

size_t ds_invidx_documents(const ds_inverted_index_t *idx) {
  return idx == NULL ? 0 : idx->docs - idx->deleted_count;
}

ds_status_t ds_invidx_term_text(const ds_inverted_index_t *idx, size_t term,
                                const void **text, size_t *length) {
  if (idx == NULL || text == NULL || length == NULL) {
    return DS_ERR_NULL;
  }
  if (term >= idx->terms) {
    return DS_ERR_RANGE;
  }
  *text = idx->term_text[term];
  *length = idx->term_len[term];
  return DS_OK;
}

ds_status_t ds_invidx_term_seek(const ds_inverted_index_t *idx,
                                const void *text, size_t length,
                                ds_invidx_dict_cursor_t *cursor) {
  if (idx == NULL || cursor == NULL || (length > 0 && text == NULL)) {
    return DS_ERR_NULL;
  }
  if (idx->dict_ops == NULL ||
      (idx->dict_ops->traits & DS_INVIDX_DICT_ORDERED) == 0) {
    return DS_ERR_UNSUPPORTED;
  }
  idx->dict_ops->seek(idx->dict, length > 0 ? text : ii_empty, length, cursor);
  return DS_OK;
}

size_t ds_invidx_term_next(const ds_inverted_index_t *idx,
                           ds_invidx_dict_cursor_t *cursor) {
  if (idx == NULL || cursor == NULL || idx->dict_ops == NULL ||
      (idx->dict_ops->traits & DS_INVIDX_DICT_ORDERED) == 0) {
    return II_NONE;
  }
  return idx->dict_ops->next(idx->dict, cursor);
}

size_t ds_invidx_document_frequency(const ds_inverted_index_t *idx,
                                    const void *text, size_t length) {
  size_t t = ds_invidx_term_id(idx, text, length);
  return t == II_NONE ? 0 : idx->postings[t].df;
}

size_t ds_invidx_postings_bytes(const ds_inverted_index_t *idx) {
  size_t t;
  size_t n = 0;
  if (idx == NULL) {
    return 0;
  }
  for (t = 0; t < idx->terms; t++) {
    n += idx->postings[t].length;
  }
  return n;
}

ds_status_t ds_invidx_block_documents(const ds_inverted_index_t *idx,
                                      size_t block, const size_t **ids,
                                      size_t *count) {
  size_t first;
  if (idx == NULL || ids == NULL || count == NULL) {
    return DS_ERR_NULL;
  }
  if (block > idx->docs / idx->block_size ||
      (first = block * idx->block_size) >= idx->docs) {
    return DS_ERR_RANGE;
  }
  *ids = idx->doc_id + first;
  *count =
      idx->docs - first < idx->block_size ? idx->docs - first : idx->block_size;
  return DS_OK;
}

static size_t ii_doc_index(const ds_inverted_index_t *idx, size_t doc_id) {
  size_t lo = 0;
  size_t hi = idx->docs;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (idx->doc_id[mid] < doc_id) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo < idx->docs && idx->doc_id[lo] == doc_id ? lo : II_NONE;
}

static bool ii_dead(const ds_inverted_index_t *idx, size_t d) {
  return d / 8 < idx->deleted_capacity &&
         ((idx->deleted[d / 8] >> (d % 8)) & 1u) != 0;
}

/* whether a posting's document (block) is gone */
static bool ii_skip_posting(const ds_inverted_index_t *idx, size_t doc) {
  size_t d;
  size_t end;
  if (idx->mode != DS_INVIDX_BLOCK) {
    d = ii_doc_index(idx, doc);
    return d == II_NONE || ii_dead(idx, d);
  }
  d = doc * idx->block_size;
  end = idx->docs - d < idx->block_size ? idx->docs : d + idx->block_size;
  for (; d < end; d++) {
    if (!ii_dead(idx, d)) {
      return false;
    }
  }
  return true;
}

bool ds_invidx_contains(const ds_inverted_index_t *idx, size_t doc_id) {
  size_t d;
  if (idx == NULL) {
    return false;
  }
  d = ii_doc_index(idx, doc_id);
  return d != II_NONE && !ii_dead(idx, d);
}

ds_status_t ds_invidx_remove(ds_inverted_index_t *idx, size_t doc_id) {
  size_t d;
  size_t old;
  ds_status_t st;
  if (idx == NULL) {
    return DS_ERR_NULL;
  }
  d = ii_doc_index(idx, doc_id);
  if (d == II_NONE || ii_dead(idx, d)) {
    return DS_NOT_FOUND;
  }
  old = idx->deleted_capacity;
  st = ii_reserve(idx->context, (void **)&idx->deleted, &idx->deleted_capacity,
                  idx->docs / 8 + 1, 1);
  if (st != DS_OK) {
    return st;
  }
  if (idx->deleted_capacity > old) {
    memset(idx->deleted + old, 0, idx->deleted_capacity - old);
  }
  idx->deleted[d / 8] = (unsigned char)(idx->deleted[d / 8] | (1u << (d % 8)));
  idx->deleted_count++;
  idx->deleted_tokens += idx->doc_len[d];
  return DS_OK;
}

size_t ds_invidx_deleted(const ds_inverted_index_t *idx) {
  return idx == NULL ? 0 : idx->deleted_count;
}

ds_status_t ds_invidx_document_terms(const ds_inverted_index_t *idx,
                                     size_t doc_id, size_t *terms,
                                     size_t *freqs, size_t capacity,
                                     size_t *count) {
  size_t d;
  size_t at;
  size_t n;
  size_t i;
  size_t term = 0;
  if (idx == NULL || count == NULL ||
      (capacity > 0 && (terms == NULL || freqs == NULL))) {
    return DS_ERR_NULL;
  }
  if (!idx->keep_forward) {
    return DS_ERR_STATE;
  }
  d = ii_doc_index(idx, doc_id);
  if (d == II_NONE || ii_dead(idx, d)) {
    return DS_ERR_RANGE;
  }
  at = idx->fwd_off[d];
  n = ii_get_varint(idx->fwd, &at);
  *count = n;
  for (i = 0; i < n && i < capacity; i++) {
    term += ii_get_varint(idx->fwd, &at);
    terms[i] = term;
    freqs[i] = ii_get_varint(idx->fwd, &at);
  }
  return n > capacity ? DS_ERR_RANGE : DS_OK;
}

/* ------------------------------------------------------------ cursors */

static void ii_cursor_term(const ds_inverted_index_t *idx, size_t t,
                           ds_invidx_cursor_t *c) {
  memset(c, 0, sizeof(*c));
  c->index = idx;
  c->term = t;
}

void ds_invidx_cursor_open(const ds_inverted_index_t *idx, const void *text,
                           size_t length, ds_invidx_cursor_t *cursor) {
  if (cursor == NULL) {
    return;
  }
  ii_cursor_term(idx, ds_invidx_term_id(idx, text, length), cursor);
}

/* the next posting, deleted documents included */
static bool ii_raw_next(ds_invidx_cursor_t *c) {
  const ds_invidx_posting_list_t *pl = &c->index->postings[c->term];
  while (c->positions_left > 0) {
    (void)ii_unpost(c->index, pl, &c->at);
    c->positions_left--;
  }
  if (c->at >= pl->bits) {
    c->ended = true;
    return false;
  }
  c->doc = (c->started ? c->doc : 0) + ii_unpost(c->index, pl, &c->at);
  c->started = true;
  c->freq = 0;
  c->position = 0;
  if (c->index->mode != DS_INVIDX_BLOCK) {
    c->freq = ii_unpost(c->index, pl, &c->at);
  }
  if (c->index->mode == DS_INVIDX_POSITIONAL) {
    c->positions_left = c->freq;
  }
  return true;
}

bool ds_invidx_cursor_next(ds_invidx_cursor_t *c) {
  if (c == NULL || c->index == NULL || c->term == II_NONE || c->ended) {
    return false;
  }
  while (ii_raw_next(c)) {
    if (c->index->deleted_count == 0 || !ii_skip_posting(c->index, c->doc)) {
      return true;
    }
  }
  return false;
}

bool ds_invidx_cursor_advance(ds_invidx_cursor_t *c, size_t target) {
  const ds_invidx_posting_list_t *pl;
  if (c == NULL || c->index == NULL || c->term == II_NONE || c->ended) {
    return false;
  }
  if (c->started && c->doc >= target) {
    return true;
  }
  pl = &c->index->postings[c->term];
  if (pl->skip_count > 0) {
    /* the last skip point after a posting below target: every posting
     * before it is below target too */
    size_t lo = 0;
    size_t hi = pl->skip_count;
    while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2;
      if (pl->skips[mid].doc < target) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    if (lo > 0 && pl->skips[lo - 1].bit > c->at) {
      c->at = pl->skips[lo - 1].bit;
      c->doc = pl->skips[lo - 1].doc;
      c->started = true;
      c->positions_left = 0;
    }
  }
  while (ds_invidx_cursor_next(c)) {
    if (c->doc >= target) {
      return true;
    }
  }
  return false;
}

bool ds_invidx_cursor_next_position(ds_invidx_cursor_t *c, size_t *position) {
  if (c == NULL || position == NULL || c->positions_left == 0) {
    return false;
  }
  if (c->positions_left == c->freq) {
    c->position = 0;
  }
  c->position += ii_unpost(c->index, &c->index->postings[c->term], &c->at);
  c->positions_left--;
  *position = c->position;
  return true;
}

/* ------------------------------------------------------------ compaction */

/* a list re-encoded without the deleted documents (blocks) */
static ds_status_t ii_rewrite(const ds_inverted_index_t *idx, size_t t,
                              ds_invidx_posting_list_t *out) {
  ds_invidx_cursor_t c;
  ds_status_t st = DS_OK;
  memset(out, 0, sizeof(*out));
  ii_cursor_term(idx, t, &c);
  while (st == DS_OK && ii_raw_next(&c)) {
    size_t doc = c.doc;
    if (ii_skip_posting(idx, doc)) {
      continue; /* its positions are passed over by the next step */
    }
    st = ii_skip_point(idx, out);
    if (st == DS_OK) {
      st = ii_post(idx, out, doc - (out->df ? out->last : 0));
    }
    if (idx->mode != DS_INVIDX_BLOCK) {
      if (st == DS_OK) {
        st = ii_post(idx, out, c.freq);
      }
      while (st == DS_OK && c.positions_left > 0) {
        st = ii_post(idx, out, ii_unpost(idx, &idx->postings[t], &c.at));
        c.positions_left--;
      }
      out->cf += c.freq;
    }
    out->df++;
    out->last = doc;
  }
  if (idx->mode == DS_INVIDX_BLOCK) {
    out->cf = idx->postings[t].cf;
  }
  if (st != DS_OK) {
    ds_context_free(idx->context, out->bytes);
    ds_context_free(idx->context, out->skips);
    memset(out, 0, sizeof(*out));
  }
  return st;
}

ds_status_t ds_invidx_compact(ds_inverted_index_t *idx) {
  ds_invidx_posting_list_t *fresh;
  size_t t;
  ds_status_t st = DS_OK;
  if (idx == NULL) {
    return DS_ERR_NULL;
  }
  if (idx->deleted_count == 0) {
    return DS_OK;
  }
  fresh = (ds_invidx_posting_list_t *)ds_context_alloc(
      idx->context, (idx->terms + 1) * sizeof(ds_invidx_posting_list_t));
  if (fresh == NULL) {
    return DS_ERR_ALLOC;
  }
  for (t = 0; t < idx->terms && st == DS_OK; t++) {
    st = ii_rewrite(idx, t, &fresh[t]);
  }
  if (st != DS_OK) {
    size_t k;
    for (k = 0; k < t; k++) {
      ds_context_free(idx->context, fresh[k].bytes);
      ds_context_free(idx->context, fresh[k].skips);
    }
    ds_context_free(idx->context, fresh);
    return st;
  }
  for (t = 0; t < idx->terms; t++) {
    ds_context_free(idx->context, idx->postings[t].bytes);
    ds_context_free(idx->context, idx->postings[t].skips);
    idx->postings[t] = fresh[t];
  }
  ds_context_free(idx->context, fresh);
  if (idx->mode != DS_INVIDX_BLOCK) {
    /* the documents leave too: slide the live ones down */
    size_t d;
    size_t k = 0;
    size_t w = 0;
    for (d = 0; d < idx->docs; d++) {
      size_t from = idx->fwd_off[d];
      size_t end = d + 1 < idx->docs ? idx->fwd_off[d + 1] : idx->fwd_used;
      if (ii_dead(idx, d)) {
        continue;
      }
      idx->doc_id[k] = idx->doc_id[d];
      idx->doc_len[k] = idx->doc_len[d];
      idx->fwd_off[k] = w;
      if (idx->keep_forward) {
        memmove(idx->fwd + w, idx->fwd + from, end - from);
        w += end - from;
      }
      k++;
    }
    if (idx->keep_forward) {
      idx->fwd_used = w;
    }
    idx->docs = k;
    idx->total_tokens -= idx->deleted_tokens;
    idx->deleted_count = 0;
    idx->deleted_tokens = 0;
    memset(idx->deleted, 0, idx->deleted_capacity);
  }
  return DS_OK;
}

/* ------------------------------------------------------------ queries */

/* decoded document (block) list of a term */
static ds_status_t ii_list(const ds_inverted_index_t *idx, size_t t,
                           size_t **out, size_t *n) {
  ds_invidx_cursor_t c;
  size_t k = 0;
  size_t df = t == II_NONE ? 0 : idx->postings[t].df;
  *out = (size_t *)ds_context_alloc(idx->context, (df + 1) * sizeof(size_t));
  if (*out == NULL) {
    return DS_ERR_ALLOC;
  }
  ii_cursor_term(idx, t, &c);
  while (ds_invidx_cursor_next(&c)) {
    (*out)[k++] = c.doc;
  }
  *n = k;
  return DS_OK;
}

static size_t ii_intersect(size_t *a, size_t na, const size_t *b, size_t nb) {
  size_t i = 0;
  size_t j = 0;
  size_t k = 0;
  while (i < na && j < nb) {
    if (a[i] < b[j]) {
      i++;
    } else if (a[i] > b[j]) {
      j++;
    } else {
      a[k++] = a[i];
      i++;
      j++;
    }
  }
  return k;
}

static size_t ii_subtract(size_t *a, size_t na, const size_t *b, size_t nb) {
  size_t i = 0;
  size_t j = 0;
  size_t k = 0;
  while (i < na) {
    while (j < nb && b[j] < a[i]) {
      j++;
    }
    if (j < nb && b[j] == a[i]) {
      i++;
    } else {
      a[k++] = a[i++];
    }
  }
  return k;
}

/* union of term lists into a fresh array */
static ds_status_t ii_union(const ds_inverted_index_t *idx,
                            const ds_invidx_term_t *terms, size_t count,
                            size_t **out, size_t *n) {
  size_t *acc = NULL;
  size_t na = 0;
  size_t i;
  ds_status_t st = DS_OK;
  acc = (size_t *)ds_context_alloc(idx->context, sizeof(size_t));
  if (acc == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < count && st == DS_OK; i++) {
    size_t *l;
    size_t nl;
    size_t *m;
    size_t x = 0;
    size_t y = 0;
    size_t k = 0;
    st = ii_list(idx, ds_invidx_term_id(idx, terms[i].text, terms[i].length),
                 &l, &nl);
    if (st != DS_OK) {
      break;
    }
    m = (size_t *)ds_context_alloc(idx->context,
                                   (na + nl + 1) * sizeof(size_t));
    if (m == NULL) {
      ds_context_free(idx->context, l);
      st = DS_ERR_ALLOC;
      break;
    }
    while (x < na || y < nl) {
      if (y == nl || (x < na && acc[x] < l[y])) {
        m[k++] = acc[x++];
      } else if (x == na || l[y] < acc[x]) {
        m[k++] = l[y++];
      } else {
        m[k++] = acc[x++];
        y++;
      }
    }
    ds_context_free(idx->context, acc);
    ds_context_free(idx->context, l);
    acc = m;
    na = k;
  }
  if (st != DS_OK) {
    ds_context_free(idx->context, acc);
    return st;
  }
  *out = acc;
  *n = na;
  return DS_OK;
}

/* filter ascending candidates by probing lists with advancing cursors:
 * keep those every list holds (II_PROBE_ALL; `skip` names a list already
 * known to hold them), those some list holds (II_PROBE_ANY) or those no
 * list holds (II_PROBE_NONE) */
#define II_PROBE_ALL 0
#define II_PROBE_ANY 1
#define II_PROBE_NONE 2

static ds_status_t ii_probe(const ds_inverted_index_t *idx, size_t *acc,
                            size_t *na, const ds_invidx_term_t *terms,
                            size_t count, size_t skip, int mode) {
  ds_invidx_cursor_t *cur;
  size_t lists = count - (skip < count ? 1 : 0);
  size_t i;
  size_t j;
  size_t k = 0;
  cur = (ds_invidx_cursor_t *)ds_context_alloc(
      idx->context, (count + 1) * sizeof(ds_invidx_cursor_t));
  if (cur == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < count; i++) {
    ds_invidx_cursor_open(idx, terms[i].text, terms[i].length, &cur[i]);
  }
  for (j = 0; j < *na; j++) {
    size_t d = acc[j];
    size_t hits = 0;
    bool over = false;
    for (i = 0; i < count; i++) {
      if (i == skip) {
        continue;
      }
      if (ds_invidx_cursor_advance(&cur[i], d) && cur[i].doc == d) {
        hits++;
      } else if (mode == II_PROBE_ALL) {
        over = cur[i].ended; /* no later candidate can pass either */
        break;
      }
    }
    if (mode == II_PROBE_ALL   ? hits == lists
        : mode == II_PROBE_ANY ? hits > 0
                               : hits == 0) {
      acc[k++] = d;
    }
    if (over) {
      break;
    }
  }
  *na = k;
  ds_context_free(idx->context, cur);
  return DS_OK;
}

void ds_invidx_result_free(const ds_inverted_index_t *idx,
                           ds_invidx_result_t *result) {
  if (idx == NULL || result == NULL) {
    return;
  }
  ds_context_free(idx->context, result->docs);
  ds_context_free(idx->context, result->scores);
  result->docs = NULL;
  result->scores = NULL;
  result->count = 0;
}

ds_status_t
ds_invidx_boolean(const ds_inverted_index_t *idx, const ds_invidx_term_t *must,
                  size_t must_count, const ds_invidx_term_t *should,
                  size_t should_count, const ds_invidx_term_t *must_not,
                  size_t must_not_count, ds_invidx_result_t *result) {
  size_t *acc = NULL;
  size_t na = 0;
  size_t i;
  ds_status_t st = DS_OK;
  if (idx == NULL || result == NULL || (must_count > 0 && must == NULL) ||
      (should_count > 0 && should == NULL) ||
      (must_not_count > 0 && must_not == NULL)) {
    return DS_ERR_NULL;
  }
  if (must_count == 0 && should_count == 0) {
    return DS_ERR_INVALID;
  }
  result->docs = NULL;
  result->scores = NULL;
  result->count = 0;
  if (must_count > 0) {
    /* start from the rarest term */
    size_t best = 0;
    size_t df_best = (size_t)-1;
    size_t df_max = 0;
    ds_invidx_intersection_t how = idx->intersection;
    for (i = 0; i < must_count; i++) {
      size_t df =
          ds_invidx_document_frequency(idx, must[i].text, must[i].length);
      if (df < df_best) {
        best = i;
        df_best = df;
      }
      if (df > df_max) {
        df_max = df;
      }
    }
    if (how == DS_INVIDX_INTERSECT_AUTO) {
      how = df_best < df_max / 16 ? DS_INVIDX_INTERSECT_SKIP
                                  : DS_INVIDX_INTERSECT_MERGE;
    }
    st =
        ii_list(idx, ds_invidx_term_id(idx, must[best].text, must[best].length),
                &acc, &na);
    if (how == DS_INVIDX_INTERSECT_SKIP) {
      /* probe the candidates into every other list */
      if (st == DS_OK && na > 0) {
        st = ii_probe(idx, acc, &na, must, must_count, best, II_PROBE_ALL);
      }
      if (st == DS_OK && should_count > 0 && na > 0) {
        st = ii_probe(idx, acc, &na, should, should_count, (size_t)-1,
                      II_PROBE_ANY);
      }
      if (st == DS_OK && must_not_count > 0 && na > 0) {
        st = ii_probe(idx, acc, &na, must_not, must_not_count, (size_t)-1,
                      II_PROBE_NONE);
      }
      if (st != DS_OK) {
        ds_context_free(idx->context, acc);
        return st;
      }
      result->docs = acc;
      result->count = na;
      return DS_OK;
    }
    for (i = 0; st == DS_OK && i < must_count && na > 0; i++) {
      size_t *l;
      size_t nl;
      if (i == best) {
        continue;
      }
      st = ii_list(idx, ds_invidx_term_id(idx, must[i].text, must[i].length),
                   &l, &nl);
      if (st == DS_OK) {
        na = ii_intersect(acc, na, l, nl);
        ds_context_free(idx->context, l);
      }
    }
  }
  if (st == DS_OK && should_count > 0) {
    size_t *u;
    size_t nu;
    st = ii_union(idx, should, should_count, &u, &nu);
    if (st == DS_OK) {
      if (acc == NULL) {
        acc = u;
        na = nu;
      } else {
        na = ii_intersect(acc, na, u, nu);
        ds_context_free(idx->context, u);
      }
    }
  }
  if (st == DS_OK && must_not_count > 0 && na > 0) {
    size_t *u;
    size_t nu;
    st = ii_union(idx, must_not, must_not_count, &u, &nu);
    if (st == DS_OK) {
      na = ii_subtract(acc, na, u, nu);
      ds_context_free(idx->context, u);
    }
  }
  if (st != DS_OK) {
    ds_context_free(idx->context, acc);
    return st;
  }
  result->docs = acc;
  result->count = na;
  return DS_OK;
}

static int ii_size_cmp(const void *x, const void *y) {
  size_t a = *(const size_t *)x;
  size_t b = *(const size_t *)y;
  return a < b ? -1 : a > b ? 1 : 0;
}

ds_status_t ds_invidx_prefix(const ds_inverted_index_t *idx, const void *prefix,
                             size_t length, ds_invidx_result_t *result,
                             size_t *terms_matched) {
  ds_invidx_dict_cursor_t c;
  size_t *acc = NULL;
  size_t na = 0;
  size_t cap = 0;
  size_t matched = 0;
  size_t t;
  ds_status_t st;
  if (idx == NULL || result == NULL || (length > 0 && prefix == NULL)) {
    return DS_ERR_NULL;
  }
  result->docs = NULL;
  result->scores = NULL;
  result->count = 0;
  st = ds_invidx_term_seek(idx, prefix, length, &c);
  if (st != DS_OK) {
    return st;
  }
  while ((t = idx->dict_ops->next(idx->dict, &c)) != II_NONE) {
    ds_invidx_cursor_t pc;
    size_t df = idx->postings[t].df;
    if (idx->term_len[t] < length ||
        (length > 0 && memcmp(idx->term_text[t], prefix, length) != 0)) {
      break; /* past the terms with the prefix */
    }
    matched++;
    if (df > (size_t)-1 - 1 - na) {
      st = DS_ERR_OVERFLOW;
      break;
    }
    st = ii_reserve(idx->context, (void **)&acc, &cap, na + df + 1,
                    sizeof(size_t));
    if (st != DS_OK) {
      break;
    }
    ii_cursor_term(idx, t, &pc);
    while (ds_invidx_cursor_next(&pc)) {
      acc[na++] = pc.doc;
    }
  }
  if (st != DS_OK) {
    ds_context_free(idx->context, acc);
    return st;
  }
  if (na > 1) {
    size_t i;
    size_t k = 1;
    qsort(acc, na, sizeof(size_t), ii_size_cmp);
    for (i = 1; i < na; i++) {
      if (acc[i] != acc[k - 1]) {
        acc[k++] = acc[i];
      }
    }
    na = k;
  }
  result->docs = acc;
  result->count = na;
  if (terms_matched != NULL) {
    *terms_matched = matched;
  }
  return DS_OK;
}

ds_status_t ds_invidx_phrase(const ds_inverted_index_t *idx,
                             const ds_invidx_term_t *terms, size_t count,
                             ds_invidx_result_t *result) {
  ds_invidx_cursor_t *cur = NULL;
  size_t *pos = NULL; /* positions of term 0 */
  size_t pos_cap = 0;
  size_t *found = NULL;
  size_t nfound = 0;
  size_t found_cap = 0;
  size_t i;
  bool live = true;
  ds_status_t st = DS_OK;
  if (idx == NULL || result == NULL || (count > 0 && terms == NULL)) {
    return DS_ERR_NULL;
  }
  if (idx->mode != DS_INVIDX_POSITIONAL) {
    return DS_ERR_STATE;
  }
  if (count == 0) {
    return DS_ERR_INVALID;
  }
  result->docs = NULL;
  result->scores = NULL;
  result->count = 0;
  cur = (ds_invidx_cursor_t *)ds_context_alloc(
      idx->context, count * sizeof(ds_invidx_cursor_t));
  if (cur == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < count; i++) {
    ds_invidx_cursor_open(idx, terms[i].text, terms[i].length, &cur[i]);
    live = live && ds_invidx_cursor_next(&cur[i]);
  }
  while (live && st == DS_OK) {
    size_t target = cur[0].doc;
    bool aligned = true;
    for (i = 1; i < count; i++) {
      if (cur[i].doc > target) {
        target = cur[i].doc;
      }
    }
    for (i = 0; live && i < count; i++) {
      live = ds_invidx_cursor_advance(&cur[i], target);
      if (live && cur[i].doc != target) {
        aligned = false;
      }
    }
    if (!live) {
      break;
    }
    if (aligned) {
      /* positions of term 0, then filter by each later term at +i */
      size_t np = 0;
      size_t p;
      st = ii_reserve(idx->context, (void **)&pos, &pos_cap, cur[0].freq + 1,
                      sizeof(size_t));
      if (st != DS_OK) {
        break;
      }
      while (ds_invidx_cursor_next_position(&cur[0], &p)) {
        pos[np++] = p;
      }
      for (i = 1; i < count && np > 0; i++) {
        size_t k = 0;
        size_t w = 0;
        bool more = ds_invidx_cursor_next_position(&cur[i], &p);
        while (k < np && more) {
          if (p < pos[k] + i) {
            more = ds_invidx_cursor_next_position(&cur[i], &p);
          } else if (p > pos[k] + i) {
            k++;
          } else {
            pos[w++] = pos[k++];
          }
        }
        np = w;
      }
      if (np > 0) {
        st = ii_reserve(idx->context, (void **)&found, &found_cap, nfound + 1,
                        sizeof(size_t));
        if (st == DS_OK) {
          found[nfound++] = target;
        }
      }
      live = ds_invidx_cursor_next(&cur[0]);
    }
  }
  ds_context_free(idx->context, cur);
  ds_context_free(idx->context, pos);
  if (st != DS_OK) {
    ds_context_free(idx->context, found);
    return st;
  }
  result->docs = found;
  result->count = nfound;
  return DS_OK;
}

typedef struct ii_scored {
  double score;
  size_t doc;
} ii_scored_t;

static int ii_scored_cmp(const void *x, const void *y) {
  const ii_scored_t *a = (const ii_scored_t *)x;
  const ii_scored_t *b = (const ii_scored_t *)y;
  if (a->score > b->score) {
    return -1;
  }
  if (a->score < b->score) {
    return 1;
  }
  return a->doc < b->doc ? -1 : a->doc > b->doc ? 1 : 0;
}

ds_status_t ds_invidx_bm25(const ds_inverted_index_t *idx,
                           const ds_invidx_term_t *terms, size_t count,
                           double k1, double b, size_t top_k,
                           ds_invidx_result_t *result) {
  double *score;
  unsigned char *hit;
  ii_scored_t *list;
  double avgdl;
  double live;
  size_t n = 0;
  size_t i;
  size_t d;
  if (idx == NULL || result == NULL || (count > 0 && terms == NULL)) {
    return DS_ERR_NULL;
  }
  if (idx->mode == DS_INVIDX_BLOCK) {
    return DS_ERR_STATE;
  }
  if (!(k1 >= 0.0) || !(b >= 0.0) || b > 1.0) {
    return DS_ERR_INVALID;
  }
  result->docs = NULL;
  result->scores = NULL;
  result->count = 0;
  if (idx->docs == idx->deleted_count || top_k == 0) {
    return DS_OK;
  }
  score = (double *)ds_context_alloc(idx->context, idx->docs * sizeof(double));
  hit = (unsigned char *)ds_context_calloc(idx->context, idx->docs, 1);
  list = (ii_scored_t *)ds_context_alloc(idx->context,
                                         idx->docs * sizeof(ii_scored_t));
  if (score == NULL || hit == NULL || list == NULL) {
    ds_context_free(idx->context, score);
    ds_context_free(idx->context, hit);
    ds_context_free(idx->context, list);
    return DS_ERR_ALLOC;
  }
  live = (double)(idx->docs - idx->deleted_count);
  avgdl = (double)(idx->total_tokens - idx->deleted_tokens) / live;
  for (d = 0; d < idx->docs; d++) {
    score[d] = 0.0;
  }
  for (i = 0; i < count; i++) {
    ds_invidx_cursor_t c;
    double df;
    double idf;
    ds_invidx_cursor_open(idx, terms[i].text, terms[i].length, &c);
    if (c.term == II_NONE) {
      continue;
    }
    df = (double)idx->postings[c.term].df;
    idf = log(1.0 + (live - df + 0.5) / (df + 0.5));
    while (ds_invidx_cursor_next(&c)) {
      size_t di = ii_doc_index(idx, c.doc);
      double tf = (double)c.freq;
      double norm = avgdl > 0.0 ? (double)idx->doc_len[di] / avgdl : 0.0;
      score[di] += idf * tf * (k1 + 1.0) / (tf + k1 * (1.0 - b + b * norm));
      hit[di] = 1;
    }
  }
  for (d = 0; d < idx->docs; d++) {
    if (hit[d]) {
      list[n].score = score[d];
      list[n].doc = idx->doc_id[d];
      n++;
    }
  }
  qsort(list, n, sizeof(ii_scored_t), ii_scored_cmp);
  if (n > top_k) {
    n = top_k;
  }
  ds_context_free(idx->context, score);
  ds_context_free(idx->context, hit);
  result->docs =
      (size_t *)ds_context_alloc(idx->context, (n + 1) * sizeof(size_t));
  result->scores =
      (double *)ds_context_alloc(idx->context, (n + 1) * sizeof(double));
  if (result->docs == NULL || result->scores == NULL) {
    ds_invidx_result_free(idx, result);
    ds_context_free(idx->context, list);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    result->docs[i] = list[i].doc;
    result->scores[i] = list[i].score;
  }
  result->count = n;
  ds_context_free(idx->context, list);
  return DS_OK;
}
