#include "external_sort.h"
#include "kway_merge.h"
#include <limits.h>
#include <string.h>

#define AT(base, i, es) ((unsigned char *)(base) + (size_t)(i) * (es))
#define CAT(base, i, es) ((const unsigned char *)(base) + (size_t)(i) * (es))
#define EXT_SINK (~(size_t)0)

/* ---- memory tapes --------------------------------------------------- */

static ds_status_t mt_write(size_t tape, const void *elem, void *state) {
  ds_ext_memory_tapes_t *s = (ds_ext_memory_tapes_t *)state;
  ds_ext_memory_tape_t *t;
  size_t cap;
  unsigned char *grown;
  if (tape >= s->count) {
    return DS_ERR_RANGE;
  }
  t = &s->tapes[tape];
  if (t->length == t->capacity) {
    cap = t->capacity < 16 ? 16 : t->capacity * 2;
    if (cap > ((size_t)-1) / s->elem_size) {
      return DS_ERR_OVERFLOW;
    }
    grown = (unsigned char *)ds_context_realloc(s->context, t->data,
                                                cap * s->elem_size);
    if (grown == NULL) {
      return DS_ERR_ALLOC;
    }
    t->data = grown;
    t->capacity = cap;
  }
  memcpy(AT(t->data, t->length, s->elem_size), elem, s->elem_size);
  t->length++;
  return DS_OK;
}

static ds_status_t mt_read(size_t tape, void *out, void *state) {
  ds_ext_memory_tapes_t *s = (ds_ext_memory_tapes_t *)state;
  ds_ext_memory_tape_t *t;
  if (tape >= s->count) {
    return DS_ERR_RANGE;
  }
  t = &s->tapes[tape];
  if (t->position >= t->length) {
    return DS_NOT_FOUND;
  }
  memcpy(out, CAT(t->data, t->position, s->elem_size), s->elem_size);
  t->position++;
  return DS_OK;
}

static ds_status_t mt_read_backward(size_t tape, void *out, void *state) {
  ds_ext_memory_tapes_t *s = (ds_ext_memory_tapes_t *)state;
  ds_ext_memory_tape_t *t;
  if (tape >= s->count) {
    return DS_ERR_RANGE;
  }
  t = &s->tapes[tape];
  if (t->length == 0) {
    return DS_NOT_FOUND;
  }
  t->length--;
  memcpy(out, CAT(t->data, t->length, s->elem_size), s->elem_size);
  if (t->position > t->length) {
    t->position = t->length;
  }
  return DS_OK;
}

static ds_status_t mt_rewind(size_t tape, void *state) {
  ds_ext_memory_tapes_t *s = (ds_ext_memory_tapes_t *)state;
  if (tape >= s->count) {
    return DS_ERR_RANGE;
  }
  s->tapes[tape].position = 0;
  return DS_OK;
}

static ds_status_t mt_clear(size_t tape, void *state) {
  ds_ext_memory_tapes_t *s = (ds_ext_memory_tapes_t *)state;
  if (tape >= s->count) {
    return DS_ERR_RANGE;
  }
  s->tapes[tape].length = 0;
  s->tapes[tape].position = 0;
  return DS_OK;
}

const ds_ext_tape_ops_t ds_ext_memory_tape_ops = {
    mt_write, mt_read, mt_read_backward, mt_rewind, mt_clear};

ds_status_t ds_ext_memory_tapes_init(ds_ext_memory_tapes_t *tapes, size_t count,
                                     size_t elem_size, ds_context_t *context) {
  if (tapes == NULL) {
    return DS_ERR_NULL;
  }
  if (count == 0 || elem_size == 0) {
    return DS_ERR_INVALID;
  }
  tapes->tapes = (ds_ext_memory_tape_t *)ds_context_calloc(
      context, count, sizeof(ds_ext_memory_tape_t));
  if (tapes->tapes == NULL) {
    return DS_ERR_ALLOC;
  }
  tapes->count = count;
  tapes->elem_size = elem_size;
  tapes->context = context;
  return DS_OK;
}

void ds_ext_memory_tapes_destroy(ds_ext_memory_tapes_t *tapes) {
  size_t i;
  if (tapes == NULL || tapes->tapes == NULL) {
    return;
  }
  for (i = 0; i < tapes->count; i++) {
    ds_context_free(tapes->context, tapes->tapes[i].data);
  }
  ds_context_free(tapes->context, tapes->tapes);
  tapes->tapes = NULL;
  tapes->count = 0;
}

/* ---- FILE tapes ----------------------------------------------------- */

static ds_status_t ft_seek(const ds_ext_file_tapes_t *s, ds_ext_file_tape_t *t,
                           long index) {
  if ((unsigned long)index > (unsigned long)LONG_MAX / s->elem_size) {
    return DS_ERR_OVERFLOW;
  }
  if (fseek(t->file, index * (long)s->elem_size, SEEK_SET) != 0) {
    return DS_ERR_STATE;
  }
  return DS_OK;
}

static ds_status_t ft_write(size_t tape, const void *elem, void *state) {
  ds_ext_file_tapes_t *s = (ds_ext_file_tapes_t *)state;
  ds_ext_file_tape_t *t;
  ds_status_t st;
  if (tape >= s->count) {
    return DS_ERR_RANGE;
  }
  t = &s->tapes[tape];
  if (t->length == LONG_MAX) {
    return DS_ERR_OVERFLOW;
  }
  if (!t->writing) {
    st = ft_seek(s, t, t->length);
    if (st != DS_OK) {
      return st;
    }
    t->writing = 1;
  }
  if (fwrite(elem, s->elem_size, 1, t->file) != 1) {
    return DS_ERR_STATE;
  }
  t->length++;
  return DS_OK;
}

static ds_status_t ft_read(size_t tape, void *out, void *state) {
  ds_ext_file_tapes_t *s = (ds_ext_file_tapes_t *)state;
  ds_ext_file_tape_t *t;
  ds_status_t st;
  if (tape >= s->count) {
    return DS_ERR_RANGE;
  }
  t = &s->tapes[tape];
  if (t->position >= t->length) {
    return DS_NOT_FOUND;
  }
  st = ft_seek(s, t, t->position);
  if (st != DS_OK) {
    return st;
  }
  t->writing = 0;
  if (fread(out, s->elem_size, 1, t->file) != 1) {
    return DS_ERR_STATE;
  }
  t->position++;
  return DS_OK;
}

static ds_status_t ft_read_backward(size_t tape, void *out, void *state) {
  ds_ext_file_tapes_t *s = (ds_ext_file_tapes_t *)state;
  ds_ext_file_tape_t *t;
  ds_status_t st;
  if (tape >= s->count) {
    return DS_ERR_RANGE;
  }
  t = &s->tapes[tape];
  if (t->length == 0) {
    return DS_NOT_FOUND;
  }
  st = ft_seek(s, t, t->length - 1);
  if (st != DS_OK) {
    return st;
  }
  t->writing = 0;
  if (fread(out, s->elem_size, 1, t->file) != 1) {
    return DS_ERR_STATE;
  }
  t->length--;
  if (t->position > t->length) {
    t->position = t->length;
  }
  return DS_OK;
}

static ds_status_t ft_rewind(size_t tape, void *state) {
  ds_ext_file_tapes_t *s = (ds_ext_file_tapes_t *)state;
  if (tape >= s->count) {
    return DS_ERR_RANGE;
  }
  s->tapes[tape].position = 0;
  return DS_OK;
}

static ds_status_t ft_clear(size_t tape, void *state) {
  ds_ext_file_tapes_t *s = (ds_ext_file_tapes_t *)state;
  if (tape >= s->count) {
    return DS_ERR_RANGE;
  }
  s->tapes[tape].length = 0;
  s->tapes[tape].position = 0;
  s->tapes[tape].writing = 0;
  return DS_OK;
}

const ds_ext_tape_ops_t ds_ext_file_tape_ops = {
    ft_write, ft_read, ft_read_backward, ft_rewind, ft_clear};

ds_status_t ds_ext_file_tapes_init(ds_ext_file_tapes_t *tapes, size_t count,
                                   size_t elem_size, ds_context_t *context) {
  size_t i;
  if (tapes == NULL) {
    return DS_ERR_NULL;
  }
  if (count == 0 || elem_size == 0) {
    return DS_ERR_INVALID;
  }
  tapes->tapes = (ds_ext_file_tape_t *)ds_context_calloc(
      context, count, sizeof(ds_ext_file_tape_t));
  if (tapes->tapes == NULL) {
    return DS_ERR_ALLOC;
  }
  tapes->count = count;
  tapes->elem_size = elem_size;
  tapes->context = context;
  for (i = 0; i < count; i++) {
    tapes->tapes[i].file = tmpfile();
    if (tapes->tapes[i].file == NULL) {
      ds_ext_file_tapes_destroy(tapes);
      return DS_ERR_STATE;
    }
  }
  return DS_OK;
}

void ds_ext_file_tapes_destroy(ds_ext_file_tapes_t *tapes) {
  size_t i;
  if (tapes == NULL || tapes->tapes == NULL) {
    return;
  }
  for (i = 0; i < tapes->count; i++) {
    if (tapes->tapes[i].file != NULL) {
      fclose(tapes->tapes[i].file);
    }
  }
  ds_context_free(tapes->context, tapes->tapes);
  tapes->tapes = NULL;
  tapes->count = 0;
}

/* ---- run bookkeeping ------------------------------------------------ */

/* Run lengths on one tape: dummy runs (length 0, conceptually first),
 * then a queue (forward tapes) or a stack (oscillating) of real runs. */
typedef struct ext_runs {
  size_t *len;
  size_t head;
  size_t tail;
  size_t cap;
  size_t dummies;
  int fresh; /* written since the last rewind */
} ext_runs_t;

typedef struct ext_dist {
  size_t inputs;  /* tapes receiving runs: 0 .. inputs - 1 */
  size_t next;    /* round robin */
  size_t *target; /* perfect counts per tape at the current level */
  size_t *written;
  int cascade;
} ext_dist_t;

typedef struct ext {
  const ds_ext_sort_config_t *cfg;
  const ds_ext_tape_ops_t *ops;
  void *ts;
  size_t es;
  size_t tapes;
  ext_runs_t *runs;
  size_t *remaining; /* per player during a merge */
  size_t *players;   /* tape of each player */
  ds_tournament_t tour;
  int desc;     /* merging in descending order */
  int backward; /* reading backward */
  ds_ext_sink_func_t sink;
  void *sink_user;
  ds_ext_source_func_t source;
  void *source_user;
  int source_done;
  ds_ext_sort_stats_t stats;
  ext_dist_t dist;
  ds_context_t *ctx;
} ext_t;

static int ext_compare(const void *a, const void *b, void *user) {
  const ext_t *e = (const ext_t *)user;
  int r = e->cfg->compare(a, b, e->cfg->user);
  return e->desc ? -r : r;
}

static ds_status_t ext_runs_push(ext_t *e, size_t tape, size_t len) {
  ext_runs_t *r = &e->runs[tape];
  size_t *grown;
  size_t n;
  if (r->tail == r->cap) {
    n = r->tail - r->head;
    if (r->head > 0 && n < r->cap / 2) {
      memmove(r->len, r->len + r->head, n * sizeof(size_t));
      r->head = 0;
      r->tail = n;
    } else {
      size_t cap = r->cap < 16 ? 16 : r->cap * 2;
      grown =
          (size_t *)ds_context_realloc(e->ctx, r->len, cap * sizeof(size_t));
      if (grown == NULL) {
        return DS_ERR_ALLOC;
      }
      r->len = grown;
      r->cap = cap;
    }
  }
  r->len[r->tail++] = len;
  return DS_OK;
}

static size_t ext_runs_count(const ext_t *e, size_t tape) {
  return e->runs[tape].dummies + e->runs[tape].tail - e->runs[tape].head;
}

static size_t ext_runs_pop_front(ext_t *e, size_t tape) {
  ext_runs_t *r = &e->runs[tape];
  if (r->dummies > 0) {
    r->dummies--;
    return 0;
  }
  return r->len[r->head++];
}

static size_t ext_runs_pop_back(ext_t *e, size_t tape) {
  ext_runs_t *r = &e->runs[tape];
  if (r->tail > r->head) {
    return r->len[--r->tail];
  }
  r->dummies--;
  return 0;
}

static size_t ext_total_runs(const ext_t *e) {
  size_t i;
  size_t total = 0;
  for (i = 0; i < e->tapes; i++) {
    total += ext_runs_count(e, i);
  }
  return total;
}

static ds_status_t ext_prepare_read(ext_t *e, size_t tape) {
  if (e->runs[tape].fresh) {
    e->runs[tape].fresh = 0;
    return e->ops->rewind(tape, e->ts);
  }
  return DS_OK;
}

static ds_status_t ext_prepare_write(ext_t *e, size_t tape) {
  e->runs[tape].fresh = 1;
  return e->ops->clear(tape, e->ts);
}

static ds_status_t ext_emit(ext_t *e, size_t out, const void *elem) {
  if (out == EXT_SINK) {
    return e->sink(elem, e->sink_user);
  }
  e->stats.elements_written++;
  return e->ops->write(out, elem, e->ts);
}

static ds_status_t ext_read_from(ext_t *e, size_t tape, void *out) {
  ds_status_t st = e->backward ? e->ops->read_backward(tape, out, e->ts)
                               : e->ops->read(tape, out, e->ts);
  return st == DS_NOT_FOUND ? DS_ERR_STATE : st; /* tape shorter than run */
}

/* Merge the next run of each tape in `in` onto out (a tape or EXT_SINK).
 * Runs are taken from the front, or from the back when reading backward. */
static ds_status_t ext_merge(ext_t *e, const size_t *in, size_t n_in,
                             size_t out) {
  size_t p;
  size_t w;
  size_t total = 0;
  ds_status_t st;
  for (p = 0; p < e->tapes; p++) {
    ds_tournament_set_alive(&e->tour, p, false);
  }
  for (p = 0; p < n_in; p++) {
    e->players[p] = in[p];
    e->remaining[p] = e->backward ? ext_runs_pop_back(e, in[p])
                                  : ext_runs_pop_front(e, in[p]);
    total += e->remaining[p];
    if (e->remaining[p] > 0) {
      st = ext_read_from(e, in[p], ds_tournament_slot(&e->tour, p));
      if (st != DS_OK) {
        return st;
      }
      e->remaining[p]--;
      ds_tournament_set_alive(&e->tour, p, true);
    }
  }
  ds_tournament_build(&e->tour);
  for (;;) {
    w = ds_tournament_winner(&e->tour);
    if (w == DS_TOURNAMENT_NONE) {
      break;
    }
    st = ext_emit(e, out, ds_tournament_slot(&e->tour, w));
    if (st != DS_OK) {
      return st;
    }
    if (e->remaining[w] > 0) {
      st = ext_read_from(e, e->players[w], ds_tournament_slot(&e->tour, w));
      if (st != DS_OK) {
        return st;
      }
      e->remaining[w]--;
    } else {
      ds_tournament_set_alive(&e->tour, w, false);
    }
    ds_tournament_replay(&e->tour, w);
  }
  e->stats.merges++;
  if (out != EXT_SINK) {
    return ext_runs_push(e, out, total);
  }
  return DS_OK;
}

/* ---- run formation -------------------------------------------------- */

typedef ds_status_t (*ext_pick_func_t)(ext_t *e, size_t *out_tape);

static int ext_rev_compare(const void *a, const void *b, void *user) {
  const ext_t *e = (const ext_t *)user;
  return -e->cfg->compare(a, b, e->cfg->user);
}

/* Fill memory from the source: number of elements read. */
static ds_status_t ext_load(ext_t *e, unsigned char *mem, size_t cap,
                            size_t *out_n) {
  size_t n = 0;
  ds_status_t st = DS_OK;
  while (n < cap && !e->source_done) {
    st = e->source(AT(mem, n, e->es), e->source_user);
    if (st == DS_NOT_FOUND) {
      e->source_done = 1;
      st = DS_OK;
      break;
    }
    if (st != DS_OK) {
      return st;
    }
    n++;
  }
  *out_n = n;
  return st;
}

/* Runs by load-sort-store, written to the tapes chosen by pick. If the
 * whole input fits in memory it goes straight to the sink (*out_direct). */
static ds_status_t ext_runs_load_sort(ext_t *e, ext_pick_func_t pick,
                                      int *out_direct) {
  unsigned char *mem = (unsigned char *)e->cfg->memory;
  size_t cap = e->cfg->memory_size / e->es;
  size_t n;
  size_t i;
  size_t tape;
  int first = 1;
  ds_status_t st;
  *out_direct = 0;
  for (;;) {
    st = ext_load(e, mem, cap, &n);
    if (st != DS_OK) {
      return st;
    }
    if (n == 0) {
      break;
    }
    ds_intro_sort(mem, n, e->es, e->cfg->compare, e->cfg->user);
    if (first && e->source_done) {
      for (i = 0; i < n; i++) {
        st = e->sink(CAT(mem, i, e->es), e->sink_user);
        if (st != DS_OK) {
          return st;
        }
      }
      *out_direct = 1;
      return DS_OK;
    }
    first = 0;
    st = pick(e, &tape);
    if (st != DS_OK) {
      return st;
    }
    for (i = 0; i < n; i++) {
      st = ext_emit(e, tape, CAT(mem, i, e->es));
      if (st != DS_OK) {
        return st;
      }
    }
    st = ext_runs_push(e, tape, n);
    if (st != DS_OK) {
      return st;
    }
    e->stats.initial_runs++;
    if (e->source_done) {
      break;
    }
  }
  return DS_OK;
}

/* Replacement selection: a heap of (run tag, element). */
typedef struct ext_rs {
  ext_t *e;
  unsigned char *vals;
  size_t *tags;
  size_t n;
} ext_rs_t;

static int ext_rs_less(const ext_rs_t *h, size_t i, size_t j) {
  if (h->tags[i] != h->tags[j]) {
    return h->tags[i] < h->tags[j];
  }
  return h->e->cfg->compare(CAT(h->vals, i, h->e->es),
                            CAT(h->vals, j, h->e->es), h->e->cfg->user) < 0;
}

static void ext_rs_swap(ext_rs_t *h, size_t i, size_t j) {
  size_t t = h->tags[i];
  h->tags[i] = h->tags[j];
  h->tags[j] = t;
  ds_elem_swap(AT(h->vals, i, h->e->es), AT(h->vals, j, h->e->es), h->e->es);
}

static void ext_rs_down(ext_rs_t *h, size_t i) {
  size_t c;
  for (;;) {
    c = 2 * i + 1;
    if (c >= h->n) {
      return;
    }
    if (c + 1 < h->n && ext_rs_less(h, c + 1, c)) {
      c++;
    }
    if (!ext_rs_less(h, c, i)) {
      return;
    }
    ext_rs_swap(h, i, c);
    i = c;
  }
}

static ds_status_t ext_runs_replacement(ext_t *e, ext_pick_func_t pick,
                                        int *out_direct) {
  ext_rs_t h;
  size_t cap;
  size_t i;
  size_t cur_tag = 0;
  size_t run_len = 0;
  size_t tape = 0;
  unsigned char *last;
  ds_status_t st;
  *out_direct = 0;
  cap = e->cfg->memory_size / (e->es + sizeof(size_t));
  if (cap < 2) {
    return DS_ERR_INVALID;
  }
  cap--; /* one element slot keeps the last output */
  h.e = e;
  h.tags = (size_t *)e->cfg->memory;
  h.vals = (unsigned char *)(h.tags + cap + 1);
  last = AT(h.vals, cap, e->es);
  st = ext_load(e, h.vals, cap, &h.n);
  if (st != DS_OK) {
    return st;
  }
  if (h.n == 0) {
    return DS_OK;
  }
  if (e->source_done) {
    ds_intro_sort(h.vals, h.n, e->es, e->cfg->compare, e->cfg->user);
    for (i = 0; i < h.n; i++) {
      st = e->sink(CAT(h.vals, i, e->es), e->sink_user);
      if (st != DS_OK) {
        return st;
      }
    }
    *out_direct = 1;
    return DS_OK;
  }
  for (i = 0; i < h.n; i++) {
    h.tags[i] = 0;
  }
  for (i = h.n / 2; i-- > 0;) {
    ext_rs_down(&h, i);
  }
  while (h.n > 0) {
    if (h.tags[0] != cur_tag || run_len == 0) {
      if (run_len > 0) {
        st = ext_runs_push(e, tape, run_len);
        if (st != DS_OK) {
          return st;
        }
        e->stats.initial_runs++;
      }
      cur_tag = h.tags[0];
      run_len = 0;
      st = pick(e, &tape);
      if (st != DS_OK) {
        return st;
      }
    }
    memcpy(last, h.vals, e->es);
    st = ext_emit(e, tape, last);
    if (st != DS_OK) {
      return st;
    }
    run_len++;
    /* replace the root with the next input, or shrink the heap */
    st = e->source_done ? DS_NOT_FOUND : e->source(h.vals, e->source_user);
    if (st == DS_OK) {
      h.tags[0] = e->cfg->compare(h.vals, last, e->cfg->user) < 0 ? cur_tag + 1
                                                                  : cur_tag;
    } else if (st == DS_NOT_FOUND) {
      e->source_done = 1;
      h.n--;
      if (h.n > 0) {
        ext_rs_swap(&h, 0, h.n);
      }
    } else {
      return st;
    }
    ext_rs_down(&h, 0);
  }
  if (run_len > 0) {
    st = ext_runs_push(e, tape, run_len);
    if (st != DS_OK) {
      return st;
    }
    e->stats.initial_runs++;
  }
  return DS_OK;
}

static ds_status_t ext_form_runs(ext_t *e, ext_pick_func_t pick,
                                 int *out_direct) {
  if (e->cfg->runs == DS_EXT_RUNS_REPLACEMENT_SELECTION) {
    return ext_runs_replacement(e, pick, out_direct);
  }
  return ext_runs_load_sort(e, pick, out_direct);
}

/* ---- distribution policies ------------------------------------------ */

static ds_status_t ext_pick_round_robin(ext_t *e, size_t *out_tape) {
  ext_dist_t *d = &e->dist;
  *out_tape = d->next;
  d->next = (d->next + 1) % d->inputs;
  return DS_OK;
}

/* Next perfect level (targets kept in descending order, tape i gets
 * target[i]). */
static ds_status_t ext_next_level(ext_dist_t *d, size_t *scratch) {
  size_t p = d->inputs;
  size_t i;
  size_t j;
  size_t sum;
  for (i = 0; i < p; i++) {
    if (d->cascade) {
      sum = 0;
      for (j = 0; j + i < p; j++) {
        if (d->target[j] > ((size_t)-1) - sum) {
          return DS_ERR_OVERFLOW;
        }
        sum += d->target[j];
      }
      scratch[i] = sum;
    } else {
      if (i + 1 < p) {
        if (d->target[0] > ((size_t)-1) - d->target[i + 1]) {
          return DS_ERR_OVERFLOW;
        }
        scratch[i] = d->target[0] + d->target[i + 1];
      } else {
        scratch[i] = d->target[0];
      }
    }
  }
  memcpy(d->target, scratch, p * sizeof(size_t));
  return DS_OK;
}

static ds_status_t ext_pick_perfect(ext_t *e, size_t *out_tape) {
  ext_dist_t *d = &e->dist;
  size_t i;
  size_t best = 0;
  size_t best_def = 0;
  ds_status_t st;
  for (;;) {
    for (i = 0; i < d->inputs; i++) {
      if (d->target[i] - d->written[i] > best_def) {
        best_def = d->target[i] - d->written[i];
        best = i;
      }
    }
    if (best_def > 0) {
      break;
    }
    st = ext_next_level(d, d->target + d->inputs);
    if (st != DS_OK) {
      return st;
    }
  }
  d->written[best]++;
  *out_tape = best;
  return DS_OK;
}

/* Copy every run of `src` round robin onto the empty tapes other than
 * src, when a merge pattern ends up with all runs on one tape. */
static ds_status_t ext_redistribute(ext_t *e, size_t src) {
  size_t t;
  size_t len;
  size_t k;
  unsigned char *tmp = (unsigned char *)ds_tournament_slot(&e->tour, 0);
  ds_status_t st;
  st = ext_prepare_read(e, src);
  if (st != DS_OK) {
    return st;
  }
  for (t = 0; t < e->tapes; t++) {
    if (t != src && ext_runs_count(e, t) == 0) {
      st = ext_prepare_write(e, t);
      if (st != DS_OK) {
        return st;
      }
    }
  }
  t = src;
  while (ext_runs_count(e, src) > 0) {
    do {
      t = (t + 1) % e->tapes;
    } while (t == src);
    len = ext_runs_pop_front(e, src);
    for (k = 0; k < len; k++) {
      st = ext_read_from(e, src, tmp);
      if (st == DS_OK) {
        st = ext_emit(e, t, tmp);
      }
      if (st != DS_OK) {
        return st;
      }
    }
    st = ext_runs_push(e, t, len);
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
}

/* Nonempty tapes other than `except`; count returned. */
static size_t ext_nonempty(const ext_t *e, size_t except, size_t *out) {
  size_t t;
  size_t n = 0;
  for (t = 0; t < e->tapes; t++) {
    if (t != except && ext_runs_count(e, t) > 0) {
      out[n++] = t;
    }
  }
  return n;
}

/* Copy the single remaining run to the sink. */
static ds_status_t ext_drain_single(ext_t *e) {
  size_t t;
  size_t in[1];
  for (t = 0; t < e->tapes; t++) {
    if (ext_runs_count(e, t) > 0) {
      in[0] = t;
      if (ext_prepare_read(e, t) != DS_OK) {
        return DS_ERR_STATE;
      }
      return ext_merge(e, in, 1, EXT_SINK);
    }
  }
  return DS_OK;
}

static ds_status_t ext_balanced(ext_t *e) {
  size_t *in = e->players + e->tapes; /* scratch list */
  size_t *outs;
  size_t n_in;
  size_t n_out;
  size_t o;
  size_t t;
  size_t total;
  size_t k;
  size_t *active;
  ds_status_t st;
  outs = (size_t *)ds_context_alloc(e->ctx, 2 * e->tapes * sizeof(size_t));
  if (outs == NULL) {
    return DS_ERR_ALLOC;
  }
  active = outs + e->tapes;
  for (;;) {
    total = ext_total_runs(e);
    if (total <= 1) {
      st = ext_drain_single(e);
      break;
    }
    n_in = ext_nonempty(e, EXT_SINK, in);
    if (n_in == 1) {
      st = ext_redistribute(e, in[0]);
      if (st != DS_OK) {
        break;
      }
      continue;
    }
    n_out = 0;
    for (t = 0; t < e->tapes; t++) {
      if (ext_runs_count(e, t) == 0) {
        outs[n_out++] = t;
      }
    }
    for (t = 0; t < n_in; t++) {
      st = ext_prepare_read(e, in[t]);
      if (st != DS_OK) {
        goto done;
      }
    }
    for (t = 0; t < n_out; t++) {
      st = ext_prepare_write(e, outs[t]);
      if (st != DS_OK) {
        goto done;
      }
    }
    o = 0;
    st = DS_OK;
    for (;;) {
      k = 0;
      for (t = 0; t < n_in; t++) {
        if (ext_runs_count(e, in[t]) > 0) {
          active[k++] = in[t];
        }
      }
      if (k == 0) {
        break;
      }
      total = ext_total_runs(e);
      st = ext_merge(e, active, k, total == k ? EXT_SINK : outs[o]);
      if (st != DS_OK || total == k) {
        goto done;
      }
      o = (o + 1) % n_out;
    }
    e->stats.passes++;
  }
done:
  ds_context_free(e->ctx, outs);
  return st;
}

/* Polyphase and cascade over run queues: every stage merges all
 * nonempty input tapes onto the empty `out` tape until one of them
 * empties; polyphase then makes that tape the next output and continues,
 * cascade continues with one way fewer onto it until a single input is
 * left, which keeps its runs for the next pass. */
static ds_status_t ext_phases(ext_t *e, int cascade) {
  size_t *in = e->players + e->tapes;
  size_t n_in;
  size_t out = e->tapes - 1;
  size_t m;
  size_t t;
  size_t r;
  size_t total;
  ds_status_t st = DS_OK;
  for (;;) {
    total = ext_total_runs(e);
    if (total <= 1) {
      return ext_drain_single(e);
    }
    if (ext_runs_count(e, out) != 0) {
      for (t = 0; t < e->tapes; t++) {
        if (ext_runs_count(e, t) == 0) {
          out = t;
          break;
        }
      }
    }
    n_in = ext_nonempty(e, out, in);
    if (n_in <= 1) {
      st = ext_redistribute(e, in[0]);
      if (st != DS_OK) {
        return st;
      }
      continue;
    }
    /* one phase (polyphase) or one pass (cascade) */
    for (;;) {
      m = (size_t)-1;
      for (t = 0; t < n_in; t++) {
        st = ext_prepare_read(e, in[t]);
        if (st != DS_OK) {
          return st;
        }
        if (ext_runs_count(e, in[t]) < m) {
          m = ext_runs_count(e, in[t]);
        }
      }
      st = ext_prepare_write(e, out);
      if (st != DS_OK) {
        return st;
      }
      for (r = 0; r < m; r++) {
        total = ext_total_runs(e);
        st = ext_merge(e, in, n_in, total == n_in ? EXT_SINK : out);
        if (st != DS_OK || total == n_in) {
          return st;
        }
      }
      /* the first emptied input becomes the next output */
      for (t = 0; t < n_in; t++) {
        if (ext_runs_count(e, in[t]) == 0) {
          out = in[t];
          break;
        }
      }
      if (!cascade) {
        break;
      }
      n_in = 0;
      for (t = 0; t < e->tapes; t++) {
        if (t != out && ext_runs_count(e, t) > 0 && !e->runs[t].fresh) {
          in[n_in++] = t;
        }
      }
      if (n_in <= 1) {
        break; /* skip the one-way copy */
      }
    }
    e->stats.passes++;
  }
}

/* ---- oscillating sort ----------------------------------------------- */

static ds_status_t ext_osc_produce(ext_t *e, size_t level, size_t target,
                                   int desc, size_t *in) {
  size_t s;
  size_t n;
  size_t i;
  size_t n_in;
  unsigned char *mem = (unsigned char *)e->cfg->memory;
  size_t cap = e->cfg->memory_size / e->es;
  ds_status_t st;
  if (level == 0) {
    st = ext_load(e, mem, cap, &n);
    if (st != DS_OK) {
      return st;
    }
    if (desc) {
      ds_intro_sort(mem, n, e->es, ext_rev_compare, e);
    } else {
      ds_intro_sort(mem, n, e->es, e->cfg->compare, e->cfg->user);
    }
    for (i = 0; i < n; i++) {
      st = ext_emit(e, target, CAT(mem, i, e->es));
      if (st != DS_OK) {
        return st;
      }
    }
    if (n > 0) {
      e->stats.initial_runs++;
    }
    return ext_runs_push(e, target, n);
  }
  for (s = 0; s < e->tapes; s++) {
    if (s != target) {
      st = ext_osc_produce(e, level - 1, s, !desc, in);
      if (st != DS_OK) {
        return st;
      }
    }
  }
  /* the runs just written read back in `desc` order when read backward */
  n_in = 0;
  for (s = 0; s < e->tapes; s++) {
    if (s != target) {
      in[n_in++] = s;
    }
  }
  e->desc = desc;
  e->backward = 1;
  st = ext_merge(e, in, n_in, target);
  e->desc = 0;
  return st;
}

static ds_status_t ext_oscillating(ext_t *e, size_t *in) {
  size_t cap = e->cfg->memory_size / e->es;
  size_t ways = e->tapes - 1;
  size_t level = 0;
  size_t capacity; /* elements one top-level source run can hold */
  size_t s;
  ds_status_t st;
  if (e->cfg->count == DS_EXT_UNKNOWN || e->ops->read_backward == NULL) {
    return DS_ERR_UNSUPPORTED;
  }
  if (e->cfg->count <= cap) {
    size_t n;
    size_t i;
    unsigned char *mem = (unsigned char *)e->cfg->memory;
    st = ext_load(e, mem, cap, &n);
    if (st != DS_OK) {
      return st;
    }
    ds_intro_sort(mem, n, e->es, e->cfg->compare, e->cfg->user);
    for (i = 0; i < n; i++) {
      st = e->sink(CAT(mem, i, e->es), e->sink_user);
      if (st != DS_OK) {
        return st;
      }
    }
    if (!e->source_done && ext_load(e, mem, 1, &n) == DS_OK && n > 0) {
      return DS_ERR_RANGE;
    }
    return DS_OK;
  }
  /* smallest level with tapes * cap * ways^level >= count */
  capacity = cap;
  while (capacity <= ((size_t)-1) / e->tapes &&
         capacity * e->tapes < e->cfg->count) {
    if (capacity > ((size_t)-1) / ways) {
      break;
    }
    capacity *= ways;
    level++;
  }
  for (s = 0; s < e->tapes; s++) {
    /* top level: a descending run on every tape, merged to the sink */
    st = ext_osc_produce(e, level, s, 1, in);
    if (st != DS_OK) {
      return st;
    }
  }
  for (s = 0; s < e->tapes; s++) {
    in[s] = s;
  }
  e->desc = 0;
  e->backward = 1;
  st = ext_merge(e, in, e->tapes, EXT_SINK);
  e->backward = 0;
  if (st == DS_OK && !e->source_done) {
    size_t extra;
    unsigned char *mem = (unsigned char *)e->cfg->memory;
    /* the count was too small: report rather than lose data */
    if (ext_load(e, mem, 1, &extra) == DS_OK && extra > 0) {
      return DS_ERR_RANGE;
    }
  }
  return st;
}

ds_status_t ds_ext_sort(const ds_ext_sort_config_t *config,
                        ds_ext_source_func_t source, void *source_user,
                        ds_ext_sink_func_t sink, void *sink_user,
                        ds_ext_sort_stats_t *stats, ds_context_t *context) {
  ext_t e;
  ds_tournament_config_t tc;
  ext_dist_t *d;
  size_t t;
  size_t inputs;
  int direct = 0;
  ds_status_t st;
  if (config == NULL || source == NULL || sink == NULL ||
      config->compare == NULL || config->ops == NULL ||
      config->memory == NULL) {
    return DS_ERR_NULL;
  }
  if (config->elem_size == 0 || config->tapes < 3 ||
      config->memory_size / config->elem_size < 2 ||
      config->merge < DS_EXT_BALANCED || config->merge > DS_EXT_OSCILLATING) {
    return DS_ERR_INVALID;
  }
  memset(&e, 0, sizeof(e));
  e.cfg = config;
  e.ops = config->ops;
  e.ts = config->tape_state;
  e.es = config->elem_size;
  e.tapes = config->tapes;
  e.sink = sink;
  e.sink_user = sink_user;
  e.source = source;
  e.source_user = source_user;
  e.ctx = context;
  e.runs =
      (ext_runs_t *)ds_context_calloc(context, e.tapes, sizeof(ext_runs_t));
  /* remaining (tapes) | players (tapes) | input list (tapes) */
  e.remaining =
      (size_t *)ds_context_calloc(context, 3 * e.tapes, sizeof(size_t));
  if (e.runs == NULL || e.remaining == NULL) {
    ds_context_free(context, e.runs);
    ds_context_free(context, e.remaining);
    return DS_ERR_ALLOC;
  }
  e.players = e.remaining + e.tapes;
  tc.players = e.tapes;
  tc.elem_size = e.es;
  tc.compare = ext_compare;
  tc.user = &e;
  tc.kind = DS_TOURNAMENT_LOSER;
  st = ds_tournament_init(&e.tour, &tc, context);
  if (st != DS_OK) {
    ds_context_free(context, e.runs);
    ds_context_free(context, e.remaining);
    return st;
  }
  d = &e.dist;
  inputs = config->merge == DS_EXT_BALANCED ? (e.tapes >= 4 ? e.tapes / 2 : 2)
                                            : e.tapes - 1;
  d->inputs = inputs;
  d->next = 0;
  d->cascade = config->merge == DS_EXT_CASCADE;
  d->target = (size_t *)ds_context_calloc(context, 3 * e.tapes, sizeof(size_t));
  if (d->target == NULL) {
    st = DS_ERR_ALLOC;
    goto out;
  }
  d->written = d->target + 2 * e.tapes;
  for (t = 0; t < inputs; t++) {
    d->target[t] = 1;
  }
  for (t = 0; t < e.tapes; t++) {
    st = ext_prepare_write(&e, t);
    if (st != DS_OK) {
      goto out;
    }
  }
  if (config->merge == DS_EXT_OSCILLATING) {
    st = ext_oscillating(&e, e.players + e.tapes);
    goto out;
  }
  st = ext_form_runs(&e,
                     config->merge == DS_EXT_BALANCED ? ext_pick_round_robin
                                                      : ext_pick_perfect,
                     &direct);
  if (st != DS_OK || direct) {
    goto out;
  }
  if (config->merge != DS_EXT_BALANCED && e.stats.initial_runs > 1) {
    /* the unfilled part of the perfect distribution: dummy runs */
    for (t = 0; t < inputs; t++) {
      e.runs[t].dummies = d->target[t] - d->written[t];
      e.stats.dummy_runs += e.runs[t].dummies;
    }
  }
  if (config->merge == DS_EXT_BALANCED) {
    st = ext_balanced(&e);
  } else {
    st = ext_phases(&e, config->merge == DS_EXT_CASCADE);
  }
out:
  if (stats != NULL) {
    *stats = e.stats;
  }
  ds_tournament_destroy(&e.tour);
  for (t = 0; t < e.tapes; t++) {
    ds_context_free(context, e.runs[t].len);
  }
  ds_context_free(context, e.dist.target);
  ds_context_free(context, e.runs);
  ds_context_free(context, e.remaining);
  return st;
}

/* ---- external radix sort -------------------------------------------- */

ds_status_t ds_ext_radix_sort(size_t elem_size, ds_key_func_t key, void *user,
                              unsigned bits, const ds_ext_tape_ops_t *ops,
                              void *tape_state, ds_ext_source_func_t source,
                              void *source_user, ds_ext_sink_func_t sink,
                              void *sink_user) {
  union {
    long l;
    double d;
    void *p;
    unsigned char b[256];
  } local;
  unsigned char *tmp = local.b;
  size_t radix;
  size_t a; /* first tape of the current set */
  size_t b;
  size_t d;
  size_t digits = 1;
  size_t shift;
  size_t keybits = sizeof(unsigned long) * CHAR_BIT;
  unsigned long maxk = 0;
  unsigned long k;
  unsigned long mask;
  ds_status_t st;
  if (key == NULL || ops == NULL || source == NULL || sink == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 || elem_size > sizeof(local) || bits < 1 || bits > 8) {
    return elem_size > sizeof(local) ? DS_ERR_UNSUPPORTED : DS_ERR_INVALID;
  }
  radix = (size_t)1 << bits;
  mask = (unsigned long)radix - 1UL;
  for (d = 0; d < 2 * radix; d++) {
    st = ops->clear(d, tape_state);
    if (st != DS_OK) {
      return st;
    }
  }
  /* pass 0 from the source, by the lowest digit */
  for (;;) {
    st = source(tmp, source_user);
    if (st == DS_NOT_FOUND) {
      break;
    }
    if (st != DS_OK) {
      return st;
    }
    k = key(tmp, user);
    if (k > maxk) {
      maxk = k;
    }
    st = ops->write((size_t)(k & mask), tmp, tape_state);
    if (st != DS_OK) {
      return st;
    }
  }
  for (k = maxk >> bits; k != 0; k >>= bits) {
    digits++;
  }
  a = 0;
  b = radix;
  for (shift = bits; shift < digits * bits && shift < keybits; shift += bits) {
    for (d = 0; d < radix; d++) {
      st = ops->clear(b + d, tape_state);
      if (st != DS_OK) {
        return st;
      }
    }
    for (d = 0; d < radix; d++) {
      st = ops->rewind(a + d, tape_state);
      if (st != DS_OK) {
        return st;
      }
      for (;;) {
        st = ops->read(a + d, tmp, tape_state);
        if (st == DS_NOT_FOUND) {
          break;
        }
        if (st != DS_OK) {
          return st;
        }
        k = key(tmp, user);
        st = ops->write(b + (size_t)((k >> shift) & mask), tmp, tape_state);
        if (st != DS_OK) {
          return st;
        }
      }
    }
    d = a;
    a = b;
    b = d;
  }
  for (d = 0; d < radix; d++) {
    st = ops->rewind(a + d, tape_state);
    if (st != DS_OK) {
      return st;
    }
    for (;;) {
      st = ops->read(a + d, tmp, tape_state);
      if (st == DS_NOT_FOUND) {
        break;
      }
      if (st != DS_OK) {
        return st;
      }
      st = sink(tmp, sink_user);
      if (st != DS_OK) {
        return st;
      }
    }
  }
  return DS_OK;
}

/* ---- external quicksort --------------------------------------------- */

typedef struct eq {
  const ds_ext_array_ops_t *ops;
  void *state;
  size_t es;
  ds_compare_func_t compare;
  void *user;
  unsigned char *mid; /* sorted middle buffer */
  size_t cnt;
  size_t cap; /* M + 1 */
} eq_t;

static void eq_insert(eq_t *q, const void *x) {
  size_t lo = 0;
  size_t hi = q->cnt;
  size_t m;
  while (lo < hi) {
    m = lo + (hi - lo) / 2;
    if (q->compare(CAT(q->mid, m, q->es), x, q->user) <= 0) {
      lo = m + 1;
    } else {
      hi = m;
    }
  }
  memmove(AT(q->mid, lo + 1, q->es), CAT(q->mid, lo, q->es),
          (q->cnt - lo) * q->es);
  memcpy(AT(q->mid, lo, q->es), x, q->es);
  q->cnt++;
}

ds_status_t ds_ext_quicksort(const ds_ext_array_ops_t *ops, void *state,
                             size_t count, size_t elem_size,
                             ds_compare_func_t compare, void *user,
                             void *memory, size_t memory_size,
                             ds_context_t *context) {
  eq_t q;
  size_t *stack;
  size_t top;
  size_t scap;
  size_t lo;
  size_t hi;
  size_t rl;
  size_t rr;
  size_t wl;
  size_t wr;
  size_t i;
  size_t m;
  unsigned char *x;
  ds_status_t st = DS_OK;
  if (ops == NULL || compare == NULL || memory == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 || memory_size / elem_size < 4) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  q.ops = ops;
  q.state = state;
  q.es = elem_size;
  q.compare = compare;
  q.user = user;
  q.cap = memory_size / elem_size - 1; /* one slot for the element read */
  q.mid = (unsigned char *)memory;
  x = AT(memory, q.cap, elem_size);
  m = q.cap - 1; /* middle size during partitioning */
  scap = 64;
  stack = (size_t *)ds_context_alloc(context, 2 * scap * sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  stack[0] = 0;
  stack[1] = count;
  top = 1;
  while (top > 0 && st == DS_OK) {
    top--;
    lo = stack[2 * top];
    hi = stack[2 * top + 1];
    if (hi - lo <= q.cap) {
      for (i = lo; i < hi && st == DS_OK; i++) {
        st = ops->read(i, AT(q.mid, i - lo, elem_size), state);
      }
      if (st != DS_OK) {
        break;
      }
      ds_intro_sort(q.mid, hi - lo, elem_size, compare, user);
      for (i = lo; i < hi && st == DS_OK; i++) {
        st = ops->write(i, CAT(q.mid, i - lo, elem_size), state);
      }
      continue;
    }
    rl = lo;
    rr = hi;
    wl = lo;
    wr = hi;
    q.cnt = 0;
    /* fill the middle, reading from both ends */
    while (q.cnt < m && st == DS_OK) {
      if (rl - wl <= wr - rr) {
        st = ops->read(rl++, x, state);
      } else {
        st = ops->read(--rr, x, state);
      }
      if (st == DS_OK) {
        eq_insert(&q, x);
      }
    }
    while (rl < rr && st == DS_OK) {
      /* read from the side with less free space */
      if (rl - wl <= wr - rr) {
        st = ops->read(rl++, x, state);
      } else {
        st = ops->read(--rr, x, state);
      }
      if (st != DS_OK) {
        break;
      }
      if (rl > wl && compare(x, q.mid, user) < 0) {
        st = ops->write(wl++, x, state);
      } else if (wr > rr &&
                 compare(x, CAT(q.mid, q.cnt - 1, elem_size), user) > 0) {
        st = ops->write(--wr, x, state);
      } else {
        eq_insert(&q, x);
        if (rl - wl >= wr - rr) {
          st = ops->write(wl++, q.mid, state);
          memmove(q.mid, CAT(q.mid, 1, elem_size), (q.cnt - 1) * elem_size);
        } else {
          st = ops->write(--wr, CAT(q.mid, q.cnt - 1, elem_size), state);
        }
        q.cnt--;
      }
    }
    for (i = 0; i < q.cnt && st == DS_OK; i++) {
      st = ops->write(wl + i, CAT(q.mid, i, elem_size), state);
    }
    if (st != DS_OK) {
      break;
    }
    if (top + 2 > scap) {
      size_t *grown = (size_t *)ds_context_realloc(context, stack,
                                                   4 * scap * sizeof(size_t));
      if (grown == NULL) {
        st = DS_ERR_ALLOC;
        break;
      }
      stack = grown;
      scap *= 2;
    }
    if (wl - lo > 1) {
      stack[2 * top] = lo;
      stack[2 * top + 1] = wl;
      top++;
    }
    if (hi - wr > 1) {
      stack[2 * top] = wr;
      stack[2 * top + 1] = hi;
      top++;
    }
  }
  ds_context_free(context, stack);
  return st;
}
