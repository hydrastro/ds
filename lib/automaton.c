#include "automaton.h"
#include <limits.h>
#include <string.h>

#define AU_NONE DS_AUTOMATON_NONE
#define AU_BITS (sizeof(unsigned long) * CHAR_BIT)

static void *au_realloc(ds_context_t *ctx, void *p, size_t count, size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > ((size_t)-1) / size) {
    return NULL;
  }
  return ds_context_realloc(ctx, p, count * size);
}

/* ---- DFA ------------------------------------------------------------ */

ds_status_t ds_dfa_init(ds_dfa_t *dfa, size_t alphabet, ds_context_t *context) {
  if (dfa == NULL) {
    return DS_ERR_NULL;
  }
  if (alphabet == 0) {
    return DS_ERR_INVALID;
  }
  memset(dfa, 0, sizeof(*dfa));
  dfa->alphabet = alphabet;
  dfa->start = AU_NONE;
  dfa->context = context;
  return DS_OK;
}

void ds_dfa_destroy(ds_dfa_t *dfa) {
  if (dfa == NULL) {
    return;
  }
  ds_context_free(dfa->context, dfa->delta);
  ds_context_free(dfa->context, dfa->accepting);
  dfa->delta = NULL;
  dfa->accepting = NULL;
  dfa->states = 0;
  dfa->capacity = 0;
}

static ds_status_t dfa_reserve(ds_dfa_t *dfa, size_t states) {
  size_t cap;
  size_t *d;
  unsigned char *a;
  if (states <= dfa->capacity) {
    return DS_OK;
  }
  cap = dfa->capacity < 8 ? 8 : dfa->capacity * 2;
  while (cap < states) {
    cap *= 2;
  }
  if (cap > ((size_t)-1) / dfa->alphabet) {
    return DS_ERR_OVERFLOW;
  }
  d = (size_t *)au_realloc(dfa->context, dfa->delta, cap * dfa->alphabet,
                           sizeof(size_t));
  if (d == NULL) {
    return DS_ERR_ALLOC;
  }
  dfa->delta = d;
  a = (unsigned char *)au_realloc(dfa->context, dfa->accepting, cap, 1);
  if (a == NULL) {
    return DS_ERR_ALLOC;
  }
  dfa->accepting = a;
  dfa->capacity = cap;
  return DS_OK;
}

ds_status_t ds_dfa_add_state(ds_dfa_t *dfa, bool accepting, size_t *out_state) {
  size_t i;
  ds_status_t st;
  if (dfa == NULL) {
    return DS_ERR_NULL;
  }
  st = dfa_reserve(dfa, dfa->states + 1);
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < dfa->alphabet; i++) {
    dfa->delta[dfa->states * dfa->alphabet + i] = AU_NONE;
  }
  dfa->accepting[dfa->states] = (unsigned char)(accepting ? 1 : 0);
  if (dfa->start == AU_NONE) {
    dfa->start = dfa->states;
  }
  if (out_state != NULL) {
    *out_state = dfa->states;
  }
  dfa->states++;
  return DS_OK;
}

ds_status_t ds_dfa_set_transition(ds_dfa_t *dfa, size_t from, size_t symbol,
                                  size_t to) {
  if (dfa == NULL) {
    return DS_ERR_NULL;
  }
  if (from >= dfa->states || symbol >= dfa->alphabet ||
      (to != AU_NONE && to >= dfa->states)) {
    return DS_ERR_RANGE;
  }
  dfa->delta[from * dfa->alphabet + symbol] = to;
  return DS_OK;
}

ds_status_t ds_dfa_set_start(ds_dfa_t *dfa, size_t state) {
  if (dfa == NULL) {
    return DS_ERR_NULL;
  }
  if (state >= dfa->states) {
    return DS_ERR_RANGE;
  }
  dfa->start = state;
  return DS_OK;
}

size_t ds_dfa_run(const ds_dfa_t *dfa, const size_t *symbols, size_t n) {
  size_t q;
  size_t i;
  if (dfa == NULL || dfa->states == 0 || (symbols == NULL && n > 0)) {
    return AU_NONE;
  }
  q = dfa->start;
  for (i = 0; i < n && q != AU_NONE; i++) {
    if (symbols[i] >= dfa->alphabet) {
      return AU_NONE;
    }
    q = dfa->delta[q * dfa->alphabet + symbols[i]];
  }
  return q;
}

bool ds_dfa_accepts(const ds_dfa_t *dfa, const size_t *symbols, size_t n) {
  size_t q = ds_dfa_run(dfa, symbols, n);
  return q != AU_NONE && dfa->accepting[q] != 0;
}

bool ds_dfa_accepts_bytes(const ds_dfa_t *dfa, const unsigned char *s,
                          size_t n) {
  size_t q;
  size_t i;
  if (dfa == NULL || dfa->states == 0 || (s == NULL && n > 0)) {
    return false;
  }
  q = dfa->start;
  for (i = 0; i < n && q != AU_NONE; i++) {
    if (s[i] >= dfa->alphabet) {
      return false;
    }
    q = dfa->delta[q * dfa->alphabet + s[i]];
  }
  return q != AU_NONE && dfa->accepting[q] != 0;
}

/* ---- NFA ------------------------------------------------------------ */

ds_status_t ds_nfa_init(ds_nfa_t *nfa, size_t alphabet, ds_context_t *context) {
  if (nfa == NULL) {
    return DS_ERR_NULL;
  }
  if (alphabet == 0) {
    return DS_ERR_INVALID;
  }
  memset(nfa, 0, sizeof(*nfa));
  nfa->alphabet = alphabet;
  nfa->start = AU_NONE;
  nfa->context = context;
  return DS_OK;
}

void ds_nfa_destroy(ds_nfa_t *nfa) {
  if (nfa == NULL) {
    return;
  }
  ds_context_free(nfa->context, nfa->accepting);
  ds_context_free(nfa->context, nfa->from);
  nfa->accepting = NULL;
  nfa->from = NULL;
  nfa->symbol = NULL;
  nfa->to = NULL;
  nfa->states = 0;
  nfa->edges = 0;
}

ds_status_t ds_nfa_add_state(ds_nfa_t *nfa, bool accepting, size_t *out_state) {
  unsigned char *a;
  size_t cap;
  if (nfa == NULL) {
    return DS_ERR_NULL;
  }
  if (nfa->states == nfa->state_capacity) {
    cap = nfa->state_capacity < 8 ? 8 : nfa->state_capacity * 2;
    a = (unsigned char *)au_realloc(nfa->context, nfa->accepting, cap, 1);
    if (a == NULL) {
      return DS_ERR_ALLOC;
    }
    nfa->accepting = a;
    nfa->state_capacity = cap;
  }
  nfa->accepting[nfa->states] = (unsigned char)(accepting ? 1 : 0);
  if (nfa->start == AU_NONE) {
    nfa->start = nfa->states;
  }
  if (out_state != NULL) {
    *out_state = nfa->states;
  }
  nfa->states++;
  return DS_OK;
}

ds_status_t ds_nfa_add_transition(ds_nfa_t *nfa, size_t from, size_t symbol,
                                  size_t to) {
  size_t cap;
  size_t *blk;
  if (nfa == NULL) {
    return DS_ERR_NULL;
  }
  if (from >= nfa->states || to >= nfa->states ||
      (symbol != DS_NFA_EPSILON && symbol >= nfa->alphabet)) {
    return DS_ERR_RANGE;
  }
  if (nfa->edges == nfa->edge_capacity) {
    cap = nfa->edge_capacity < 16 ? 16 : nfa->edge_capacity * 2;
    blk = (size_t *)au_realloc(nfa->context, NULL, 3 * cap, sizeof(size_t));
    if (blk == NULL) {
      return DS_ERR_ALLOC;
    }
    if (nfa->edges > 0) {
      memcpy(blk, nfa->from, nfa->edges * sizeof(size_t));
      memcpy(blk + cap, nfa->symbol, nfa->edges * sizeof(size_t));
      memcpy(blk + 2 * cap, nfa->to, nfa->edges * sizeof(size_t));
    }
    ds_context_free(nfa->context, nfa->from);
    nfa->from = blk;
    nfa->symbol = blk + cap;
    nfa->to = blk + 2 * cap;
    nfa->edge_capacity = cap;
  }
  nfa->from[nfa->edges] = from;
  nfa->symbol[nfa->edges] = symbol;
  nfa->to[nfa->edges] = to;
  nfa->edges++;
  return DS_OK;
}

ds_status_t ds_nfa_set_start(ds_nfa_t *nfa, size_t state) {
  if (nfa == NULL) {
    return DS_ERR_NULL;
  }
  if (state >= nfa->states) {
    return DS_ERR_RANGE;
  }
  nfa->start = state;
  return DS_OK;
}

/* Edges grouped by source (CSR). */
typedef struct nfa_csr {
  size_t *off;  /* states + 1 */
  size_t *edge; /* edge ids */
  size_t *stack;
  size_t words;
} nfa_csr_t;

static ds_status_t csr_build(const ds_nfa_t *nfa, nfa_csr_t *c,
                             ds_context_t *ctx) {
  size_t i;
  c->words = nfa->states / AU_BITS + 1;
  c->off = (size_t *)au_realloc(ctx, NULL, 2 * nfa->states + nfa->edges + 2,
                                sizeof(size_t));
  if (c->off == NULL) {
    return DS_ERR_ALLOC;
  }
  c->edge = c->off + nfa->states + 1;
  c->stack = c->edge + nfa->edges + 1;
  memset(c->off, 0, (nfa->states + 1) * sizeof(size_t));
  for (i = 0; i < nfa->edges; i++) {
    c->off[nfa->from[i] + 1]++;
  }
  for (i = 0; i < nfa->states; i++) {
    c->off[i + 1] += c->off[i];
  }
  for (i = 0; i < nfa->edges; i++) {
    c->edge[c->off[nfa->from[i]]++] = i;
  }
  for (i = nfa->states; i > 0; i--) {
    c->off[i] = c->off[i - 1];
  }
  c->off[0] = 0;
  return DS_OK;
}

static int bit_get(const unsigned long *s, size_t i) {
  return (int)((s[i / AU_BITS] >> (i % AU_BITS)) & 1UL);
}

static void bit_set(unsigned long *s, size_t i) {
  s[i / AU_BITS] |= 1UL << (i % AU_BITS);
}

/* Epsilon closure of set s, in place. */
static void csr_closure(const ds_nfa_t *nfa, const nfa_csr_t *c,
                        unsigned long *s) {
  size_t top = 0;
  size_t q;
  size_t k;
  size_t e;
  for (q = 0; q < nfa->states; q++) {
    if (bit_get(s, q)) {
      c->stack[top++] = q;
    }
  }
  while (top > 0) {
    q = c->stack[--top];
    for (k = c->off[q]; k < c->off[q + 1]; k++) {
      e = c->edge[k];
      if (nfa->symbol[e] == DS_NFA_EPSILON && !bit_get(s, nfa->to[e])) {
        bit_set(s, nfa->to[e]);
        c->stack[top++] = nfa->to[e];
      }
    }
  }
}

static void csr_move(const ds_nfa_t *nfa, const nfa_csr_t *c,
                     const unsigned long *s, size_t sym, unsigned long *out) {
  size_t q;
  size_t k;
  size_t e;
  memset(out, 0, c->words * sizeof(unsigned long));
  for (q = 0; q < nfa->states; q++) {
    if (!bit_get(s, q)) {
      continue;
    }
    for (k = c->off[q]; k < c->off[q + 1]; k++) {
      e = c->edge[k];
      if (nfa->symbol[e] == sym) {
        bit_set(out, nfa->to[e]);
      }
    }
  }
}

static int set_accepting(const ds_nfa_t *nfa, const unsigned long *s) {
  size_t q;
  for (q = 0; q < nfa->states; q++) {
    if (bit_get(s, q) && nfa->accepting[q]) {
      return 1;
    }
  }
  return 0;
}

static int set_empty(const unsigned long *s, size_t words) {
  size_t i;
  for (i = 0; i < words; i++) {
    if (s[i] != 0) {
      return 0;
    }
  }
  return 1;
}

ds_status_t ds_nfa_accepts(const ds_nfa_t *nfa, const size_t *symbols, size_t n,
                           bool *out_accepts) {
  nfa_csr_t c;
  unsigned long *cur;
  unsigned long *nxt;
  unsigned long *t;
  size_t i;
  ds_status_t st;
  if (nfa == NULL || out_accepts == NULL || (symbols == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  *out_accepts = false;
  if (nfa->states == 0) {
    return DS_OK;
  }
  st = csr_build(nfa, &c, nfa->context);
  if (st != DS_OK) {
    return st;
  }
  cur = (unsigned long *)au_realloc(nfa->context, NULL, 2 * c.words,
                                    sizeof(unsigned long));
  if (cur == NULL) {
    ds_context_free(nfa->context, c.off);
    return DS_ERR_ALLOC;
  }
  nxt = cur + c.words;
  memset(cur, 0, c.words * sizeof(unsigned long));
  bit_set(cur, nfa->start);
  csr_closure(nfa, &c, cur);
  for (i = 0; i < n && !set_empty(cur, c.words); i++) {
    if (symbols[i] >= nfa->alphabet) {
      memset(cur, 0, c.words * sizeof(unsigned long));
      break;
    }
    csr_move(nfa, &c, cur, symbols[i], nxt);
    csr_closure(nfa, &c, nxt);
    t = cur;
    cur = nxt;
    nxt = t;
  }
  *out_accepts = set_accepting(nfa, cur) != 0;
  ds_context_free(nfa->context, cur < nxt ? cur : nxt);
  ds_context_free(nfa->context, c.off);
  return DS_OK;
}

static size_t set_hash(const unsigned long *s, size_t words) {
  size_t h = 1469598103UL;
  size_t i;
  for (i = 0; i < words; i++) {
    h = (h ^ (size_t)s[i]) * 16777619UL;
    h ^= h >> 13;
  }
  return h;
}

typedef struct sc_intern {
  unsigned long *sets; /* DFA state d: sets + d * words */
  size_t set_cap;
  size_t *table; /* open addressing over DFA ids */
  size_t tsize;
  size_t words;
  size_t max_states;
  ds_context_t *ctx;
} sc_intern_t;

static ds_status_t sc_rehash(sc_intern_t *sc, size_t count) {
  size_t ns = sc->tsize * 2;
  size_t *nt;
  size_t i;
  size_t h;
  nt = (size_t *)au_realloc(sc->ctx, NULL, ns, sizeof(size_t));
  if (nt == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < ns; i++) {
    nt[i] = AU_NONE;
  }
  for (i = 0; i < count; i++) {
    h = set_hash(sc->sets + i * sc->words, sc->words) & (ns - 1);
    while (nt[h] != AU_NONE) {
      h = (h + 1) & (ns - 1);
    }
    nt[h] = i;
  }
  ds_context_free(sc->ctx, sc->table);
  sc->table = nt;
  sc->tsize = ns;
  return DS_OK;
}

/* DFA state for the set s, created when new. */
static ds_status_t sc_get(sc_intern_t *sc, const ds_nfa_t *nfa, ds_dfa_t *out,
                          const unsigned long *s, size_t *out_id) {
  size_t h = set_hash(s, sc->words) & (sc->tsize - 1);
  size_t id;
  unsigned long *grown;
  ds_status_t st;
  while (sc->table[h] != AU_NONE &&
         memcmp(sc->sets + sc->table[h] * sc->words, s,
                sc->words * sizeof(unsigned long)) != 0) {
    h = (h + 1) & (sc->tsize - 1);
  }
  if (sc->table[h] != AU_NONE) {
    *out_id = sc->table[h];
    return DS_OK;
  }
  if (sc->max_states != 0 && out->states >= sc->max_states) {
    return DS_ERR_OVERFLOW;
  }
  st = ds_dfa_add_state(out, set_accepting(nfa, s) != 0, &id);
  if (st != DS_OK) {
    return st;
  }
  if (id >= sc->set_cap) {
    sc->set_cap = sc->set_cap < 16 ? 16 : sc->set_cap * 2;
    grown = (unsigned long *)au_realloc(
        sc->ctx, sc->sets, sc->set_cap * sc->words, sizeof(unsigned long));
    if (grown == NULL) {
      return DS_ERR_ALLOC;
    }
    sc->sets = grown;
  }
  memcpy(sc->sets + id * sc->words, s, sc->words * sizeof(unsigned long));
  sc->table[h] = id;
  *out_id = id;
  if (2 * out->states > sc->tsize) {
    return sc_rehash(sc, out->states);
  }
  return DS_OK;
}

ds_status_t ds_nfa_to_dfa(const ds_nfa_t *nfa, ds_dfa_t *out, size_t max_states,
                          ds_context_t *context) {
  nfa_csr_t c;
  sc_intern_t sc;
  unsigned long *tmp = NULL;
  size_t d;
  size_t sym;
  size_t id;
  size_t i;
  ds_status_t st;
  if (nfa == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_dfa_init(out, nfa->alphabet, context);
  if (st != DS_OK || nfa->states == 0) {
    return st;
  }
  st = csr_build(nfa, &c, context);
  if (st != DS_OK) {
    return st;
  }
  sc.sets = NULL;
  sc.set_cap = 0;
  sc.tsize = 64;
  sc.words = c.words;
  sc.max_states = max_states;
  sc.ctx = context;
  sc.table = (size_t *)au_realloc(context, NULL, sc.tsize, sizeof(size_t));
  tmp = (unsigned long *)au_realloc(context, NULL, c.words,
                                    sizeof(unsigned long));
  if (tmp == NULL || sc.table == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (i = 0; i < sc.tsize; i++) {
    sc.table[i] = AU_NONE;
  }
  memset(tmp, 0, c.words * sizeof(unsigned long));
  bit_set(tmp, nfa->start);
  csr_closure(nfa, &c, tmp);
  st = sc_get(&sc, nfa, out, tmp, &id);
  for (d = 0; st == DS_OK && d < out->states; d++) {
    for (sym = 0; sym < nfa->alphabet && st == DS_OK; sym++) {
      csr_move(nfa, &c, sc.sets + d * c.words, sym, tmp);
      if (set_empty(tmp, c.words)) {
        continue;
      }
      csr_closure(nfa, &c, tmp);
      st = sc_get(&sc, nfa, out, tmp, &id);
      if (st == DS_OK) {
        out->delta[d * nfa->alphabet + sym] = id;
      }
    }
  }
done:
  ds_context_free(context, c.off);
  ds_context_free(context, sc.sets);
  ds_context_free(context, sc.table);
  ds_context_free(context, tmp);
  if (st != DS_OK) {
    ds_dfa_destroy(out);
  }
  return st;
}

ds_status_t ds_nfa_union(const ds_nfa_t *a, const ds_nfa_t *b, ds_nfa_t *out,
                         ds_context_t *context) {
  size_t s;
  size_t i;
  ds_status_t st;
  if (a == NULL || b == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (a->alphabet != b->alphabet) {
    return DS_ERR_INVALID;
  }
  st = ds_nfa_init(out, a->alphabet, context);
  if (st != DS_OK) {
    return st;
  }
  st = ds_nfa_add_state(out, false, &s);
  for (i = 0; i < a->states && st == DS_OK; i++) {
    st = ds_nfa_add_state(out, a->accepting[i] != 0, NULL);
  }
  for (i = 0; i < b->states && st == DS_OK; i++) {
    st = ds_nfa_add_state(out, b->accepting[i] != 0, NULL);
  }
  for (i = 0; i < a->edges && st == DS_OK; i++) {
    st = ds_nfa_add_transition(out, 1 + a->from[i], a->symbol[i], 1 + a->to[i]);
  }
  for (i = 0; i < b->edges && st == DS_OK; i++) {
    st = ds_nfa_add_transition(out, 1 + a->states + b->from[i], b->symbol[i],
                               1 + a->states + b->to[i]);
  }
  if (st == DS_OK && a->states > 0) {
    st = ds_nfa_add_transition(out, s, DS_NFA_EPSILON, 1 + a->start);
  }
  if (st == DS_OK && b->states > 0) {
    st =
        ds_nfa_add_transition(out, s, DS_NFA_EPSILON, 1 + a->states + b->start);
  }
  if (st != DS_OK) {
    ds_nfa_destroy(out);
  }
  return st;
}

/* ---- minimization (Hopcroft) ---------------------------------------- */

ds_status_t ds_dfa_minimize(const ds_dfa_t *dfa, ds_dfa_t *out,
                            ds_context_t *context) {
  size_t k;
  size_t n; /* reachable states + dead */
  size_t dead;
  size_t *map = NULL;   /* original -> compact */
  size_t *orig = NULL;  /* compact -> original */
  size_t *delta = NULL; /* complete table over compact states */
  size_t *inv_off = NULL;
  size_t *inv = NULL;
  size_t *elems = NULL;
  size_t *loc = NULL;
  size_t *block = NULL;
  size_t *bstart = NULL;
  size_t *bend = NULL;
  size_t *marked = NULL;
  size_t *work = NULL;
  unsigned char *inwork = NULL;
  size_t *touched = NULL;
  size_t *snap = NULL;
  size_t *newid = NULL;
  size_t *queue = NULL;
  size_t blocks = 0;
  size_t wtop = 0;
  size_t i;
  size_t j;
  size_t q;
  size_t a;
  size_t p;
  size_t b;
  size_t ntouched;
  size_t ssize;
  size_t head;
  size_t tail;
  ds_status_t st = DS_OK;
  if (dfa == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  k = dfa->alphabet;
  st = ds_dfa_init(out, k, context);
  if (st != DS_OK) {
    return st;
  }
  if (dfa->states == 0) {
    return ds_dfa_add_state(out, false, NULL);
  }
  map = (size_t *)au_realloc(context, NULL, dfa->states, sizeof(size_t));
  orig = (size_t *)au_realloc(context, NULL, dfa->states + 1, sizeof(size_t));
  if (map == NULL || orig == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  /* reachable states, breadth-first */
  for (i = 0; i < dfa->states; i++) {
    map[i] = AU_NONE;
  }
  n = 0;
  orig[n] = dfa->start;
  map[dfa->start] = n++;
  for (head = 0; head < n; head++) {
    for (a = 0; a < k; a++) {
      q = dfa->delta[orig[head] * k + a];
      if (q != AU_NONE && map[q] == AU_NONE) {
        map[q] = n;
        orig[n++] = q;
      }
    }
  }
  dead = n++;
  if (n > ((size_t)-1) / k) {
    st = DS_ERR_OVERFLOW;
    goto done;
  }
  delta = (size_t *)au_realloc(context, NULL, n * k, sizeof(size_t));
  inv_off = (size_t *)au_realloc(context, NULL, n * k + 1, sizeof(size_t));
  inv = (size_t *)au_realloc(context, NULL, n * k, sizeof(size_t));
  elems = (size_t *)au_realloc(context, NULL, 11 * n + 2, sizeof(size_t));
  inwork = (unsigned char *)au_realloc(context, NULL, n, 1);
  if (delta == NULL || inv_off == NULL || inv == NULL || elems == NULL ||
      inwork == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  loc = elems + n;
  block = loc + n;
  bstart = block + n;
  bend = bstart + n;
  marked = bend + n;
  work = marked + n;
  touched = work + n;
  snap = touched + n;
  newid = snap + n;
  queue = newid + n;
  for (i = 0; i < n; i++) {
    for (a = 0; a < k; a++) {
      if (i == dead) {
        q = dead;
      } else {
        q = dfa->delta[orig[i] * k + a];
        q = q == AU_NONE ? dead : map[q];
      }
      delta[i * k + a] = q;
    }
  }
  /* inverse transitions: inv_off indexed by (a, target) */
  memset(inv_off, 0, (n * k + 1) * sizeof(size_t));
  for (i = 0; i < n; i++) {
    for (a = 0; a < k; a++) {
      inv_off[a * n + delta[i * k + a] + 1]++;
    }
  }
  for (i = 0; i < n * k; i++) {
    inv_off[i + 1] += inv_off[i];
  }
  for (i = 0; i < n; i++) {
    for (a = 0; a < k; a++) {
      inv[inv_off[a * n + delta[i * k + a]]++] = i;
    }
  }
  for (i = n * k; i > 0; i--) {
    inv_off[i] = inv_off[i - 1];
  }
  inv_off[0] = 0;
  /* initial partition: accepting, then non-accepting */
  j = 0;
  for (i = 0; i < n; i++) {
    if (i != dead && dfa->accepting[orig[i]]) {
      elems[j] = i;
      loc[i] = j++;
    }
  }
  b = j;
  for (i = 0; i < n; i++) {
    if (i == dead || !dfa->accepting[orig[i]]) {
      elems[j] = i;
      loc[i] = j++;
    }
  }
  if (b > 0) {
    bstart[blocks] = 0;
    bend[blocks] = b;
    for (i = 0; i < b; i++) {
      block[elems[i]] = blocks;
    }
    blocks++;
  }
  bstart[blocks] = b;
  bend[blocks] = n;
  for (i = b; i < n; i++) {
    block[elems[i]] = blocks;
  }
  blocks++;
  memset(inwork, 0, n);
  for (i = 0; i < blocks; i++) {
    marked[i] = 0;
  }
  /* the smaller initial block (or the only one) starts the work list */
  if (blocks == 2) {
    i = bend[0] - bstart[0] <= bend[1] - bstart[1] ? 0 : 1;
  } else {
    i = 0;
  }
  work[wtop++] = i;
  inwork[i] = 1;
  while (wtop > 0) {
    size_t s = work[--wtop];
    inwork[s] = 0;
    ssize = bend[s] - bstart[s];
    memcpy(snap, elems + bstart[s], ssize * sizeof(size_t));
    for (a = 0; a < k; a++) {
      ntouched = 0;
      for (i = 0; i < ssize; i++) {
        q = snap[i];
        for (j = inv_off[a * n + q]; j < inv_off[a * n + q + 1]; j++) {
          size_t pos;
          size_t other;
          p = inv[j];
          b = block[p];
          pos = loc[p];
          if (pos < bstart[b] + marked[b]) {
            continue; /* already marked */
          }
          if (marked[b] == 0) {
            touched[ntouched++] = b;
          }
          /* swap p into the marked prefix of its block */
          other = elems[bstart[b] + marked[b]];
          elems[pos] = other;
          loc[other] = pos;
          elems[bstart[b] + marked[b]] = p;
          loc[p] = bstart[b] + marked[b];
          marked[b]++;
        }
      }
      for (i = 0; i < ntouched; i++) {
        size_t nb;
        b = touched[i];
        if (marked[b] == bend[b] - bstart[b]) {
          marked[b] = 0;
          continue;
        }
        /* split: the marked prefix becomes a new block */
        nb = blocks++;
        bstart[nb] = bstart[b];
        bend[nb] = bstart[b] + marked[b];
        bstart[b] = bend[nb];
        marked[b] = 0;
        marked[nb] = 0;
        inwork[nb] = 0;
        for (j = bstart[nb]; j < bend[nb]; j++) {
          block[elems[j]] = nb;
        }
        if (inwork[b]) {
          work[wtop++] = nb;
          inwork[nb] = 1;
        } else if (bend[nb] - bstart[nb] <= bend[b] - bstart[b]) {
          work[wtop++] = nb;
          inwork[nb] = 1;
        } else {
          work[wtop++] = b;
          inwork[b] = 1;
        }
      }
    }
  }
  /* blocks to states, breadth-first from the start; the dead block (all
   * states that cannot accept) is dropped */
  for (i = 0; i < blocks; i++) {
    newid[i] = AU_NONE;
  }
  head = 0;
  tail = 0;
  b = block[0]; /* compact state 0 is the original start */
  if (b == block[dead] && blocks > 0) {
    /* the start cannot reach acceptance: empty language */
    st = ds_dfa_add_state(out, false, NULL);
    goto done;
  }
  newid[b] = 0;
  queue[tail++] = b;
  st = ds_dfa_add_state(out, dfa->accepting[orig[elems[bstart[b]]]] != 0, NULL);
  while (st == DS_OK && head < tail) {
    size_t cb = queue[head++];
    size_t rep = elems[bstart[cb]];
    for (a = 0; a < k && st == DS_OK; a++) {
      size_t tb = block[delta[rep * k + a]];
      if (tb == block[dead]) {
        continue;
      }
      if (newid[tb] == AU_NONE) {
        size_t r2 = elems[bstart[tb]];
        newid[tb] = out->states;
        st = ds_dfa_add_state(out, r2 != dead && dfa->accepting[orig[r2]] != 0,
                              NULL);
        queue[tail++] = tb;
        if (st != DS_OK) {
          break;
        }
      }
      out->delta[newid[cb] * k + a] = newid[tb];
    }
  }
done:
  ds_context_free(context, map);
  ds_context_free(context, orig);
  ds_context_free(context, delta);
  ds_context_free(context, inv_off);
  ds_context_free(context, inv);
  ds_context_free(context, elems);
  ds_context_free(context, inwork);
  if (st != DS_OK) {
    ds_dfa_destroy(out);
  }
  return st;
}

ds_status_t ds_dfa_equivalent(const ds_dfa_t *a, const ds_dfa_t *b,
                              bool *out_equal, ds_context_t *context) {
  ds_dfa_t ma;
  ds_dfa_t mb;
  ds_status_t st;
  size_t i;
  if (a == NULL || b == NULL || out_equal == NULL) {
    return DS_ERR_NULL;
  }
  if (a->alphabet != b->alphabet) {
    *out_equal = false;
    return DS_OK;
  }
  st = ds_dfa_minimize(a, &ma, context);
  if (st != DS_OK) {
    return st;
  }
  st = ds_dfa_minimize(b, &mb, context);
  if (st != DS_OK) {
    ds_dfa_destroy(&ma);
    return st;
  }
  *out_equal = ma.states == mb.states;
  for (i = 0; *out_equal && i < ma.states; i++) {
    if (ma.accepting[i] != mb.accepting[i] ||
        memcmp(ma.delta + i * ma.alphabet, mb.delta + i * mb.alphabet,
               ma.alphabet * sizeof(size_t)) != 0) {
      *out_equal = false;
    }
  }
  ds_dfa_destroy(&ma);
  ds_dfa_destroy(&mb);
  return DS_OK;
}

typedef struct pr_intern {
  size_t *pa; /* product state -> component states (dead = states) */
  size_t *pb;
  size_t cap;
  size_t *hash;
  size_t hsize;
  ds_context_t *ctx;
} pr_intern_t;

static size_t pr_hash(size_t x, size_t y) {
  return (size_t)((x * 2654435761UL) ^ (y * 40503UL) ^ (y << 7));
}

static ds_status_t pr_get(pr_intern_t *p, ds_dfa_t *out, size_t x, size_t y,
                          int acc, size_t *out_id) {
  size_t h = pr_hash(x, y) & (p->hsize - 1);
  size_t id;
  size_t i;
  size_t *g;
  ds_status_t st;
  while (p->hash[h] != AU_NONE &&
         (p->pa[p->hash[h]] != x || p->pb[p->hash[h]] != y)) {
    h = (h + 1) & (p->hsize - 1);
  }
  if (p->hash[h] != AU_NONE) {
    *out_id = p->hash[h];
    return DS_OK;
  }
  st = ds_dfa_add_state(out, acc != 0, &id);
  if (st != DS_OK) {
    return st;
  }
  if (id >= p->cap) {
    p->cap = p->cap < 16 ? 16 : p->cap * 2;
    g = (size_t *)au_realloc(p->ctx, p->pa, p->cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    p->pa = g;
    g = (size_t *)au_realloc(p->ctx, p->pb, p->cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    p->pb = g;
  }
  p->pa[id] = x;
  p->pb[id] = y;
  p->hash[h] = id;
  *out_id = id;
  if (2 * out->states > p->hsize) {
    size_t ns = p->hsize * 2;
    g = (size_t *)au_realloc(p->ctx, NULL, ns, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    for (i = 0; i < ns; i++) {
      g[i] = AU_NONE;
    }
    for (i = 0; i < out->states; i++) {
      h = pr_hash(p->pa[i], p->pb[i]) & (ns - 1);
      while (g[h] != AU_NONE) {
        h = (h + 1) & (ns - 1);
      }
      g[h] = i;
    }
    ds_context_free(p->ctx, p->hash);
    p->hash = g;
    p->hsize = ns;
  }
  return DS_OK;
}

ds_status_t ds_dfa_product(const ds_dfa_t *a, const ds_dfa_t *b, unsigned table,
                           ds_dfa_t *out, ds_context_t *context) {
  pr_intern_t p;
  size_t k;
  size_t s;
  size_t x;
  size_t y;
  size_t id;
  size_t sym;
  size_t i;
  int acc;
  ds_status_t st;
  if (a == NULL || b == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (a->alphabet != b->alphabet) {
    return DS_ERR_INVALID;
  }
  k = a->alphabet;
  table &= 0xFu;
  st = ds_dfa_init(out, k, context);
  if (st != DS_OK) {
    return st;
  }
  p.pa = NULL;
  p.pb = NULL;
  p.cap = 0;
  p.hsize = 64;
  p.ctx = context;
  p.hash = (size_t *)au_realloc(context, NULL, p.hsize, sizeof(size_t));
  if (p.hash == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < p.hsize; i++) {
    p.hash[i] = AU_NONE;
  }
#define PR_ACC(x, y)                                                           \
  ((int)((table >> (((x) < a->states && a->accepting[x] ? 2u : 0u) +           \
                    ((y) < b->states && b->accepting[y] ? 1u : 0u))) &         \
         1u))
  x = a->states > 0 ? a->start : a->states;
  y = b->states > 0 ? b->start : b->states;
  st = pr_get(&p, out, x, y, PR_ACC(x, y), &id);
  for (s = 0; st == DS_OK && s < out->states; s++) {
    for (sym = 0; sym < k && st == DS_OK; sym++) {
      x = p.pa[s];
      y = p.pb[s];
      x = x == a->states ? x : a->delta[x * k + sym];
      y = y == b->states ? y : b->delta[y * k + sym];
      if (x == AU_NONE) {
        x = a->states;
      }
      if (y == AU_NONE) {
        y = b->states;
      }
      acc = PR_ACC(x, y);
      if (x == a->states && y == b->states && !acc) {
        continue; /* both dead and rejecting: no transition */
      }
      st = pr_get(&p, out, x, y, acc, &id);
      if (st == DS_OK) {
        out->delta[s * k + sym] = id;
      }
    }
  }
#undef PR_ACC
  ds_context_free(context, p.pa);
  ds_context_free(context, p.pb);
  ds_context_free(context, p.hash);
  if (st != DS_OK) {
    ds_dfa_destroy(out);
  }
  return st;
}

/* ---- Mealy and Moore machines --------------------------------------- */

ds_status_t ds_mealy_init(ds_mealy_t *m, size_t alphabet,
                          ds_context_t *context) {
  if (m == NULL) {
    return DS_ERR_NULL;
  }
  m->output = NULL;
  m->output_capacity = 0;
  return ds_dfa_init(&m->dfa, alphabet, context);
}

void ds_mealy_destroy(ds_mealy_t *m) {
  if (m == NULL) {
    return;
  }
  ds_context_free(m->dfa.context, m->output);
  m->output = NULL;
  ds_dfa_destroy(&m->dfa);
}

ds_status_t ds_mealy_add_state(ds_mealy_t *m, size_t *out_state) {
  ds_status_t st;
  size_t *g;
  size_t i;
  if (m == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_dfa_add_state(&m->dfa, false, out_state);
  if (st != DS_OK) {
    return st;
  }
  if (m->dfa.capacity > m->output_capacity) {
    g = (size_t *)au_realloc(m->dfa.context, m->output,
                             m->dfa.capacity * m->dfa.alphabet, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    m->output = g;
    m->output_capacity = m->dfa.capacity;
  }
  for (i = 0; i < m->dfa.alphabet; i++) {
    m->output[(m->dfa.states - 1) * m->dfa.alphabet + i] = 0;
  }
  return DS_OK;
}

ds_status_t ds_mealy_set_transition(ds_mealy_t *m, size_t from, size_t symbol,
                                    size_t to, size_t output) {
  ds_status_t st;
  if (m == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_dfa_set_transition(&m->dfa, from, symbol, to);
  if (st == DS_OK) {
    m->output[from * m->dfa.alphabet + symbol] = output;
  }
  return st;
}

ds_status_t ds_mealy_run(const ds_mealy_t *m, const size_t *input, size_t n,
                         size_t *out, size_t *out_count) {
  size_t q;
  size_t i;
  size_t t;
  if (m == NULL || (n > 0 && (input == NULL || out == NULL))) {
    return DS_ERR_NULL;
  }
  if (out_count != NULL) {
    *out_count = 0;
  }
  if (m->dfa.states == 0) {
    return n == 0 ? DS_OK : DS_NOT_FOUND;
  }
  q = m->dfa.start;
  for (i = 0; i < n; i++) {
    if (input[i] >= m->dfa.alphabet) {
      return DS_NOT_FOUND;
    }
    t = q * m->dfa.alphabet + input[i];
    if (m->dfa.delta[t] == AU_NONE) {
      return DS_NOT_FOUND;
    }
    out[i] = m->output[t];
    q = m->dfa.delta[t];
    if (out_count != NULL) {
      *out_count = i + 1;
    }
  }
  return DS_OK;
}

ds_status_t ds_moore_init(ds_moore_t *m, size_t alphabet,
                          ds_context_t *context) {
  if (m == NULL) {
    return DS_ERR_NULL;
  }
  m->output = NULL;
  m->output_capacity = 0;
  return ds_dfa_init(&m->dfa, alphabet, context);
}

void ds_moore_destroy(ds_moore_t *m) {
  if (m == NULL) {
    return;
  }
  ds_context_free(m->dfa.context, m->output);
  m->output = NULL;
  ds_dfa_destroy(&m->dfa);
}

ds_status_t ds_moore_add_state(ds_moore_t *m, size_t output,
                               size_t *out_state) {
  ds_status_t st;
  size_t *g;
  if (m == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_dfa_add_state(&m->dfa, false, out_state);
  if (st != DS_OK) {
    return st;
  }
  if (m->dfa.capacity > m->output_capacity) {
    g = (size_t *)au_realloc(m->dfa.context, m->output, m->dfa.capacity,
                             sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    m->output = g;
    m->output_capacity = m->dfa.capacity;
  }
  m->output[m->dfa.states - 1] = output;
  return DS_OK;
}

ds_status_t ds_moore_set_transition(ds_moore_t *m, size_t from, size_t symbol,
                                    size_t to) {
  if (m == NULL) {
    return DS_ERR_NULL;
  }
  return ds_dfa_set_transition(&m->dfa, from, symbol, to);
}

ds_status_t ds_moore_run(const ds_moore_t *m, const size_t *input, size_t n,
                         size_t *out, size_t *out_count) {
  size_t q;
  size_t i;
  if (m == NULL || out == NULL || (n > 0 && input == NULL)) {
    return DS_ERR_NULL;
  }
  if (out_count != NULL) {
    *out_count = 0;
  }
  if (m->dfa.states == 0) {
    return DS_NOT_FOUND;
  }
  q = m->dfa.start;
  out[0] = m->output[q];
  if (out_count != NULL) {
    *out_count = 1;
  }
  for (i = 0; i < n; i++) {
    if (input[i] >= m->dfa.alphabet ||
        m->dfa.delta[q * m->dfa.alphabet + input[i]] == AU_NONE) {
      return DS_NOT_FOUND;
    }
    q = m->dfa.delta[q * m->dfa.alphabet + input[i]];
    out[i + 1] = m->output[q];
    if (out_count != NULL) {
      *out_count = i + 2;
    }
  }
  return DS_OK;
}

ds_status_t ds_moore_to_mealy(const ds_moore_t *m, ds_mealy_t *out,
                              ds_context_t *context) {
  size_t i;
  size_t a;
  size_t t;
  ds_status_t st;
  if (m == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_mealy_init(out, m->dfa.alphabet, context);
  for (i = 0; i < m->dfa.states && st == DS_OK; i++) {
    st = ds_mealy_add_state(out, NULL);
  }
  for (i = 0; i < m->dfa.states && st == DS_OK; i++) {
    for (a = 0; a < m->dfa.alphabet && st == DS_OK; a++) {
      t = m->dfa.delta[i * m->dfa.alphabet + a];
      if (t != AU_NONE) {
        st = ds_mealy_set_transition(out, i, a, t, m->output[t]);
      }
    }
  }
  if (st == DS_OK && m->dfa.states > 0) {
    out->dfa.start = m->dfa.start;
  }
  if (st != DS_OK) {
    ds_mealy_destroy(out);
  }
  return st;
}

/* Moore state for (q, o), created when new (linear lookup: conversions
 * are small). */
static ds_status_t mm_get(ds_moore_t *out, size_t **pq, size_t **po,
                          size_t *cap, size_t q, size_t o, size_t *out_id,
                          ds_context_t *ctx) {
  size_t j;
  size_t id;
  size_t *g;
  ds_status_t st;
  for (j = 0; j < out->dfa.states; j++) {
    if ((*pq)[j] == q && (*po)[j] == o) {
      *out_id = j;
      return DS_OK;
    }
  }
  st = ds_moore_add_state(out, o, &id);
  if (st != DS_OK) {
    return st;
  }
  if (id >= *cap) {
    *cap = *cap < 16 ? 16 : *cap * 2;
    g = (size_t *)au_realloc(ctx, *pq, *cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    *pq = g;
    g = (size_t *)au_realloc(ctx, *po, *cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    *po = g;
  }
  (*pq)[id] = q;
  (*po)[id] = o;
  *out_id = id;
  return DS_OK;
}

ds_status_t ds_mealy_to_moore(const ds_mealy_t *m, size_t initial_output,
                              ds_moore_t *out, ds_context_t *context) {
  size_t *pq = NULL;
  size_t *po = NULL;
  size_t cap = 0;
  size_t s;
  size_t a;
  size_t t;
  size_t id;
  size_t k;
  ds_status_t st;
  if (m == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  k = m->dfa.alphabet;
  st = ds_moore_init(out, k, context);
  if (st != DS_OK || m->dfa.states == 0) {
    return st;
  }
  st = mm_get(out, &pq, &po, &cap, m->dfa.start, initial_output, &id, context);
  for (s = 0; st == DS_OK && s < out->dfa.states; s++) {
    for (a = 0; a < k && st == DS_OK; a++) {
      t = pq[s] * k + a;
      if (m->dfa.delta[t] == AU_NONE) {
        continue;
      }
      st = mm_get(out, &pq, &po, &cap, m->dfa.delta[t], m->output[t], &id,
                  context);
      if (st == DS_OK) {
        out->dfa.delta[s * k + a] = id;
      }
    }
  }
  ds_context_free(context, pq);
  ds_context_free(context, po);
  if (st != DS_OK) {
    ds_moore_destroy(out);
  }
  return st;
}

/* ---- finite state transducer ---------------------------------------- */

ds_status_t ds_fst_init(ds_fst_t *t, size_t alphabet, ds_context_t *context) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  t->out_start = NULL;
  t->out_len = NULL;
  t->final_start = NULL;
  t->final_len = NULL;
  t->pool = NULL;
  t->pool_len = 0;
  t->pool_capacity = 0;
  t->capacity = 0;
  t->state_capacity = 0;
  return ds_dfa_init(&t->dfa, alphabet, context);
}

void ds_fst_destroy(ds_fst_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->dfa.context, t->out_start);
  ds_context_free(t->dfa.context, t->final_start);
  ds_context_free(t->dfa.context, t->pool);
  t->out_start = NULL;
  t->final_start = NULL;
  t->pool = NULL;
  ds_dfa_destroy(&t->dfa);
}

static ds_status_t fst_pool_add(ds_fst_t *t, const size_t *s, size_t len,
                                size_t *out_start) {
  size_t *g;
  size_t cap;
  if (len > 0 && s == NULL) {
    return DS_ERR_NULL;
  }
  if (t->pool_len + len > t->pool_capacity) {
    cap = t->pool_capacity < 16 ? 16 : t->pool_capacity * 2;
    while (cap < t->pool_len + len) {
      cap *= 2;
    }
    g = (size_t *)au_realloc(t->dfa.context, t->pool, cap, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    t->pool = g;
    t->pool_capacity = cap;
  }
  *out_start = t->pool_len;
  if (len > 0) {
    memcpy(t->pool + t->pool_len, s, len * sizeof(size_t));
  }
  t->pool_len += len;
  return DS_OK;
}

ds_status_t ds_fst_add_state(ds_fst_t *t, bool final,
                             const size_t *final_output, size_t final_len,
                             size_t *out_state) {
  ds_status_t st;
  size_t id;
  size_t i;
  size_t *g;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_dfa_add_state(&t->dfa, final, &id);
  if (st != DS_OK) {
    return st;
  }
  if (t->dfa.capacity > t->capacity) {
    size_t tr = t->dfa.capacity * t->dfa.alphabet;
    g = (size_t *)au_realloc(t->dfa.context, NULL, 2 * tr, sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    if (t->out_start != NULL) {
      memcpy(g, t->out_start, id * t->dfa.alphabet * sizeof(size_t));
      memcpy(g + tr, t->out_len, id * t->dfa.alphabet * sizeof(size_t));
      ds_context_free(t->dfa.context, t->out_start);
    }
    t->out_start = g;
    t->out_len = g + tr;
    g = (size_t *)au_realloc(t->dfa.context, NULL, 2 * t->dfa.capacity,
                             sizeof(size_t));
    if (g == NULL) {
      return DS_ERR_ALLOC;
    }
    if (t->final_start != NULL) {
      memcpy(g, t->final_start, id * sizeof(size_t));
      memcpy(g + t->dfa.capacity, t->final_len, id * sizeof(size_t));
      ds_context_free(t->dfa.context, t->final_start);
    }
    t->final_start = g;
    t->final_len = g + t->dfa.capacity;
    t->capacity = t->dfa.capacity;
  }
  for (i = 0; i < t->dfa.alphabet; i++) {
    t->out_start[id * t->dfa.alphabet + i] = 0;
    t->out_len[id * t->dfa.alphabet + i] = 0;
  }
  t->final_len[id] = final ? final_len : 0;
  st =
      fst_pool_add(t, final_output, final ? final_len : 0, &t->final_start[id]);
  if (st == DS_OK && out_state != NULL) {
    *out_state = id;
  }
  return st;
}

ds_status_t ds_fst_set_transition(ds_fst_t *t, size_t from, size_t symbol,
                                  size_t to, const size_t *output,
                                  size_t output_len) {
  ds_status_t st;
  size_t idx;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_dfa_set_transition(&t->dfa, from, symbol, to);
  if (st != DS_OK) {
    return st;
  }
  idx = from * t->dfa.alphabet + symbol;
  t->out_len[idx] = output_len;
  return fst_pool_add(t, output, output_len, &t->out_start[idx]);
}

ds_status_t ds_fst_run(const ds_fst_t *t, const size_t *input, size_t n,
                       size_t *out, size_t capacity, size_t *out_count) {
  size_t q;
  size_t i;
  size_t j;
  size_t idx;
  size_t o = 0;
  if (t == NULL || out_count == NULL || (n > 0 && input == NULL)) {
    return DS_ERR_NULL;
  }
  *out_count = 0;
  if (t->dfa.states == 0) {
    return DS_NOT_FOUND;
  }
  q = t->dfa.start;
  for (i = 0; i < n; i++) {
    if (input[i] >= t->dfa.alphabet) {
      return DS_NOT_FOUND;
    }
    idx = q * t->dfa.alphabet + input[i];
    if (t->dfa.delta[idx] == AU_NONE) {
      return DS_NOT_FOUND;
    }
    for (j = 0; j < t->out_len[idx]; j++) {
      if (o < capacity && out != NULL) {
        out[o] = t->pool[t->out_start[idx] + j];
      }
      o++;
    }
    q = t->dfa.delta[idx];
  }
  if (!t->dfa.accepting[q]) {
    return DS_NOT_FOUND;
  }
  for (j = 0; j < t->final_len[q]; j++) {
    if (o < capacity && out != NULL) {
      out[o] = t->pool[t->final_start[q] + j];
    }
    o++;
  }
  *out_count = o;
  return o > capacity ? DS_ERR_OVERFLOW : DS_OK;
}
