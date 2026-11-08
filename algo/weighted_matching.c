#include "weighted_matching.h"
#include <math.h>
#include <string.h>

typedef struct wm {
  long n;
  long m;
  const size_t *eu;
  const size_t *ev;
  const double *w;
  long *endpoint;  /* 2m */
  long *nb_off;    /* n + 1 */
  long *nb;        /* 2m: remote endpoints of each vertex's edges */
  long *mate;      /* n: remote endpoint or -1 */
  long *label;     /* 2n */
  long *labelend;  /* 2n */
  long *inblossom; /* n */
  long *parent;    /* 2n */
  long **childs;   /* 2n */
  long **endps;    /* 2n */
  long *nchilds;   /* 2n */
  long *base;      /* 2n */
  long *bestedge;  /* 2n */
  long **bbest;    /* 2n, NULL = not computed */
  long *nbbest;    /* 2n */
  long *unused;    /* stack of free blossom ids */
  long nunused;
  double *dual;         /* 2n */
  unsigned char *allow; /* m */
  long *queue;
  long nq;
  long qcap;
  long *bestedgeto; /* 2n scratch */
  long *leaves;     /* n scratch */
  long *stack;      /* 2n scratch for leaf walks */
  long *work;       /* 4n scratch for expansion / augmentation */
  long *tmp;        /* n scratch for rotations */
  double eps;
  ds_context_t *ctx;
  ds_status_t st;
} wm_t;

static double wm_slack(const wm_t *s, long k) {
  return s->dual[s->eu[k]] + s->dual[s->ev[k]] - 2.0 * s->w[k];
}

static long wm_ix(long j, long len) { return j < 0 ? j + len : j; }

/* leaves of blossom b into s->leaves, returns their count */
static long wm_leaves(wm_t *s, long b) {
  long top = 0;
  long cnt = 0;
  s->stack[top++] = b;
  while (top > 0) {
    long x = s->stack[--top];
    if (x < s->n) {
      s->leaves[cnt++] = x;
    } else {
      long i;
      for (i = s->nchilds[x]; i-- > 0;) {
        s->stack[top++] = s->childs[x][i];
      }
    }
  }
  return cnt;
}

static void wm_push_queue(wm_t *s, long v) {
  if (s->nq == s->qcap) {
    long nc = s->qcap * 2;
    long *q =
        (long *)ds_context_realloc(s->ctx, s->queue, (size_t)nc * sizeof(long));
    if (q == NULL) {
      s->st = DS_ERR_ALLOC;
      return;
    }
    s->queue = q;
    s->qcap = nc;
  }
  s->queue[s->nq++] = v;
}

static void wm_assign_label(wm_t *s, long w, long t, long p) {
  for (;;) {
    long b = s->inblossom[w];
    s->label[w] = s->label[b] = t;
    s->labelend[w] = s->labelend[b] = p;
    s->bestedge[w] = s->bestedge[b] = -1;
    if (t == 1) {
      long cnt = wm_leaves(s, b);
      long i;
      for (i = 0; i < cnt; i++) {
        wm_push_queue(s, s->leaves[i]);
      }
      return;
    }
    /* t == 2: the base's mate becomes an S-vertex */
    {
      long bs = s->base[b];
      w = s->endpoint[s->mate[bs]];
      t = 1;
      p = s->mate[bs] ^ 1;
    }
  }
}

/* trace back from v and w to find a common base (or -1: augmenting) */
static long wm_scan_blossom(wm_t *s, long v, long w) {
  long pathlen = 0;
  long base = -1;
  long i;
  while (v != -1 || w != -1) {
    long b = s->inblossom[v];
    if (s->label[b] & 4) {
      base = s->base[b];
      break;
    }
    s->stack[pathlen++] = b;
    s->label[b] = 5;
    if (s->labelend[b] == -1) {
      v = -1;
    } else {
      v = s->endpoint[s->labelend[b]];
      b = s->inblossom[v];
      v = s->endpoint[s->labelend[b]];
    }
    if (w != -1) {
      long t = v;
      v = w;
      w = t;
    }
  }
  for (i = 0; i < pathlen; i++) {
    s->label[s->stack[i]] = 1;
  }
  return base;
}

static void wm_add_blossom(wm_t *s, long base, long k) {
  long v = (long)s->eu[k];
  long w = (long)s->ev[k];
  long bb = s->inblossom[base];
  long bv = s->inblossom[v];
  long bw = s->inblossom[w];
  long b;
  long len = 0;
  long i;
  long cnt;
  long *path;
  long *endps;
  long nbe = 0;
  /* count the cycle first */
  {
    long x = bv;
    len = 1;
    while (x != bb) {
      len++;
      x = s->inblossom[s->endpoint[s->labelend[x]]];
    }
    x = bw;
    while (x != bb) {
      len++;
      x = s->inblossom[s->endpoint[s->labelend[x]]];
    }
  }
  path = (long *)ds_context_alloc(s->ctx, (size_t)len * sizeof(long));
  endps = (long *)ds_context_alloc(s->ctx, (size_t)len * sizeof(long));
  if (path == NULL || endps == NULL) {
    ds_context_free(s->ctx, path);
    ds_context_free(s->ctx, endps);
    s->st = DS_ERR_ALLOC;
    return;
  }
  b = s->unused[--s->nunused];
  s->base[b] = base;
  s->parent[b] = -1;
  s->parent[bb] = b;
  /* v side, collected backwards then reversed: [bb, ..., bv] */
  {
    long n1 = 0;
    long x = bv;
    while (x != bb) {
      s->parent[x] = b;
      s->tmp[n1] = x;
      s->stack[n1] = s->labelend[x];
      n1++;
      x = s->inblossom[s->endpoint[s->labelend[x]]];
    }
    path[0] = bb;
    for (i = 0; i < n1; i++) {
      path[1 + i] = s->tmp[n1 - 1 - i];
      endps[i] = s->stack[n1 - 1 - i];
    }
    endps[n1] = 2 * k;
    cnt = n1 + 1;
    x = bw;
    while (x != bb) {
      s->parent[x] = b;
      path[cnt] = x;
      endps[cnt] = s->labelend[x] ^ 1;
      cnt++;
      x = s->inblossom[s->endpoint[s->labelend[x]]];
    }
  }
  s->childs[b] = path;
  s->endps[b] = endps;
  s->nchilds[b] = len;
  s->label[b] = 1;
  s->labelend[b] = s->labelend[bb];
  s->dual[b] = 0.0;
  cnt = wm_leaves(s, b);
  for (i = 0; i < cnt; i++) {
    long x = s->leaves[i];
    if (s->label[s->inblossom[x]] == 2) {
      wm_push_queue(s, x);
    }
    s->inblossom[x] = b;
  }
  /* least-slack edges from the new blossom to each S-blossom */
  for (i = 0; i < 2 * s->n; i++) {
    s->bestedgeto[i] = -1;
  }
  for (i = 0; i < len; i++) {
    long c = path[i];
    long j;
    if (s->bbest[c] == NULL) {
      long lc = wm_leaves(s, c);
      long a;
      for (a = 0; a < lc; a++) {
        long x = s->leaves[a];
        long e;
        for (e = s->nb_off[x]; e < s->nb_off[x + 1]; e++) {
          long kk = s->nb[e] / 2;
          long ii = (long)s->eu[kk];
          long jj = (long)s->ev[kk];
          long bj;
          if (s->inblossom[jj] == b) {
            jj = ii;
          }
          bj = s->inblossom[jj];
          if (bj != b && s->label[bj] == 1 &&
              (s->bestedgeto[bj] == -1 ||
               wm_slack(s, kk) < wm_slack(s, s->bestedgeto[bj]))) {
            s->bestedgeto[bj] = kk;
          }
        }
      }
    } else {
      for (j = 0; j < s->nbbest[c]; j++) {
        long kk = s->bbest[c][j];
        long ii = (long)s->eu[kk];
        long jj = (long)s->ev[kk];
        long bj;
        if (s->inblossom[jj] == b) {
          jj = ii;
        }
        bj = s->inblossom[jj];
        if (bj != b && s->label[bj] == 1 &&
            (s->bestedgeto[bj] == -1 ||
             wm_slack(s, kk) < wm_slack(s, s->bestedgeto[bj]))) {
          s->bestedgeto[bj] = kk;
        }
      }
    }
    ds_context_free(s->ctx, s->bbest[c]);
    s->bbest[c] = NULL;
    s->nbbest[c] = 0;
    s->bestedge[c] = -1;
  }
  for (i = 0; i < 2 * s->n; i++) {
    if (s->bestedgeto[i] != -1) {
      nbe++;
    }
  }
  s->bbest[b] =
      (long *)ds_context_alloc(s->ctx, (size_t)(nbe + 1) * sizeof(long));
  if (s->bbest[b] == NULL) {
    s->st = DS_ERR_ALLOC;
    return;
  }
  s->nbbest[b] = 0;
  s->bestedge[b] = -1;
  for (i = 0; i < 2 * s->n; i++) {
    long kk = s->bestedgeto[i];
    if (kk != -1) {
      s->bbest[b][s->nbbest[b]++] = kk;
      if (s->bestedge[b] == -1 ||
          wm_slack(s, kk) < wm_slack(s, s->bestedge[b])) {
        s->bestedge[b] = kk;
      }
    }
  }
}

static void wm_free_blossom(wm_t *s, long b) {
  s->label[b] = s->labelend[b] = -1;
  ds_context_free(s->ctx, s->childs[b]);
  ds_context_free(s->ctx, s->endps[b]);
  ds_context_free(s->ctx, s->bbest[b]);
  s->childs[b] = s->endps[b] = s->bbest[b] = NULL;
  s->nchilds[b] = 0;
  s->nbbest[b] = 0;
  s->base[b] = -1;
  s->bestedge[b] = -1;
  s->unused[s->nunused++] = b;
}

/* expand blossom b during a stage (it is a T-blossom with zero dual) */
static void wm_expand(wm_t *s, long b) {
  long nc = s->nchilds[b];
  long i;
  for (i = 0; i < nc; i++) {
    long c = s->childs[b][i];
    s->parent[c] = -1;
    if (c < s->n) {
      s->inblossom[c] = c;
    } else {
      long cnt = wm_leaves(s, c);
      long a;
      for (a = 0; a < cnt; a++) {
        s->inblossom[s->leaves[a]] = c;
      }
    }
  }
  if (s->label[b] == 2) {
    long entry = s->inblossom[s->endpoint[s->labelend[b] ^ 1]];
    long j = 0;
    long jstep;
    long trick;
    long p;
    long bv;
    while (s->childs[b][j] != entry) {
      j++;
    }
    if (j & 1) {
      j -= nc;
      jstep = 1;
      trick = 0;
    } else {
      jstep = -1;
      trick = 1;
    }
    p = s->labelend[b];
    while (j != 0) {
      long q;
      s->label[s->endpoint[p ^ 1]] = 0;
      q = s->endps[b][wm_ix(j - trick, nc)];
      s->label[s->endpoint[q ^ trick ^ 1]] = 0;
      wm_assign_label(s, s->endpoint[p ^ 1], 2, p);
      s->allow[q / 2] = 1;
      j += jstep;
      p = s->endps[b][wm_ix(j - trick, nc)] ^ trick;
      s->allow[p / 2] = 1;
      j += jstep;
    }
    bv = s->childs[b][wm_ix(j, nc)];
    s->label[s->endpoint[p ^ 1]] = s->label[bv] = 2;
    s->labelend[s->endpoint[p ^ 1]] = s->labelend[bv] = p;
    s->bestedge[bv] = -1;
    j += jstep;
    while (s->childs[b][wm_ix(j, nc)] != entry) {
      long v = -1;
      bv = s->childs[b][wm_ix(j, nc)];
      if (s->label[bv] == 1) {
        j += jstep;
        continue;
      }
      {
        long cnt = wm_leaves(s, bv);
        long a;
        for (a = 0; a < cnt; a++) {
          if (s->label[s->leaves[a]] != 0) {
            v = s->leaves[a];
            break;
          }
        }
      }
      if (v != -1) {
        s->label[v] = 0;
        s->label[s->endpoint[s->mate[s->base[bv]]]] = 0;
        wm_assign_label(s, v, 2, s->labelend[v]);
      }
      j += jstep;
    }
  }
  wm_free_blossom(s, b);
}

/* end of a stage: expand b and, inside it, every sub-blossom with zero
 * dual, with a work list */
static void wm_expand_end(wm_t *s, long b) {
  long top = 0;
  s->work[top++] = b;
  while (top > 0) {
    long x = s->work[--top];
    long i;
    for (i = 0; i < s->nchilds[x]; i++) {
      long c = s->childs[x][i];
      s->parent[c] = -1;
      if (c < s->n) {
        s->inblossom[c] = c;
      } else if (s->dual[c] <= s->eps) {
        s->work[top++] = c;
      } else {
        long cnt = wm_leaves(s, c);
        long a;
        for (a = 0; a < cnt; a++) {
          s->inblossom[s->leaves[a]] = c;
        }
      }
    }
    wm_free_blossom(s, x);
  }
}

/* swap matched/unmatched edges along the even path from v to b's base;
 * sub-blossom augmentations are independent, so they go on a work list */
static void wm_augment_blossom(wm_t *s, long b0, long v0) {
  long top = 0;
  s->work[top++] = b0;
  s->work[top++] = v0;
  while (top > 0) {
    long v = s->work[--top];
    long b = s->work[--top];
    long nc = s->nchilds[b];
    long t = v;
    long i;
    long j;
    long jstep;
    long trick;
    while (s->parent[t] != b) {
      t = s->parent[t];
    }
    if (t >= s->n) {
      s->work[top++] = t;
      s->work[top++] = v;
    }
    i = 0;
    while (s->childs[b][i] != t) {
      i++;
    }
    j = i;
    if (i & 1) {
      j -= nc;
      jstep = 1;
      trick = 0;
    } else {
      jstep = -1;
      trick = 1;
    }
    while (j != 0) {
      long p;
      j += jstep;
      t = s->childs[b][wm_ix(j, nc)];
      p = s->endps[b][wm_ix(j - trick, nc)] ^ trick;
      if (t >= s->n) {
        s->work[top++] = t;
        s->work[top++] = s->endpoint[p];
      }
      j += jstep;
      t = s->childs[b][wm_ix(j, nc)];
      if (t >= s->n) {
        s->work[top++] = t;
        s->work[top++] = s->endpoint[p ^ 1];
      }
      s->mate[s->endpoint[p]] = p ^ 1;
      s->mate[s->endpoint[p ^ 1]] = p;
    }
    /* rotate so the child holding v comes first */
    memcpy(s->tmp, s->childs[b], (size_t)nc * sizeof(long));
    for (j = 0; j < nc; j++) {
      s->childs[b][j] = s->tmp[(i + j) % nc];
    }
    memcpy(s->tmp, s->endps[b], (size_t)nc * sizeof(long));
    for (j = 0; j < nc; j++) {
      s->endps[b][j] = s->tmp[(i + j) % nc];
    }
    /* the base is now v (the child's own rotation may still be pending
     * on the work list, so do not read it back from the child) */
    s->base[b] = v;
  }
}

static void wm_augment_matching(wm_t *s, long k) {
  long side;
  for (side = 0; side < 2; side++) {
    long sv = side == 0 ? (long)s->eu[k] : (long)s->ev[k];
    long p = side == 0 ? 2 * k + 1 : 2 * k;
    for (;;) {
      long bs = s->inblossom[sv];
      long t;
      long bt;
      long jv;
      if (bs >= s->n) {
        wm_augment_blossom(s, bs, sv);
      }
      s->mate[sv] = p;
      if (s->labelend[bs] == -1) {
        break;
      }
      t = s->endpoint[s->labelend[bs]];
      bt = s->inblossom[t];
      sv = s->endpoint[s->labelend[bt]];
      jv = s->endpoint[s->labelend[bt] ^ 1];
      if (bt >= s->n) {
        wm_augment_blossom(s, bt, jv);
      }
      s->mate[jv] = s->labelend[bt];
      p = s->labelend[bt] ^ 1;
    }
  }
}

static ds_status_t wm_run(wm_t *s, bool maxcard) {
  long n = s->n;
  long t;
  long i;
  for (t = 0; t < n && s->st == DS_OK; t++) {
    bool augmented = false;
    for (i = 0; i < 2 * n; i++) {
      s->label[i] = 0;
      s->bestedge[i] = -1;
    }
    for (i = n; i < 2 * n; i++) {
      ds_context_free(s->ctx, s->bbest[i]);
      s->bbest[i] = NULL;
      s->nbbest[i] = 0;
    }
    memset(s->allow, 0, (size_t)s->m);
    s->nq = 0;
    for (i = 0; i < n; i++) {
      if (s->mate[i] == -1 && s->label[s->inblossom[i]] == 0) {
        wm_assign_label(s, i, 1, -1);
      }
    }
    for (;;) {
      long deltatype = -1;
      double delta = 0.0;
      long deltaedge = -1;
      long deltablossom = -1;
      while (s->nq > 0 && !augmented && s->st == DS_OK) {
        long v = s->queue[--s->nq];
        long e;
        for (e = s->nb_off[v]; e < s->nb_off[v + 1]; e++) {
          long p = s->nb[e];
          long k = p / 2;
          long w = s->endpoint[p];
          double kslack = 0.0;
          if (s->inblossom[v] == s->inblossom[w]) {
            continue;
          }
          if (!s->allow[k]) {
            kslack = wm_slack(s, k);
            if (kslack <= s->eps) {
              s->allow[k] = 1;
            }
          }
          if (s->allow[k]) {
            if (s->label[s->inblossom[w]] == 0) {
              wm_assign_label(s, w, 2, p ^ 1);
            } else if (s->label[s->inblossom[w]] == 1) {
              long base = wm_scan_blossom(s, v, w);
              if (base >= 0) {
                wm_add_blossom(s, base, k);
              } else {
                wm_augment_matching(s, k);
                augmented = true;
                break;
              }
            } else if (s->label[w] == 0) {
              s->label[w] = 2;
              s->labelend[w] = p ^ 1;
            }
          } else if (s->label[s->inblossom[w]] == 1) {
            long b = s->inblossom[v];
            if (s->bestedge[b] == -1 || kslack < wm_slack(s, s->bestedge[b])) {
              s->bestedge[b] = k;
            }
          } else if (s->label[w] == 0) {
            if (s->bestedge[w] == -1 || kslack < wm_slack(s, s->bestedge[w])) {
              s->bestedge[w] = k;
            }
          }
          if (s->st != DS_OK) {
            break;
          }
        }
      }
      if (augmented || s->st != DS_OK) {
        break;
      }
      if (!maxcard) {
        deltatype = 1;
        delta = s->dual[0];
        for (i = 1; i < n; i++) {
          if (s->dual[i] < delta) {
            delta = s->dual[i];
          }
        }
      }
      for (i = 0; i < n; i++) {
        if (s->label[s->inblossom[i]] == 0 && s->bestedge[i] != -1) {
          double d = wm_slack(s, s->bestedge[i]);
          if (deltatype == -1 || d < delta) {
            delta = d;
            deltatype = 2;
            deltaedge = s->bestedge[i];
          }
        }
      }
      for (i = 0; i < 2 * n; i++) {
        if (s->parent[i] == -1 && s->label[i] == 1 && s->bestedge[i] != -1) {
          double d = wm_slack(s, s->bestedge[i]) / 2.0;
          if (deltatype == -1 || d < delta) {
            delta = d;
            deltatype = 3;
            deltaedge = s->bestedge[i];
          }
        }
      }
      for (i = n; i < 2 * n; i++) {
        if (s->base[i] >= 0 && s->parent[i] == -1 && s->label[i] == 2 &&
            (deltatype == -1 || s->dual[i] < delta)) {
          delta = s->dual[i];
          deltatype = 4;
          deltablossom = i;
        }
      }
      if (deltatype == -1) {
        deltatype = 1;
        delta = s->dual[0];
        for (i = 1; i < n; i++) {
          if (s->dual[i] < delta) {
            delta = s->dual[i];
          }
        }
        if (delta < 0.0) {
          delta = 0.0;
        }
      }
      for (i = 0; i < n; i++) {
        if (s->label[s->inblossom[i]] == 1) {
          s->dual[i] -= delta;
        } else if (s->label[s->inblossom[i]] == 2) {
          s->dual[i] += delta;
        }
      }
      for (i = n; i < 2 * n; i++) {
        if (s->base[i] >= 0 && s->parent[i] == -1) {
          if (s->label[i] == 1) {
            s->dual[i] += delta;
          } else if (s->label[i] == 2) {
            s->dual[i] -= delta;
          }
        }
      }
      if (deltatype == 1) {
        break;
      } else if (deltatype == 2) {
        long a = (long)s->eu[deltaedge];
        s->allow[deltaedge] = 1;
        if (s->label[s->inblossom[a]] == 0) {
          a = (long)s->ev[deltaedge];
        }
        wm_push_queue(s, a);
      } else if (deltatype == 3) {
        s->allow[deltaedge] = 1;
        wm_push_queue(s, (long)s->eu[deltaedge]);
      } else {
        wm_expand(s, deltablossom);
      }
    }
    if (!augmented || s->st != DS_OK) {
      break;
    }
    for (i = n; i < 2 * n; i++) {
      if (s->parent[i] == -1 && s->base[i] >= 0 && s->label[i] == 1 &&
          s->dual[i] <= s->eps) {
        wm_expand_end(s, i);
      }
    }
  }
  return s->st;
}

static void wm_free(wm_t *s) {
  long i;
  if (s->childs != NULL) {
    for (i = 0; i < 2 * s->n; i++) {
      ds_context_free(s->ctx, s->childs[i]);
      ds_context_free(s->ctx, s->endps[i]);
      ds_context_free(s->ctx, s->bbest[i]);
    }
  }
  ds_context_free(s->ctx, s->endpoint);
  ds_context_free(s->ctx, s->nb_off);
  ds_context_free(s->ctx, s->nb);
  ds_context_free(s->ctx, s->mate); /* the block of long arrays */
  ds_context_free(s->ctx, (void *)s->childs);
  ds_context_free(s->ctx, (void *)s->endps);
  ds_context_free(s->ctx, (void *)s->bbest);
  ds_context_free(s->ctx, s->dual);
  ds_context_free(s->ctx, s->allow);
  ds_context_free(s->ctx, s->queue);
}

ds_status_t ds_matching_max_weight(size_t n, const size_t *u, const size_t *v,
                                   const double *w, size_t m,
                                   bool max_cardinality, size_t *out_mate,
                                   double *out_weight, ds_context_t *context) {
  wm_t s;
  long i;
  long k;
  double maxw = 0.0;
  long *blk;
  ds_status_t st;
  if (out_mate == NULL || (m > 0 && (u == NULL || v == NULL || w == NULL))) {
    return DS_ERR_NULL;
  }
  if (n > 0x3fffffffUL || m > 0x3fffffffUL) {
    return DS_ERR_OVERFLOW;
  }
  for (k = 0; k < (long)m; k++) {
    if (u[k] >= n || v[k] >= n) {
      return DS_ERR_RANGE;
    }
    if (u[k] == v[k] || !(fabs(w[k]) < HUGE_VAL)) {
      return DS_ERR_INVALID;
    }
    if (w[k] > maxw) {
      maxw = w[k];
    }
  }
  for (i = 0; i < (long)n; i++) {
    out_mate[i] = DS_WMATCH_NONE;
  }
  if (out_weight != NULL) {
    *out_weight = 0.0;
  }
  if (n == 0 || m == 0) {
    return DS_OK;
  }
  memset(&s, 0, sizeof(s));
  s.n = (long)n;
  s.m = (long)m;
  s.eu = u;
  s.ev = v;
  s.w = w;
  s.ctx = context;
  s.st = DS_OK;
  s.eps = 1e-10 * (1.0 + maxw);
  s.endpoint = (long *)ds_context_alloc(context, 2 * m * sizeof(long));
  s.nb_off = (long *)ds_context_calloc(context, n + 1, sizeof(long));
  s.nb = (long *)ds_context_alloc(context, 2 * m * sizeof(long));
  /* one block: five n-sized arrays, nine 2n-sized ones and the 4n work
   * list (27 n longs) */
  s.mate = (long *)ds_context_alloc(context, 27 * n * sizeof(long));
  s.childs = (long **)ds_context_calloc(context, 2 * n, sizeof(long *));
  s.endps = (long **)ds_context_calloc(context, 2 * n, sizeof(long *));
  s.bbest = (long **)ds_context_calloc(context, 2 * n, sizeof(long *));
  s.dual = (double *)ds_context_alloc(context, 2 * n * sizeof(double));
  s.allow = (unsigned char *)ds_context_alloc(context, m);
  s.qcap = (long)n + 16;
  s.queue = (long *)ds_context_alloc(context, (size_t)s.qcap * sizeof(long));
  if (s.endpoint == NULL || s.nb_off == NULL || s.nb == NULL ||
      s.mate == NULL || s.childs == NULL || s.endps == NULL ||
      s.bbest == NULL || s.dual == NULL || s.allow == NULL || s.queue == NULL) {
    wm_free(&s);
    return DS_ERR_ALLOC;
  }
  blk = s.mate;
  s.mate = blk;
  blk += n;
  s.inblossom = blk;
  blk += n;
  s.leaves = blk;
  blk += n;
  s.tmp = blk;
  blk += n;
  s.unused = blk;
  blk += n;
  s.label = blk;
  blk += 2 * n;
  s.labelend = blk;
  blk += 2 * n;
  s.parent = blk;
  blk += 2 * n;
  s.nchilds = blk;
  blk += 2 * n;
  s.base = blk;
  blk += 2 * n;
  s.bestedge = blk;
  blk += 2 * n;
  s.nbbest = blk;
  blk += 2 * n;
  s.bestedgeto = blk;
  blk += 2 * n;
  s.stack = blk;
  blk += 2 * n;
  s.work = blk; /* 4n */
  for (k = 0; k < (long)m; k++) {
    s.endpoint[2 * k] = (long)u[k];
    s.endpoint[2 * k + 1] = (long)v[k];
    s.nb_off[u[k] + 1]++;
    s.nb_off[v[k] + 1]++;
  }
  for (i = 0; i < (long)n; i++) {
    s.nb_off[i + 1] += s.nb_off[i];
  }
  {
    long *fill = s.tmp;
    for (i = 0; i < (long)n; i++) {
      fill[i] = s.nb_off[i];
    }
    for (k = 0; k < (long)m; k++) {
      s.nb[fill[u[k]]++] = 2 * k + 1;
      s.nb[fill[v[k]]++] = 2 * k;
    }
  }
  for (i = 0; i < (long)n; i++) {
    s.mate[i] = -1;
    s.inblossom[i] = i;
    s.dual[i] = maxw;
  }
  for (i = 0; i < 2 * (long)n; i++) {
    s.label[i] = 0;
    s.labelend[i] = -1;
    s.parent[i] = -1;
    s.nchilds[i] = 0;
    s.base[i] = i < (long)n ? i : -1;
    s.bestedge[i] = -1;
    s.nbbest[i] = 0;
    if (i >= (long)n) {
      s.dual[i] = 0.0;
    }
  }
  s.nunused = 0;
  for (i = 2 * (long)n; i-- > (long)n;) {
    s.unused[s.nunused++] = i;
  }
  st = wm_run(&s, max_cardinality);
  if (st == DS_OK) {
    double total = 0.0;
    for (i = 0; i < (long)n; i++) {
      if (s.mate[i] >= 0) {
        out_mate[i] = (size_t)s.endpoint[s.mate[i]];
        if ((long)out_mate[i] > i) {
          total += w[s.mate[i] / 2];
        }
      }
    }
    if (out_weight != NULL) {
      *out_weight = total;
    }
  }
  wm_free(&s);
  return st;
}

ds_status_t ds_matching_min_weight_perfect(size_t n, const size_t *u,
                                           const size_t *v, const double *w,
                                           size_t m, size_t *out_mate,
                                           double *out_weight,
                                           ds_context_t *context) {
  double *t;
  double big = 0.0;
  double total = 0.0;
  size_t k;
  size_t i;
  ds_status_t st;
  if (out_mate == NULL || (m > 0 && (u == NULL || v == NULL || w == NULL))) {
    return DS_ERR_NULL;
  }
  if (n % 2 != 0) {
    return DS_ERR_STATE;
  }
  for (k = 0; k < m; k++) {
    if (fabs(w[k]) > big) {
      big = fabs(w[k]);
    }
  }
  t = (double *)ds_context_alloc(context, (m + 1) * sizeof(double));
  if (t == NULL) {
    return DS_ERR_ALLOC;
  }
  /* maximise (C - w) among maximum matchings: C > every w keeps all
   * transformed weights positive */
  for (k = 0; k < m; k++) {
    t[k] = 2.0 * big + 1.0 - w[k];
  }
  st = ds_matching_max_weight(n, u, v, t, m, true, out_mate, NULL, context);
  ds_context_free(context, t);
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < n; i++) {
    if (out_mate[i] == DS_WMATCH_NONE) {
      return DS_ERR_STATE;
    }
  }
  /* weight of the matching in the original weights (the cheapest of
   * parallel edges between a matched pair), in one pass over the edges */
  t = (double *)ds_context_alloc(context, (n + 1) * sizeof(double));
  if (t == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    t[i] = HUGE_VAL;
  }
  for (k = 0; k < m; k++) {
    size_t lo = u[k] < v[k] ? u[k] : v[k];
    size_t hi = u[k] < v[k] ? v[k] : u[k];
    if (out_mate[lo] == hi && w[k] < t[lo]) {
      t[lo] = w[k];
    }
  }
  for (i = 0; i < n; i++) {
    if (out_mate[i] > i) {
      total += t[i];
    }
  }
  ds_context_free(context, t);
  if (out_weight != NULL) {
    *out_weight = total;
  }
  return DS_OK;
}
