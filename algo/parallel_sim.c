#include "parallel_sim.h"
#include <stdbool.h>
#include <string.h>

#define PR_NONE DS_PRAM_NONE

static size_t pr_log_bound(size_t n) {
  size_t r = 1;
  size_t x = 1;
  while (x < n) {
    x <<= 1;
    r++;
  }
  return r + 1;
}

static ds_status_t pr_check_next(const size_t *next, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    if (next[i] != PR_NONE && (next[i] >= n || next[i] == i)) {
      return DS_ERR_INVALID;
    }
  }
  return DS_OK;
}

/* a coin per (seed, round, node): the same whatever runs which task */
static unsigned pr_coin(unsigned long seed, size_t round, size_t v) {
  unsigned long x = (seed ^ ((unsigned long)round * 0x9E3779B9UL) ^
                     ((unsigned long)v * 0x85EBCA6BUL)) &
                    0xFFFFFFFFUL;
  x ^= x >> 16;
  x = (x * 0x7FEB352DUL) & 0xFFFFFFFFUL;
  x ^= x >> 15;
  x = (x * 0x846CA68BUL) & 0xFFFFFFFFUL;
  x ^= x >> 16;
  return (unsigned)(x & 1UL);
}

static bool pr_any(const unsigned char *flag, size_t tasks) {
  size_t t;
  for (t = 0; t < tasks; t++) {
    if (flag[t]) {
      return true;
    }
  }
  return false;
}

/* ---- pointer jumping and shortcutting ---- */

typedef struct pr_jump {
  size_t n;
  size_t tasks;
  const size_t *next;
  size_t *nx[2];
  size_t *rk[2]; /* ranks, or roots */
  unsigned char *flag;
  int cur;
  int init;
} pr_jump_t;

static void pr_rank_task(size_t t, void *arg) {
  pr_jump_t *j = (pr_jump_t *)arg;
  size_t lo;
  size_t hi;
  size_t i;
  unsigned char any = 0;
  ds_executor_chunk(j->n, j->tasks, t, &lo, &hi);
  if (j->init) {
    for (i = lo; i < hi; i++) {
      j->nx[0][i] = j->next[i];
      j->rk[0][i] = j->next[i] == PR_NONE ? 0 : 1;
    }
    return;
  }
  {
    const size_t *nx = j->nx[j->cur];
    const size_t *rk = j->rk[j->cur];
    size_t *nx2 = j->nx[1 - j->cur];
    size_t *rk2 = j->rk[1 - j->cur];
    for (i = lo; i < hi; i++) {
      if (nx[i] != PR_NONE) {
        rk2[i] = rk[i] + rk[nx[i]];
        nx2[i] = nx[nx[i]];
        any |= (unsigned char)(nx2[i] != PR_NONE);
      } else {
        rk2[i] = rk[i];
        nx2[i] = PR_NONE;
      }
    }
  }
  j->flag[t] = any;
}

ds_status_t ds_pram_list_rank_on(const ds_executor_t *ex, const size_t *next,
                                 size_t n, size_t *rank, size_t *out_rounds,
                                 ds_context_t *context) {
  pr_jump_t j;
  size_t *buf;
  size_t rounds = 0;
  size_t limit = pr_log_bound(n);
  ds_status_t st;
  if (n > 0 && (next == NULL || rank == NULL)) {
    return DS_ERR_NULL;
  }
  st = pr_check_next(next, n);
  if (st != DS_OK) {
    return st;
  }
  if (n > ((size_t)-1) / (3 * sizeof(size_t)) - 1) {
    return DS_ERR_OVERFLOW;
  }
  j.n = n;
  j.tasks = ds_executor_tasks(ex, n);
  buf = (size_t *)ds_context_alloc(context, (3 * n + 1) * sizeof(size_t));
  j.flag = (unsigned char *)ds_context_calloc(context, j.tasks, 1);
  if (buf == NULL || j.flag == NULL) {
    ds_context_free(context, buf);
    ds_context_free(context, j.flag);
    return DS_ERR_ALLOC;
  }
  j.next = next;
  j.nx[0] = buf;
  j.nx[1] = buf + n;
  j.rk[0] = rank;
  j.rk[1] = buf + 2 * n;
  j.cur = 0;
  j.init = 1;
  ds_executor_run(ex, j.tasks, pr_rank_task, &j);
  j.init = 0;
  while (n > 0) {
    memset(j.flag, 0, j.tasks);
    ds_executor_run(ex, j.tasks, pr_rank_task, &j);
    j.cur = 1 - j.cur;
    rounds++;
    if (!pr_any(j.flag, j.tasks)) {
      break;
    }
    if (rounds > limit) {
      st = DS_ERR_INVALID; /* a cycle */
      break;
    }
  }
  if (st == DS_OK && j.rk[j.cur] != rank && n > 0) {
    memcpy(rank, j.rk[j.cur], n * sizeof(size_t));
  }
  ds_context_free(context, buf);
  ds_context_free(context, j.flag);
  if (st == DS_OK && out_rounds != NULL) {
    *out_rounds = rounds;
  }
  return st;
}

ds_status_t ds_pram_list_rank(const size_t *next, size_t n, size_t *rank,
                              size_t *out_rounds, ds_context_t *context) {
  return ds_pram_list_rank_on(NULL, next, n, rank, out_rounds, context);
}

typedef struct pr_scan {
  size_t n;
  size_t tasks;
  size_t es;
  ds_pram_op_func_t op;
  void *user;
  size_t *nx[2];
  unsigned char *v[2];
  unsigned char *flag;
  int cur;
} pr_scan_t;

static void pr_scan_task(size_t t, void *arg) {
  pr_scan_t *j = (pr_scan_t *)arg;
  const size_t *nx = j->nx[j->cur];
  const unsigned char *v = j->v[j->cur];
  size_t *nx2 = j->nx[1 - j->cur];
  unsigned char *v2 = j->v[1 - j->cur];
  size_t lo;
  size_t hi;
  size_t i;
  unsigned char any = 0;
  ds_executor_chunk(j->n, j->tasks, t, &lo, &hi);
  for (i = lo; i < hi; i++) {
    if (nx[i] != PR_NONE) {
      j->op(v + i * j->es, v + nx[i] * j->es, v2 + i * j->es, j->user);
      nx2[i] = nx[nx[i]];
      any |= (unsigned char)(nx2[i] != PR_NONE);
    } else {
      memcpy(v2 + i * j->es, v + i * j->es, j->es);
      nx2[i] = PR_NONE;
    }
  }
  j->flag[t] = any;
}

ds_status_t ds_pram_list_scan_on(const ds_executor_t *ex, const size_t *next,
                                 size_t n, const void *values, size_t elem_size,
                                 ds_pram_op_func_t op, void *user, void *out,
                                 size_t *out_rounds, ds_context_t *context) {
  pr_scan_t j;
  size_t *nbuf;
  unsigned char *vbuf;
  size_t rounds = 0;
  size_t limit = pr_log_bound(n);
  ds_status_t st;
  if (n > 0 && (next == NULL || values == NULL || out == NULL || op == NULL)) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  st = pr_check_next(next, n);
  if (st != DS_OK) {
    return st;
  }
  if (n > ((size_t)-1) / (2 * sizeof(size_t)) - 1 ||
      n > ((size_t)-1) / elem_size - 1) {
    return DS_ERR_OVERFLOW;
  }
  j.n = n;
  j.tasks = ds_executor_tasks(ex, n);
  j.es = elem_size;
  j.op = op;
  j.user = user;
  nbuf = (size_t *)ds_context_alloc(context, (2 * n + 1) * sizeof(size_t));
  vbuf = (unsigned char *)ds_context_alloc(context, (n + 1) * elem_size);
  j.flag = (unsigned char *)ds_context_calloc(context, j.tasks, 1);
  if (nbuf == NULL || vbuf == NULL || j.flag == NULL) {
    ds_context_free(context, nbuf);
    ds_context_free(context, vbuf);
    ds_context_free(context, j.flag);
    return DS_ERR_ALLOC;
  }
  j.nx[0] = nbuf;
  j.nx[1] = nbuf + n;
  j.v[0] = (unsigned char *)out;
  j.v[1] = vbuf;
  j.cur = 0;
  if (n > 0) {
    memcpy(j.nx[0], next, n * sizeof(size_t));
    memmove(out, values, n * elem_size);
  }
  while (n > 0) {
    memset(j.flag, 0, j.tasks);
    ds_executor_run(ex, j.tasks, pr_scan_task, &j);
    j.cur = 1 - j.cur;
    rounds++;
    if (!pr_any(j.flag, j.tasks)) {
      break;
    }
    if (rounds > limit) {
      st = DS_ERR_INVALID;
      break;
    }
  }
  if (st == DS_OK && j.v[j.cur] != (unsigned char *)out && n > 0) {
    memcpy(out, j.v[j.cur], n * elem_size);
  }
  ds_context_free(context, nbuf);
  ds_context_free(context, vbuf);
  ds_context_free(context, j.flag);
  if (st == DS_OK && out_rounds != NULL) {
    *out_rounds = rounds;
  }
  return st;
}

ds_status_t ds_pram_list_scan(const size_t *next, size_t n, const void *values,
                              size_t elem_size, ds_pram_op_func_t op,
                              void *user, void *out, size_t *out_rounds,
                              ds_context_t *context) {
  return ds_pram_list_scan_on(NULL, next, n, values, elem_size, op, user, out,
                              out_rounds, context);
}

static void pr_roots_task(size_t t, void *arg) {
  pr_jump_t *j = (pr_jump_t *)arg;
  size_t lo;
  size_t hi;
  size_t i;
  unsigned char changed = 0;
  ds_executor_chunk(j->n, j->tasks, t, &lo, &hi);
  if (j->init) {
    for (i = lo; i < hi; i++) {
      j->rk[0][i] = j->next[i] == PR_NONE ? i : j->next[i];
    }
    return;
  }
  {
    const size_t *r = j->rk[j->cur];
    size_t *r2 = j->rk[1 - j->cur];
    for (i = lo; i < hi; i++) {
      r2[i] = r[r[i]];
      changed |= (unsigned char)(r2[i] != r[i]);
    }
  }
  j->flag[t] = changed;
}

ds_status_t ds_pram_find_roots_on(const ds_executor_t *ex, const size_t *parent,
                                  size_t n, size_t *root, size_t *out_rounds,
                                  ds_context_t *context) {
  pr_jump_t j;
  size_t *buf;
  size_t i;
  size_t rounds = 0;
  size_t limit = pr_log_bound(n);
  ds_status_t st = DS_OK;
  if (n > 0 && (parent == NULL || root == NULL)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n; i++) {
    if (parent[i] != PR_NONE && parent[i] >= n) {
      return DS_ERR_INVALID;
    }
  }
  if (n > ((size_t)-1) / sizeof(size_t) - 1) {
    return DS_ERR_OVERFLOW;
  }
  j.n = n;
  j.tasks = ds_executor_tasks(ex, n);
  buf = (size_t *)ds_context_alloc(context, (n + 1) * sizeof(size_t));
  j.flag = (unsigned char *)ds_context_calloc(context, j.tasks, 1);
  if (buf == NULL || j.flag == NULL) {
    ds_context_free(context, buf);
    ds_context_free(context, j.flag);
    return DS_ERR_ALLOC;
  }
  j.next = parent;
  j.nx[0] = NULL;
  j.nx[1] = NULL;
  j.rk[0] = root;
  j.rk[1] = buf;
  j.cur = 0;
  j.init = 1;
  ds_executor_run(ex, j.tasks, pr_roots_task, &j);
  j.init = 0;
  while (n > 0) {
    memset(j.flag, 0, j.tasks);
    ds_executor_run(ex, j.tasks, pr_roots_task, &j);
    j.cur = 1 - j.cur;
    if (!pr_any(j.flag, j.tasks)) {
      break;
    }
    if (++rounds > limit) {
      st = DS_ERR_INVALID; /* a cycle without a root */
      break;
    }
  }
  if (st == DS_OK && j.rk[j.cur] != root && n > 0) {
    memcpy(root, j.rk[j.cur], n * sizeof(size_t));
  }
  ds_context_free(context, buf);
  ds_context_free(context, j.flag);
  if (st == DS_OK && out_rounds != NULL) {
    *out_rounds = rounds;
  }
  return st;
}

ds_status_t ds_pram_find_roots(const size_t *parent, size_t n, size_t *root,
                               size_t *out_rounds, ds_context_t *context) {
  return ds_pram_find_roots_on(NULL, parent, n, root, out_rounds, context);
}

/* ---- list contraction ---- */

typedef struct pr_contract {
  size_t tasks;
  unsigned long seed;
  size_t round;
  size_t *succ;
  size_t *pred;
  size_t *w;
  size_t *osucc; /* when spliced: the successor then */
  size_t *ow;    /* and the distance to it */
  size_t *cand;  /* the interior nodes still in a list */
  size_t ncand;
  size_t *cand2;
  size_t *removed; /* every spliced node, round after round */
  size_t nremoved;
  size_t *sel_at; /* per task: where its selected and kept nodes go */
  size_t *keep_at;
  size_t *rank;
  size_t from; /* the nodes of one round: removed[from .. to) */
  size_t to;
  int phase;
} pr_contract_t;

static bool pr_selected(const pr_contract_t *j, size_t v) {
  return pr_coin(j->seed, j->round, v) &&
         !pr_coin(j->seed, j->round, j->pred[v]);
}

static void pr_contract_task(size_t t, void *arg) {
  pr_contract_t *j = (pr_contract_t *)arg;
  size_t lo;
  size_t hi;
  size_t i;
  if (j->phase >= 2) {
    ds_executor_chunk(j->to - j->from, j->tasks, t, &lo, &hi);
    for (i = lo; i < hi; i++) {
      size_t v = j->removed[j->from + i];
      if (j->phase == 2) { /* splice: the selected are independent, so no
                            * two touch the same node */
        size_t p = j->pred[v];
        size_t s = j->succ[v];
        j->osucc[v] = s;
        j->ow[v] = j->w[v];
        j->succ[p] = s;
        j->w[p] += j->w[v];
        j->pred[s] = p;
      } else { /* expansion */
        j->rank[v] = j->rank[j->osucc[v]] + j->ow[v];
      }
    }
    return;
  }
  ds_executor_chunk(j->ncand, j->tasks, t, &lo, &hi);
  if (j->phase == 0) { /* count the selected and the kept */
    size_t sel = 0;
    for (i = lo; i < hi; i++) {
      sel += pr_selected(j, j->cand[i]);
    }
    j->sel_at[t] = sel;
    j->keep_at[t] = (hi - lo) - sel;
  } else { /* move them to their places */
    size_t at = j->sel_at[t];
    size_t keep = j->keep_at[t];
    for (i = lo; i < hi; i++) {
      size_t v = j->cand[i];
      if (pr_selected(j, v)) {
        j->removed[at++] = v;
      } else {
        j->cand2[keep++] = v;
      }
    }
  }
}

ds_status_t ds_pram_list_contract_rank_on(const ds_executor_t *ex,
                                          const size_t *next, size_t n,
                                          size_t *rank, unsigned long seed,
                                          size_t *out_rounds,
                                          ds_context_t *context) {
  pr_contract_t j;
  size_t *buf;
  size_t *offsets; /* per task twice, then where each round's nodes start */
  size_t *round_at;
  size_t i;
  size_t rounds = 0;
  size_t limit = 64 * pr_log_bound(n) + 64;
  size_t tasks_max;
  ds_status_t st;
  if (n > 0 && (next == NULL || rank == NULL)) {
    return DS_ERR_NULL;
  }
  st = pr_check_next(next, n);
  if (st != DS_OK) {
    return st;
  }
  if (n > ((size_t)-1) / (9 * sizeof(size_t)) - 1) {
    return DS_ERR_OVERFLOW;
  }
  memset(&j, 0, sizeof(j));
  tasks_max = ds_executor_tasks(ex, n);
  buf = (size_t *)ds_context_alloc(context, (8 * n + 1) * sizeof(size_t));
  offsets = (size_t *)ds_context_alloc(context, (2 * tasks_max + limit + 2) *
                                                    sizeof(size_t));
  if (buf == NULL || offsets == NULL) {
    ds_context_free(context, buf);
    ds_context_free(context, offsets);
    return DS_ERR_ALLOC;
  }
  j.seed = seed == 0 ? 2463534242UL : seed;
  j.succ = buf;
  j.pred = buf + n;
  j.w = buf + 2 * n;
  j.osucc = buf + 3 * n;
  j.ow = buf + 4 * n;
  j.cand = buf + 5 * n;
  j.cand2 = buf + 6 * n;
  j.removed = buf + 7 * n;
  j.sel_at = offsets;
  j.keep_at = offsets + tasks_max;
  round_at = offsets + 2 * tasks_max;
  j.rank = rank;
  for (i = 0; i < n; i++) {
    j.pred[i] = PR_NONE;
  }
  for (i = 0; i < n && st == DS_OK; i++) {
    j.succ[i] = next[i];
    j.w[i] = next[i] == PR_NONE ? 0 : 1;
    if (next[i] != PR_NONE) {
      if (j.pred[next[i]] != PR_NONE) {
        st = DS_ERR_INVALID; /* two predecessors */
      }
      j.pred[next[i]] = i;
    }
  }
  for (i = 0; i < n && st == DS_OK; i++) {
    if (j.pred[i] != PR_NONE && j.succ[i] != PR_NONE) {
      j.cand[j.ncand++] = i;
    }
  }
  /* contraction: each round splices the interior nodes that flipped heads
   * while their predecessor flipped tails */
  while (st == DS_OK && j.ncand > 0) {
    size_t t;
    size_t sel = 0;
    size_t keep = 0;
    size_t *tmp;
    if (rounds == limit) {
      st = DS_ERR_INVALID; /* a cycle */
      break;
    }
    j.round = ++rounds;
    j.tasks = ds_executor_tasks(ex, j.ncand);
    j.phase = 0;
    ds_executor_run(ex, j.tasks, pr_contract_task, &j);
    for (t = 0; t < j.tasks; t++) { /* prefix sums: where each task writes */
      size_t s2 = j.sel_at[t];
      size_t k2 = j.keep_at[t];
      j.sel_at[t] = j.nremoved + sel;
      j.keep_at[t] = keep;
      sel += s2;
      keep += k2;
    }
    j.phase = 1;
    ds_executor_run(ex, j.tasks, pr_contract_task, &j);
    round_at[rounds - 1] = j.nremoved;
    j.from = j.nremoved;
    j.to = j.nremoved + sel;
    if (sel > 0) {
      j.tasks = ds_executor_tasks(ex, sel);
      j.phase = 2;
      ds_executor_run(ex, j.tasks, pr_contract_task, &j);
    }
    j.nremoved += sel;
    tmp = j.cand;
    j.cand = j.cand2;
    j.cand2 = tmp;
    j.ncand = keep;
  }
  if (st == DS_OK) {
    size_t r;
    /* survivors: tails rank 0, a head ranks its distance to its tail */
    for (i = 0; i < n; i++) {
      rank[i] = j.succ[i] == PR_NONE ? 0 : j.w[i];
    }
    /* expansion, round by round in reverse */
    j.phase = 3;
    for (r = rounds; r-- > 0;) {
      j.from = round_at[r];
      j.to = r + 1 < rounds ? round_at[r + 1] : j.nremoved;
      if (j.to > j.from) {
        j.tasks = ds_executor_tasks(ex, j.to - j.from);
        ds_executor_run(ex, j.tasks, pr_contract_task, &j);
      }
    }
  }
  ds_context_free(context, buf);
  ds_context_free(context, offsets);
  if (st == DS_OK && out_rounds != NULL) {
    *out_rounds = rounds;
  }
  return st;
}

ds_status_t ds_pram_list_contract_rank(const size_t *next, size_t n,
                                       size_t *rank, unsigned long seed,
                                       size_t *out_rounds,
                                       ds_context_t *context) {
  return ds_pram_list_contract_rank_on(NULL, next, n, rank, seed, out_rounds,
                                       context);
}

/* ---- tree contraction ---- */

typedef struct pr_tree {
  size_t tasks;
  const ds_pram_expr_op_t *op;
  const double *value;
  size_t *l;
  size_t *r;
  size_t *par;
  size_t *leaves;
  size_t nl;
  size_t *leaves2;
  size_t *count;             /* per task */
  unsigned char *left_child; /* v is its parent's left child */
  double *fa;
  double *fb;
  size_t root;
  int phase; /* 0, 1: rakes; 2: count survivors; 3: compact them */
} pr_tree_t;

/* rakes the odd-numbered leaves (indices 0, 2, ...) that are left (phase
 * 0) or right (phase 1) children: no two of them touch the same nodes */
static void pr_tree_task(size_t t, void *arg) {
  pr_tree_t *j = (pr_tree_t *)arg;
  size_t lo;
  size_t hi;
  size_t i;
  if (j->phase >= 2) {
    ds_executor_chunk(j->nl, j->tasks, t, &lo, &hi);
    if (j->phase == 2) {
      size_t c = 0;
      for (i = lo; i < hi; i++) {
        c += j->leaves[i] != PR_NONE;
      }
      j->count[t] = c;
    } else {
      size_t at = j->count[t];
      for (i = lo; i < hi; i++) {
        if (j->leaves[i] != PR_NONE) {
          j->leaves2[at++] = j->leaves[i];
        }
      }
    }
    return;
  }
  ds_executor_chunk((j->nl + 1) / 2, j->tasks, t, &lo, &hi);
  for (i = lo; i < hi; i++) {
    size_t k = 2 * i;
    size_t u = j->leaves[k];
    size_t p;
    size_t s;
    size_t g;
    double c;
    double ga;
    double gb;
    if (u == PR_NONE) {
      continue;
    }
    p = j->par[u];
    /* the side is u's own flag: reading l[p] instead would race with a
     * rake below p's other child rewriting it */
    if (p == PR_NONE || (j->phase == 0) != (j->left_child[u] != 0)) {
      continue;
    }
    s = j->left_child[u] ? j->r[p] : j->l[p];
    c = j->fa[u] * j->value[u] + j->fb[u];
    if (j->op[p] == DS_PRAM_ADD) {
      ga = j->fa[s];
      gb = j->fb[s] + c;
    } else {
      ga = c * j->fa[s];
      gb = c * j->fb[s];
    }
    /* s takes p's place with label f_p o g */
    j->fb[s] = j->fa[p] * gb + j->fb[p];
    j->fa[s] = j->fa[p] * ga;
    g = j->par[p];
    j->par[s] = g;
    j->left_child[s] = j->left_child[p];
    if (g != PR_NONE) {
      if (j->left_child[p]) {
        j->l[g] = s;
      } else {
        j->r[g] = s;
      }
    } else {
      j->root = s; /* only the rake at the root writes it */
    }
    j->par[u] = PR_NONE;
    j->leaves[k] = PR_NONE; /* raked */
  }
}

ds_status_t ds_pram_expr_eval_on(const ds_executor_t *ex, const size_t *left,
                                 const size_t *right,
                                 const ds_pram_expr_op_t *op,
                                 const double *value, size_t n, size_t root,
                                 double *out_value, size_t *out_rounds,
                                 ds_context_t *context) {
  pr_tree_t j;
  size_t *stack;
  size_t *buf;
  size_t top = 0;
  size_t i;
  size_t rounds = 0;
  size_t v;
  size_t tasks_max;
  if (n == 0 || left == NULL || right == NULL || op == NULL || value == NULL ||
      out_value == NULL) {
    return n == 0 ? DS_ERR_INVALID : DS_ERR_NULL;
  }
  if (root >= n || n > ((size_t)-1) / (7 * sizeof(size_t))) {
    return DS_ERR_RANGE;
  }
  for (i = 0; i < n; i++) {
    if (op[i] == DS_PRAM_LEAF) {
      if (left[i] != PR_NONE || right[i] != PR_NONE) {
        return DS_ERR_INVALID;
      }
    } else if ((op[i] != DS_PRAM_ADD && op[i] != DS_PRAM_MUL) || left[i] >= n ||
               right[i] >= n) {
      return DS_ERR_INVALID;
    }
  }
  memset(&j, 0, sizeof(j));
  tasks_max = ds_executor_tasks(ex, n);
  buf = (size_t *)ds_context_alloc(context,
                                   (6 * n + tasks_max + 1) * sizeof(size_t));
  j.fa = (double *)ds_context_alloc(context, 2 * n * sizeof(double));
  j.left_child = (unsigned char *)ds_context_calloc(context, n, 1);
  if (buf == NULL || j.fa == NULL || j.left_child == NULL) {
    ds_context_free(context, buf);
    ds_context_free(context, j.fa);
    ds_context_free(context, j.left_child);
    return DS_ERR_ALLOC;
  }
  j.l = buf;
  j.r = buf + n;
  j.par = buf + 2 * n;
  j.leaves = buf + 3 * n;
  j.leaves2 = buf + 4 * n;
  stack = buf + 5 * n;
  j.count = buf + 6 * n;
  j.fb = j.fa + n;
  j.op = op;
  j.value = value;
  j.root = root;
  for (i = 0; i < n; i++) {
    j.l[i] = left[i];
    j.r[i] = right[i];
    j.par[i] = PR_NONE;
    j.fa[i] = 1.0;
    j.fb[i] = 0.0;
  }
  /* leaves left to right (and parent links), checking it is a tree */
  stack[top++] = root;
  while (top > 0) {
    v = stack[--top];
    if (op[v] == DS_PRAM_LEAF) {
      j.leaves[j.nl++] = v;
      continue;
    }
    if (j.par[j.l[v]] != PR_NONE || j.par[j.r[v]] != PR_NONE ||
        j.l[v] == root || j.r[v] == root || top + 2 > n) {
      ds_context_free(context, buf);
      ds_context_free(context, j.fa);
      ds_context_free(context, j.left_child);
      return DS_ERR_INVALID;
    }
    j.par[j.l[v]] = v;
    j.par[j.r[v]] = v;
    j.left_child[j.l[v]] = 1;
    stack[top++] = j.r[v];
    stack[top++] = j.l[v];
  }
  /* rake rounds: odd-numbered left leaves, then odd-numbered right leaves,
   * then the survivors compacted in order */
  while (j.nl > 1) {
    size_t t;
    size_t sum = 0;
    size_t *tmp;
    j.tasks = ds_executor_tasks(ex, (j.nl + 1) / 2);
    j.phase = 0;
    ds_executor_run(ex, j.tasks, pr_tree_task, &j);
    j.phase = 1;
    ds_executor_run(ex, j.tasks, pr_tree_task, &j);
    j.tasks = ds_executor_tasks(ex, j.nl);
    j.phase = 2;
    ds_executor_run(ex, j.tasks, pr_tree_task, &j);
    for (t = 0; t < j.tasks; t++) {
      size_t c = j.count[t];
      j.count[t] = sum;
      sum += c;
    }
    j.phase = 3;
    ds_executor_run(ex, j.tasks, pr_tree_task, &j);
    tmp = j.leaves;
    j.leaves = j.leaves2;
    j.leaves2 = tmp;
    j.nl = sum;
    rounds++;
  }
  *out_value = j.fa[j.root] * value[j.root] + j.fb[j.root];
  ds_context_free(context, buf);
  ds_context_free(context, j.fa);
  ds_context_free(context, j.left_child);
  if (out_rounds != NULL) {
    *out_rounds = rounds;
  }
  return DS_OK;
}

ds_status_t ds_pram_expr_eval(const size_t *left, const size_t *right,
                              const ds_pram_expr_op_t *op, const double *value,
                              size_t n, size_t root, double *out_value,
                              size_t *out_rounds, ds_context_t *context) {
  return ds_pram_expr_eval_on(NULL, left, right, op, value, n, root, out_value,
                              out_rounds, context);
}
