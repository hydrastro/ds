#include "turing_machine.h"
#include <string.h>

/* ----------------------------------------------------------- machine */

ds_status_t ds_tm_init(ds_tm_t *tm, size_t states, unsigned symbols,
                       size_t tapes, unsigned char blank, size_t start,
                       ds_context_t *context) {
  size_t i;
  size_t pw = 1;
  if (tm == NULL) {
    return DS_ERR_NULL;
  }
  memset(tm, 0, sizeof(*tm));
  if (states == 0 || symbols == 0 || symbols > 256 || tapes == 0 ||
      tapes > DS_TM_MAX_TAPES || blank >= symbols || start >= states) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < tapes; i++) {
    if (pw > (size_t)-1 / symbols) {
      return DS_ERR_OVERFLOW;
    }
    pw *= symbols;
  }
  tm->kind = (unsigned char *)ds_context_calloc(context, states, 1);
  if (tm->kind == NULL) {
    return DS_ERR_ALLOC;
  }
  tm->states = states;
  tm->tapes = tapes;
  tm->symbols = symbols;
  tm->blank = blank;
  tm->start = start;
  tm->sorted = true;
  tm->context = context;
  return DS_OK;
}

void ds_tm_destroy(ds_tm_t *tm) {
  if (tm == NULL) {
    return;
  }
  ds_context_free(tm->context, tm->kind);
  ds_context_free(tm->context, tm->from);
  ds_context_free(tm->context, tm->to);
  ds_context_free(tm->context, tm->key);
  ds_context_free(tm->context, tm->read);
  ds_context_free(tm->context, tm->write);
  ds_context_free(tm->context, tm->move);
  memset(tm, 0, sizeof(*tm));
}

ds_status_t ds_tm_set_kind(ds_tm_t *tm, size_t state, ds_tm_state_kind_t kind) {
  if (tm == NULL) {
    return DS_ERR_NULL;
  }
  if (state >= tm->states) {
    return DS_ERR_RANGE;
  }
  tm->kind[state] = (unsigned char)kind;
  return DS_OK;
}

static size_t tm_key(const ds_tm_t *tm, const unsigned char *read) {
  size_t k = 0;
  size_t i;
  for (i = tm->tapes; i-- > 0;) {
    k = k * tm->symbols + read[i];
  }
  return k;
}

/* first transition index with (from, key) >= (state, key) */
static size_t tm_lower(const ds_tm_t *tm, size_t state, size_t key) {
  size_t lo = 0;
  size_t hi = tm->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (tm->from[mid] < state ||
        (tm->from[mid] == state && tm->key[mid] < key)) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

static ds_status_t tm_grow(ds_context_t *ctx, void **p, size_t bytes) {
  void *q = ds_context_realloc(ctx, *p, bytes);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *p = q;
  return DS_OK;
}

ds_status_t ds_tm_add(ds_tm_t *tm, size_t from, const unsigned char *read,
                      size_t to, const unsigned char *write,
                      const ds_tm_move_t *move) {
  size_t k;
  size_t key;
  size_t at;
  size_t i;
  size_t n;
  if (tm == NULL || read == NULL || write == NULL || move == NULL) {
    return DS_ERR_NULL;
  }
  k = tm->tapes;
  if (from >= tm->states || to >= tm->states) {
    return DS_ERR_RANGE;
  }
  for (i = 0; i < k; i++) {
    if (read[i] >= tm->symbols || write[i] >= tm->symbols ||
        (move[i] != DS_TM_LEFT && move[i] != DS_TM_RIGHT &&
         move[i] != DS_TM_STAY)) {
      return DS_ERR_RANGE;
    }
  }
  if (tm->count == tm->capacity) {
    size_t nc = tm->capacity < 8 ? 16 : tm->capacity * 2;
    if (tm_grow(tm->context, (void **)&tm->from, nc * sizeof(size_t)) ||
        tm_grow(tm->context, (void **)&tm->to, nc * sizeof(size_t)) ||
        tm_grow(tm->context, (void **)&tm->key, nc * sizeof(size_t)) ||
        tm_grow(tm->context, (void **)&tm->read, nc * k) ||
        tm_grow(tm->context, (void **)&tm->write, nc * k) ||
        tm_grow(tm->context, (void **)&tm->move, nc * k)) {
      return DS_ERR_ALLOC;
    }
    tm->capacity = nc;
  }
  key = tm_key(tm, read);
  /* insert after every transition with an equal (from, key) */
  at = tm_lower(tm, from, key);
  while (at < tm->count && tm->from[at] == from && tm->key[at] == key) {
    at++;
  }
  n = tm->count - at;
  memmove(tm->from + at + 1, tm->from + at, n * sizeof(size_t));
  memmove(tm->to + at + 1, tm->to + at, n * sizeof(size_t));
  memmove(tm->key + at + 1, tm->key + at, n * sizeof(size_t));
  memmove(tm->read + (at + 1) * k, tm->read + at * k, n * k);
  memmove(tm->write + (at + 1) * k, tm->write + at * k, n * k);
  memmove(tm->move + (at + 1) * k, tm->move + at * k, n * k);
  tm->from[at] = from;
  tm->to[at] = to;
  tm->key[at] = key;
  for (i = 0; i < k; i++) {
    tm->read[at * k + i] = read[i];
    tm->write[at * k + i] = write[i];
    tm->move[at * k + i] = (unsigned char)move[i];
  }
  tm->count++;
  return DS_OK;
}

ds_status_t ds_tm_add1(ds_tm_t *tm, size_t from, unsigned char read, size_t to,
                       unsigned char write, ds_tm_move_t move) {
  if (tm != NULL && tm->tapes != 1) {
    return DS_ERR_STATE;
  }
  return ds_tm_add(tm, from, &read, to, &write, &move);
}

bool ds_tm_is_deterministic(ds_tm_t *tm) {
  size_t i;
  if (tm == NULL) {
    return false;
  }
  for (i = 1; i < tm->count; i++) {
    if (tm->from[i] == tm->from[i - 1] && tm->key[i] == tm->key[i - 1]) {
      return false;
    }
  }
  return true;
}

ds_status_t ds_tm_parse(ds_tm_t *tm, const char *text, ds_context_t *context) {
  size_t n = 1;
  size_t m;
  size_t len;
  size_t s;
  size_t i;
  const char *p;
  ds_status_t st;
  if (tm == NULL || text == NULL) {
    return DS_ERR_NULL;
  }
  for (p = text; *p != '\0'; p++) {
    if (*p == '_') {
      n++;
    }
  }
  p = strchr(text, '_');
  len = p == NULL ? strlen(text) : (size_t)(p - text);
  if (len == 0 || len % 3 != 0 || n > 25) {
    return DS_ERR_INVALID;
  }
  m = len / 3;
  if (m > 10) {
    return DS_ERR_INVALID;
  }
  st = ds_tm_init(tm, n + 1, (unsigned)m, 1, 0, 0, context);
  if (st != DS_OK) {
    return st;
  }
  tm->kind[n] = DS_TM_HALTING;
  p = text;
  for (s = 0; s < n; s++) {
    for (i = 0; i < m; i++) {
      char w;
      char mv;
      char nx;
      size_t to;
      ds_tm_move_t move;
      if (p[0] == '\0' || p[1] == '\0' || p[2] == '\0') {
        st = DS_ERR_INVALID;
        goto fail;
      }
      w = p[0];
      mv = p[1];
      nx = p[2];
      p += 3;
      if (w == '-' && mv == '-' && nx == '-') {
        continue;
      }
      if (w < '0' || (size_t)(w - '0') >= m) {
        st = DS_ERR_INVALID;
        goto fail;
      }
      if (mv == 'L') {
        move = DS_TM_LEFT;
      } else if (mv == 'R') {
        move = DS_TM_RIGHT;
      } else if (mv == 'S' || mv == 'N') {
        move = DS_TM_STAY;
      } else {
        st = DS_ERR_INVALID;
        goto fail;
      }
      if (nx == 'Z' || (nx == 'H' && n <= 7)) {
        to = n;
      } else if (nx >= 'A' && (size_t)(nx - 'A') < n) {
        to = (size_t)(nx - 'A');
      } else {
        st = DS_ERR_INVALID;
        goto fail;
      }
      st = ds_tm_add1(tm, s, (unsigned char)i, to, (unsigned char)(w - '0'),
                      move);
      if (st != DS_OK) {
        goto fail;
      }
    }
    if (*p != (s + 1 == n ? '\0' : '_')) {
      st = DS_ERR_INVALID;
      goto fail;
    }
    if (*p == '_') {
      p++;
    }
  }
  return DS_OK;
fail:
  ds_tm_destroy(tm);
  return st;
}

ds_status_t ds_tm_busy_beaver(ds_tm_t *tm, size_t n, ds_context_t *context) {
  static const char *const champion[5] = {
      "1RZ---", "1RB1LB_1LA1RZ", "1RB1RZ_1LB0RC_1LC1LA",
      "1RB1LB_1LA0LC_1RZ1LD_1RD0RA", "1RB1LC_1RC1RB_1RD0LE_1LA1LD_1RZ0LA"};
  if (tm == NULL) {
    return DS_ERR_NULL;
  }
  if (n == 0 || n > 5) {
    return DS_ERR_RANGE;
  }
  return ds_tm_parse(tm, champion[n - 1], context);
}

/* -------------------------------------------------------------- tapes */

static unsigned char tm_cell(const ds_tm_tape_t *t, long pos,
                             unsigned char blank) {
  long i = pos - t->base;
  if (i < 0 || (size_t)i >= t->capacity) {
    return blank;
  }
  return t->cells[i];
}

static ds_status_t tm_ensure(ds_context_t *ctx, ds_tm_tape_t *t, long pos,
                             unsigned char blank) {
  long i = pos - t->base;
  size_t nc;
  size_t shift;
  unsigned char *c;
  if (t->capacity > 0 && i >= 0 && (size_t)i < t->capacity) {
    return DS_OK;
  }
  nc = t->capacity < 16 ? 32 : t->capacity * 2;
  if (t->capacity > 0) {
    size_t need = i < 0 ? t->capacity + (size_t)(-i) : (size_t)i + 1;
    while (nc < need) {
      if (nc > (size_t)-1 / 2) {
        return DS_ERR_OVERFLOW;
      }
      nc *= 2;
    }
  }
  c = (unsigned char *)ds_context_alloc(ctx, nc);
  if (c == NULL) {
    return DS_ERR_ALLOC;
  }
  memset(c, blank, nc);
  if (t->capacity == 0) {
    shift = nc / 2;
    t->base = pos - (long)shift;
  } else if (i < 0) {
    shift = nc - t->capacity; /* grow to the left */
    memcpy(c + shift, t->cells, t->capacity);
    t->base -= (long)shift;
  } else {
    memcpy(c, t->cells, t->capacity);
  }
  ds_context_free(ctx, t->cells);
  t->cells = c;
  t->capacity = nc;
  return DS_OK;
}

ds_status_t ds_tm_config_init(ds_tm_config_t *cfg, const ds_tm_t *tm,
                              const unsigned char *input, size_t length,
                              ds_context_t *context) {
  size_t i;
  ds_status_t st;
  if (cfg == NULL || tm == NULL || (length > 0 && input == NULL)) {
    return DS_ERR_NULL;
  }
  memset(cfg, 0, sizeof(*cfg));
  for (i = 0; i < length; i++) {
    if (input[i] >= tm->symbols) {
      return DS_ERR_RANGE;
    }
  }
  cfg->state = tm->start;
  cfg->tapes = tm->tapes;
  cfg->blank = tm->blank;
  cfg->outcome = DS_TM_RUNNING;
  cfg->context = context;
  if (length > 0) {
    ds_tm_tape_t *t = &cfg->tape[0];
    st = tm_ensure(context, t, 0, tm->blank);
    if (st == DS_OK) {
      st = tm_ensure(context, t, (long)length - 1, tm->blank);
    }
    if (st != DS_OK) {
      ds_tm_config_destroy(cfg);
      return st;
    }
    memcpy(t->cells + (0 - t->base), input, length);
  }
  return DS_OK;
}

ds_status_t ds_tm_config_copy(ds_tm_config_t *dst, const ds_tm_config_t *src) {
  size_t i;
  if (dst == NULL || src == NULL) {
    return DS_ERR_NULL;
  }
  *dst = *src;
  for (i = 0; i < src->tapes; i++) {
    dst->tape[i].cells = NULL;
  }
  for (i = 0; i < src->tapes; i++) {
    if (src->tape[i].capacity == 0) {
      continue;
    }
    dst->tape[i].cells =
        (unsigned char *)ds_context_alloc(src->context, src->tape[i].capacity);
    if (dst->tape[i].cells == NULL) {
      ds_tm_config_destroy(dst);
      return DS_ERR_ALLOC;
    }
    memcpy(dst->tape[i].cells, src->tape[i].cells, src->tape[i].capacity);
  }
  return DS_OK;
}

void ds_tm_config_destroy(ds_tm_config_t *cfg) {
  size_t i;
  if (cfg == NULL) {
    return;
  }
  for (i = 0; i < cfg->tapes; i++) {
    ds_context_free(cfg->context, cfg->tape[i].cells);
    cfg->tape[i].cells = NULL;
    cfg->tape[i].capacity = 0;
  }
}

unsigned char ds_tm_tape_get(const ds_tm_config_t *cfg, size_t tape, long pos) {
  if (cfg == NULL || tape >= cfg->tapes) {
    return 0;
  }
  return tm_cell(&cfg->tape[tape], pos, cfg->blank);
}

bool ds_tm_tape_extent(const ds_tm_config_t *cfg, size_t tape, long *lo,
                       long *hi) {
  const ds_tm_tape_t *t;
  size_t a;
  size_t b;
  if (cfg == NULL || tape >= cfg->tapes || lo == NULL || hi == NULL) {
    return false;
  }
  t = &cfg->tape[tape];
  for (a = 0; a < t->capacity && t->cells[a] == cfg->blank; a++) {
  }
  if (a == t->capacity) {
    return false;
  }
  for (b = t->capacity; t->cells[b - 1] == cfg->blank; b--) {
  }
  *lo = t->base + (long)a;
  *hi = t->base + (long)b - 1;
  return true;
}

size_t ds_tm_tape_count(const ds_tm_config_t *cfg, size_t tape,
                        unsigned char symbol) {
  size_t i;
  size_t n = 0;
  if (cfg == NULL || tape >= cfg->tapes || symbol == cfg->blank) {
    return 0;
  }
  for (i = 0; i < cfg->tape[tape].capacity; i++) {
    if (cfg->tape[tape].cells[i] == symbol) {
      n++;
    }
  }
  return n;
}

/* --------------------------------------------------------------- runs */

static size_t tm_read_key(const ds_tm_t *tm, const ds_tm_config_t *cfg) {
  size_t k = 0;
  size_t i;
  for (i = tm->tapes; i-- > 0;) {
    k = k * tm->symbols + tm_cell(&cfg->tape[i], cfg->tape[i].head, cfg->blank);
  }
  return k;
}

static bool tm_halted(const ds_tm_t *tm, ds_tm_config_t *cfg) {
  switch ((ds_tm_state_kind_t)tm->kind[cfg->state]) {
  case DS_TM_ACCEPTING:
    cfg->outcome = DS_TM_ACCEPT;
    return true;
  case DS_TM_REJECTING:
    cfg->outcome = DS_TM_REJECT;
    return true;
  case DS_TM_HALTING:
    cfg->outcome = DS_TM_HALT;
    return true;
  case DS_TM_NORMAL:
    break;
  }
  return false;
}

static ds_status_t tm_apply(const ds_tm_t *tm, ds_tm_config_t *cfg, size_t tr) {
  size_t i;
  for (i = 0; i < tm->tapes; i++) {
    ds_tm_tape_t *t = &cfg->tape[i];
    unsigned char w = tm->write[tr * tm->tapes + i];
    if (w != tm_cell(t, t->head, cfg->blank)) {
      ds_status_t st = tm_ensure(cfg->context, t, t->head, cfg->blank);
      if (st != DS_OK) {
        return st;
      }
      t->cells[t->head - t->base] = w;
    }
    switch ((ds_tm_move_t)tm->move[tr * tm->tapes + i]) {
    case DS_TM_LEFT:
      t->head--;
      break;
    case DS_TM_RIGHT:
      t->head++;
      break;
    case DS_TM_STAY:
      break;
    }
  }
  cfg->state = tm->to[tr];
  cfg->steps++;
  return DS_OK;
}

ds_status_t ds_tm_step(ds_tm_t *tm, ds_tm_config_t *cfg) {
  size_t key;
  size_t tr;
  ds_status_t st;
  if (tm == NULL || cfg == NULL) {
    return DS_ERR_NULL;
  }
  if (cfg->outcome != DS_TM_RUNNING && cfg->outcome != DS_TM_LIMIT) {
    return DS_ERR_STATE;
  }
  cfg->outcome = DS_TM_RUNNING;
  if (tm_halted(tm, cfg)) {
    return DS_OK;
  }
  key = tm_read_key(tm, cfg);
  tr = tm_lower(tm, cfg->state, key);
  if (tr == tm->count || tm->from[tr] != cfg->state || tm->key[tr] != key) {
    cfg->outcome = DS_TM_HALT;
    return DS_OK;
  }
  st = tm_apply(tm, cfg, tr);
  if (st == DS_OK) {
    (void)tm_halted(tm, cfg);
  }
  return st;
}

ds_status_t ds_tm_run(ds_tm_t *tm, ds_tm_config_t *cfg, unsigned long max_steps,
                      ds_tm_outcome_t *out) {
  unsigned long k;
  if (tm == NULL || cfg == NULL) {
    return DS_ERR_NULL;
  }
  if (cfg->outcome == DS_TM_RUNNING && tm_halted(tm, cfg)) {
    max_steps = 0;
  }
  for (k = 0; k < max_steps &&
              (cfg->outcome == DS_TM_RUNNING || cfg->outcome == DS_TM_LIMIT);
       k++) {
    ds_status_t st = ds_tm_step(tm, cfg);
    if (st != DS_OK) {
      return st;
    }
  }
  if (cfg->outcome == DS_TM_RUNNING) {
    cfg->outcome = DS_TM_LIMIT;
  }
  if (out != NULL) {
    *out = cfg->outcome;
  }
  return DS_OK;
}

ds_status_t ds_tm_accepts(ds_tm_t *tm, const unsigned char *input,
                          size_t length, unsigned long max_steps,
                          size_t max_configs, ds_tm_outcome_t *out) {
  ds_tm_config_t *cur;
  ds_tm_config_t *nxt;
  size_t ncur = 0;
  size_t nnxt;
  size_t i;
  unsigned long step;
  ds_tm_outcome_t result = DS_TM_REJECT;
  ds_status_t st;
  if (tm == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (max_configs == 0) {
    return DS_ERR_INVALID;
  }
  cur = (ds_tm_config_t *)ds_context_alloc(
      tm->context, 2 * max_configs * sizeof(ds_tm_config_t));
  if (cur == NULL) {
    return DS_ERR_ALLOC;
  }
  nxt = cur + max_configs;
  st = ds_tm_config_init(&cur[0], tm, input, length, tm->context);
  if (st != DS_OK) {
    ds_context_free(tm->context, cur);
    return st;
  }
  ncur = 1;
  for (step = 0;; step++) {
    bool overflow = false;
    nnxt = 0;
    for (i = 0; i < ncur && st == DS_OK; i++) {
      size_t key;
      size_t tr;
      if (tm_halted(tm, &cur[i])) {
        if (cur[i].outcome == DS_TM_ACCEPT) {
          result = DS_TM_ACCEPT;
        }
        continue;
      }
      if (step >= max_steps) {
        overflow = true; /* still running at the limit */
        continue;
      }
      key = tm_read_key(tm, &cur[i]);
      for (tr = tm_lower(tm, cur[i].state, key);
           tr < tm->count && tm->from[tr] == cur[i].state && tm->key[tr] == key;
           tr++) {
        if (nnxt == max_configs) {
          overflow = true;
          break;
        }
        st = ds_tm_config_copy(&nxt[nnxt], &cur[i]);
        if (st != DS_OK) {
          break;
        }
        nnxt++;
        st = tm_apply(tm, &nxt[nnxt - 1], tr);
        if (st != DS_OK) {
          break;
        }
      }
    }
    for (i = 0; i < ncur; i++) {
      ds_tm_config_destroy(&cur[i]);
    }
    if (st != DS_OK || result == DS_TM_ACCEPT || overflow || nnxt == 0) {
      for (i = 0; i < nnxt; i++) {
        ds_tm_config_destroy(&nxt[i]);
      }
      if (result != DS_TM_ACCEPT && overflow) {
        result = DS_TM_LIMIT;
      }
      break;
    }
    memcpy(cur, nxt, nnxt * sizeof(ds_tm_config_t));
    ncur = nnxt;
  }
  ds_context_free(tm->context, cur);
  if (st == DS_OK) {
    *out = result;
  }
  return st;
}

ds_status_t ds_tm_transduce(ds_tm_t *tm, const unsigned char *input,
                            size_t length, unsigned long max_steps,
                            unsigned char *out, size_t capacity,
                            size_t *out_length, ds_tm_outcome_t *outcome) {
  ds_tm_config_t cfg;
  long lo;
  long hi;
  long p;
  size_t len = 0;
  size_t last;
  ds_status_t st;
  if (tm == NULL || out_length == NULL || (capacity > 0 && out == NULL)) {
    return DS_ERR_NULL;
  }
  st = ds_tm_config_init(&cfg, tm, input, length, tm->context);
  if (st != DS_OK) {
    return st;
  }
  st = ds_tm_run(tm, &cfg, max_steps, outcome);
  last = tm->tapes - 1;
  if (st == DS_OK && ds_tm_tape_extent(&cfg, last, &lo, &hi)) {
    len = (size_t)(hi - lo + 1);
    for (p = lo; p <= hi && (size_t)(p - lo) < capacity; p++) {
      out[p - lo] = ds_tm_tape_get(&cfg, last, p);
    }
    if (len > capacity) {
      st = DS_ERR_RANGE;
    }
  }
  *out_length = len;
  ds_tm_config_destroy(&cfg);
  return st;
}

/* ------------------------------------------------------- busy beaver */

#define BB_UNDEF 0xFFu

/* run the partial machine; returns 0 halted, 1 limit, 2 missing
 * transition (state, symbol in *miss) */
static int bb_run(const unsigned char *table, size_t n, unsigned long max,
                  ds_tm_tape_t *t, ds_context_t *ctx, unsigned long *steps,
                  unsigned long *ones, size_t *miss, ds_status_t *st) {
  size_t state = 0;
  unsigned long k;
  size_t i;
  if (t->capacity > 0) {
    memset(t->cells, 0, t->capacity);
  }
  t->head = 0;
  for (k = 0; k < max; k++) {
    unsigned char sym = tm_cell(t, t->head, 0);
    unsigned char e = table[state * 2 + sym];
    unsigned char w;
    if (e == BB_UNDEF) {
      *miss = state * 2 + sym;
      return 2;
    }
    w = (unsigned char)(e & 1u);
    if (w != sym) {
      *st = tm_ensure(ctx, t, t->head, 0);
      if (*st != DS_OK) {
        return 1;
      }
      t->cells[t->head - t->base] = w;
    }
    t->head += (e & 2u) != 0 ? 1 : -1;
    state = (size_t)(e >> 2);
    if (state == n) {
      unsigned long c = 0;
      for (i = 0; i < t->capacity; i++) {
        c += t->cells[i];
      }
      *steps = k + 1;
      *ones = c;
      return 0;
    }
  }
  return 1;
}

ds_status_t ds_tm_busy_beaver_search(size_t n, unsigned long max_steps,
                                     unsigned long *sigma, unsigned long *steps,
                                     unsigned long *undecided,
                                     ds_context_t *context) {
  unsigned char *stack;
  size_t top = 0;
  size_t width;
  size_t cap;
  ds_tm_tape_t tape;
  unsigned long best_ones = 0;
  unsigned long best_steps = 0;
  unsigned long open = 0;
  ds_status_t st = DS_OK;
  if (sigma == NULL || steps == NULL) {
    return DS_ERR_NULL;
  }
  if (n == 0 || n > 6) {
    return DS_ERR_RANGE;
  }
  width = 2 * n;
  /* depth <= 2n, at most 4n + 1 children per level */
  cap = (2 * n + 1) * (4 * n + 1);
  stack = (unsigned char *)ds_context_alloc(context, cap * width);
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  memset(&tape, 0, sizeof(tape));
  memset(stack, BB_UNDEF, width);
  top = 1;
  while (top > 0 && st == DS_OK) {
    unsigned char table[12];
    unsigned long s = 0;
    unsigned long ones = 0;
    size_t miss = 0;
    int r;
    top--;
    memcpy(table, stack + top * width, width);
    r = bb_run(table, n, max_steps, &tape, context, &s, &ones, &miss, &st);
    if (st != DS_OK) {
      break;
    }
    if (r == 0) {
      if (ones > best_ones) {
        best_ones = ones;
      }
      if (s > best_steps) {
        best_steps = s;
      }
    } else if (r == 1) {
      open++;
    } else {
      unsigned w;
      unsigned mv;
      size_t nx;
      /* halt here (writing 1), or any (write, move, next state) */
      table[miss] = (unsigned char)(1u | 2u | (n << 2));
      memcpy(stack + top * width, table, width);
      top++;
      for (w = 0; w < 2; w++) {
        for (mv = 0; mv < 2; mv++) {
          for (nx = 0; nx < n; nx++) {
            table[miss] = (unsigned char)(w | (mv << 1) | (nx << 2));
            memcpy(stack + top * width, table, width);
            top++;
          }
        }
      }
    }
  }
  ds_context_free(context, tape.cells);
  ds_context_free(context, stack);
  if (st != DS_OK) {
    return st;
  }
  *sigma = best_ones;
  *steps = best_steps;
  if (undecided != NULL) {
    *undecided = open;
  }
  return DS_OK;
}
