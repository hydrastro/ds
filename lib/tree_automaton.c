#include "tree_automaton.h"
#include <limits.h>
#include <string.h>

#define TA_NONE (~(size_t)0)
#define TA_BITS (sizeof(unsigned long) * CHAR_BIT)

static size_t ta_words(size_t n) { return n / TA_BITS + 1; }

static bool ta_has(const unsigned long *s, size_t i) {
  return ((s[i / TA_BITS] >> (i % TA_BITS)) & 1ul) != 0;
}

static void ta_put(unsigned long *s, size_t i) {
  s[i / TA_BITS] |= 1ul << (i % TA_BITS);
}

static ds_status_t ta_reserve(ds_context_t *ctx, void **p, size_t *cap,
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

/* prefix sequence is exactly one tree */
static ds_status_t ta_check_tree(const size_t *rank, size_t symbols,
                                 const size_t *tree, size_t length) {
  size_t need = 1;
  size_t i;
  if (length == 0) {
    return DS_ERR_INVALID;
  }
  if (tree == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < length; i++) {
    if (tree[i] >= symbols) {
      return DS_ERR_RANGE;
    }
    if (need == 0) {
      return DS_ERR_INVALID;
    }
    need += rank[tree[i]];
    need--;
  }
  return need == 0 ? DS_OK : DS_ERR_INVALID;
}

/* ------------------------------------------------------- bottom-up */

ds_status_t ds_fta_init(ds_fta_t *fta, size_t states, size_t symbols,
                        const size_t *rank, ds_context_t *context) {
  if (fta == NULL || (symbols > 0 && rank == NULL)) {
    return DS_ERR_NULL;
  }
  memset(fta, 0, sizeof(*fta));
  if (states == 0 || symbols == 0) {
    return DS_ERR_INVALID;
  }
  fta->rank = (size_t *)ds_context_alloc(context, symbols * sizeof(size_t));
  fta->final = (bool *)ds_context_calloc(context, states, sizeof(bool));
  if (fta->rank == NULL || fta->final == NULL) {
    ds_context_free(context, fta->rank);
    ds_context_free(context, fta->final);
    memset(fta, 0, sizeof(*fta));
    return DS_ERR_ALLOC;
  }
  memcpy(fta->rank, rank, symbols * sizeof(size_t));
  fta->states = states;
  fta->symbols = symbols;
  fta->context = context;
  return DS_OK;
}

void ds_fta_destroy(ds_fta_t *fta) {
  if (fta == NULL) {
    return;
  }
  ds_context_free(fta->context, fta->rank);
  ds_context_free(fta->context, fta->final);
  ds_context_free(fta->context, fta->sym);
  ds_context_free(fta->context, fta->target);
  ds_context_free(fta->context, fta->args);
  ds_context_free(fta->context, fta->pool);
  memset(fta, 0, sizeof(*fta));
}

ds_status_t ds_fta_set_final(ds_fta_t *fta, size_t state, bool final) {
  if (fta == NULL) {
    return DS_ERR_NULL;
  }
  if (state >= fta->states) {
    return DS_ERR_RANGE;
  }
  fta->final[state] = final;
  return DS_OK;
}

ds_status_t ds_fta_add(ds_fta_t *fta, size_t symbol, const size_t *args,
                       size_t target) {
  size_t k;
  size_t i;
  size_t cap;
  ds_status_t st;
  if (fta == NULL) {
    return DS_ERR_NULL;
  }
  if (symbol >= fta->symbols || target >= fta->states) {
    return DS_ERR_RANGE;
  }
  k = fta->rank[symbol];
  if (k > 0 && args == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < k; i++) {
    if (args[i] >= fta->states) {
      return DS_ERR_RANGE;
    }
  }
  cap = fta->capacity;
  st = ta_reserve(fta->context, (void **)&fta->sym, &cap, fta->count + 1,
                  sizeof(size_t));
  if (st == DS_OK) {
    cap = fta->capacity;
    st = ta_reserve(fta->context, (void **)&fta->target, &cap, fta->count + 1,
                    sizeof(size_t));
  }
  if (st == DS_OK) {
    cap = fta->capacity;
    st = ta_reserve(fta->context, (void **)&fta->args, &cap, fta->count + 1,
                    sizeof(size_t));
  }
  if (st == DS_OK) {
    st = ta_reserve(fta->context, (void **)&fta->pool, &fta->pool_capacity,
                    fta->pool_used + k, sizeof(size_t));
  }
  if (st != DS_OK) {
    return st;
  }
  fta->capacity = cap;
  fta->sym[fta->count] = symbol;
  fta->target[fta->count] = target;
  fta->args[fta->count] = fta->pool_used;
  if (k > 0) {
    memcpy(fta->pool + fta->pool_used, args, k * sizeof(size_t));
  }
  fta->pool_used += k;
  fta->count++;
  return DS_OK;
}

static bool ta_same_lhs(const ds_fta_t *f, size_t a, size_t b) {
  size_t k = f->rank[f->sym[a]];
  return f->sym[a] == f->sym[b] &&
         (k == 0 || memcmp(f->pool + f->args[a], f->pool + f->args[b],
                           k * sizeof(size_t)) == 0);
}

bool ds_fta_is_deterministic(const ds_fta_t *fta) {
  size_t i;
  size_t j;
  if (fta == NULL) {
    return false;
  }
  for (i = 0; i < fta->count; i++) {
    for (j = i + 1; j < fta->count; j++) {
      if (ta_same_lhs(fta, i, j) && fta->target[i] != fta->target[j]) {
        return false;
      }
    }
  }
  return true;
}

bool ds_fta_is_complete(const ds_fta_t *fta) {
  size_t f;
  bool ok = true;
  if (fta == NULL) {
    return false;
  }
  for (f = 0; ok && f < fta->symbols; f++) {
    size_t k = fta->rank[f];
    size_t total = 1;
    size_t have = 0;
    size_t i;
    size_t t;
    size_t idx;
    for (t = 0; t < fta->count; t++) {
      if (fta->sym[t] == f) {
        have++;
      }
    }
    for (i = 0; i < k; i++) {
      if (total > have / fta->states) {
        return false; /* more tuples than transitions */
      }
      total *= fta->states;
    }
    if (total > have) {
      return false;
    }
    /* every tuple, as a base-states number, needs a transition */
    for (idx = 0; ok && idx < total; idx++) {
      bool found = false;
      for (t = 0; !found && t < fta->count; t++) {
        size_t rest = idx;
        bool match = fta->sym[t] == f;
        for (i = 0; match && i < k; i++) {
          match = fta->pool[fta->args[t] + i] == rest % fta->states;
          rest /= fta->states;
        }
        found = match;
      }
      ok = found;
    }
  }
  return ok;
}

/* transitions bucketed by symbol */
static ds_status_t ta_buckets(const ds_fta_t *f, size_t **off, size_t **ord) {
  size_t i;
  *off =
      (size_t *)ds_context_calloc(f->context, f->symbols + 1, sizeof(size_t));
  *ord =
      (size_t *)ds_context_alloc(f->context, (f->count + 1) * sizeof(size_t));
  if (*off == NULL || *ord == NULL) {
    ds_context_free(f->context, *off);
    ds_context_free(f->context, *ord);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < f->count; i++) {
    (*off)[f->sym[i] + 1]++;
  }
  for (i = 0; i < f->symbols; i++) {
    (*off)[i + 1] += (*off)[i];
  }
  for (i = 0; i < f->count; i++) {
    (*ord)[(*off)[f->sym[i]]++] = i;
  }
  for (i = f->symbols; i > 0; i--) {
    (*off)[i] = (*off)[i - 1];
  }
  (*off)[0] = 0;
  return DS_OK;
}

/* states reachable by f(S1, ..., Sk); sets[i] is the i-th argument set */
static void ta_apply(const ds_fta_t *fa, const size_t *off, const size_t *ord,
                     size_t f, const unsigned long *const *sets,
                     unsigned long *out, size_t w) {
  size_t k = fa->rank[f];
  size_t b;
  memset(out, 0, w * sizeof(unsigned long));
  for (b = off[f]; b < off[f + 1]; b++) {
    size_t t = ord[b];
    size_t i;
    bool ok = true;
    for (i = 0; ok && i < k; i++) {
      ok = ta_has(sets[i], fa->pool[fa->args[t] + i]);
    }
    if (ok) {
      ta_put(out, fa->target[t]);
    }
  }
}

ds_status_t ds_fta_run(const ds_fta_t *fta, const size_t *tree, size_t length,
                       bool *reached) {
  size_t w;
  size_t *off;
  size_t *ord;
  unsigned long *stack;
  unsigned long *tmp;
  const unsigned long **args;
  size_t maxrank = 0;
  size_t sp = 0;
  size_t i;
  ds_status_t st;
  if (fta == NULL || reached == NULL) {
    return DS_ERR_NULL;
  }
  st = ta_check_tree(fta->rank, fta->symbols, tree, length);
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < fta->symbols; i++) {
    if (fta->rank[i] > maxrank) {
      maxrank = fta->rank[i];
    }
  }
  w = ta_words(fta->states);
  st = ta_buckets(fta, &off, &ord);
  if (st != DS_OK) {
    return st;
  }
  stack = (unsigned long *)ds_context_alloc(
      fta->context, (length + 1) * w * sizeof(unsigned long));
  args = (const unsigned long **)ds_context_alloc(
      fta->context, (maxrank + 1) * sizeof(unsigned long *));
  if (stack == NULL || args == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  tmp = stack + length * w;
  for (i = length; i-- > 0;) {
    size_t f = tree[i];
    size_t k = fta->rank[f];
    size_t j;
    for (j = 0; j < k; j++) {
      args[j] = stack + (sp - 1 - j) * w;
    }
    ta_apply(fta, off, ord, f, args, tmp, w);
    sp -= k;
    memcpy(stack + sp * w, tmp, w * sizeof(unsigned long));
    sp++;
  }
  for (i = 0; i < fta->states; i++) {
    reached[i] = ta_has(stack, i);
  }
done:
  ds_context_free(fta->context, stack);
  ds_context_free(fta->context, (void *)args);
  ds_context_free(fta->context, off);
  ds_context_free(fta->context, ord);
  return st;
}

ds_status_t ds_fta_accepts(const ds_fta_t *fta, const size_t *tree,
                           size_t length, bool *out_accepted) {
  bool *reached;
  size_t i;
  ds_status_t st;
  if (fta == NULL || out_accepted == NULL) {
    return DS_ERR_NULL;
  }
  reached = (bool *)ds_context_alloc(fta->context, fta->states * sizeof(bool));
  if (reached == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ds_fta_run(fta, tree, length, reached);
  if (st == DS_OK) {
    *out_accepted = false;
    for (i = 0; i < fta->states; i++) {
      if (reached[i] && fta->final[i]) {
        *out_accepted = true;
      }
    }
  }
  ds_context_free(fta->context, reached);
  return st;
}

/* subsets: a growable list with an open-addressing index */
typedef struct ta_sets {
  unsigned long *bits;
  size_t n;
  size_t cap;
  size_t *slot;
  size_t slots;
  size_t w;
  ds_context_t *ctx;
} ta_sets_t;

static size_t ta_hash(const unsigned long *s, size_t w) {
  unsigned long h = 2166136261ul;
  size_t i;
  for (i = 0; i < w; i++) {
    h = (h ^ s[i]) * 16777619ul;
    h ^= h >> 15;
  }
  return (size_t)h;
}

static ds_status_t ta_sets_find(ta_sets_t *s, const unsigned long *set,
                                size_t max, size_t *out, bool *fresh) {
  size_t at;
  size_t i;
  if (2 * (s->n + 1) > s->slots) {
    size_t ns = s->slots < 16 ? 32 : s->slots * 2;
    size_t *sl = (size_t *)ds_context_alloc(s->ctx, ns * sizeof(size_t));
    if (sl == NULL) {
      return DS_ERR_ALLOC;
    }
    for (i = 0; i < ns; i++) {
      sl[i] = TA_NONE;
    }
    for (i = 0; i < s->n; i++) {
      at = ta_hash(s->bits + i * s->w, s->w) & (ns - 1);
      while (sl[at] != TA_NONE) {
        at = (at + 1) & (ns - 1);
      }
      sl[at] = i;
    }
    ds_context_free(s->ctx, s->slot);
    s->slot = sl;
    s->slots = ns;
  }
  at = ta_hash(set, s->w) & (s->slots - 1);
  while (s->slot[at] != TA_NONE) {
    if (memcmp(s->bits + s->slot[at] * s->w, set,
               s->w * sizeof(unsigned long)) == 0) {
      *out = s->slot[at];
      *fresh = false;
      return DS_OK;
    }
    at = (at + 1) & (s->slots - 1);
  }
  if (s->n >= max) {
    return DS_ERR_RANGE;
  }
  if (ta_reserve(s->ctx, (void **)&s->bits, &s->cap, (s->n + 1) * s->w,
                 sizeof(unsigned long)) != DS_OK) {
    return DS_ERR_ALLOC;
  }
  memcpy(s->bits + s->n * s->w, set, s->w * sizeof(unsigned long));
  s->slot[at] = s->n;
  *out = s->n++;
  *fresh = true;
  return DS_OK;
}

ds_status_t ds_fta_determinize(const ds_fta_t *fta, size_t max_states,
                               ds_fta_t *out, ds_context_t *context) {
  ta_sets_t sets;
  size_t *off = NULL;
  size_t *ord = NULL;
  size_t *tuple = NULL;
  size_t *ids = NULL;
  const unsigned long **args = NULL;
  unsigned long *tmp = NULL;
  size_t maxrank = 0;
  size_t i;
  size_t f;
  bool changed = true;
  ds_status_t st;
  if (fta == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (max_states == 0) {
    return DS_ERR_INVALID;
  }
  memset(&sets, 0, sizeof(sets));
  sets.w = ta_words(fta->states);
  sets.ctx = context;
  for (i = 0; i < fta->symbols; i++) {
    if (fta->rank[i] > maxrank) {
      maxrank = fta->rank[i];
    }
  }
  st = ta_buckets(fta, &off, &ord);
  if (st != DS_OK) {
    return st;
  }
  tuple =
      (size_t *)ds_context_alloc(context, 2 * (maxrank + 1) * sizeof(size_t));
  args = (const unsigned long **)ds_context_alloc(
      context, (maxrank + 1) * sizeof(unsigned long *));
  tmp = (unsigned long *)ds_context_alloc(context,
                                          sets.w * sizeof(unsigned long));
  if (tuple == NULL || args == NULL || tmp == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  ids = tuple + maxrank + 1;
  /* pass 0..: grow the reachable subsets to a fixpoint; the last pass
   * (after no change) emits the transitions */
  while (st == DS_OK) {
    bool emit = !changed;
    changed = false;
    if (emit && sets.n == 0) {
      size_t id;
      bool fresh;
      memset(tmp, 0, sets.w * sizeof(unsigned long));
      st = ta_sets_find(&sets, tmp, max_states, &id, &fresh);
      if (st != DS_OK) {
        break;
      }
    }
    if (emit) {
      st = ds_fta_init(out, sets.n, fta->symbols, fta->rank, context);
      if (st != DS_OK) {
        break;
      }
      for (i = 0; i < sets.n; i++) {
        size_t q;
        for (q = 0; q < fta->states; q++) {
          if (fta->final[q] && ta_has(sets.bits + i * sets.w, q)) {
            out->final[i] = true;
          }
        }
      }
    }
    for (f = 0; st == DS_OK && f < fta->symbols; f++) {
      size_t k = fta->rank[f];
      size_t limit = sets.n; /* snapshot */
      size_t j;
      if (k > 0 && limit == 0) {
        continue;
      }
      for (j = 0; j < k; j++) {
        tuple[j] = 0;
      }
      for (;;) {
        size_t id;
        bool fresh;
        for (j = 0; j < k; j++) {
          args[j] = sets.bits + tuple[j] * sets.w;
        }
        ta_apply(fta, off, ord, f, args, tmp, sets.w);
        st = ta_sets_find(&sets, tmp, max_states, &id, &fresh);
        if (st != DS_OK) {
          break;
        }
        if (fresh) {
          changed = true;
        }
        if (emit) {
          for (j = 0; j < k; j++) {
            ids[j] = tuple[j];
          }
          st = ds_fta_add(out, f, ids, id);
          if (st != DS_OK) {
            break;
          }
        }
        for (j = 0; j < k && ++tuple[j] == limit; j++) {
          tuple[j] = 0;
        }
        if (j == k) {
          break;
        }
      }
    }
    if (emit) {
      if (st != DS_OK) {
        ds_fta_destroy(out);
      }
      break;
    }
  }
done:
  ds_context_free(context, sets.bits);
  ds_context_free(context, sets.slot);
  ds_context_free(fta->context, off);
  ds_context_free(fta->context, ord);
  ds_context_free(context, tuple);
  ds_context_free(context, (void *)args);
  ds_context_free(context, tmp);
  return st;
}

static ds_status_t ta_same_alphabet(const ds_fta_t *a, const ds_fta_t *b) {
  if (a->symbols != b->symbols ||
      memcmp(a->rank, b->rank, a->symbols * sizeof(size_t)) != 0) {
    return DS_ERR_INVALID;
  }
  return DS_OK;
}

ds_status_t ds_fta_intersection(const ds_fta_t *a, const ds_fta_t *b,
                                ds_fta_t *out, ds_context_t *context) {
  size_t *offa;
  size_t *orda;
  size_t *offb;
  size_t *ordb;
  size_t *tuple;
  size_t maxrank = 0;
  size_t i;
  size_t f;
  ds_status_t st;
  if (a == NULL || b == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  st = ta_same_alphabet(a, b);
  if (st != DS_OK) {
    return st;
  }
  if (a->states > (size_t)-1 / b->states) {
    return DS_ERR_OVERFLOW;
  }
  for (i = 0; i < a->symbols; i++) {
    if (a->rank[i] > maxrank) {
      maxrank = a->rank[i];
    }
  }
  st = ta_buckets(a, &offa, &orda);
  if (st != DS_OK) {
    return st;
  }
  st = ta_buckets(b, &offb, &ordb);
  if (st != DS_OK) {
    ds_context_free(a->context, offa);
    ds_context_free(a->context, orda);
    return st;
  }
  tuple = (size_t *)ds_context_alloc(context, (maxrank + 1) * sizeof(size_t));
  st = tuple == NULL ? DS_ERR_ALLOC
                     : ds_fta_init(out, a->states * b->states, a->symbols,
                                   a->rank, context);
  for (i = 0; st == DS_OK && i < a->states * b->states; i++) {
    out->final[i] = a->final[i / b->states] && b->final[i % b->states];
  }
  for (f = 0; st == DS_OK && f < a->symbols; f++) {
    size_t x;
    size_t y;
    for (x = offa[f]; st == DS_OK && x < offa[f + 1]; x++) {
      for (y = offb[f]; st == DS_OK && y < offb[f + 1]; y++) {
        size_t ta = orda[x];
        size_t tb = ordb[y];
        size_t j;
        for (j = 0; j < a->rank[f]; j++) {
          tuple[j] =
              a->pool[a->args[ta] + j] * b->states + b->pool[b->args[tb] + j];
        }
        st = ds_fta_add(out, f, tuple,
                        a->target[ta] * b->states + b->target[tb]);
      }
    }
  }
  if (st != DS_OK && tuple != NULL) {
    ds_fta_destroy(out);
  }
  ds_context_free(context, tuple);
  ds_context_free(a->context, offa);
  ds_context_free(a->context, orda);
  ds_context_free(b->context, offb);
  ds_context_free(b->context, ordb);
  return st;
}

ds_status_t ds_fta_union(const ds_fta_t *a, const ds_fta_t *b, ds_fta_t *out,
                         ds_context_t *context) {
  size_t *tuple;
  size_t maxrank = 0;
  size_t i;
  size_t t;
  ds_status_t st;
  if (a == NULL || b == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  st = ta_same_alphabet(a, b);
  if (st != DS_OK) {
    return st;
  }
  if (a->states > (size_t)-1 - b->states) {
    return DS_ERR_OVERFLOW;
  }
  for (i = 0; i < a->symbols; i++) {
    if (a->rank[i] > maxrank) {
      maxrank = a->rank[i];
    }
  }
  tuple = (size_t *)ds_context_alloc(context, (maxrank + 1) * sizeof(size_t));
  if (tuple == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ds_fta_init(out, a->states + b->states, a->symbols, a->rank, context);
  if (st != DS_OK) {
    ds_context_free(context, tuple);
    return st;
  }
  for (i = 0; i < a->states; i++) {
    out->final[i] = a->final[i];
  }
  for (i = 0; i < b->states; i++) {
    out->final[a->states + i] = b->final[i];
  }
  for (t = 0; st == DS_OK && t < a->count; t++) {
    st = ds_fta_add(out, a->sym[t], a->pool + a->args[t], a->target[t]);
  }
  for (t = 0; st == DS_OK && t < b->count; t++) {
    size_t j;
    for (j = 0; j < b->rank[b->sym[t]]; j++) {
      tuple[j] = a->states + b->pool[b->args[t] + j];
    }
    st = ds_fta_add(out, b->sym[t], tuple, a->states + b->target[t]);
  }
  if (st != DS_OK) {
    ds_fta_destroy(out);
  }
  ds_context_free(context, tuple);
  return st;
}

ds_status_t ds_fta_complement(ds_fta_t *fta) {
  size_t i;
  if (fta == NULL) {
    return DS_ERR_NULL;
  }
  if (!ds_fta_is_deterministic(fta) || !ds_fta_is_complete(fta)) {
    return DS_ERR_STATE;
  }
  for (i = 0; i < fta->states; i++) {
    fta->final[i] = !fta->final[i];
  }
  return DS_OK;
}

ds_status_t ds_fta_is_empty(const ds_fta_t *fta, bool *out_empty,
                            size_t *witness, size_t capacity,
                            size_t *witness_length) {
  size_t n;
  size_t *via;
  size_t *size;
  size_t *stack;
  unsigned char *reached;
  unsigned char *now;
  size_t best = TA_NONE;
  size_t i;
  size_t t;
  bool changed = true;
  ds_status_t st = DS_OK;
  if (fta == NULL || out_empty == NULL) {
    return DS_ERR_NULL;
  }
  n = fta->states;
  via = (size_t *)ds_context_alloc(fta->context, 2 * n * sizeof(size_t));
  reached = (unsigned char *)ds_context_calloc(fta->context, 2 * n, 1);
  if (via == NULL || reached == NULL) {
    ds_context_free(fta->context, via);
    ds_context_free(fta->context, reached);
    return DS_ERR_ALLOC;
  }
  size = via + n;
  now = reached + n;
  /* passes over the previous pass's states: discovery order = height */
  while (changed) {
    changed = false;
    memcpy(now, reached, n);
    for (t = 0; t < fta->count; t++) {
      size_t q = fta->target[t];
      size_t k = fta->rank[fta->sym[t]];
      size_t j;
      size_t total = 1;
      bool ok = !now[q];
      for (j = 0; ok && j < k; j++) {
        size_t c = fta->pool[fta->args[t] + j];
        ok = reached[c] != 0;
        if (ok) {
          total = total > (size_t)-1 - size[c] ? (size_t)-1 : total + size[c];
        }
      }
      if (ok) {
        now[q] = 1;
        via[q] = t;
        size[q] = total;
        changed = true;
      }
    }
    for (i = 0; i < n; i++) {
      if (now[i] && !reached[i]) {
        reached[i] = 1;
        if (fta->final[i] && best == TA_NONE) {
          best = i;
        }
      }
    }
  }
  *out_empty = best == TA_NONE;
  if (best != TA_NONE && witness != NULL) {
    size_t len = size[best];
    if (witness_length != NULL) {
      *witness_length = len;
    }
    if (len > capacity) {
      st = DS_ERR_RANGE;
    } else {
      size_t sp = 0;
      size_t w = 0;
      stack = (size_t *)ds_context_alloc(fta->context, len * sizeof(size_t));
      if (stack == NULL) {
        st = DS_ERR_ALLOC;
      } else {
        stack[sp++] = best;
        while (sp > 0) {
          size_t q = stack[--sp];
          size_t tr = via[q];
          size_t k = fta->rank[fta->sym[tr]];
          witness[w++] = fta->sym[tr];
          while (k-- > 0) {
            stack[sp++] = fta->pool[fta->args[tr] + k];
          }
        }
        ds_context_free(fta->context, stack);
      }
    }
  }
  ds_context_free(fta->context, via);
  ds_context_free(fta->context, reached);
  return st;
}

/* -------------------------------------------------------- top-down */

ds_status_t ds_tdta_init(ds_tdta_t *tdta, size_t states, size_t symbols,
                         const size_t *rank, size_t initial,
                         ds_context_t *context) {
  if (tdta == NULL || (symbols > 0 && rank == NULL)) {
    return DS_ERR_NULL;
  }
  memset(tdta, 0, sizeof(*tdta));
  if (states == 0 || symbols == 0 || initial >= states) {
    return DS_ERR_INVALID;
  }
  tdta->rank = (size_t *)ds_context_alloc(context, symbols * sizeof(size_t));
  if (tdta->rank == NULL) {
    return DS_ERR_ALLOC;
  }
  memcpy(tdta->rank, rank, symbols * sizeof(size_t));
  tdta->states = states;
  tdta->symbols = symbols;
  tdta->initial = initial;
  tdta->context = context;
  return DS_OK;
}

void ds_tdta_destroy(ds_tdta_t *tdta) {
  if (tdta == NULL) {
    return;
  }
  ds_context_free(tdta->context, tdta->rank);
  ds_context_free(tdta->context, tdta->from);
  ds_context_free(tdta->context, tdta->sym);
  ds_context_free(tdta->context, tdta->args);
  ds_context_free(tdta->context, tdta->pool);
  memset(tdta, 0, sizeof(*tdta));
}

ds_status_t ds_tdta_add(ds_tdta_t *tdta, size_t from, size_t symbol,
                        const size_t *children) {
  size_t k;
  size_t i;
  size_t cap;
  ds_status_t st;
  if (tdta == NULL) {
    return DS_ERR_NULL;
  }
  if (from >= tdta->states || symbol >= tdta->symbols) {
    return DS_ERR_RANGE;
  }
  k = tdta->rank[symbol];
  if (k > 0 && children == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < k; i++) {
    if (children[i] >= tdta->states) {
      return DS_ERR_RANGE;
    }
  }
  cap = tdta->capacity;
  st = ta_reserve(tdta->context, (void **)&tdta->from, &cap, tdta->count + 1,
                  sizeof(size_t));
  if (st == DS_OK) {
    cap = tdta->capacity;
    st = ta_reserve(tdta->context, (void **)&tdta->sym, &cap, tdta->count + 1,
                    sizeof(size_t));
  }
  if (st == DS_OK) {
    cap = tdta->capacity;
    st = ta_reserve(tdta->context, (void **)&tdta->args, &cap, tdta->count + 1,
                    sizeof(size_t));
  }
  if (st == DS_OK) {
    st = ta_reserve(tdta->context, (void **)&tdta->pool, &tdta->pool_capacity,
                    tdta->pool_used + k, sizeof(size_t));
  }
  if (st != DS_OK) {
    return st;
  }
  tdta->capacity = cap;
  tdta->from[tdta->count] = from;
  tdta->sym[tdta->count] = symbol;
  tdta->args[tdta->count] = tdta->pool_used;
  if (k > 0) {
    memcpy(tdta->pool + tdta->pool_used, children, k * sizeof(size_t));
  }
  tdta->pool_used += k;
  tdta->count++;
  return DS_OK;
}

bool ds_tdta_is_deterministic(const ds_tdta_t *tdta) {
  size_t i;
  size_t j;
  if (tdta == NULL) {
    return false;
  }
  for (i = 0; i < tdta->count; i++) {
    for (j = i + 1; j < tdta->count; j++) {
      if (tdta->from[i] == tdta->from[j] && tdta->sym[i] == tdta->sym[j]) {
        return false;
      }
    }
  }
  return true;
}

ds_status_t ds_tdta_to_fta(const ds_tdta_t *tdta, ds_fta_t *out,
                           ds_context_t *context) {
  size_t t;
  ds_status_t st;
  if (tdta == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_fta_init(out, tdta->states, tdta->symbols, tdta->rank, context);
  if (st != DS_OK) {
    return st;
  }
  out->final[tdta->initial] = true;
  for (t = 0; st == DS_OK && t < tdta->count; t++) {
    st = ds_fta_add(out, tdta->sym[t], tdta->pool + tdta->args[t],
                    tdta->from[t]);
  }
  if (st != DS_OK) {
    ds_fta_destroy(out);
  }
  return st;
}

ds_status_t ds_tdta_accepts(const ds_tdta_t *tdta, const size_t *tree,
                            size_t length, bool *out_accepted) {
  size_t *stack;
  size_t *off;
  size_t sp = 0;
  size_t i;
  size_t t;
  ds_status_t st;
  if (tdta == NULL || out_accepted == NULL) {
    return DS_ERR_NULL;
  }
  st = ta_check_tree(tdta->rank, tdta->symbols, tree, length);
  if (st != DS_OK) {
    return st;
  }
  if (!ds_tdta_is_deterministic(tdta)) {
    ds_fta_t f;
    st = ds_tdta_to_fta(tdta, &f, tdta->context);
    if (st == DS_OK) {
      st = ds_fta_accepts(&f, tree, length, out_accepted);
      ds_fta_destroy(&f);
    }
    return st;
  }
  /* transitions grouped by source state */
  off = (size_t *)ds_context_calloc(
      tdta->context, 2 * tdta->states + 1 + tdta->count, sizeof(size_t));
  stack =
      (size_t *)ds_context_alloc(tdta->context, (length + 1) * sizeof(size_t));
  if (off == NULL || stack == NULL) {
    ds_context_free(tdta->context, off);
    ds_context_free(tdta->context, stack);
    return DS_ERR_ALLOC;
  }
  {
    size_t *ord = off + tdta->states + 1;
    size_t *fill = ord + tdta->count;
    for (t = 0; t < tdta->count; t++) {
      off[tdta->from[t] + 1]++;
    }
    for (i = 0; i < tdta->states; i++) {
      off[i + 1] += off[i];
      fill[i] = off[i];
    }
    for (t = 0; t < tdta->count; t++) {
      ord[fill[tdta->from[t]]++] = t;
    }
    *out_accepted = true;
    stack[sp++] = tdta->initial;
    for (i = 0; i < length; i++) {
      size_t q = stack[--sp];
      size_t found = TA_NONE;
      size_t k;
      for (t = off[q]; t < off[q + 1]; t++) {
        if (tdta->sym[ord[t]] == tree[i]) {
          found = ord[t];
          break;
        }
      }
      if (found == TA_NONE) {
        *out_accepted = false;
        break;
      }
      for (k = tdta->rank[tree[i]]; k-- > 0;) {
        stack[sp++] = tdta->pool[tdta->args[found] + k];
      }
    }
  }
  ds_context_free(tdta->context, off);
  ds_context_free(tdta->context, stack);
  return DS_OK;
}

/* ---------------------------------------------------- tree transducers */

ds_status_t ds_tt_init(ds_tree_transducer_t *tt, size_t states,
                       size_t in_symbols, const size_t *in_rank,
                       size_t out_symbols, const size_t *out_rank,
                       ds_context_t *context) {
  if (tt == NULL || in_rank == NULL || out_rank == NULL) {
    return DS_ERR_NULL;
  }
  memset(tt, 0, sizeof(*tt));
  if (states == 0 || in_symbols == 0 || out_symbols == 0 ||
      out_symbols >= DS_TT_VAR(0) / 2) {
    return DS_ERR_INVALID;
  }
  tt->in_rank =
      (size_t *)ds_context_alloc(context, in_symbols * sizeof(size_t));
  tt->out_rank =
      (size_t *)ds_context_alloc(context, out_symbols * sizeof(size_t));
  tt->final = (bool *)ds_context_calloc(context, states, sizeof(bool));
  if (tt->in_rank == NULL || tt->out_rank == NULL || tt->final == NULL) {
    ds_context_free(context, tt->in_rank);
    ds_context_free(context, tt->out_rank);
    ds_context_free(context, tt->final);
    memset(tt, 0, sizeof(*tt));
    return DS_ERR_ALLOC;
  }
  memcpy(tt->in_rank, in_rank, in_symbols * sizeof(size_t));
  memcpy(tt->out_rank, out_rank, out_symbols * sizeof(size_t));
  tt->states = states;
  tt->in_symbols = in_symbols;
  tt->out_symbols = out_symbols;
  tt->context = context;
  return DS_OK;
}

void ds_tt_destroy(ds_tree_transducer_t *tt) {
  if (tt == NULL) {
    return;
  }
  ds_context_free(tt->context, tt->in_rank);
  ds_context_free(tt->context, tt->out_rank);
  ds_context_free(tt->context, tt->final);
  ds_context_free(tt->context, tt->sym);
  ds_context_free(tt->context, tt->target);
  ds_context_free(tt->context, tt->args);
  ds_context_free(tt->context, tt->rhs);
  ds_context_free(tt->context, tt->rhs_len);
  ds_context_free(tt->context, tt->pool);
  memset(tt, 0, sizeof(*tt));
}

ds_status_t ds_tt_set_final(ds_tree_transducer_t *tt, size_t state,
                            bool final) {
  if (tt == NULL) {
    return DS_ERR_NULL;
  }
  if (state >= tt->states) {
    return DS_ERR_RANGE;
  }
  tt->final[state] = final;
  return DS_OK;
}

static size_t tt_find(const ds_tree_transducer_t *tt, size_t symbol,
                      const size_t *args) {
  size_t r;
  size_t k = tt->in_rank[symbol];
  for (r = 0; r < tt->count; r++) {
    if (tt->sym[r] == symbol && (k == 0 || memcmp(tt->pool + tt->args[r], args,
                                                  k * sizeof(size_t)) == 0)) {
      return r;
    }
  }
  return TA_NONE;
}

ds_status_t ds_tt_add(ds_tree_transducer_t *tt, size_t symbol,
                      const size_t *args, size_t target, const size_t *rhs,
                      size_t rhs_len) {
  size_t k;
  size_t i;
  size_t need = 1;
  size_t cap;
  ds_status_t st;
  if (tt == NULL || (rhs_len > 0 && rhs == NULL)) {
    return DS_ERR_NULL;
  }
  if (symbol >= tt->in_symbols || target >= tt->states) {
    return DS_ERR_RANGE;
  }
  k = tt->in_rank[symbol];
  if (k > 0 && args == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < k; i++) {
    if (args[i] >= tt->states) {
      return DS_ERR_RANGE;
    }
  }
  /* rhs must be exactly one tree; variables are leaves */
  if (rhs_len == 0) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < rhs_len; i++) {
    size_t x = rhs[i];
    if (need == 0) {
      return DS_ERR_INVALID;
    }
    need--;
    if (x < tt->out_symbols) {
      need += tt->out_rank[x];
    } else if (DS_TT_VAR(0) - x >= k) {
      return DS_ERR_INVALID;
    }
  }
  if (need != 0 || tt_find(tt, symbol, args) != TA_NONE) {
    return DS_ERR_INVALID;
  }
  cap = tt->capacity;
  st = ta_reserve(tt->context, (void **)&tt->sym, &cap, tt->count + 1,
                  sizeof(size_t));
#define TT_GROW(field)                                                         \
  if (st == DS_OK) {                                                           \
    cap = tt->capacity;                                                        \
    st = ta_reserve(tt->context, (void **)&tt->field, &cap, tt->count + 1,     \
                    sizeof(size_t));                                           \
  }
  TT_GROW(target)
  TT_GROW(args)
  TT_GROW(rhs)
  TT_GROW(rhs_len)
#undef TT_GROW
  if (st == DS_OK) {
    st = ta_reserve(tt->context, (void **)&tt->pool, &tt->pool_capacity,
                    tt->pool_used + k + rhs_len, sizeof(size_t));
  }
  if (st != DS_OK) {
    return st;
  }
  tt->capacity = cap;
  tt->sym[tt->count] = symbol;
  tt->target[tt->count] = target;
  tt->args[tt->count] = tt->pool_used;
  if (k > 0) {
    memcpy(tt->pool + tt->pool_used, args, k * sizeof(size_t));
  }
  tt->pool_used += k;
  tt->rhs[tt->count] = tt->pool_used;
  tt->rhs_len[tt->count] = rhs_len;
  memcpy(tt->pool + tt->pool_used, rhs, rhs_len * sizeof(size_t));
  tt->pool_used += rhs_len;
  tt->count++;
  return DS_OK;
}

ds_status_t ds_tt_run(const ds_tree_transducer_t *tt, const size_t *tree,
                      size_t length, size_t max_work, bool *accepted,
                      size_t *out, size_t capacity, size_t *out_len) {
  /* stack entries: state, output offset and length in the work buffer */
  size_t *stk;
  size_t *work = NULL;
  size_t work_cap = 0;
  size_t used = 0;
  size_t sp = 0;
  size_t maxrank = 0;
  size_t *args;
  size_t i;
  ds_status_t st;
  if (tt == NULL || accepted == NULL || out_len == NULL ||
      (capacity > 0 && out == NULL)) {
    return DS_ERR_NULL;
  }
  st = ta_check_tree(tt->in_rank, tt->in_symbols, tree, length);
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < tt->in_symbols; i++) {
    if (tt->in_rank[i] > maxrank) {
      maxrank = tt->in_rank[i];
    }
  }
  *accepted = false;
  *out_len = 0;
  stk = (size_t *)ds_context_alloc(
      tt->context, (3 * (length + 1) + maxrank + 1) * sizeof(size_t));
  if (stk == NULL) {
    return DS_ERR_ALLOC;
  }
  args = stk + 3 * (length + 1);
  for (i = length; i-- > 0;) {
    size_t f = tree[i];
    size_t k = tt->in_rank[f];
    size_t r;
    size_t j;
    size_t total = 0;
    size_t start;
    for (j = 0; j < k; j++) {
      args[j] = stk[3 * (sp - 1 - j)];
    }
    r = tt_find(tt, f, args);
    if (r == TA_NONE) {
      goto done; /* no rule: rejected */
    }
    /* size of the new output */
    for (j = 0; j < tt->rhs_len[r]; j++) {
      size_t x = tt->pool[tt->rhs[r] + j];
      size_t add =
          x < tt->out_symbols ? 1 : stk[3 * (sp - 1 - (DS_TT_VAR(0) - x)) + 2];
      if (total > (size_t)-1 - add) {
        st = DS_ERR_RANGE;
        goto done;
      }
      total += add;
    }
    if (used > max_work || total > max_work - used) {
      st = DS_ERR_RANGE;
      goto done;
    }
    st = ta_reserve(tt->context, (void **)&work, &work_cap, used + total + 1,
                    sizeof(size_t));
    if (st != DS_OK) {
      goto done;
    }
    start = used;
    for (j = 0; j < tt->rhs_len[r]; j++) {
      size_t x = tt->pool[tt->rhs[r] + j];
      if (x < tt->out_symbols) {
        work[used++] = x;
      } else {
        size_t c = sp - 1 - (DS_TT_VAR(0) - x);
        memcpy(work + used, work + stk[3 * c + 1],
               stk[3 * c + 2] * sizeof(size_t));
        used += stk[3 * c + 2];
      }
    }
    sp -= k;
    stk[3 * sp] = tt->target[r];
    stk[3 * sp + 1] = start;
    stk[3 * sp + 2] = total;
    sp++;
  }
  if (tt->final[stk[0]]) {
    *accepted = true;
    *out_len = stk[2];
    if (stk[2] > capacity) {
      st = DS_ERR_RANGE;
    } else {
      memcpy(out, work + stk[1], stk[2] * sizeof(size_t));
    }
  }
done:
  ds_context_free(tt->context, stk);
  ds_context_free(tt->context, work);
  return st;
}
