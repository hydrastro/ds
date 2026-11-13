#include "model_check.h"
#include <string.h>

#define MC_NONE (~(size_t)0)
#define MC_MAX_DEPTH 256

static ds_status_t mc_reserve(ds_context_t *ctx, void **p, size_t *cap,
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

/* ------------------------------------------------------------ Kripke */

ds_status_t ds_kripke_init(ds_kripke_t *k, size_t states, size_t props,
                           ds_context_t *context) {
  if (k == NULL) {
    return DS_ERR_NULL;
  }
  memset(k, 0, sizeof(*k));
  if (states == 0) {
    return DS_ERR_INVALID;
  }
  if (props > 0 && states > (size_t)-1 / props) {
    return DS_ERR_OVERFLOW;
  }
  k->label =
      (bool *)ds_context_calloc(context, states * props + 1, sizeof(bool));
  k->initial = (bool *)ds_context_calloc(context, states, sizeof(bool));
  if (k->label == NULL || k->initial == NULL) {
    ds_context_free(context, k->label);
    ds_context_free(context, k->initial);
    memset(k, 0, sizeof(*k));
    return DS_ERR_ALLOC;
  }
  k->states = states;
  k->props = props;
  k->context = context;
  return DS_OK;
}

static void mc_free_csr(ds_kripke_t *k) {
  ds_context_free(k->context, k->succ_off);
  ds_context_free(k->context, k->succ);
  ds_context_free(k->context, k->pred_off);
  ds_context_free(k->context, k->pred);
  k->succ_off = k->succ = k->pred_off = k->pred = NULL;
  k->built = false;
}

void ds_kripke_destroy(ds_kripke_t *k) {
  if (k == NULL) {
    return;
  }
  mc_free_csr(k);
  ds_context_free(k->context, k->label);
  ds_context_free(k->context, k->initial);
  ds_context_free(k->context, k->edge_from);
  ds_context_free(k->context, k->edge_to);
  memset(k, 0, sizeof(*k));
}

ds_status_t ds_kripke_add_edge(ds_kripke_t *k, size_t from, size_t to) {
  size_t cap;
  ds_status_t st;
  if (k == NULL) {
    return DS_ERR_NULL;
  }
  if (from >= k->states || to >= k->states) {
    return DS_ERR_RANGE;
  }
  cap = k->edge_capacity;
  st = mc_reserve(k->context, (void **)&k->edge_from, &cap, k->edges + 1,
                  sizeof(size_t));
  if (st == DS_OK) {
    cap = k->edge_capacity;
    st = mc_reserve(k->context, (void **)&k->edge_to, &cap, k->edges + 1,
                    sizeof(size_t));
  }
  if (st != DS_OK) {
    return st;
  }
  k->edge_capacity = cap;
  k->edge_from[k->edges] = from;
  k->edge_to[k->edges] = to;
  k->edges++;
  if (k->built) {
    mc_free_csr(k);
  }
  return DS_OK;
}

ds_status_t ds_kripke_set_label(ds_kripke_t *k, size_t state, size_t prop,
                                bool value) {
  if (k == NULL) {
    return DS_ERR_NULL;
  }
  if (state >= k->states || prop >= k->props) {
    return DS_ERR_RANGE;
  }
  k->label[state * k->props + prop] = value;
  return DS_OK;
}

ds_status_t ds_kripke_set_initial(ds_kripke_t *k, size_t state, bool value) {
  if (k == NULL) {
    return DS_ERR_NULL;
  }
  if (state >= k->states) {
    return DS_ERR_RANGE;
  }
  k->initial[state] = value;
  return DS_OK;
}

static ds_status_t mc_csr(ds_context_t *ctx, size_t n, size_t m,
                          const size_t *src, const size_t *dst, size_t **off,
                          size_t **adj) {
  size_t i;
  *off = (size_t *)ds_context_calloc(ctx, n + 1, sizeof(size_t));
  *adj = (size_t *)ds_context_alloc(ctx, (m + 1) * sizeof(size_t));
  if (*off == NULL || *adj == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < m; i++) {
    (*off)[src[i] + 1]++;
  }
  for (i = 0; i < n; i++) {
    (*off)[i + 1] += (*off)[i];
  }
  for (i = 0; i < m; i++) {
    (*adj)[(*off)[src[i]]++] = dst[i];
  }
  for (i = n; i > 0; i--) {
    (*off)[i] = (*off)[i - 1];
  }
  (*off)[0] = 0;
  return DS_OK;
}

static ds_status_t mc_build(ds_kripke_t *k) {
  ds_status_t st;
  if (k->built) {
    return DS_OK;
  }
  st = mc_csr(k->context, k->states, k->edges, k->edge_from, k->edge_to,
              &k->succ_off, &k->succ);
  if (st == DS_OK) {
    st = mc_csr(k->context, k->states, k->edges, k->edge_to, k->edge_from,
                &k->pred_off, &k->pred);
  }
  if (st != DS_OK) {
    mc_free_csr(k);
    return st;
  }
  k->built = true;
  return DS_OK;
}

bool ds_kripke_is_total(ds_kripke_t *k) {
  size_t s;
  if (k == NULL || mc_build(k) != DS_OK) {
    return false;
  }
  for (s = 0; s < k->states; s++) {
    if (k->succ_off[s] == k->succ_off[s + 1]) {
      return false;
    }
  }
  return true;
}

ds_status_t ds_kripke_make_total(ds_kripke_t *k) {
  size_t s;
  size_t n;
  bool *has;
  ds_status_t st = DS_OK;
  if (k == NULL) {
    return DS_ERR_NULL;
  }
  n = k->states;
  has = (bool *)ds_context_calloc(k->context, n, sizeof(bool));
  if (has == NULL) {
    return DS_ERR_ALLOC;
  }
  for (s = 0; s < k->edges; s++) {
    has[k->edge_from[s]] = true;
  }
  for (s = 0; st == DS_OK && s < n; s++) {
    if (!has[s]) {
      st = ds_kripke_add_edge(k, s, s);
    }
  }
  ds_context_free(k->context, has);
  return st;
}

ds_status_t ds_kripke_successors(ds_kripke_t *k, size_t state,
                                 const size_t **out, size_t *count) {
  ds_status_t st;
  if (k == NULL || out == NULL || count == NULL) {
    return DS_ERR_NULL;
  }
  if (state >= k->states) {
    return DS_ERR_RANGE;
  }
  st = mc_build(k);
  if (st != DS_OK) {
    return st;
  }
  *out = k->succ + k->succ_off[state];
  *count = k->succ_off[state + 1] - k->succ_off[state];
  return DS_OK;
}

/* ------------------------------------------------------- formulas */

ds_status_t ds_ctl_init(ds_ctl_t *ctl, ds_context_t *context) {
  if (ctl == NULL) {
    return DS_ERR_NULL;
  }
  memset(ctl, 0, sizeof(*ctl));
  ctl->context = context;
  return DS_OK;
}

void ds_ctl_destroy(ds_ctl_t *ctl) {
  if (ctl == NULL) {
    return;
  }
  ds_context_free(ctl->context, ctl->op);
  ds_context_free(ctl->context, ctl->a);
  ds_context_free(ctl->context, ctl->b);
  memset(ctl, 0, sizeof(*ctl));
}

static ds_status_t mc_node(ds_ctl_t *ctl, ds_ctl_op_t op, size_t a, size_t b,
                           size_t *out) {
  size_t cap = ctl->capacity;
  ds_status_t st =
      mc_reserve(ctl->context, (void **)&ctl->op, &cap, ctl->count + 1, 1);
  if (st == DS_OK) {
    cap = ctl->capacity;
    st = mc_reserve(ctl->context, (void **)&ctl->a, &cap, ctl->count + 1,
                    sizeof(size_t));
  }
  if (st == DS_OK) {
    cap = ctl->capacity;
    st = mc_reserve(ctl->context, (void **)&ctl->b, &cap, ctl->count + 1,
                    sizeof(size_t));
  }
  if (st != DS_OK) {
    return st;
  }
  ctl->capacity = cap;
  ctl->op[ctl->count] = (unsigned char)op;
  ctl->a[ctl->count] = a;
  ctl->b[ctl->count] = b;
  *out = ctl->count++;
  return DS_OK;
}

ds_status_t ds_ctl_const(ds_ctl_t *ctl, bool value, size_t *out) {
  if (ctl == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  return mc_node(ctl, value ? DS_CTL_TRUE : DS_CTL_FALSE, 0, 0, out);
}

ds_status_t ds_ctl_atom(ds_ctl_t *ctl, size_t prop, size_t *out) {
  if (ctl == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  return mc_node(ctl, DS_CTL_ATOM, prop, 0, out);
}

static bool mc_is_unary(ds_ctl_op_t op) {
  return op == DS_CTL_NOT || (op >= DS_CTL_EX && op <= DS_CTL_AG);
}

static bool mc_is_binary(ds_ctl_op_t op) {
  return (op >= DS_CTL_AND && op <= DS_CTL_IFF) ||
         (op >= DS_CTL_EU && op <= DS_CTL_AW);
}

ds_status_t ds_ctl_unary(ds_ctl_t *ctl, ds_ctl_op_t op, size_t a, size_t *out) {
  if (ctl == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!mc_is_unary(op)) {
    return DS_ERR_INVALID;
  }
  if (a >= ctl->count) {
    return DS_ERR_RANGE;
  }
  return mc_node(ctl, op, a, 0, out);
}

ds_status_t ds_ctl_binary(ds_ctl_t *ctl, ds_ctl_op_t op, size_t a, size_t b,
                          size_t *out) {
  if (ctl == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!mc_is_binary(op)) {
    return DS_ERR_INVALID;
  }
  if (a >= ctl->count || b >= ctl->count) {
    return DS_ERR_RANGE;
  }
  return mc_node(ctl, op, a, b, out);
}

/* --------------------------------------------------------- parser */

typedef struct mc_parser {
  ds_ctl_t *ctl;
  const char *text;
  size_t pos;
  const char *const *names;
  size_t count;
  size_t depth;
} mc_parser_t;

static bool mc_ident_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool mc_ident_char(char c) {
  return mc_ident_start(c) || (c >= '0' && c <= '9');
}

static void mc_skip(mc_parser_t *p) {
  while (p->text[p->pos] == ' ' || p->text[p->pos] == '\t' ||
         p->text[p->pos] == '\n' || p->text[p->pos] == '\r') {
    p->pos++;
  }
}

static size_t mc_word(mc_parser_t *p) {
  size_t n = 0;
  mc_skip(p);
  if (!mc_ident_start(p->text[p->pos])) {
    return 0;
  }
  while (mc_ident_char(p->text[p->pos + n])) {
    n++;
  }
  return n;
}

static bool mc_word_is(mc_parser_t *p, size_t n, const char *w) {
  return strlen(w) == n && strncmp(p->text + p->pos, w, n) == 0;
}

static bool mc_accept(mc_parser_t *p, const char *tok) {
  size_t n = strlen(tok);
  mc_skip(p);
  if (strncmp(p->text + p->pos, tok, n) == 0) {
    p->pos += n;
    return true;
  }
  return false;
}

static ds_status_t mc_iff(mc_parser_t *p, size_t *out);

static ds_status_t mc_unary_expr(mc_parser_t *p, size_t *out) {
  static const char *const temporal[6] = {"EX", "AX", "EF", "AF", "EG", "AG"};
  static const ds_ctl_op_t ops[6] = {DS_CTL_EX, DS_CTL_AX, DS_CTL_EF,
                                     DS_CTL_AF, DS_CTL_EG, DS_CTL_AG};
  size_t n;
  size_t i;
  size_t a;
  ds_status_t st;
  if (++p->depth > MC_MAX_DEPTH) {
    return DS_ERR_RANGE;
  }
  mc_skip(p);
  if (mc_accept(p, "!")) {
    st = mc_unary_expr(p, &a);
    if (st == DS_OK) {
      st = mc_node(p->ctl, DS_CTL_NOT, a, 0, out);
    }
    p->depth--;
    return st;
  }
  if (mc_accept(p, "(")) {
    st = mc_iff(p, out);
    if (st == DS_OK && !mc_accept(p, ")")) {
      st = DS_ERR_INVALID;
    }
    p->depth--;
    return st;
  }
  n = mc_word(p);
  if (n == 0) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < 6; i++) {
    if (mc_word_is(p, n, temporal[i])) {
      p->pos += n;
      st = mc_unary_expr(p, &a);
      if (st == DS_OK) {
        st = mc_node(p->ctl, ops[i], a, 0, out);
      }
      p->depth--;
      return st;
    }
  }
  if (mc_word_is(p, n, "E") || mc_word_is(p, n, "A")) {
    bool exists = p->text[p->pos] == 'E';
    size_t save = p->pos;
    size_t b;
    size_t w;
    p->pos += n;
    if (mc_accept(p, "[")) {
      bool weak;
      st = mc_iff(p, &a);
      if (st != DS_OK) {
        return st;
      }
      w = mc_word(p);
      if (w == 0 || !(mc_word_is(p, w, "U") || mc_word_is(p, w, "W"))) {
        return DS_ERR_INVALID;
      }
      weak = p->text[p->pos] == 'W';
      p->pos += w;
      st = mc_iff(p, &b);
      if (st != DS_OK) {
        return st;
      }
      if (!mc_accept(p, "]")) {
        return DS_ERR_INVALID;
      }
      p->depth--;
      return mc_node(p->ctl,
                     exists ? (weak ? DS_CTL_EW : DS_CTL_EU)
                            : (weak ? DS_CTL_AW : DS_CTL_AU),
                     a, b, out);
    }
    p->pos = save; /* a proposition called E or A */
  }
  if (mc_word_is(p, n, "true") || mc_word_is(p, n, "false")) {
    bool v = p->text[p->pos] == 't';
    p->pos += n;
    p->depth--;
    return mc_node(p->ctl, v ? DS_CTL_TRUE : DS_CTL_FALSE, 0, 0, out);
  }
  for (i = 0; i < p->count; i++) {
    if (p->names[i] != NULL && mc_word_is(p, n, p->names[i])) {
      p->pos += n;
      p->depth--;
      return mc_node(p->ctl, DS_CTL_ATOM, i, 0, out);
    }
  }
  return DS_ERR_INVALID;
}

static ds_status_t mc_and(mc_parser_t *p, size_t *out) {
  ds_status_t st = mc_unary_expr(p, out);
  while (st == DS_OK && mc_accept(p, "&")) {
    size_t b;
    (void)mc_accept(p, "&"); /* && */
    st = mc_unary_expr(p, &b);
    if (st == DS_OK) {
      st = mc_node(p->ctl, DS_CTL_AND, *out, b, out);
    }
  }
  return st;
}

static ds_status_t mc_or(mc_parser_t *p, size_t *out) {
  ds_status_t st = mc_and(p, out);
  while (st == DS_OK && mc_accept(p, "|")) {
    size_t b;
    (void)mc_accept(p, "|"); /* || */
    st = mc_and(p, &b);
    if (st == DS_OK) {
      st = mc_node(p->ctl, DS_CTL_OR, *out, b, out);
    }
  }
  return st;
}

static ds_status_t mc_implies(mc_parser_t *p, size_t *out) {
  ds_status_t st;
  size_t b;
  if (++p->depth > MC_MAX_DEPTH) {
    return DS_ERR_RANGE;
  }
  st = mc_or(p, out);
  if (st == DS_OK && mc_accept(p, "->")) {
    st = mc_implies(p, &b);
    if (st == DS_OK) {
      st = mc_node(p->ctl, DS_CTL_IMPLIES, *out, b, out);
    }
  }
  p->depth--;
  return st;
}

static ds_status_t mc_iff(mc_parser_t *p, size_t *out) {
  ds_status_t st;
  if (++p->depth > MC_MAX_DEPTH) {
    return DS_ERR_RANGE;
  }
  st = mc_implies(p, out);
  while (st == DS_OK && mc_accept(p, "<->")) {
    size_t b;
    st = mc_implies(p, &b);
    if (st == DS_OK) {
      st = mc_node(p->ctl, DS_CTL_IFF, *out, b, out);
    }
  }
  p->depth--;
  return st;
}

ds_status_t ds_ctl_parse(ds_ctl_t *ctl, const char *text,
                         const char *const *names, size_t count, size_t *out,
                         size_t *error_at) {
  mc_parser_t p;
  size_t keep;
  ds_status_t st;
  if (ctl == NULL || text == NULL || out == NULL ||
      (count > 0 && names == NULL)) {
    return DS_ERR_NULL;
  }
  p.ctl = ctl;
  p.text = text;
  p.pos = 0;
  p.names = names;
  p.count = count;
  p.depth = 0;
  keep = ctl->count;
  st = mc_iff(&p, out);
  if (st == DS_OK) {
    mc_skip(&p);
    if (text[p.pos] != '\0') {
      st = DS_ERR_INVALID;
    }
  }
  if (st != DS_OK) {
    ctl->count = keep; /* drop partial nodes */
    if (error_at != NULL) {
      *error_at = p.pos;
    }
  }
  return st;
}

/* ------------------------------------------------------- checking */

typedef struct mc_ws {
  ds_kripke_t *k;
  size_t *queue;
  size_t *cnt;
  size_t *idx; /* Tarjan */
  size_t *low;
  size_t *stk;
  size_t *cs_v;
  size_t *cs_e;
  bool *on;
  bool *t1;
  bool *t2;
  bool *t3;
  const bool *fair_states; /* NULL without fairness */
  const bool *const *fair;
  size_t fair_count;
  size_t *comp; /* optional: the fair component of each state, or NONE */
} mc_ws_t;

static void mc_ex(mc_ws_t *w, const bool *a, bool *out) {
  ds_kripke_t *k = w->k;
  size_t s;
  size_t e;
  for (s = 0; s < k->states; s++) {
    out[s] = false;
  }
  for (s = 0; s < k->states; s++) {
    if (!a[s] || (w->fair_states != NULL && !w->fair_states[s])) {
      continue;
    }
    for (e = k->pred_off[s]; e < k->pred_off[s + 1]; e++) {
      out[k->pred[e]] = true;
    }
  }
}

/* E[a U b]; a may be NULL (true) */
static void mc_eu(mc_ws_t *w, const bool *a, const bool *b, bool *out) {
  ds_kripke_t *k = w->k;
  size_t head = 0;
  size_t tail = 0;
  size_t s;
  size_t e;
  for (s = 0; s < k->states; s++) {
    out[s] = b[s] && (w->fair_states == NULL || w->fair_states[s]);
    if (out[s]) {
      w->queue[tail++] = s;
    }
  }
  while (head < tail) {
    s = w->queue[head++];
    for (e = k->pred_off[s]; e < k->pred_off[s + 1]; e++) {
      size_t p = k->pred[e];
      if (!out[p] && (a == NULL || a[p])) {
        out[p] = true;
        w->queue[tail++] = p;
      }
    }
  }
}

/* EG a as a greatest fixpoint (no fairness) */
static void mc_eg_plain(mc_ws_t *w, const bool *a, bool *out) {
  ds_kripke_t *k = w->k;
  size_t tail = 0;
  size_t s;
  size_t e;
  for (s = 0; s < k->states; s++) {
    out[s] = a == NULL || a[s];
  }
  for (s = 0; s < k->states; s++) {
    w->cnt[s] = 0;
    if (!out[s]) {
      continue;
    }
    for (e = k->succ_off[s]; e < k->succ_off[s + 1]; e++) {
      if (out[k->succ[e]]) {
        w->cnt[s]++;
      }
    }
    if (w->cnt[s] == 0) {
      w->queue[tail++] = s;
    }
  }
  while (tail > 0) {
    s = w->queue[--tail];
    out[s] = false;
    for (e = k->pred_off[s]; e < k->pred_off[s + 1]; e++) {
      size_t p = k->pred[e];
      if (out[p] && w->cnt[p] > 0 && --w->cnt[p] == 0) {
        w->queue[tail++] = p;
      }
    }
  }
}

/* EG a under fairness: reach, inside a, a nontrivial SCC of the
 * a-subgraph meeting every fairness set */
static void mc_eg_fair(mc_ws_t *w, const bool *a, bool *out) {
  ds_kripke_t *k = w->k;
  size_t n = k->states;
  size_t counter = 0;
  size_t sp = 0;
  size_t root;
  size_t s;
  size_t head = 0;
  size_t tail = 0;
  bool *target = w->t3;
  for (s = 0; s < n; s++) {
    w->idx[s] = MC_NONE;
    w->on[s] = false;
    target[s] = false;
    if (w->comp != NULL) {
      w->comp[s] = MC_NONE;
    }
  }
  for (root = 0; root < n; root++) {
    size_t csp = 0;
    if ((a != NULL && !a[root]) || w->idx[root] != MC_NONE) {
      continue;
    }
    w->cs_v[csp] = root;
    w->cs_e[csp++] = k->succ_off[root];
    w->idx[root] = w->low[root] = counter++;
    w->stk[sp++] = root;
    w->on[root] = true;
    while (csp > 0) {
      size_t v = w->cs_v[csp - 1];
      bool pushed = false;
      while (w->cs_e[csp - 1] < k->succ_off[v + 1]) {
        size_t u = k->succ[w->cs_e[csp - 1]++];
        if (a != NULL && !a[u]) {
          continue;
        }
        if (w->idx[u] == MC_NONE) {
          w->idx[u] = w->low[u] = counter++;
          w->stk[sp++] = u;
          w->on[u] = true;
          w->cs_v[csp] = u;
          w->cs_e[csp++] = k->succ_off[u];
          pushed = true;
          break;
        }
        if (w->on[u] && w->idx[u] < w->low[v]) {
          w->low[v] = w->idx[u];
        }
      }
      if (pushed) {
        continue;
      }
      if (w->low[v] == w->idx[v]) {
        /* pop the component; decide whether it is fair */
        size_t first = sp;
        size_t size;
        size_t i;
        size_t f;
        bool fair = true;
        bool nontrivial;
        do {
          first--;
        } while (w->stk[first] != v);
        size = sp - first;
        nontrivial = size > 1;
        if (!nontrivial) {
          size_t e;
          for (e = k->succ_off[v]; e < k->succ_off[v + 1]; e++) {
            if (k->succ[e] == v) {
              nontrivial = true;
            }
          }
        }
        for (f = 0; fair && f < w->fair_count; f++) {
          bool meets = false;
          for (i = first; !meets && i < sp; i++) {
            meets = w->fair[f][w->stk[i]];
          }
          fair = meets;
        }
        for (i = first; i < sp; i++) {
          w->on[w->stk[i]] = false;
          if (nontrivial && fair) {
            target[w->stk[i]] = true;
            if (w->comp != NULL) {
              w->comp[w->stk[i]] = v; /* named by its root */
            }
          }
        }
        sp = first;
      }
      csp--;
      if (csp > 0) {
        size_t u = w->cs_v[csp - 1];
        if (w->low[v] < w->low[u]) {
          w->low[u] = w->low[v];
        }
      }
    }
  }
  /* backward reachability of the fair components inside a */
  for (s = 0; s < n; s++) {
    out[s] = target[s];
    if (out[s]) {
      w->queue[tail++] = s;
    }
  }
  while (head < tail) {
    size_t e;
    s = w->queue[head++];
    for (e = k->pred_off[s]; e < k->pred_off[s + 1]; e++) {
      size_t p = k->pred[e];
      if (!out[p] && (a == NULL || a[p])) {
        out[p] = true;
        w->queue[tail++] = p;
      }
    }
  }
}

static void mc_eg(mc_ws_t *w, const bool *a, bool *out) {
  if (w->fair_count > 0) {
    mc_eg_fair(w, a, out);
  } else {
    mc_eg_plain(w, a, out);
  }
}

static void mc_not(const bool *a, bool *out, size_t n) {
  size_t s;
  for (s = 0; s < n; s++) {
    out[s] = !a[s];
  }
}

static void mc_eval(mc_ws_t *w, ds_ctl_op_t op, const bool *a, const bool *b,
                    size_t atom, bool *out) {
  ds_kripke_t *k = w->k;
  size_t n = k->states;
  size_t s;
  switch (op) {
  case DS_CTL_TRUE:
  case DS_CTL_FALSE:
    for (s = 0; s < n; s++) {
      out[s] = op == DS_CTL_TRUE;
    }
    break;
  case DS_CTL_ATOM:
    for (s = 0; s < n; s++) {
      out[s] = atom < k->props && k->label[s * k->props + atom];
    }
    break;
  case DS_CTL_NOT:
    mc_not(a, out, n);
    break;
  case DS_CTL_AND:
  case DS_CTL_OR:
  case DS_CTL_IMPLIES:
  case DS_CTL_IFF:
    for (s = 0; s < n; s++) {
      out[s] = op == DS_CTL_AND       ? (a[s] && b[s])
               : op == DS_CTL_OR      ? (a[s] || b[s])
               : op == DS_CTL_IMPLIES ? (!a[s] || b[s])
                                      : (a[s] == b[s]);
    }
    break;
  case DS_CTL_EX:
    mc_ex(w, a, out);
    break;
  case DS_CTL_AX: /* !EX !a */
    mc_not(a, w->t1, n);
    mc_ex(w, w->t1, out);
    mc_not(out, out, n);
    break;
  case DS_CTL_EF:
    mc_eu(w, NULL, a, out);
    break;
  case DS_CTL_AG: /* !EF !a */
    mc_not(a, w->t1, n);
    mc_eu(w, NULL, w->t1, out);
    mc_not(out, out, n);
    break;
  case DS_CTL_EG:
    mc_eg(w, a, out);
    break;
  case DS_CTL_AF: /* !EG !a */
    mc_not(a, w->t1, n);
    mc_eg(w, w->t1, out);
    mc_not(out, out, n);
    break;
  case DS_CTL_EU:
    mc_eu(w, a, b, out);
    break;
  case DS_CTL_EW: /* E[a U b] | EG a */
    mc_eu(w, a, b, out);
    mc_eg(w, a, w->t1);
    for (s = 0; s < n; s++) {
      out[s] = out[s] || w->t1[s];
    }
    break;
  case DS_CTL_AU: /* !(E[!b U (!a & !b)] | EG !b) */
  case DS_CTL_AW: /* !E[!b U (!a & !b)] */
    for (s = 0; s < n; s++) {
      w->t1[s] = !b[s];
      w->t2[s] = !a[s] && !b[s];
    }
    mc_eu(w, w->t1, w->t2, out);
    if (op == DS_CTL_AU) {
      mc_eg(w, w->t1, w->t2);
      for (s = 0; s < n; s++) {
        out[s] = out[s] || w->t2[s];
      }
    }
    mc_not(out, out, n);
    break;
  }
}

ds_status_t ds_ctl_check(ds_kripke_t *k, const ds_ctl_t *ctl, size_t formula,
                         const bool *const *fair, size_t fair_count,
                         bool *sat) {
  mc_ws_t w;
  ds_context_t *ctx;
  size_t n;
  bool *need = NULL;
  bool **buf = NULL;
  bool *fair_states = NULL;
  size_t *mem = NULL;
  bool *bmem = NULL;
  size_t i;
  ds_status_t st;
  if (k == NULL || ctl == NULL || sat == NULL ||
      (fair_count > 0 && fair == NULL)) {
    return DS_ERR_NULL;
  }
  if (formula >= ctl->count) {
    return DS_ERR_RANGE;
  }
  for (i = 0; i < fair_count; i++) {
    if (fair[i] == NULL) {
      return DS_ERR_NULL;
    }
  }
  st = mc_build(k);
  if (st != DS_OK) {
    return st;
  }
  ctx = k->context;
  n = k->states;
  memset(&w, 0, sizeof(w));
  w.k = k;
  w.fair = fair;
  w.fair_count = fair_count;
  mem = (size_t *)ds_context_alloc(ctx, 7 * n * sizeof(size_t));
  bmem = (bool *)ds_context_alloc(ctx, 5 * n * sizeof(bool));
  need = (bool *)ds_context_calloc(ctx, formula + 1, sizeof(bool));
  buf = (bool **)ds_context_calloc(ctx, formula + 1, sizeof(bool *));
  if (mem == NULL || bmem == NULL || need == NULL || buf == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  w.queue = mem;
  w.cnt = mem + n;
  w.idx = mem + 2 * n;
  w.low = mem + 3 * n;
  w.stk = mem + 4 * n;
  w.cs_v = mem + 5 * n;
  w.cs_e = mem + 6 * n;
  w.on = bmem;
  w.t1 = bmem + n;
  w.t2 = bmem + 2 * n;
  w.t3 = bmem + 3 * n;
  fair_states = bmem + 4 * n;
  if (fair_count > 0) {
    mc_eg_fair(&w, NULL, fair_states);
    w.fair_states = fair_states;
  }
  need[formula] = true;
  for (i = formula + 1; i-- > 0;) {
    ds_ctl_op_t op = (ds_ctl_op_t)ctl->op[i];
    if (!need[i]) {
      continue;
    }
    if (mc_is_unary(op) || mc_is_binary(op)) {
      if (ctl->a[i] >= i || (mc_is_binary(op) && ctl->b[i] >= i)) {
        st = DS_ERR_INVALID;
        goto done;
      }
      need[ctl->a[i]] = true;
      if (mc_is_binary(op)) {
        need[ctl->b[i]] = true;
      }
    }
  }
  for (i = 0; i <= formula; i++) {
    ds_ctl_op_t op = (ds_ctl_op_t)ctl->op[i];
    bool *out;
    if (!need[i]) {
      continue;
    }
    out = i == formula ? sat : (bool *)ds_context_alloc(ctx, n * sizeof(bool));
    if (out == NULL) {
      st = DS_ERR_ALLOC;
      goto done;
    }
    buf[i] = out;
    mc_eval(&w, op, mc_is_unary(op) || mc_is_binary(op) ? buf[ctl->a[i]] : NULL,
            mc_is_binary(op) ? buf[ctl->b[i]] : NULL, ctl->a[i], out);
  }
done:
  if (buf != NULL) {
    for (i = 0; i < formula; i++) {
      ds_context_free(ctx, buf[i]);
    }
  }
  ds_context_free(ctx, (void *)buf);
  ds_context_free(ctx, need);
  ds_context_free(ctx, mem);
  ds_context_free(ctx, bmem);
  return st;
}

ds_status_t ds_ctl_holds(ds_kripke_t *k, const ds_ctl_t *ctl, size_t formula,
                         const bool *const *fair, size_t fair_count,
                         bool *out) {
  bool *sat;
  bool any = false;
  size_t s;
  ds_status_t st;
  if (k == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  for (s = 0; s < k->states; s++) {
    any = any || k->initial[s];
  }
  if (!any) {
    return DS_ERR_STATE;
  }
  sat = (bool *)ds_context_alloc(k->context, k->states * sizeof(bool));
  if (sat == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ds_ctl_check(k, ctl, formula, fair, fair_count, sat);
  if (st == DS_OK) {
    *out = true;
    for (s = 0; s < k->states; s++) {
      if (k->initial[s] && !sat[s]) {
        *out = false;
      }
    }
  }
  ds_context_free(k->context, sat);
  return st;
}

ds_status_t ds_ctl_witness_until(ds_kripke_t *k, const bool *a, const bool *b,
                                 size_t start, size_t *path, size_t capacity,
                                 size_t *length) {
  size_t *parent;
  size_t *queue;
  size_t head = 0;
  size_t tail = 0;
  size_t found = MC_NONE;
  size_t s;
  ds_status_t st = DS_OK;
  if (k == NULL || a == NULL || b == NULL || length == NULL ||
      (capacity > 0 && path == NULL)) {
    return DS_ERR_NULL;
  }
  if (start >= k->states) {
    return DS_ERR_RANGE;
  }
  st = mc_build(k);
  if (st != DS_OK) {
    return st;
  }
  parent =
      (size_t *)ds_context_alloc(k->context, 2 * k->states * sizeof(size_t));
  if (parent == NULL) {
    return DS_ERR_ALLOC;
  }
  queue = parent + k->states;
  for (s = 0; s < k->states; s++) {
    parent[s] = MC_NONE;
  }
  parent[start] = start;
  queue[tail++] = start;
  while (head < tail && found == MC_NONE) {
    size_t e;
    s = queue[head++];
    if (b[s]) {
      found = s;
      break;
    }
    if (!a[s]) {
      continue;
    }
    for (e = k->succ_off[s]; e < k->succ_off[s + 1]; e++) {
      size_t u = k->succ[e];
      if (parent[u] == MC_NONE) {
        parent[u] = s;
        queue[tail++] = u;
      }
    }
  }
  if (found == MC_NONE) {
    st = DS_ERR_STATE;
  } else {
    size_t len = 1;
    size_t i;
    for (s = found; s != start; s = parent[s]) {
      len++;
    }
    *length = len;
    if (len > capacity) {
      st = DS_ERR_RANGE;
    } else {
      s = found;
      for (i = len; i-- > 0;) {
        path[i] = s;
        s = parent[s];
      }
    }
  }
  ds_context_free(k->context, parent);
  return st;
}

ds_status_t ds_ctl_witness_globally(ds_kripke_t *k, const bool *a, size_t start,
                                    size_t *path, size_t capacity,
                                    size_t *length, size_t *loop_at) {
  mc_ws_t w;
  size_t *mem;
  bool *eg;
  size_t n;
  size_t s;
  size_t len = 0;
  ds_status_t st = DS_OK;
  if (k == NULL || a == NULL || length == NULL || loop_at == NULL ||
      (capacity > 0 && path == NULL)) {
    return DS_ERR_NULL;
  }
  if (start >= k->states) {
    return DS_ERR_RANGE;
  }
  st = mc_build(k);
  if (st != DS_OK) {
    return st;
  }
  n = k->states;
  mem = (size_t *)ds_context_alloc(k->context, 3 * n * sizeof(size_t));
  eg = (bool *)ds_context_alloc(k->context, n * sizeof(bool));
  if (mem == NULL || eg == NULL) {
    ds_context_free(k->context, mem);
    ds_context_free(k->context, eg);
    return DS_ERR_ALLOC;
  }
  memset(&w, 0, sizeof(w));
  w.k = k;
  w.queue = mem;
  w.cnt = mem + n;
  mc_eg_plain(&w, a, eg);
  if (!eg[start]) {
    st = DS_ERR_STATE;
  } else {
    size_t *seen = mem + 2 * n; /* position on the path, or NONE */
    for (s = 0; s < n; s++) {
      seen[s] = MC_NONE;
    }
    s = start;
    while (seen[s] == MC_NONE) {
      size_t e;
      seen[s] = len;
      if (len < capacity) {
        path[len] = s;
      }
      len++;
      for (e = k->succ_off[s]; e < k->succ_off[s + 1]; e++) {
        if (eg[k->succ[e]]) {
          break;
        }
      }
      s = k->succ[e]; /* an EG state always has an EG successor */
    }
    *length = len;
    *loop_at = seen[s];
    if (len > capacity) {
      st = DS_ERR_RANGE;
    }
  }
  ds_context_free(k->context, mem);
  ds_context_free(k->context, eg);
  return st;
}

/* ---- witnesses under fairness ---- */

/* what a breadth-first search may use and where it stops */
typedef struct mc_bfs {
  const bool *allow; /* states a path may use (NULL: all) */
  const size_t *comp;
  size_t comp_id;    /* with comp: only states of this component */
  const bool *goal;  /* goal states, or NULL */
  size_t goal_state; /* or this one state (MC_NONE: unused) */
  bool any_comp;     /* or any state with comp != NONE */
} mc_bfs_t;

static bool mc_bfs_allowed(const mc_bfs_t *b, size_t s) {
  return (b->allow == NULL || b->allow[s]) &&
         (b->comp == NULL || b->comp_id == MC_NONE || b->comp[s] == b->comp_id);
}

static bool mc_bfs_goal(const mc_bfs_t *b, size_t s) {
  return (b->goal != NULL && b->goal[s]) || s == b->goal_state ||
         (b->any_comp && b->comp[s] != MC_NONE);
}

/* the shortest path from `from` (or from its successors, when a path of
 * at least one edge is needed) to a goal, appended to path after its
 * first state: path[*len ..), counting past capacity. DS_ERR_STATE when
 * no goal is reachable. */
static ds_status_t mc_bfs_append(ds_kripke_t *k, const mc_bfs_t *b, size_t from,
                                 bool one_edge, size_t *parent, size_t *queue,
                                 size_t *path, size_t capacity, size_t *len,
                                 size_t *end) {
  size_t head = 0;
  size_t tail = 0;
  size_t found = MC_NONE;
  size_t s;
  size_t seg;
  size_t i;
  for (s = 0; s < k->states; s++) {
    parent[s] = MC_NONE;
  }
  if (!one_edge && mc_bfs_goal(b, from)) {
    *end = from;
    return DS_OK;
  }
  if (!one_edge) {
    parent[from] = from;
  }
  queue[tail++] = from;
  while (head < tail && found == MC_NONE) {
    size_t e;
    s = queue[head++];
    for (e = k->succ_off[s]; e < k->succ_off[s + 1]; e++) {
      size_t u = k->succ[e];
      if (parent[u] != MC_NONE || !mc_bfs_allowed(b, u)) {
        continue;
      }
      parent[u] = s;
      if (mc_bfs_goal(b, u)) {
        found = u;
        break;
      }
      queue[tail++] = u;
    }
  }
  if (found == MC_NONE) {
    return DS_ERR_STATE;
  }
  /* states after `from`, written back to front */
  seg = 0;
  s = found;
  do {
    seg++;
    s = parent[s];
  } while (s != from);
  s = found;
  for (i = seg; i-- > 0;) {
    if (*len + i < capacity) {
      path[*len + i] = s;
    }
    s = parent[s];
  }
  *len += seg;
  *end = found;
  return DS_OK;
}

ds_status_t ds_ctl_witness_until_fair(ds_kripke_t *k, const bool *a,
                                      const bool *b, const bool *const *fair,
                                      size_t fair_count, size_t start,
                                      size_t *path, size_t capacity,
                                      size_t *length) {
  mc_ws_t w;
  size_t *mem;
  bool *bmem;
  size_t n;
  size_t s;
  ds_status_t st;
  if (fair_count == 0) {
    return ds_ctl_witness_until(k, a, b, start, path, capacity, length);
  }
  if (k == NULL || a == NULL || b == NULL || fair == NULL || length == NULL) {
    return DS_ERR_NULL;
  }
  for (s = 0; s < fair_count; s++) {
    if (fair[s] == NULL) {
      return DS_ERR_NULL;
    }
  }
  st = mc_build(k);
  if (st != DS_OK) {
    return st;
  }
  n = k->states;
  mem = (size_t *)ds_context_alloc(k->context, 6 * (n + 1) * sizeof(size_t));
  bmem = (bool *)ds_context_alloc(k->context, 3 * (n + 1) * sizeof(bool));
  if (mem == NULL || bmem == NULL) {
    ds_context_free(k->context, mem);
    ds_context_free(k->context, bmem);
    return DS_ERR_ALLOC;
  }
  memset(&w, 0, sizeof(w));
  w.k = k;
  w.queue = mem;
  w.idx = mem + n;
  w.low = mem + 2 * n;
  w.stk = mem + 3 * n;
  w.cs_v = mem + 4 * n;
  w.cs_e = mem + 5 * n;
  w.on = bmem;
  w.t3 = bmem + n;
  w.fair = fair;
  w.fair_count = fair_count;
  /* the goal: b-states from which a fair path goes on */
  mc_eg_fair(&w, NULL, bmem + 2 * n);
  for (s = 0; s < n; s++) {
    bmem[2 * n + s] = bmem[2 * n + s] && b[s];
  }
  st = ds_ctl_witness_until(k, a, bmem + 2 * n, start, path, capacity, length);
  ds_context_free(k->context, mem);
  ds_context_free(k->context, bmem);
  return st;
}

ds_status_t ds_ctl_witness_globally_fair(ds_kripke_t *k, const bool *a,
                                         const bool *const *fair,
                                         size_t fair_count, size_t start,
                                         size_t *path, size_t capacity,
                                         size_t *length, size_t *loop_at) {
  mc_ws_t w;
  mc_bfs_t spec;
  size_t *mem;
  bool *bmem;
  bool *eg;
  size_t *parent;
  size_t *queue;
  size_t n;
  size_t len = 0;
  size_t r;
  size_t cur;
  size_t f;
  ds_status_t st;
  if (fair_count == 0) {
    return ds_ctl_witness_globally(k, a, start, path, capacity, length,
                                   loop_at);
  }
  if (k == NULL || a == NULL || fair == NULL || length == NULL ||
      loop_at == NULL || (capacity > 0 && path == NULL)) {
    return DS_ERR_NULL;
  }
  for (f = 0; f < fair_count; f++) {
    if (fair[f] == NULL) {
      return DS_ERR_NULL;
    }
  }
  if (start >= k->states) {
    return DS_ERR_RANGE;
  }
  st = mc_build(k);
  if (st != DS_OK) {
    return st;
  }
  n = k->states;
  mem = (size_t *)ds_context_alloc(k->context, 9 * (n + 1) * sizeof(size_t));
  bmem = (bool *)ds_context_alloc(k->context, 3 * (n + 1) * sizeof(bool));
  if (mem == NULL || bmem == NULL) {
    ds_context_free(k->context, mem);
    ds_context_free(k->context, bmem);
    return DS_ERR_ALLOC;
  }
  memset(&w, 0, sizeof(w));
  w.k = k;
  w.queue = mem;
  w.idx = mem + n;
  w.low = mem + 2 * n;
  w.stk = mem + 3 * n;
  w.cs_v = mem + 4 * n;
  w.cs_e = mem + 5 * n;
  w.comp = mem + 6 * n;
  parent = mem + 7 * n;
  queue = mem + 8 * n;
  w.on = bmem;
  w.t3 = bmem + n;
  eg = bmem + 2 * n;
  w.fair = fair;
  w.fair_count = fair_count;
  mc_eg_fair(&w, a, eg);
  if (!eg[start]) {
    st = DS_ERR_STATE;
    goto done;
  }
  /* the stem: inside EG a to a fair component */
  memset(&spec, 0, sizeof(spec));
  spec.allow = eg;
  spec.comp = w.comp;
  spec.comp_id = MC_NONE;
  spec.goal_state = MC_NONE;
  spec.any_comp = true;
  if (capacity > 0) {
    path[0] = start;
  }
  len = 1;
  st = mc_bfs_append(k, &spec, start, false, parent, queue, path, capacity,
                     &len, &r);
  if (st != DS_OK) {
    goto done;
  }
  *loop_at = len - 1;
  /* the loop: inside r's component through every fairness set, then back
   * to r along at least one edge */
  spec.allow = NULL;
  spec.comp_id = w.comp[r];
  spec.any_comp = false;
  cur = r;
  for (f = 0; f < fair_count && st == DS_OK; f++) {
    spec.goal = fair[f];
    st = mc_bfs_append(k, &spec, cur, false, parent, queue, path, capacity,
                       &len, &cur);
  }
  if (st == DS_OK) {
    spec.goal = NULL;
    spec.goal_state = r;
    st = mc_bfs_append(k, &spec, cur, true, parent, queue, path, capacity, &len,
                       &cur);
    len--; /* the loop ends by stepping back to r, which is not repeated */
  }
  if (st != DS_OK) {
    st = DS_ERR_INTERNAL; /* a fair component is strongly connected */
    goto done;
  }
  *length = len;
  if (len > capacity) {
    st = DS_ERR_RANGE;
  }
done:
  ds_context_free(k->context, mem);
  ds_context_free(k->context, bmem);
  return st;
}
