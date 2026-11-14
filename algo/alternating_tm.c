#include "alternating_tm.h"
#include "game.h"
#include "hash.h"
#include <string.h>

#define AT_BEYOND ((size_t) - 1)
#define AT_DEFAULT_CONFIGS ((size_t)1 << 20)
#define AT_DEFAULT_BYTES ((size_t)1 << 26)

/* the configurations, encoded back to back: the state, then per tape the
 * head's offset in its window, the window's width and its cells */
typedef struct at_store {
  unsigned char *bytes;
  size_t used;
  size_t cap;
  size_t *at; /* at[i]: where configuration i starts; at[count] == used */
  size_t at_cap;
  size_t count;
  ds_context_t *context;
} at_store_t;

static unsigned long at_hash(const void *key, void *user) {
  const at_store_t *s = (const at_store_t *)user;
  size_t id;
  memcpy(&id, key, sizeof(id));
  return ds_hash_xxh32(s->bytes + s->at[id], s->at[id + 1] - s->at[id],
                       0x9E3779B1ul);
}

static bool at_equal(const void *a, const void *b, void *user) {
  const at_store_t *s = (const at_store_t *)user;
  size_t x;
  size_t y;
  memcpy(&x, a, sizeof(x));
  memcpy(&y, b, sizeof(y));
  return s->at[x + 1] - s->at[x] == s->at[y + 1] - s->at[y] &&
         memcmp(s->bytes + s->at[x], s->bytes + s->at[y],
                s->at[x + 1] - s->at[x]) == 0;
}

/* room for `more` bytes past the used ones and the slot of a candidate */
static ds_status_t at_reserve(at_store_t *s, size_t more) {
  if (s->used > (size_t)-1 - more) {
    return DS_ERR_OVERFLOW;
  }
  if (s->used + more > s->cap) {
    size_t cap = s->cap < 256 ? 256 : s->cap;
    unsigned char *b;
    while (cap < s->used + more) {
      if (cap > (size_t)-1 / 2) {
        return DS_ERR_OVERFLOW;
      }
      cap *= 2;
    }
    b = (unsigned char *)ds_context_realloc(s->context, s->bytes, cap);
    if (b == NULL) {
      return DS_ERR_ALLOC;
    }
    s->bytes = b;
    s->cap = cap;
  }
  if (s->count + 2 > s->at_cap) {
    size_t cap = s->at_cap < 64 ? 64 : s->at_cap;
    size_t *a;
    while (cap < s->count + 2) {
      cap *= 2;
    }
    if (cap > (size_t)-1 / sizeof(size_t)) {
      return DS_ERR_OVERFLOW;
    }
    a = (size_t *)ds_context_realloc(s->context, s->at, cap * sizeof(size_t));
    if (a == NULL) {
      return DS_ERR_ALLOC;
    }
    s->at = a;
    s->at_cap = cap;
  }
  return DS_OK;
}

static void at_put(at_store_t *s, const void *p, size_t n) {
  memcpy(s->bytes + s->used, p, n);
  s->used += n;
}

/* one tape of a candidate: cells[0 .. width) with the head at `head`,
 * which may lie one cell outside (-1 or width); appended trimmed to its
 * window. *out_width: the window's width */
static ds_status_t at_put_tape(at_store_t *s, const unsigned char *cells,
                               size_t width, long head, unsigned char blank,
                               size_t *out_width) {
  long lo = head;
  long hi = head;
  long i;
  size_t w;
  size_t off;
  ds_status_t st;
  for (i = 0; i < (long)width; i++) {
    if (cells[i] != blank) {
      if (i < lo) {
        lo = i;
      }
      break;
    }
  }
  for (i = (long)width - 1; i >= 0; i--) {
    if (cells[i] != blank) {
      if (i > hi) {
        hi = i;
      }
      break;
    }
  }
  w = (size_t)(hi - lo) + 1;
  off = (size_t)(head - lo);
  st = at_reserve(s, 2 * sizeof(size_t) + w);
  if (st != DS_OK) {
    return st;
  }
  at_put(s, &off, sizeof(off));
  at_put(s, &w, sizeof(w));
  for (i = lo; i <= hi; i++) {
    s->bytes[s->used++] = i >= 0 && i < (long)width ? cells[i] : blank;
  }
  *out_width = w;
  return DS_OK;
}

/* the candidate (bytes past at[count]) as a configuration: its id, a new
 * one, or AT_BEYOND past the limits */
static ds_status_t at_intern(at_store_t *s, ds_map_t *map, size_t max_configs,
                             size_t max_bytes, bool too_wide, size_t *out_id) {
  size_t cand = s->count;
  size_t *hit;
  ds_status_t st;
  s->at[cand + 1] = s->used;
  if (too_wide) {
    s->used = s->at[cand];
    *out_id = AT_BEYOND;
    return DS_OK;
  }
  hit = (size_t *)ds_map_get(map, &cand);
  if (hit != NULL) {
    s->used = s->at[cand];
    *out_id = *hit;
    return DS_OK;
  }
  if (s->count == max_configs || s->used > max_bytes) {
    s->used = s->at[cand];
    *out_id = AT_BEYOND;
    return DS_OK;
  }
  st = ds_map_put(map, &cand, &cand);
  if (st != DS_OK) {
    s->used = s->at[cand];
    return st;
  }
  s->count++;
  *out_id = cand;
  return DS_OK;
}

typedef struct at_graph {
  size_t *offset; /* per explored configuration, then the sentinel */
  size_t *succ;
  size_t edges;
  size_t edge_cap;
  size_t offset_cap;
} at_graph_t;

static ds_status_t at_edge(at_graph_t *g, ds_context_t *ctx, size_t to) {
  if (g->edges == g->edge_cap) {
    size_t cap = g->edge_cap < 64 ? 64 : g->edge_cap * 2;
    size_t *p;
    if (cap > (size_t)-1 / sizeof(size_t)) {
      return DS_ERR_OVERFLOW;
    }
    p = (size_t *)ds_context_realloc(ctx, g->succ, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    g->succ = p;
    g->edge_cap = cap;
  }
  g->succ[g->edges++] = to;
  return DS_OK;
}

static ds_status_t at_offset(at_graph_t *g, ds_context_t *ctx, size_t i,
                             size_t value) {
  if (i >= g->offset_cap) {
    size_t cap = g->offset_cap < 64 ? 64 : g->offset_cap * 2;
    size_t *p;
    while (cap <= i) {
      cap *= 2;
    }
    if (cap > (size_t)-1 / sizeof(size_t)) {
      return DS_ERR_OVERFLOW;
    }
    p = (size_t *)ds_context_realloc(ctx, g->offset, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    g->offset = p;
    g->offset_cap = cap;
  }
  g->offset[i] = value;
  return DS_OK;
}

/* a decoded configuration: views into a private copy of its bytes */
typedef struct at_config {
  size_t state;
  size_t head[DS_TM_MAX_TAPES];
  size_t width[DS_TM_MAX_TAPES];
  const unsigned char *cells[DS_TM_MAX_TAPES];
} at_config_t;

static void at_decode(const unsigned char *p, size_t tapes, at_config_t *c) {
  size_t i;
  memcpy(&c->state, p, sizeof(size_t));
  p += sizeof(size_t);
  for (i = 0; i < tapes; i++) {
    memcpy(&c->head[i], p, sizeof(size_t));
    memcpy(&c->width[i], p + sizeof(size_t), sizeof(size_t));
    c->cells[i] = p + 2 * sizeof(size_t);
    p += 2 * sizeof(size_t) + c->width[i];
  }
}

/* the successor of c by transition tr into the candidate slot */
static ds_status_t at_successor(at_store_t *s, const ds_tm_t *tm,
                                const at_config_t *c, size_t tr,
                                unsigned char *scratch, size_t space,
                                bool *too_wide) {
  size_t i;
  ds_status_t st = at_reserve(s, sizeof(size_t));
  *too_wide = false;
  if (st != DS_OK) {
    return st;
  }
  at_put(s, &tm->to[tr], sizeof(size_t));
  for (i = 0; i < tm->tapes && st == DS_OK; i++) {
    long head = (long)c->head[i];
    size_t w = 0;
    memcpy(scratch, c->cells[i], c->width[i]);
    scratch[c->head[i]] = tm->write[tr * tm->tapes + i];
    switch ((ds_tm_move_t)tm->move[tr * tm->tapes + i]) {
    case DS_TM_LEFT:
      head--;
      break;
    case DS_TM_RIGHT:
      head++;
      break;
    case DS_TM_STAY:
      break;
    }
    st = at_put_tape(s, scratch, c->width[i], head, tm->blank, &w);
    if (space > 0 && w > space) {
      *too_wide = true;
    }
  }
  return st;
}

static unsigned char at_kind(const ds_tm_t *tm, size_t state) {
  switch ((ds_tm_state_kind_t)tm->kind[state]) {
  case DS_TM_ACCEPTING:
    return DS_GAME_TRUE;
  case DS_TM_REJECTING:
  case DS_TM_HALTING:
    return DS_GAME_FALSE;
  case DS_TM_UNIVERSAL:
    return DS_GAME_AND;
  case DS_TM_NORMAL:
    break;
  }
  return DS_GAME_OR;
}

void ds_atm_options_init(ds_atm_options_t *options) {
  if (options != NULL) {
    options->space = 0;
    options->max_configs = 0;
    options->max_bytes = 0;
    options->map = NULL;
  }
}

/* a buffer of at least n bytes */
static ds_status_t at_buffer(ds_context_t *ctx, unsigned char **p, size_t *cap,
                             size_t n) {
  if (n > *cap) {
    unsigned char *q = (unsigned char *)ds_context_realloc(ctx, *p, n);
    if (q == NULL) {
      return DS_ERR_ALLOC;
    }
    *p = q;
    *cap = n;
  }
  return DS_OK;
}

typedef struct at_run {
  const ds_tm_t *tm;
  at_store_t store;
  at_graph_t graph;
  ds_map_t map;
  unsigned char *copy; /* the configuration being expanded */
  size_t copy_cap;
  unsigned char *scratch; /* one of its tapes, being rewritten */
  size_t scratch_cap;
  size_t space;
  size_t max_configs;
  size_t max_bytes;
  size_t beyond;
} at_run_t;

/* every stored configuration's successors, in id order (breadth first
 * from the start, id 0) */
static ds_status_t at_explore(at_run_t *r) {
  at_store_t *s = &r->store;
  const ds_tm_t *tm = r->tm;
  size_t id;
  ds_status_t st = DS_OK;
  for (id = 0; id < s->count && st == DS_OK; id++) {
    at_config_t c;
    unsigned char read[DS_TM_MAX_TAPES];
    unsigned char kind;
    size_t first = 0;
    size_t k;
    size_t j;
    size_t widest = 0;
    size_t len = s->at[id + 1] - s->at[id];
    st = at_offset(&r->graph, s->context, id, r->graph.edges);
    if (st == DS_OK) {
      st = at_buffer(s->context, &r->copy, &r->copy_cap, len);
    }
    if (st != DS_OK) {
      break;
    }
    memcpy(r->copy, s->bytes + s->at[id], len);
    at_decode(r->copy, tm->tapes, &c);
    kind = at_kind(tm, c.state);
    if (kind == DS_GAME_TRUE || kind == DS_GAME_FALSE) {
      continue;
    }
    for (j = 0; j < tm->tapes; j++) {
      read[j] = c.cells[j][c.head[j]];
      if (c.width[j] > widest) {
        widest = c.width[j];
      }
    }
    st = at_buffer(s->context, &r->scratch, &r->scratch_cap, widest);
    k = ds_tm_transitions(tm, c.state, read, &first);
    for (j = 0; j < k && st == DS_OK; j++) {
      bool too_wide = false;
      size_t to = AT_BEYOND;
      st = at_successor(s, tm, &c, first + j, r->scratch, r->space, &too_wide);
      if (st == DS_OK) {
        st = at_intern(s, &r->map, r->max_configs, r->max_bytes, too_wide, &to);
      } else {
        s->used = s->at[s->count]; /* drop the partial candidate */
      }
      if (st == DS_OK) {
        st = at_edge(&r->graph, s->context, to);
        r->beyond += to == AT_BEYOND;
      }
    }
  }
  return st;
}

/* the start configuration as id 0, unless it is past the limits */
static ds_status_t at_start(at_run_t *r, const unsigned char *input,
                            size_t length) {
  at_store_t *s = &r->store;
  const ds_tm_t *tm = r->tm;
  bool too_wide = false;
  size_t i;
  size_t id = 0;
  ds_status_t st = at_reserve(s, sizeof(size_t));
  if (st != DS_OK) {
    return st;
  }
  s->at[0] = 0;
  at_put(s, &tm->start, sizeof(size_t));
  for (i = 0; i < tm->tapes && st == DS_OK; i++) {
    unsigned char b = tm->blank;
    size_t w = 0;
    st = i == 0 && length > 0 ? at_put_tape(s, input, length, 0, tm->blank, &w)
                              : at_put_tape(s, &b, 1, 0, tm->blank, &w);
    if (r->space > 0 && w > r->space) {
      too_wide = true;
    }
  }
  if (st != DS_OK) {
    s->used = 0;
    return st;
  }
  return at_intern(s, &r->map, r->max_configs, r->max_bytes, too_wide, &id);
}

/* acceptance over the explored graph, the configurations past the limits
 * (one sentinel node) taken as `rest` */
static ds_status_t at_solve(at_run_t *r, unsigned char *kind, size_t *rank,
                            unsigned char rest) {
  size_t n = r->store.count;
  size_t e;
  kind[n] = rest;
  for (e = 0; e < r->graph.edges; e++) {
    if (r->graph.succ[e] == AT_BEYOND) {
      r->graph.succ[e] = n;
    }
  }
  return ds_game_attractor(n + 1, kind, r->graph.offset, r->graph.succ, rank,
                           NULL, r->store.context);
}

static void at_free(at_run_t *r) {
  ds_context_t *ctx = r->store.context;
  ds_map_destroy(&r->map);
  ds_context_free(ctx, r->store.bytes);
  ds_context_free(ctx, r->store.at);
  ds_context_free(ctx, r->graph.offset);
  ds_context_free(ctx, r->graph.succ);
  ds_context_free(ctx, r->copy);
  ds_context_free(ctx, r->scratch);
}

ds_status_t ds_atm_accepts(ds_tm_t *tm, const unsigned char *input,
                           size_t length, const ds_atm_options_t *options,
                           ds_tm_outcome_t *out, ds_atm_stats_t *stats) {
  ds_atm_options_t o;
  at_run_t r;
  unsigned char *kind = NULL;
  size_t *rank = NULL;
  size_t n;
  size_t i;
  ds_tm_outcome_t result = DS_TM_LIMIT;
  ds_status_t st;
  if (tm == NULL || out == NULL || (input == NULL && length > 0)) {
    return DS_ERR_NULL;
  }
  if (tm->states == 0 || tm->kind == NULL) {
    return DS_ERR_STATE;
  }
  for (i = 0; i < length; i++) {
    if (input[i] >= tm->symbols) {
      return DS_ERR_RANGE;
    }
  }
  if (options != NULL) {
    o = *options;
  } else {
    ds_atm_options_init(&o);
  }
  if (o.max_configs == 0) {
    o.max_configs = AT_DEFAULT_CONFIGS;
  }
  if (o.max_bytes == 0) {
    o.max_bytes = AT_DEFAULT_BYTES;
  }
  if (o.max_configs > (size_t)-1 / 4 / sizeof(size_t)) {
    return DS_ERR_INVALID;
  }
  memset(&r, 0, sizeof(r));
  r.tm = tm;
  r.store.context = tm->context;
  r.space = o.space;
  r.max_configs = o.max_configs;
  r.max_bytes = o.max_bytes;
  st = ds_map_init(&r.map, o.map, sizeof(size_t), sizeof(size_t), at_hash,
                   at_equal, &r.store, tm->context);
  if (st != DS_OK) {
    return st;
  }
  st = at_start(&r, input, length);
  if (st == DS_OK && r.store.count == 0) { /* the start is past the limits */
    r.beyond = 1;
  }
  if (st == DS_OK && r.store.count > 0) {
    st = at_explore(&r);
  }
  if (st == DS_OK && r.store.count > 0) {
    n = r.store.count;
    st = at_offset(&r.graph, tm->context, n, r.graph.edges);
    if (st == DS_OK) {
      st = at_offset(&r.graph, tm->context, n + 1, r.graph.edges);
    }
    if (st == DS_OK) {
      kind = (unsigned char *)ds_context_alloc(tm->context, n + 1);
      rank = (size_t *)ds_context_alloc(tm->context, (n + 1) * sizeof(size_t));
      if (kind == NULL || rank == NULL) {
        st = DS_ERR_ALLOC;
      }
    }
    if (st == DS_OK) {
      for (i = 0; i < n; i++) {
        size_t state;
        memcpy(&state, r.store.bytes + r.store.at[i], sizeof(size_t));
        kind[i] = at_kind(tm, state);
      }
      st = at_solve(&r, kind, rank, DS_GAME_FALSE);
    }
    if (st == DS_OK) {
      if (rank[0] != DS_GAME_LOSE) {
        result = DS_TM_ACCEPT;
      } else if (r.beyond == 0) {
        result = DS_TM_REJECT;
      } else {
        st = at_solve(&r, kind, rank, DS_GAME_TRUE);
        if (st == DS_OK && rank[0] == DS_GAME_LOSE) {
          result = DS_TM_REJECT;
        }
      }
    }
  }
  if (st == DS_OK) {
    *out = result;
    if (stats != NULL) {
      stats->configs = r.store.count;
      stats->transitions = r.graph.edges;
      stats->beyond = r.beyond;
      stats->depth = result == DS_TM_ACCEPT ? rank[0] : 0;
    }
  }
  ds_context_free(tm->context, kind);
  ds_context_free(tm->context, rank);
  at_free(&r);
  return st;
}
