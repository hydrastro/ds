#include "kway_merge.h"
#include <string.h>

typedef union km_align {
  long l;
  double d;
  void *p;
  long double ld;
} km_align_t;

#define KM_ALIGN (sizeof(km_align_t))
#define KM_SIZE_MAX (~(size_t)0)

static size_t km_round(size_t n) {
  size_t r = n % KM_ALIGN;
  return r == 0 ? n : n + (KM_ALIGN - r);
}

/* Layout: tree (2k size_t) | values (k * es) | alive (k bytes). */
static int km_layout(size_t k, size_t es, size_t *out_values, size_t *out_alive,
                     size_t *out_total) {
  size_t tree;
  size_t values;
  size_t n = k == 0 ? 1 : k;
  if (n > KM_SIZE_MAX / 2 / sizeof(size_t) || n > (KM_SIZE_MAX - 64) / es) {
    return 0;
  }
  tree = km_round(2 * n * sizeof(size_t));
  values = km_round(n * es);
  if (tree > KM_SIZE_MAX - values || tree + values > KM_SIZE_MAX - n) {
    return 0;
  }
  *out_values = tree;
  *out_alive = tree + values;
  *out_total = tree + values + n;
  return 1;
}

size_t ds_tournament_buffer_size(size_t players, size_t elem_size) {
  size_t v;
  size_t a;
  size_t total;
  if (elem_size == 0 || !km_layout(players, elem_size, &v, &a, &total)) {
    return 0;
  }
  return total;
}

static void km_attach(ds_tournament_t *t, const ds_tournament_config_t *config,
                      void *buffer) {
  size_t v = 0;
  size_t a = 0;
  size_t total = 0;
  km_layout(config->players, config->elem_size, &v, &a, &total);
  t->config = *config;
  t->tree = (size_t *)buffer;
  t->values = (unsigned char *)buffer + v;
  t->alive = (unsigned char *)buffer + a;
  memset(t->alive, 0, config->players);
}

static ds_status_t km_check_config(const ds_tournament_config_t *config) {
  if (config == NULL || config->compare == NULL) {
    return DS_ERR_NULL;
  }
  if (config->elem_size == 0 || (config->kind != DS_TOURNAMENT_LOSER &&
                                 config->kind != DS_TOURNAMENT_WINNER)) {
    return DS_ERR_INVALID;
  }
  return DS_OK;
}

ds_status_t ds_tournament_init(ds_tournament_t *t,
                               const ds_tournament_config_t *config,
                               ds_context_t *context) {
  size_t size;
  void *block;
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  st = km_check_config(config);
  if (st != DS_OK) {
    return st;
  }
  size = ds_tournament_buffer_size(config->players, config->elem_size);
  if (size == 0) {
    return DS_ERR_OVERFLOW;
  }
  block = ds_context_alloc(context, size);
  if (block == NULL) {
    return DS_ERR_ALLOC;
  }
  km_attach(t, config, block);
  t->block = block;
  t->context = context;
  return DS_OK;
}

ds_status_t ds_tournament_init_buffer(ds_tournament_t *t,
                                      const ds_tournament_config_t *config,
                                      void *buffer, size_t buffer_size) {
  size_t size;
  ds_status_t st;
  if (t == NULL || buffer == NULL) {
    return DS_ERR_NULL;
  }
  st = km_check_config(config);
  if (st != DS_OK) {
    return st;
  }
  size = ds_tournament_buffer_size(config->players, config->elem_size);
  if (size == 0 || buffer_size < size) {
    return DS_ERR_OVERFLOW;
  }
  km_attach(t, config, buffer);
  t->block = NULL;
  t->context = NULL;
  return DS_OK;
}

void ds_tournament_destroy(ds_tournament_t *t) {
  if (t == NULL) {
    return;
  }
  if (t->block != NULL) {
    ds_context_free(t->context, t->block);
  }
  t->block = NULL;
  t->tree = NULL;
  t->values = NULL;
  t->alive = NULL;
}

void *ds_tournament_slot(ds_tournament_t *t, size_t player) {
  return t->values + player * t->config.elem_size;
}

void ds_tournament_set_alive(ds_tournament_t *t, size_t player, bool alive) {
  t->alive[player] = (unsigned char)(alive ? 1 : 0);
}

bool ds_tournament_is_alive(const ds_tournament_t *t, size_t player) {
  return t->alive[player] != 0;
}

/* True when player a beats player b. */
static bool km_beats(const ds_tournament_t *t, size_t a, size_t b) {
  int c;
  if (t->alive[a] != t->alive[b]) {
    return t->alive[a] != 0;
  }
  if (!t->alive[a]) {
    return a < b;
  }
  c = t->config.compare(t->values + a * t->config.elem_size,
                        t->values + b * t->config.elem_size, t->config.user);
  if (c != 0) {
    return c < 0;
  }
  return a < b;
}

/* Winner of the subtree at node c (leaves are k + player). The loser
 * tree keeps internal winners at tree[k + c - 1] while building. */
static size_t km_child_winner(const ds_tournament_t *t, size_t c) {
  size_t k = t->config.players;
  if (c >= k) {
    return c - k;
  }
  return t->config.kind == DS_TOURNAMENT_LOSER ? t->tree[k + c - 1]
                                               : t->tree[c];
}

void ds_tournament_build(ds_tournament_t *t) {
  size_t k = t->config.players;
  size_t i;
  size_t l;
  size_t r;
  if (k == 0) {
    return;
  }
  for (i = k - 1; i >= 1; i--) {
    l = km_child_winner(t, 2 * i);
    r = km_child_winner(t, 2 * i + 1);
    if (t->config.kind == DS_TOURNAMENT_LOSER) {
      if (km_beats(t, l, r)) {
        t->tree[k + i - 1] = l;
        t->tree[i] = r;
      } else {
        t->tree[k + i - 1] = r;
        t->tree[i] = l;
      }
    } else {
      t->tree[i] = km_beats(t, l, r) ? l : r;
    }
  }
  t->tree[0] = k > 1 ? km_child_winner(t, 1) : 0;
}

size_t ds_tournament_winner(const ds_tournament_t *t) {
  if (t->config.players == 0 || !t->alive[t->tree[0]]) {
    return DS_TOURNAMENT_NONE;
  }
  return t->tree[0];
}

void ds_tournament_replay(ds_tournament_t *t, size_t player) {
  size_t k = t->config.players;
  size_t pos = (k + player) / 2;
  size_t w = player;
  size_t tmp;
  size_t l;
  size_t r;
  if (k <= 1) {
    t->tree[0] = 0;
    return;
  }
  if (t->config.kind == DS_TOURNAMENT_LOSER) {
    if (player != t->tree[0]) {
      /* the losers on the path only know about the old winner */
      ds_tournament_build(t);
      return;
    }
    while (pos >= 1) {
      if (km_beats(t, t->tree[pos], w)) {
        tmp = t->tree[pos];
        t->tree[pos] = w;
        w = tmp;
      }
      pos /= 2;
    }
    t->tree[0] = w;
    return;
  }
  while (pos >= 1) {
    l = 2 * pos >= k ? 2 * pos - k : t->tree[2 * pos];
    r = 2 * pos + 1 >= k ? 2 * pos + 1 - k : t->tree[2 * pos + 1];
    t->tree[pos] = km_beats(t, l, r) ? l : r;
    pos /= 2;
  }
  t->tree[0] = t->tree[1];
}

/* ---- k-way merge ---------------------------------------------------- */

size_t ds_kway_merge_buffer_size(size_t ways, size_t elem_size,
                                 ds_merge_policy_t policy) {
  (void)policy;
  return ds_tournament_buffer_size(ways, elem_size);
}

static void km_heap_sift_down(const ds_tournament_t *t, size_t *heap, size_t n,
                              size_t i) {
  size_t child;
  size_t tmp;
  for (;;) {
    child = 2 * i + 1;
    if (child >= n) {
      return;
    }
    if (child + 1 < n && km_beats(t, heap[child + 1], heap[child])) {
      child++;
    }
    if (!km_beats(t, heap[child], heap[i])) {
      return;
    }
    tmp = heap[i];
    heap[i] = heap[child];
    heap[child] = tmp;
    i = child;
  }
}

static ds_status_t km_fetch(ds_tournament_t *t, ds_merge_next_func_t next,
                            size_t source, void *user) {
  ds_status_t st = next(source, ds_tournament_slot(t, source), user);
  if (st == DS_OK) {
    t->alive[source] = 1;
    return DS_OK;
  }
  t->alive[source] = 0;
  return st == DS_NOT_FOUND ? DS_OK : st;
}

static ds_status_t km_run(ds_tournament_t *t, ds_merge_policy_t policy,
                          ds_merge_next_func_t next, ds_merge_emit_func_t emit,
                          void *user, size_t *out_count) {
  size_t k = t->config.players;
  size_t es = t->config.elem_size;
  size_t i;
  size_t j;
  size_t w;
  size_t n;
  size_t first;
  size_t count = 0;
  size_t *order = t->tree;
  ds_status_t st = DS_OK;
  for (i = 0; i < k; i++) {
    st = km_fetch(t, next, i, user);
    if (st != DS_OK) {
      return st;
    }
  }
  switch (policy) {
  case DS_MERGE_LOSER_TREE:
  case DS_MERGE_WINNER_TREE:
    ds_tournament_build(t);
    for (;;) {
      w = ds_tournament_winner(t);
      if (w == DS_TOURNAMENT_NONE) {
        break;
      }
      st = emit(t->values + w * es, w, user);
      if (st != DS_OK) {
        break;
      }
      count++;
      st = km_fetch(t, next, w, user);
      if (st != DS_OK) {
        break;
      }
      ds_tournament_replay(t, w);
    }
    break;
  case DS_MERGE_HEAP:
    n = 0;
    for (i = 0; i < k; i++) {
      if (t->alive[i]) {
        order[n++] = i;
      }
    }
    for (i = n / 2; i-- > 0;) {
      km_heap_sift_down(t, order, n, i);
    }
    while (n > 0) {
      w = order[0];
      st = emit(t->values + w * es, w, user);
      if (st != DS_OK) {
        break;
      }
      count++;
      st = km_fetch(t, next, w, user);
      if (st != DS_OK) {
        break;
      }
      if (!t->alive[w]) {
        order[0] = order[--n];
      }
      km_heap_sift_down(t, order, n, 0);
    }
    break;
  case DS_MERGE_IDEAL:
    n = 0;
    for (i = 0; i < k; i++) {
      if (!t->alive[i]) {
        continue;
      }
      j = n;
      while (j > 0 && km_beats(t, i, order[j - 1])) {
        order[j] = order[j - 1];
        j--;
      }
      order[j] = i;
      n++;
    }
    first = 0;
    while (first < n) {
      w = order[first];
      st = emit(t->values + w * es, w, user);
      if (st != DS_OK) {
        break;
      }
      count++;
      st = km_fetch(t, next, w, user);
      if (st != DS_OK) {
        break;
      }
      if (!t->alive[w]) {
        first++;
        continue;
      }
      j = first + 1;
      while (j < n && km_beats(t, order[j], w)) {
        order[j - 1] = order[j];
        j++;
      }
      order[j - 1] = w;
    }
    break;
  case DS_MERGE_LINEAR:
    for (;;) {
      w = DS_TOURNAMENT_NONE;
      for (i = 0; i < k; i++) {
        if (t->alive[i] && (w == DS_TOURNAMENT_NONE || km_beats(t, i, w))) {
          w = i;
        }
      }
      if (w == DS_TOURNAMENT_NONE) {
        break;
      }
      st = emit(t->values + w * es, w, user);
      if (st != DS_OK) {
        break;
      }
      count++;
      st = km_fetch(t, next, w, user);
      if (st != DS_OK) {
        break;
      }
    }
    break;
  default:
    return DS_ERR_INVALID;
  }
  if (out_count != NULL) {
    *out_count = count;
  }
  return st;
}

ds_status_t ds_kway_merge(const ds_kway_merge_config_t *config,
                          ds_merge_next_func_t next, ds_merge_emit_func_t emit,
                          size_t *out_count, ds_context_t *context) {
  ds_tournament_config_t tc;
  ds_tournament_t t;
  ds_status_t st;
  if (config == NULL || next == NULL || emit == NULL) {
    return DS_ERR_NULL;
  }
  if (config->policy < DS_MERGE_LOSER_TREE ||
      config->policy > DS_MERGE_LINEAR) {
    return DS_ERR_INVALID;
  }
  if (out_count != NULL) {
    *out_count = 0;
  }
  tc.players = config->ways;
  tc.elem_size = config->elem_size;
  tc.compare = config->compare;
  tc.user = config->user;
  tc.kind = config->policy == DS_MERGE_WINNER_TREE ? DS_TOURNAMENT_WINNER
                                                   : DS_TOURNAMENT_LOSER;
  if (config->buffer != NULL) {
    st =
        ds_tournament_init_buffer(&t, &tc, config->buffer, config->buffer_size);
  } else {
    st = ds_tournament_init(&t, &tc, context);
  }
  if (st != DS_OK) {
    return st;
  }
  st = km_run(&t, config->policy, next, emit, config->user, out_count);
  ds_tournament_destroy(&t);
  return st;
}

typedef struct km_arrays {
  const void *const *arrays;
  const size_t *counts;
  size_t *pos;
  size_t es;
  unsigned char *out;
  ds_compare_func_t compare;
  void *user;
} km_arrays_t;

static int km_arrays_compare(const void *a, const void *b, void *user) {
  const km_arrays_t *s = (const km_arrays_t *)user;
  return s->compare(a, b, s->user);
}

static ds_status_t km_arrays_next(size_t source, void *out, void *user) {
  km_arrays_t *s = (km_arrays_t *)user;
  if (s->pos[source] >= s->counts[source]) {
    return DS_NOT_FOUND;
  }
  memcpy(out, (const unsigned char *)s->arrays[source] + s->pos[source] * s->es,
         s->es);
  s->pos[source]++;
  return DS_OK;
}

static ds_status_t km_arrays_emit(const void *elem, size_t source, void *user) {
  km_arrays_t *s = (km_arrays_t *)user;
  (void)source;
  memcpy(s->out, elem, s->es);
  s->out += s->es;
  return DS_OK;
}

ds_status_t ds_kway_merge_arrays(const void *const *arrays,
                                 const size_t *counts, size_t ways,
                                 size_t elem_size, ds_compare_func_t compare,
                                 void *user, ds_merge_policy_t policy,
                                 void *out, ds_context_t *context) {
  km_arrays_t s;
  ds_kway_merge_config_t cfg;
  size_t i;
  size_t total = 0;
  ds_status_t st;
  if (compare == NULL || (ways > 0 && (arrays == NULL || counts == NULL))) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < ways; i++) {
    if (counts[i] > 0 && arrays[i] == NULL) {
      return DS_ERR_NULL;
    }
    total += counts[i];
  }
  if (total > 0 && out == NULL) {
    return DS_ERR_NULL;
  }
  if (ways == 0) {
    return DS_OK;
  }
  s.pos = (size_t *)ds_context_calloc(context, ways, sizeof(size_t));
  if (s.pos == NULL) {
    return DS_ERR_ALLOC;
  }
  s.arrays = arrays;
  s.counts = counts;
  s.es = elem_size;
  s.out = (unsigned char *)out;
  s.compare = compare;
  s.user = user;
  cfg.ways = ways;
  cfg.elem_size = elem_size;
  cfg.compare = km_arrays_compare;
  cfg.user = &s;
  cfg.policy = policy;
  cfg.buffer = NULL;
  cfg.buffer_size = 0;
  st = ds_kway_merge(&cfg, km_arrays_next, km_arrays_emit, NULL, context);
  ds_context_free(context, s.pos);
  return st;
}

/* ---- optimal merge pattern ------------------------------------------ */

typedef struct km_run_entry {
  size_t length;
  size_t id;
} km_run_entry_t;

static bool km_run_less(const km_run_entry_t *a, const km_run_entry_t *b) {
  if (a->length != b->length) {
    return a->length < b->length;
  }
  return a->id < b->id;
}

static void km_run_sift_down(km_run_entry_t *h, size_t n, size_t i) {
  size_t c;
  km_run_entry_t tmp;
  for (;;) {
    c = 2 * i + 1;
    if (c >= n) {
      return;
    }
    if (c + 1 < n && km_run_less(&h[c + 1], &h[c])) {
      c++;
    }
    if (!km_run_less(&h[c], &h[i])) {
      return;
    }
    tmp = h[i];
    h[i] = h[c];
    h[c] = tmp;
    i = c;
  }
}

static void km_run_sift_up(km_run_entry_t *h, size_t i) {
  km_run_entry_t tmp;
  while (i > 0 && km_run_less(&h[i], &h[(i - 1) / 2])) {
    tmp = h[i];
    h[i] = h[(i - 1) / 2];
    h[(i - 1) / 2] = tmp;
    i = (i - 1) / 2;
  }
}

ds_status_t ds_optimal_merge_plan(const size_t *lengths, size_t runs,
                                  size_t ways, size_t *out_parent,
                                  size_t *out_merges, double *out_cost,
                                  ds_context_t *context) {
  km_run_entry_t *heap;
  km_run_entry_t top;
  size_t dummies;
  size_t n;
  size_t i;
  size_t j;
  size_t merges = 0;
  size_t sum;
  double cost = 0.0;
  if (out_merges == NULL || out_cost == NULL ||
      (runs > 0 && (lengths == NULL || out_parent == NULL))) {
    return DS_ERR_NULL;
  }
  if (ways < 2) {
    return DS_ERR_INVALID;
  }
  *out_merges = 0;
  *out_cost = 0.0;
  if (runs == 0) {
    return DS_OK;
  }
  if (runs > KM_SIZE_MAX / 2 - ways) {
    return DS_ERR_OVERFLOW;
  }
  for (i = 0; i < 2 * runs; i++) {
    out_parent[i] = DS_TOURNAMENT_NONE;
  }
  if (runs == 1) {
    return DS_OK;
  }
  dummies = (ways - 1 - (runs - 1) % (ways - 1)) % (ways - 1);
  heap = (km_run_entry_t *)ds_context_alloc(
      context, (runs + dummies) * sizeof(km_run_entry_t));
  if (heap == NULL) {
    return DS_ERR_ALLOC;
  }
  n = 0;
  for (i = 0; i < dummies; i++) {
    heap[n].length = 0;
    heap[n].id = DS_TOURNAMENT_NONE; /* dummy, never reported */
    n++;
  }
  for (i = 0; i < runs; i++) {
    heap[n].length = lengths[i];
    heap[n].id = i;
    n++;
  }
  for (i = n / 2; i-- > 0;) {
    km_run_sift_down(heap, n, i);
  }
  while (n > 1) {
    sum = 0;
    for (j = 0; j < ways && n > 0; j++) {
      top = heap[0];
      heap[0] = heap[--n];
      km_run_sift_down(heap, n, 0);
      if (top.length > KM_SIZE_MAX - sum) {
        ds_context_free(context, heap);
        return DS_ERR_OVERFLOW;
      }
      sum += top.length;
      if (top.id != DS_TOURNAMENT_NONE) {
        out_parent[top.id] = runs + merges;
      }
    }
    cost += (double)sum;
    heap[n].length = sum;
    heap[n].id = runs + merges;
    km_run_sift_up(heap, n);
    n++;
    merges++;
  }
  ds_context_free(context, heap);
  *out_merges = merges;
  *out_cost = cost;
  return DS_OK;
}

ds_status_t ds_optimal_merge_arrays(const void *const *arrays,
                                    const size_t *counts, size_t runs,
                                    size_t ways, size_t elem_size,
                                    ds_compare_func_t compare, void *user,
                                    void *out, ds_context_t *context) {
  size_t *parent = NULL;
  size_t *len = NULL;
  size_t *start = NULL;
  size_t *child = NULL;
  const void **ptr = NULL;
  unsigned char **owned = NULL;
  size_t merges;
  size_t ids;
  size_t i;
  size_t m;
  size_t id;
  size_t c;
  double cost;
  ds_status_t st;
  if (compare == NULL || (runs > 0 && (arrays == NULL || counts == NULL))) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 || ways < 2) {
    return DS_ERR_INVALID;
  }
  if (runs == 0) {
    return DS_OK;
  }
  if (runs == 1) {
    if (counts[0] > 0) {
      if (out == NULL || arrays[0] == NULL) {
        return DS_ERR_NULL;
      }
      memmove(out, arrays[0], counts[0] * elem_size);
    }
    return DS_OK;
  }
  ids = 2 * runs;
  parent = (size_t *)ds_context_alloc(context, ids * sizeof(size_t));
  len = (size_t *)ds_context_calloc(context, ids, sizeof(size_t));
  start = (size_t *)ds_context_calloc(context, ids + 1, sizeof(size_t));
  child = (size_t *)ds_context_alloc(context, ids * sizeof(size_t));
  ptr = (const void **)ds_context_calloc(context, ids, sizeof(void *));
  owned = (unsigned char **)ds_context_calloc(context, ids, sizeof(void *));
  if (parent == NULL || len == NULL || start == NULL || child == NULL ||
      ptr == NULL || owned == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  st = ds_optimal_merge_plan(counts, runs, ways, parent, &merges, &cost,
                             context);
  if (st != DS_OK) {
    goto done;
  }
  for (i = 0; i < runs; i++) {
    len[i] = counts[i];
    ptr[i] = arrays[i];
  }
  /* children of merge m (run id runs + m), in id order */
  for (id = 0; id < runs + merges; id++) {
    if (parent[id] != DS_TOURNAMENT_NONE) {
      start[parent[id] - runs + 1]++;
    }
  }
  for (m = 0; m < merges; m++) {
    start[m + 1] += start[m];
  }
  for (id = 0; id < runs + merges; id++) {
    if (parent[id] != DS_TOURNAMENT_NONE) {
      child[start[parent[id] - runs]++] = id;
    }
  }
  for (m = merges; m > 0; m--) {
    start[m] = start[m - 1];
  }
  start[0] = 0;
  for (m = 0; m < merges; m++) {
    const void *ins[64];
    size_t lens[64];
    size_t nin = start[m + 1] - start[m];
    const void **inp = ins;
    size_t *lenp = lens;
    unsigned char *dst;
    id = runs + m;
    for (c = start[m]; c < start[m + 1]; c++) {
      len[id] += len[child[c]];
    }
    if (nin > 64) {
      inp = (const void **)ds_context_alloc(context, nin * sizeof(void *));
      lenp = (size_t *)ds_context_alloc(context, nin * sizeof(size_t));
      if (inp == NULL || lenp == NULL) {
        ds_context_free(context, (void *)inp);
        ds_context_free(context, lenp);
        st = DS_ERR_ALLOC;
        goto done;
      }
    }
    for (c = start[m]; c < start[m + 1]; c++) {
      inp[c - start[m]] = ptr[child[c]];
      lenp[c - start[m]] = len[child[c]];
    }
    if (m + 1 == merges) {
      dst = (unsigned char *)out;
    } else {
      dst = (unsigned char *)ds_context_alloc(
          context, (len[id] == 0 ? 1 : len[id]) * elem_size);
      owned[id] = dst;
    }
    if (dst == NULL && len[id] > 0) {
      st = m + 1 == merges ? DS_ERR_NULL : DS_ERR_ALLOC;
    } else {
      st = ds_kway_merge_arrays(inp, lenp, nin, elem_size, compare, user,
                                DS_MERGE_LOSER_TREE, dst, context);
    }
    if (inp != ins) {
      ds_context_free(context, (void *)inp);
      ds_context_free(context, lenp);
    }
    if (st != DS_OK) {
      goto done;
    }
    ptr[id] = dst;
    for (c = start[m]; c < start[m + 1]; c++) {
      if (owned[child[c]] != NULL) {
        ds_context_free(context, owned[child[c]]);
        owned[child[c]] = NULL;
      }
    }
  }
done:
  if (owned != NULL) {
    for (i = 0; i < ids; i++) {
      if (owned[i] != NULL) {
        ds_context_free(context, owned[i]);
      }
    }
  }
  ds_context_free(context, parent);
  ds_context_free(context, len);
  ds_context_free(context, start);
  ds_context_free(context, child);
  ds_context_free(context, (void *)ptr);
  ds_context_free(context, owned);
  return st;
}
