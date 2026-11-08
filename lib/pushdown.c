#include "pushdown.h"
#include <string.h>

#define PD_NONE (~(size_t)0)
#define PD_EPS DS_PDA_EPSILON

ds_status_t ds_pda_init(ds_pda_t *pda, size_t states, size_t input_symbols,
                        size_t stack_symbols, size_t start, size_t start_stack,
                        ds_pda_acceptance_t acceptance, ds_context_t *context) {
  if (pda == NULL) {
    return DS_ERR_NULL;
  }
  memset(pda, 0, sizeof(*pda));
  if (states == 0 || stack_symbols == 0 || start >= states ||
      (start_stack != PD_EPS && start_stack >= stack_symbols) ||
      states > (size_t)-1 / 8 || stack_symbols > (size_t)-1 / 8) {
    return DS_ERR_INVALID;
  }
  pda->accepting = (bool *)ds_context_calloc(context, states, sizeof(bool));
  if (pda->accepting == NULL) {
    return DS_ERR_ALLOC;
  }
  pda->states = states;
  pda->input_symbols = input_symbols;
  pda->stack_symbols = stack_symbols;
  pda->start = start;
  pda->start_stack = start_stack;
  pda->acceptance = acceptance;
  pda->context = context;
  return DS_OK;
}

void ds_pda_destroy(ds_pda_t *pda) {
  if (pda == NULL) {
    return;
  }
  ds_context_free(pda->context, pda->accepting);
  ds_context_free(pda->context, pda->transitions);
  ds_context_free(pda->context, pda->push_pool);
  ds_context_free(pda->context, pda->output_pool);
  memset(pda, 0, sizeof(*pda));
}

ds_status_t ds_pda_set_accepting(ds_pda_t *pda, size_t state, bool accepting) {
  if (pda == NULL) {
    return DS_ERR_NULL;
  }
  if (state >= pda->states) {
    return DS_ERR_RANGE;
  }
  pda->accepting[state] = accepting;
  return DS_OK;
}

static ds_status_t pd_reserve(ds_context_t *ctx, void **p, size_t *cap,
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

ds_status_t ds_pda_add(ds_pda_t *pda, size_t from, size_t input, size_t pop,
                       size_t to, const size_t *push, size_t push_length,
                       const size_t *output, size_t output_length) {
  ds_pda_transition_t *t;
  size_t i;
  ds_status_t st;
  if (pda == NULL || (push_length > 0 && push == NULL) ||
      (output_length > 0 && output == NULL)) {
    return DS_ERR_NULL;
  }
  if (from >= pda->states || to >= pda->states ||
      (input != PD_EPS && input >= pda->input_symbols) ||
      (pop != PD_EPS && pop >= pda->stack_symbols)) {
    return DS_ERR_RANGE;
  }
  for (i = 0; i < push_length; i++) {
    if (push[i] >= pda->stack_symbols) {
      return DS_ERR_RANGE;
    }
  }
  if (push_length > (size_t)-1 - pda->push_used ||
      output_length > (size_t)-1 - pda->output_used) {
    return DS_ERR_OVERFLOW;
  }
  st = pd_reserve(pda->context, (void **)&pda->transitions, &pda->capacity,
                  pda->count + 1, sizeof(ds_pda_transition_t));
  if (st == DS_OK) {
    st = pd_reserve(pda->context, (void **)&pda->push_pool, &pda->push_capacity,
                    pda->push_used + push_length, sizeof(size_t));
  }
  if (st == DS_OK) {
    st = pd_reserve(pda->context, (void **)&pda->output_pool,
                    &pda->output_capacity, pda->output_used + output_length,
                    sizeof(size_t));
  }
  if (st != DS_OK) {
    return st;
  }
  t = &pda->transitions[pda->count++];
  t->from = from;
  t->input = input;
  t->pop = pop;
  t->to = to;
  t->push_offset = pda->push_used;
  t->push_length = push_length;
  t->output_offset = pda->output_used;
  t->output_length = output_length;
  if (push_length > 0) {
    memcpy(pda->push_pool + pda->push_used, push, push_length * sizeof(size_t));
  }
  if (output_length > 0) {
    memcpy(pda->output_pool + pda->output_used, output,
           output_length * sizeof(size_t));
  }
  pda->push_used += push_length;
  pda->output_used += output_length;
  return DS_OK;
}

bool ds_pda_is_deterministic(const ds_pda_t *pda) {
  size_t i;
  size_t j;
  if (pda == NULL) {
    return false;
  }
  for (i = 0; i < pda->count; i++) {
    const ds_pda_transition_t *a = &pda->transitions[i];
    for (j = i + 1; j < pda->count; j++) {
      const ds_pda_transition_t *b = &pda->transitions[j];
      if (a->from == b->from &&
          (a->pop == b->pop || a->pop == PD_EPS || b->pop == PD_EPS) &&
          (a->input == b->input || a->input == PD_EPS || b->input == PD_EPS)) {
        return false;
      }
    }
  }
  return true;
}

/* ------------------------------------------------- triple hash table */

typedef struct pd_hash {
  size_t *slot;
  size_t cap;  /* power of two */
  size_t *key; /* 3 per entry */
  size_t n;
  size_t key_cap;
  ds_context_t *ctx;
} pd_hash_t;

static size_t pd_mix(size_t a, size_t b, size_t c) {
  unsigned long h = 2166136261ul;
  h = (h ^ (unsigned long)a) * 16777619ul;
  h ^= h >> 13;
  h = (h ^ (unsigned long)b) * 16777619ul;
  h ^= h >> 13;
  h = (h ^ (unsigned long)c) * 16777619ul;
  h ^= h >> 15;
  return (size_t)h;
}

static ds_status_t pd_hash_rehash(pd_hash_t *h, size_t ncap) {
  size_t *s = (size_t *)ds_context_alloc(h->ctx, ncap * sizeof(size_t));
  size_t i;
  if (s == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < ncap; i++) {
    s[i] = PD_NONE;
  }
  for (i = 0; i < h->n; i++) {
    size_t at = pd_mix(h->key[3 * i], h->key[3 * i + 1], h->key[3 * i + 2]) &
                (ncap - 1);
    while (s[at] != PD_NONE) {
      at = (at + 1) & (ncap - 1);
    }
    s[at] = i;
  }
  ds_context_free(h->ctx, h->slot);
  h->slot = s;
  h->cap = ncap;
  return DS_OK;
}

/* index of (a, b, c), inserting it if new (*fresh says which) */
static ds_status_t pd_hash_get(pd_hash_t *h, size_t a, size_t b, size_t c,
                               size_t *out, bool *fresh) {
  size_t at;
  ds_status_t st;
  if (2 * (h->n + 1) > h->cap) {
    if (h->cap > (size_t)-1 / 4) {
      return DS_ERR_OVERFLOW;
    }
    st = pd_hash_rehash(h, h->cap < 16 ? 32 : h->cap * 2);
    if (st != DS_OK) {
      return st;
    }
  }
  at = pd_mix(a, b, c) & (h->cap - 1);
  while (h->slot[at] != PD_NONE) {
    size_t i = h->slot[at];
    if (h->key[3 * i] == a && h->key[3 * i + 1] == b &&
        h->key[3 * i + 2] == c) {
      *out = i;
      *fresh = false;
      return DS_OK;
    }
    at = (at + 1) & (h->cap - 1);
  }
  st = pd_reserve(h->ctx, (void **)&h->key, &h->key_cap, 3 * (h->n + 1),
                  sizeof(size_t));
  if (st != DS_OK) {
    return st;
  }
  h->key[3 * h->n] = a;
  h->key[3 * h->n + 1] = b;
  h->key[3 * h->n + 2] = c;
  h->slot[at] = h->n;
  *out = h->n++;
  *fresh = true;
  return DS_OK;
}

/* -------------------------------------------------------- membership */

typedef struct pd_norm {
  size_t from;
  size_t input;
  size_t to;
  size_t push; /* offset into the normalised pool */
  size_t length;
  size_t dot; /* first dot id of this transition */
} pd_norm_t;

typedef struct pd_solver {
  ds_context_t *ctx;
  const size_t *input;
  size_t n;
  size_t q;       /* augmented state count */
  size_t g;       /* augmented stack alphabet */
  pd_norm_t *tr;  /* bucketed by (from, pop) */
  size_t *bucket; /* q * g + 1 offsets */
  size_t *pool;
  size_t *dot_tr;    /* dot id -> transition */
  pd_hash_t calls;   /* (jr, Y, 0) */
  pd_hash_t results; /* (call, jr', 0) */
  pd_hash_t items;   /* (call, dot, jr) */
  /* per call: list heads */
  size_t *res_head;
  size_t *wait_head;
  size_t call_cap;
  /* per result / waiter: next links */
  size_t *res_next;
  size_t res_cap;
  size_t *wait_item;
  size_t *wait_next;
  size_t wait_n;
  size_t wait_cap;
  /* worklist: 2 * index + (0 item, 1 result) */
  size_t *work;
  size_t work_n;
  size_t work_cap;
} pd_solver_t;

static ds_status_t pd_push_work(pd_solver_t *s, size_t v) {
  ds_status_t st = pd_reserve(s->ctx, (void **)&s->work, &s->work_cap,
                              s->work_n + 1, sizeof(size_t));
  if (st == DS_OK) {
    s->work[s->work_n++] = v;
  }
  return st;
}

static ds_status_t pd_item(pd_solver_t *s, size_t call, size_t dot, size_t jr) {
  size_t idx;
  bool fresh;
  ds_status_t st = pd_hash_get(&s->items, call, dot, jr, &idx, &fresh);
  if (st != DS_OK || !fresh) {
    return st;
  }
  return pd_push_work(s, 2 * idx);
}

/* the call (jr, Y), created and predicted if new */
static ds_status_t pd_call(pd_solver_t *s, size_t jr, size_t y, size_t *out) {
  bool fresh;
  size_t j = jr / s->q;
  size_t r = jr % s->q;
  size_t k;
  ds_status_t st = pd_hash_get(&s->calls, jr, y, 0, out, &fresh);
  if (st != DS_OK || !fresh) {
    return st;
  }
  if (*out >= s->call_cap) {
    size_t cap = s->call_cap;
    st = pd_reserve(s->ctx, (void **)&s->res_head, &cap, *out + 1,
                    sizeof(size_t));
    if (st == DS_OK) {
      cap = s->call_cap;
      st = pd_reserve(s->ctx, (void **)&s->wait_head, &cap, *out + 1,
                      sizeof(size_t));
    }
    if (st != DS_OK) {
      return st;
    }
    s->call_cap = cap;
  }
  s->res_head[*out] = PD_NONE;
  s->wait_head[*out] = PD_NONE;
  for (k = s->bucket[r * s->g + y]; k < s->bucket[r * s->g + y + 1]; k++) {
    const pd_norm_t *t = &s->tr[k];
    size_t nj = j;
    if (t->input != PD_EPS) {
      if (j >= s->n || s->input[j] != t->input) {
        continue;
      }
      nj = j + 1;
    }
    st = pd_item(s, *out, t->dot, nj * s->q + t->to);
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
}

static ds_status_t pd_process_item(pd_solver_t *s, size_t idx) {
  size_t call = s->items.key[3 * idx];
  size_t dot = s->items.key[3 * idx + 1];
  size_t jr = s->items.key[3 * idx + 2];
  const pd_norm_t *t = &s->tr[s->dot_tr[dot]];
  size_t m = dot - t->dot;
  size_t sub;
  size_t r;
  ds_status_t st;
  if (m == t->length) {
    bool fresh;
    size_t ri;
    st = pd_hash_get(&s->results, call, jr, 0, &ri, &fresh);
    if (st != DS_OK || !fresh) {
      return st;
    }
    return pd_push_work(s, 2 * ri + 1);
  }
  st = pd_call(s, jr, s->pool[t->push + m], &sub);
  if (st != DS_OK) {
    return st;
  }
  {
    size_t cap = s->wait_cap;
    st = pd_reserve(s->ctx, (void **)&s->wait_item, &cap, s->wait_n + 1,
                    sizeof(size_t));
    if (st == DS_OK) {
      cap = s->wait_cap;
      st = pd_reserve(s->ctx, (void **)&s->wait_next, &cap, s->wait_n + 1,
                      sizeof(size_t));
    }
    if (st != DS_OK) {
      return st;
    }
    s->wait_cap = cap;
  }
  s->wait_item[s->wait_n] = idx;
  s->wait_next[s->wait_n] = s->wait_head[sub];
  s->wait_head[sub] = s->wait_n++;
  for (r = s->res_head[sub]; r != PD_NONE; r = s->res_next[r]) {
    st = pd_item(s, call, dot + 1, s->results.key[3 * r + 1]);
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
}

static ds_status_t pd_process_result(pd_solver_t *s, size_t ri) {
  size_t call = s->results.key[3 * ri];
  size_t jr = s->results.key[3 * ri + 1];
  size_t w;
  ds_status_t st = pd_reserve(s->ctx, (void **)&s->res_next, &s->res_cap,
                              ri + 1, sizeof(size_t));
  if (st != DS_OK) {
    return st;
  }
  s->res_next[ri] = s->res_head[call];
  s->res_head[call] = ri;
  for (w = s->wait_head[call]; w != PD_NONE; w = s->wait_next[w]) {
    size_t it = s->wait_item[w];
    st = pd_item(s, s->items.key[3 * it], s->items.key[3 * it + 1] + 1, jr);
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
}

/* Build the augmented automaton: every move pops exactly one symbol of
 * G + {bottom}, new states S0 (start), D (drain), E (end); acceptance
 * becomes "pop the bottom marker exactly at the end of the input". */
typedef struct pd_build {
  pd_norm_t *tr;
  size_t *pop;
  size_t count;
  size_t cap;
  size_t *pool;
  size_t used;
  size_t pool_cap;
  ds_context_t *ctx;
} pd_build_t;

static ds_status_t pd_emit(pd_build_t *b, size_t from, size_t input, size_t pop,
                           size_t to, const size_t *push, size_t length,
                           size_t extra) {
  size_t i;
  size_t total = length + (extra != PD_NONE ? 1 : 0);
  size_t cap = b->cap;
  ds_status_t st = pd_reserve(b->ctx, (void **)&b->tr, &cap, b->count + 1,
                              sizeof(pd_norm_t));
  if (st == DS_OK) {
    cap = b->cap;
    st = pd_reserve(b->ctx, (void **)&b->pop, &cap, b->count + 1,
                    sizeof(size_t));
  }
  if (st == DS_OK) {
    st = pd_reserve(b->ctx, (void **)&b->pool, &b->pool_cap, b->used + total,
                    sizeof(size_t));
  }
  if (st != DS_OK) {
    return st;
  }
  b->cap = cap;
  b->tr[b->count].from = from;
  b->tr[b->count].input = input;
  b->tr[b->count].to = to;
  b->tr[b->count].push = b->used;
  b->tr[b->count].length = total;
  b->pop[b->count] = pop;
  b->count++;
  for (i = 0; i < length; i++) {
    b->pool[b->used++] = push[i];
  }
  if (extra != PD_NONE) {
    b->pool[b->used++] = extra;
  }
  return DS_OK;
}

static ds_status_t pd_build(const ds_pda_t *pda, pd_build_t *b) {
  size_t n = pda->states;
  size_t bottom = pda->stack_symbols;
  size_t s0 = n;
  size_t d = n + 1;
  size_t e = n + 2;
  size_t i;
  size_t x;
  size_t init[2];
  ds_status_t st = DS_OK;
  for (i = 0; st == DS_OK && i < pda->count; i++) {
    const ds_pda_transition_t *t = &pda->transitions[i];
    const size_t *push = pda->push_pool + t->push_offset;
    if (t->pop != PD_EPS) {
      st = pd_emit(b, t->from, t->input, t->pop, t->to, push, t->push_length,
                   PD_NONE);
    } else {
      for (x = 0; st == DS_OK && x <= bottom; x++) {
        st = pd_emit(b, t->from, t->input, x, t->to, push, t->push_length, x);
      }
    }
  }
  init[0] = pda->start_stack;
  init[1] = bottom;
  if (st == DS_OK) {
    st = pda->start_stack == PD_EPS
             ? pd_emit(b, s0, PD_EPS, bottom, pda->start, init + 1, 1, PD_NONE)
             : pd_emit(b, s0, PD_EPS, bottom, pda->start, init, 2, PD_NONE);
  }
  for (i = 0; st == DS_OK && i < n; i++) {
    switch (pda->acceptance) {
    case DS_PDA_FINAL_STATE:
      for (x = 0; st == DS_OK && pda->accepting[i] && x <= bottom; x++) {
        st = pd_emit(b, i, PD_EPS, x, d, &x, 1, PD_NONE);
      }
      break;
    case DS_PDA_EMPTY_STACK:
      st = pd_emit(b, i, PD_EPS, bottom, e, NULL, 0, PD_NONE);
      break;
    case DS_PDA_FINAL_AND_EMPTY:
      if (pda->accepting[i]) {
        st = pd_emit(b, i, PD_EPS, bottom, e, NULL, 0, PD_NONE);
      }
      break;
    }
  }
  if (pda->acceptance == DS_PDA_FINAL_STATE) {
    for (x = 0; st == DS_OK && x < bottom; x++) {
      st = pd_emit(b, d, PD_EPS, x, d, NULL, 0, PD_NONE);
    }
    if (st == DS_OK) {
      st = pd_emit(b, d, PD_EPS, bottom, e, NULL, 0, PD_NONE);
    }
  }
  return st;
}

ds_status_t ds_pda_accepts(const ds_pda_t *pda, const size_t *input,
                           size_t length, bool *out_accepted) {
  pd_build_t b;
  pd_solver_t s;
  size_t i;
  size_t cells;
  size_t dots = 0;
  size_t start_call;
  size_t found;
  bool fresh;
  ds_status_t st;
  if (pda == NULL || out_accepted == NULL || (length > 0 && input == NULL)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < length; i++) {
    if (input[i] >= pda->input_symbols) {
      return DS_ERR_RANGE;
    }
  }
  memset(&b, 0, sizeof(b));
  memset(&s, 0, sizeof(s));
  b.ctx = pda->context;
  s.ctx = pda->context;
  s.calls.ctx = s.results.ctx = s.items.ctx = pda->context;
  s.input = input;
  s.n = length;
  s.q = pda->states + 3;
  s.g = pda->stack_symbols + 1;
  if (length + 1 > (size_t)-1 / s.q) {
    return DS_ERR_OVERFLOW;
  }
  st = pd_build(pda, &b);
  if (st != DS_OK) {
    goto done;
  }
  /* bucket by (from, pop) */
  cells = s.q * s.g;
  s.bucket =
      (size_t *)ds_context_calloc(pda->context, cells + 1, sizeof(size_t));
  s.tr = (pd_norm_t *)ds_context_alloc(pda->context,
                                       (b.count + 1) * sizeof(pd_norm_t));
  if (s.bucket == NULL || s.tr == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (i = 0; i < b.count; i++) {
    s.bucket[b.tr[i].from * s.g + b.pop[i] + 1]++;
  }
  for (i = 0; i < cells; i++) {
    s.bucket[i + 1] += s.bucket[i];
  }
  for (i = 0; i < b.count; i++) {
    size_t c = b.tr[i].from * s.g + b.pop[i];
    s.tr[s.bucket[c]++] = b.tr[i];
  }
  for (i = cells; i > 0; i--) {
    s.bucket[i] = s.bucket[i - 1];
  }
  s.bucket[0] = 0;
  s.pool = b.pool;
  for (i = 0; i < b.count; i++) {
    s.tr[i].dot = dots;
    dots += s.tr[i].length + 1;
  }
  s.dot_tr =
      (size_t *)ds_context_alloc(pda->context, (dots + 1) * sizeof(size_t));
  if (s.dot_tr == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (i = 0; i < b.count; i++) {
    size_t k;
    for (k = 0; k <= s.tr[i].length; k++) {
      s.dot_tr[s.tr[i].dot + k] = i;
    }
  }
  st = pd_call(&s, pda->states, pda->stack_symbols, &start_call);
  while (st == DS_OK && s.work_n > 0) {
    size_t v = s.work[--s.work_n];
    st = v % 2 == 0 ? pd_process_item(&s, v / 2) : pd_process_result(&s, v / 2);
  }
  if (st == DS_OK) {
    st = pd_hash_get(&s.results, start_call, length * s.q + pda->states + 2, 0,
                     &found, &fresh);
    *out_accepted = st == DS_OK && !fresh;
  }
done:
  ds_context_free(pda->context, b.tr);
  ds_context_free(pda->context, b.pop);
  ds_context_free(pda->context, b.pool);
  ds_context_free(pda->context, s.tr);
  ds_context_free(pda->context, s.bucket);
  ds_context_free(pda->context, s.dot_tr);
  ds_context_free(pda->context, s.calls.slot);
  ds_context_free(pda->context, s.calls.key);
  ds_context_free(pda->context, s.results.slot);
  ds_context_free(pda->context, s.results.key);
  ds_context_free(pda->context, s.items.slot);
  ds_context_free(pda->context, s.items.key);
  ds_context_free(pda->context, s.res_head);
  ds_context_free(pda->context, s.wait_head);
  ds_context_free(pda->context, s.res_next);
  ds_context_free(pda->context, s.wait_item);
  ds_context_free(pda->context, s.wait_next);
  ds_context_free(pda->context, s.work);
  return st;
}

/* -------------------------------------------------- deterministic run */

static bool pd_accepting_now(const ds_pda_t *pda, size_t state, size_t depth) {
  switch (pda->acceptance) {
  case DS_PDA_FINAL_STATE:
    return pda->accepting[state];
  case DS_PDA_EMPTY_STACK:
    return depth == 0;
  case DS_PDA_FINAL_AND_EMPTY:
    return depth == 0 && pda->accepting[state];
  }
  return false;
}

ds_status_t ds_pda_run(const ds_pda_t *pda, const size_t *input, size_t length,
                       unsigned long max_steps, ds_pda_outcome_t *outcome,
                       size_t *out, size_t capacity, size_t *out_length) {
  size_t *stack = NULL;
  size_t depth = 0;
  size_t cap = 0;
  size_t state;
  size_t pos = 0;
  size_t emitted = 0;
  unsigned long steps = 0;
  ds_pda_outcome_t res = DS_PDA_REJECT;
  ds_status_t st = DS_OK;
  size_t i;
  if (pda == NULL || outcome == NULL || (length > 0 && input == NULL) ||
      (capacity > 0 && out == NULL)) {
    return DS_ERR_NULL;
  }
  if (!ds_pda_is_deterministic(pda)) {
    return DS_ERR_STATE;
  }
  for (i = 0; i < length; i++) {
    if (input[i] >= pda->input_symbols) {
      return DS_ERR_RANGE;
    }
  }
  state = pda->start;
  if (pda->start_stack != PD_EPS) {
    st = pd_reserve(pda->context, (void **)&stack, &cap, 1, sizeof(size_t));
    if (st != DS_OK) {
      return st;
    }
    stack[depth++] = pda->start_stack;
  }
  for (;;) {
    const ds_pda_transition_t *t = NULL;
    size_t top = depth > 0 ? stack[depth - 1] : PD_EPS;
    size_t k;
    if (pos == length && pd_accepting_now(pda, state, depth)) {
      res = DS_PDA_ACCEPT;
      break;
    }
    for (k = 0; k < pda->count; k++) {
      const ds_pda_transition_t *c = &pda->transitions[k];
      if (c->from == state && (c->pop == PD_EPS || c->pop == top) &&
          (c->input == PD_EPS || (pos < length && c->input == input[pos]))) {
        t = c;
        break;
      }
    }
    if (t == NULL) {
      break;
    }
    if (steps == max_steps) {
      res = DS_PDA_LIMIT;
      break;
    }
    steps++;
    if (t->pop != PD_EPS) {
      depth--;
    }
    st = pd_reserve(pda->context, (void **)&stack, &cap, depth + t->push_length,
                    sizeof(size_t));
    if (st != DS_OK) {
      break;
    }
    for (k = t->push_length; k-- > 0;) {
      stack[depth++] = pda->push_pool[t->push_offset + k];
    }
    for (k = 0; k < t->output_length; k++) {
      if (emitted < capacity) {
        out[emitted] = pda->output_pool[t->output_offset + k];
      }
      emitted++;
    }
    if (t->input != PD_EPS) {
      pos++;
    }
    state = t->to;
  }
  ds_context_free(pda->context, stack);
  if (st != DS_OK) {
    return st;
  }
  *outcome = res;
  if (out_length != NULL) {
    *out_length = emitted;
  }
  return emitted > capacity ? DS_ERR_RANGE : DS_OK;
}
