#include "planarity.h"
#include <string.h>

/*
 * Left-right planarity test (Brandes, "The Left-Right Planarity Test",
 * 2009), with the three depth-first passes unrolled into explicit stacks.
 *
 * Edges are numbered 0 .. m - 1 and oriented by the first pass from src to
 * dst. Half-edge 2e runs from src[e] to dst[e], half-edge 2e + 1 back.
 * Conflict pairs hold four edge numbers (the lowest and highest return
 * edges of a left and a right interval) and an identity: the reference
 * algorithm compares stack elements by identity (stack_bottom), and a pair
 * that is trimmed and pushed back keeps its identity, so the comparison
 * here is by id, not by stack height.
 */

#define PL_NONE DS_GVIEW_NONE

typedef struct pl_pair {
  size_t ll; /* left interval: lowest return edge */
  size_t lh; /* left interval: highest return edge */
  size_t rl; /* right interval */
  size_t rh;
  size_t id;
} pl_pair_t;

typedef struct pl {
  ds_context_t *ctx;
  size_t ncap;
  size_t mcap;
  size_t n;
  size_t m;
  size_t *block; /* every size_t array below */
  size_t *eu;    /* endpoints, mcap */
  size_t *ev;
  size_t *aoff;  /* undirected adjacency, ncap + 1 */
  size_t *aedge; /* 2 mcap; reused for half-edge positions on export */
  size_t *src;   /* orientation, mcap */
  size_t *dst;
  size_t *height; /* ncap; PL_NONE = unvisited */
  size_t *pedge;  /* parent edge, ncap */
  size_t *lowpt;  /* mcap */
  size_t *lowpt2; /* shares ref's storage (pass 1 only) */
  size_t *nest;   /* nesting depth, then the signed sort key of pass 3 */
  size_t *pos;    /* DFS cursor, ncap */
  size_t *stack;  /* DFS vertex stack, ncap */
  size_t *roots;  /* ncap */
  size_t nroots;
  size_t *ooff;  /* out-edges ordered by nesting depth, ncap + 1 */
  size_t *oedge; /* mcap */
  size_t *order; /* sorted edges, mcap; also the sign chain */
  size_t *count; /* bucket counts, 4 ncap + 2 */
  size_t *ref;   /* mcap */
  size_t *lowpt_edge;
  size_t *bottom; /* id of the pair on top when the edge started */
  size_t *cw;     /* 2 mcap: clockwise successor of a half-edge */
  size_t *ccw;
  size_t *first; /* ncap: a half-edge of each vertex */
  size_t *lref;  /* ncap */
  size_t *rref;
  signed char *side; /* mcap */
  pl_pair_t *S;      /* mcap + 1: a pair holds a back edge no other does */
  size_t sp;
  size_t next_id;
} pl_t;

static void *pl_alloc(ds_context_t *ctx, size_t count, size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > ((size_t)-1) / size) {
    return NULL;
  }
  return ds_context_alloc(ctx, count * size);
}

static void pl_free(pl_t *p) {
  ds_context_free(p->ctx, p->block);
  ds_context_free(p->ctx, p->side);
  ds_context_free(p->ctx, p->S);
  p->block = NULL;
  p->side = NULL;
  p->S = NULL;
}

/* room for graphs of up to ncap vertices and mcap edges */
static ds_status_t pl_init(pl_t *p, size_t ncap, size_t mcap,
                           ds_context_t *ctx) {
  size_t total;
  size_t *b;
  memset(p, 0, sizeof(*p));
  p->ctx = ctx;
  if (ncap > ((size_t)-1) / 32 || mcap > ((size_t)-1) / 32) {
    return DS_ERR_OVERFLOW;
  }
  p->ncap = ncap;
  p->mcap = mcap;
  total = 17 * mcap + 14 * ncap + 4;
  p->block = (size_t *)pl_alloc(ctx, total, sizeof(size_t));
  p->side = (signed char *)pl_alloc(ctx, mcap, sizeof(signed char));
  p->S = (pl_pair_t *)pl_alloc(ctx, mcap + 1, sizeof(pl_pair_t));
  if (p->block == NULL || p->side == NULL || p->S == NULL) {
    pl_free(p);
    return DS_ERR_ALLOC;
  }
  b = p->block;
  p->eu = b;
  b += mcap;
  p->ev = b;
  b += mcap;
  p->aoff = b;
  b += ncap + 1;
  p->aedge = b;
  b += 2 * mcap;
  p->src = b;
  b += mcap;
  p->dst = b;
  b += mcap;
  p->height = b;
  b += ncap;
  p->pedge = b;
  b += ncap;
  p->lowpt = b;
  b += mcap;
  p->nest = b;
  b += mcap;
  p->pos = b;
  b += ncap;
  p->stack = b;
  b += ncap;
  p->roots = b;
  b += ncap;
  p->ooff = b;
  b += ncap + 1;
  p->oedge = b;
  b += mcap;
  p->order = b;
  b += mcap;
  p->count = b;
  b += 4 * ncap + 2;
  p->ref = b;
  p->lowpt2 = b;
  b += mcap;
  p->lowpt_edge = b;
  b += mcap;
  p->bottom = b;
  b += mcap;
  p->cw = b;
  b += 2 * mcap;
  p->ccw = b;
  b += 2 * mcap;
  p->first = b;
  b += ncap;
  p->lref = b;
  b += ncap;
  p->rref = b;
  return DS_OK;
}

static size_t pl_min(size_t a, size_t b) { return a < b ? a : b; }

static void pl_adjacency(pl_t *p) {
  size_t v;
  size_t e;
  for (v = 0; v <= p->n; v++) {
    p->aoff[v] = 0;
  }
  for (e = 0; e < p->m; e++) {
    p->aoff[p->eu[e] + 1]++;
    p->aoff[p->ev[e] + 1]++;
  }
  for (v = 0; v < p->n; v++) {
    p->aoff[v + 1] += p->aoff[v];
    p->pos[v] = p->aoff[v];
  }
  for (e = 0; e < p->m; e++) {
    p->aedge[p->pos[p->eu[e]]++] = e;
    p->aedge[p->pos[p->ev[e]]++] = e;
  }
}

/* ---- pass 1: orientation, lowpoints, nesting depths ---------------------
 */

/* the work after edge e = (v, w) is explored: its nesting depth, and the
 * lowpoints of v's parent edge */
static void pl_finish(pl_t *p, size_t e) {
  size_t v = p->src[e];
  size_t ep = p->pedge[v];
  p->nest[e] = 2 * p->lowpt[e] + (p->lowpt2[e] < p->height[v] ? 1u : 0u);
  if (ep == PL_NONE) {
    return;
  }
  if (p->lowpt[e] < p->lowpt[ep]) {
    p->lowpt2[ep] = pl_min(p->lowpt[ep], p->lowpt2[e]);
    p->lowpt[ep] = p->lowpt[e];
  } else if (p->lowpt[e] > p->lowpt[ep]) {
    p->lowpt2[ep] = pl_min(p->lowpt2[ep], p->lowpt[e]);
  } else {
    p->lowpt2[ep] = pl_min(p->lowpt2[ep], p->lowpt2[e]);
  }
}

static void pl_orient(pl_t *p) {
  size_t r;
  size_t e;
  for (r = 0; r < p->n; r++) {
    p->height[r] = PL_NONE;
    p->pedge[r] = PL_NONE;
  }
  for (e = 0; e < p->m; e++) {
    p->src[e] = PL_NONE;
  }
  p->nroots = 0;
  for (r = 0; r < p->n; r++) {
    size_t top = 0;
    if (p->height[r] != PL_NONE) {
      continue;
    }
    p->height[r] = 0;
    p->roots[p->nroots++] = r;
    p->pos[r] = p->aoff[r];
    p->stack[top++] = r;
    while (top > 0) {
      size_t v = p->stack[top - 1];
      if (p->pos[v] < p->aoff[v + 1]) {
        size_t w;
        e = p->aedge[p->pos[v]++];
        if (p->src[e] != PL_NONE) {
          continue;
        }
        w = p->eu[e] == v ? p->ev[e] : p->eu[e];
        p->src[e] = v;
        p->dst[e] = w;
        p->lowpt[e] = p->height[v];
        p->lowpt2[e] = p->height[v];
        if (p->height[w] == PL_NONE) { /* tree edge */
          p->pedge[w] = e;
          p->height[w] = p->height[v] + 1;
          p->pos[w] = p->aoff[w];
          p->stack[top++] = w;
        } else { /* back edge */
          p->lowpt[e] = p->height[w];
          pl_finish(p, e);
        }
      } else {
        top--;
        if (p->pedge[v] != PL_NONE) {
          pl_finish(p, p->pedge[v]);
        }
      }
    }
  }
}

/* out-edges of every vertex sorted by key (0 <= key < range), by a
 * counting sort over all edges and a stable distribution by source */
static void pl_order(pl_t *p, const size_t *key, size_t range) {
  size_t k;
  size_t e;
  size_t v;
  for (k = 0; k <= range; k++) {
    p->count[k] = 0;
  }
  for (e = 0; e < p->m; e++) {
    p->count[key[e] + 1]++;
  }
  for (k = 0; k < range; k++) {
    p->count[k + 1] += p->count[k];
  }
  for (e = 0; e < p->m; e++) {
    p->order[p->count[key[e]]++] = e;
  }
  for (v = 0; v <= p->n; v++) {
    p->ooff[v] = 0;
  }
  for (e = 0; e < p->m; e++) {
    p->ooff[p->src[e] + 1]++;
  }
  for (v = 0; v < p->n; v++) {
    p->ooff[v + 1] += p->ooff[v];
    p->pos[v] = p->ooff[v];
  }
  for (k = 0; k < p->m; k++) {
    e = p->order[k];
    p->oedge[p->pos[p->src[e]]++] = e;
  }
}

/* ---- pass 2: the left-right partition ------------------------------------
 */

static size_t pl_lp(const pl_t *p, size_t e) {
  return e == PL_NONE ? PL_NONE : p->lowpt[e];
}

static size_t pl_top_id(const pl_t *p) {
  return p->sp > 0 ? p->S[p->sp - 1].id : 0;
}

static bool pl_empty(size_t low, size_t high) {
  return low == PL_NONE && high == PL_NONE;
}

/* an interval conflicts with return edge b when it returns higher */
static bool pl_conflicting(const pl_t *p, size_t high, size_t b) {
  return high != PL_NONE && p->lowpt[high] > p->lowpt[b];
}

static size_t pl_lowest(const pl_t *p, const pl_pair_t *q) {
  return pl_min(pl_lp(p, q->ll), pl_lp(p, q->rl));
}

static void pl_swap(pl_pair_t *q) {
  size_t t = q->ll;
  q->ll = q->rl;
  q->rl = t;
  t = q->lh;
  q->lh = q->rh;
  q->rh = t;
}

static bool pl_push(pl_t *p, size_t ll, size_t lh, size_t rl, size_t rh) {
  pl_pair_t *q;
  if (p->sp > p->m) {
    return false;
  }
  q = &p->S[p->sp++];
  q->ll = ll;
  q->lh = lh;
  q->rl = rl;
  q->rh = rh;
  q->id = p->next_id++;
  return true;
}

/* 1: constraints added, 0: not planar, -1: inconsistent state */
static int pl_add_constraints(pl_t *p, size_t ei, size_t e) {
  pl_pair_t P;
  pl_pair_t Q;
  P.ll = PL_NONE;
  P.lh = PL_NONE;
  P.rl = PL_NONE;
  P.rh = PL_NONE;
  /* merge the return edges of ei into P.R */
  do {
    if (p->sp == 0) {
      return -1;
    }
    Q = p->S[--p->sp];
    if (!pl_empty(Q.ll, Q.lh)) {
      pl_swap(&Q);
    }
    if (!pl_empty(Q.ll, Q.lh)) {
      return 0;
    }
    if (Q.rl == PL_NONE) {
      return -1;
    }
    if (p->lowpt[Q.rl] > p->lowpt[e]) { /* merge intervals */
      if (pl_empty(P.rl, P.rh)) {
        P.rh = Q.rh;
      } else {
        p->ref[P.rl] = Q.rh;
      }
      P.rl = Q.rl;
    } else { /* align */
      p->ref[Q.rl] = p->lowpt_edge[e];
    }
  } while (pl_top_id(p) != p->bottom[ei]);
  /* merge the conflicting return edges of the earlier edges into P.L */
  while (p->sp > 0 && (pl_conflicting(p, p->S[p->sp - 1].lh, ei) ||
                       pl_conflicting(p, p->S[p->sp - 1].rh, ei))) {
    Q = p->S[--p->sp];
    if (pl_conflicting(p, Q.rh, ei)) {
      pl_swap(&Q);
    }
    if (pl_conflicting(p, Q.rh, ei)) {
      return 0;
    }
    /* the interval below lowpt(ei) joins P.R */
    if (pl_empty(P.rl, P.rh)) {
      P.rl = Q.rl;
      P.rh = Q.rh;
    } else {
      p->ref[P.rl] = Q.rh;
      if (Q.rl != PL_NONE) {
        P.rl = Q.rl;
      }
    }
    if (pl_empty(P.ll, P.lh)) {
      P.lh = Q.lh;
    } else if (P.ll != PL_NONE) {
      p->ref[P.ll] = Q.lh;
    }
    P.ll = Q.ll;
  }
  if ((!pl_empty(P.ll, P.lh) || !pl_empty(P.rl, P.rh)) &&
      !pl_push(p, P.ll, P.lh, P.rl, P.rh)) {
    return -1;
  }
  return 1;
}

/* drop the back edges returning to u */
static void pl_trim(pl_t *p, size_t u) {
  pl_pair_t *P;
  while (p->sp > 0 && pl_lowest(p, &p->S[p->sp - 1]) == p->height[u]) {
    P = &p->S[--p->sp];
    if (P->ll != PL_NONE) {
      p->side[P->ll] = -1;
    }
  }
  if (p->sp == 0) {
    return;
  }
  P = &p->S[p->sp - 1]; /* trimmed in place: it keeps its identity */
  while (P->lh != PL_NONE && p->dst[P->lh] == u) {
    P->lh = p->ref[P->lh];
  }
  if (P->lh == PL_NONE && P->ll != PL_NONE) { /* just emptied */
    p->ref[P->ll] = P->rl;
    p->side[P->ll] = -1;
    P->ll = PL_NONE;
  }
  while (P->rh != PL_NONE && p->dst[P->rh] == u) {
    P->rh = p->ref[P->rh];
  }
  if (P->rh == PL_NONE && P->rl != PL_NONE) {
    p->ref[P->rl] = P->ll;
    p->side[P->rl] = -1;
    P->rl = PL_NONE;
  }
}

/* after edge ei of v is explored: integrate its return edges */
static int pl_integrate(pl_t *p, size_t v, size_t ei) {
  size_t e;
  if (p->lowpt[ei] >= p->height[v]) {
    return 1;
  }
  e = p->pedge[v];
  if (e == PL_NONE) {
    return -1;
  }
  if (p->pos[v] == p->ooff[v]) { /* the first edge of v */
    p->lowpt_edge[e] = p->lowpt_edge[ei];
    return 1;
  }
  return pl_add_constraints(p, ei, e);
}

static int pl_partition(pl_t *p) {
  size_t i;
  size_t e;
  p->sp = 0;
  p->next_id = 1;
  for (e = 0; e < p->m; e++) {
    p->ref[e] = PL_NONE;
    p->side[e] = 1;
    p->lowpt_edge[e] = PL_NONE;
    p->bottom[e] = 0;
  }
  for (i = 0; i < p->nroots; i++) {
    size_t top = 0;
    size_t r = p->roots[i];
    p->pos[r] = p->ooff[r];
    p->stack[top++] = r;
    while (top > 0) {
      size_t v = p->stack[top - 1];
      int rc;
      if (p->pos[v] < p->ooff[v + 1]) {
        size_t ei = p->oedge[p->pos[v]];
        size_t w = p->dst[ei];
        p->bottom[ei] = pl_top_id(p);
        if (p->pedge[w] == ei) { /* tree edge: integrated when w is done */
          p->pos[w] = p->ooff[w];
          p->stack[top++] = w;
          continue;
        }
        p->lowpt_edge[ei] = ei;
        if (!pl_push(p, PL_NONE, PL_NONE, ei, ei)) {
          return -1;
        }
        rc = pl_integrate(p, v, ei);
        if (rc != 1) {
          return rc;
        }
        p->pos[v]++;
      } else {
        size_t u;
        top--;
        e = p->pedge[v];
        if (e == PL_NONE) {
          continue;
        }
        u = p->src[e];
        pl_trim(p, u);
        if (p->lowpt[e] < p->height[u]) { /* e has a return edge */
          size_t hl;
          size_t hr;
          if (p->sp == 0) {
            return -1;
          }
          hl = p->S[p->sp - 1].lh;
          hr = p->S[p->sp - 1].rh;
          if (hl != PL_NONE && (hr == PL_NONE || p->lowpt[hl] > p->lowpt[hr])) {
            p->ref[e] = hl;
          } else {
            p->ref[e] = hr;
          }
        }
        rc = pl_integrate(p, u, e);
        if (rc != 1) {
          return rc;
        }
        p->pos[u]++;
      }
    }
  }
  return 1;
}

/* 1 planar, 0 not, -1 inconsistent; the graph is p->eu/ev, p->n, p->m */
static int pl_run(pl_t *p) {
  if (p->n >= 3 && p->m > 3 * p->n - 6) {
    return 0;
  }
  pl_adjacency(p);
  pl_orient(p);
  pl_order(p, p->nest, 2 * p->n);
  return pl_partition(p);
}

/* ---- pass 3: the embedding -----------------------------------------------
 */

static int pl_signs(pl_t *p) {
  size_t e;
  size_t *chain = p->order;
  for (e = 0; e < p->m; e++) {
    size_t len = 0;
    size_t x = e;
    while (p->ref[x] != PL_NONE) {
      if (len >= p->m) {
        return -1; /* a cycle of references */
      }
      chain[len++] = x;
      x = p->ref[x];
    }
    while (len > 0) {
      size_t y = chain[--len];
      p->side[y] = (signed char)(p->side[y] * p->side[p->ref[y]]);
      p->ref[y] = PL_NONE;
    }
  }
  return 1;
}

static void pl_link_cw(pl_t *p, size_t x, size_t h, size_t ref) {
  size_t next;
  if (ref == PL_NONE) {
    p->cw[h] = h;
    p->ccw[h] = h;
    p->first[x] = h;
    return;
  }
  next = p->cw[ref];
  p->cw[ref] = h;
  p->ccw[h] = ref;
  p->cw[h] = next;
  p->ccw[next] = h;
}

static void pl_link_ccw(pl_t *p, size_t x, size_t h, size_t ref) {
  if (ref == PL_NONE) {
    pl_link_cw(p, x, h, PL_NONE);
    return;
  }
  pl_link_cw(p, x, h, p->ccw[ref]);
  if (p->first[x] == ref) {
    p->first[x] = h;
  }
}

static int pl_embed(pl_t *p) {
  size_t n = p->n;
  size_t e;
  size_t v;
  size_t i;
  if (pl_signs(p) != 1) {
    return -1;
  }
  for (e = 0; e < p->m; e++) { /* the signed nesting depth, shifted */
    p->nest[e] = p->side[e] > 0 ? 2 * n + p->nest[e] : 2 * n - p->nest[e];
  }
  pl_order(p, p->nest, 4 * n);
  /* the out-edges of each vertex, clockwise by signed nesting depth */
  for (v = 0; v < n; v++) {
    size_t prev = PL_NONE;
    p->first[v] = PL_NONE;
    for (i = p->ooff[v]; i < p->ooff[v + 1]; i++) {
      size_t h = 2 * p->oedge[i];
      pl_link_cw(p, v, h, prev);
      prev = h;
    }
  }
  /* the in-edges, by a third depth-first pass */
  for (i = 0; i < p->nroots; i++) {
    size_t top = 0;
    size_t r = p->roots[i];
    p->pos[r] = p->ooff[r];
    p->stack[top++] = r;
    while (top > 0) {
      v = p->stack[top - 1];
      if (p->pos[v] < p->ooff[v + 1]) {
        size_t ei = p->oedge[p->pos[v]++];
        size_t w = p->dst[ei];
        size_t h = 2 * ei + 1; /* w's half-edge back to v */
        if (p->pedge[w] == ei) {
          if (p->first[w] == PL_NONE) {
            pl_link_cw(p, w, h, PL_NONE);
          } else {
            pl_link_ccw(p, w, h, p->first[w]);
          }
          p->lref[v] = 2 * ei;
          p->rref[v] = 2 * ei;
          p->pos[w] = p->ooff[w];
          p->stack[top++] = w;
        } else if (p->side[ei] > 0) {
          pl_link_cw(p, w, h, p->rref[w]);
        } else {
          pl_link_ccw(p, w, h, p->lref[w]);
          p->lref[w] = h;
        }
      } else {
        top--;
      }
    }
  }
  return 1;
}

static size_t pl_head(const pl_t *p, size_t h) {
  return (h & 1u) == 0 ? p->dst[h / 2] : p->src[h / 2];
}

/* copy the rotation system out; vertices >= keep (an apex) are dropped
 * together with their edges, and their positions mark `outer` */
static ds_status_t pl_export(pl_t *p, size_t keep, ds_planar_embedding_t *out) {
  size_t *where = p->aedge; /* half-edge -> output position */
  size_t v;
  size_t total = 0;
  size_t h;
  size_t kept_edges = 0;
  size_t e;
  for (e = 0; e < p->m; e++) {
    if (p->eu[e] < keep && p->ev[e] < keep) {
      kept_edges++;
    }
  }
  out->vertices = keep;
  out->edges = kept_edges;
  out->offsets = (size_t *)pl_alloc(p->ctx, keep + 1, sizeof(size_t));
  out->target = (size_t *)pl_alloc(p->ctx, 2 * kept_edges, sizeof(size_t));
  out->twin = (size_t *)pl_alloc(p->ctx, 2 * kept_edges, sizeof(size_t));
  out->outer = NULL;
  if (keep < p->n) {
    out->outer = (size_t *)pl_alloc(p->ctx, keep, sizeof(size_t));
  }
  if (out->offsets == NULL || out->target == NULL || out->twin == NULL ||
      (keep < p->n && out->outer == NULL)) {
    ds_planar_embedding_destroy(out);
    return DS_ERR_ALLOC;
  }
  for (v = 0; v < keep; v++) {
    size_t start = p->first[v];
    size_t apex = PL_NONE;
    out->offsets[v] = total;
    if (start != PL_NONE) {
      h = start;
      do {
        if (pl_head(p, h) < keep) {
          where[h] = total;
          out->target[total++] = pl_head(p, h);
        } else {
          apex = h;
        }
        h = p->cw[h];
      } while (h != start);
    }
    if (out->outer != NULL) {
      out->outer[v] = PL_NONE;
      if (apex != PL_NONE && p->ccw[apex] != apex) {
        out->outer[v] = p->ccw[apex]; /* a half-edge; renumbered below */
      }
    }
  }
  out->offsets[keep] = total;
  for (v = 0; v < keep; v++) {
    size_t start = p->first[v];
    if (start == PL_NONE) {
      continue;
    }
    h = start;
    do {
      if (pl_head(p, h) < keep) {
        out->twin[where[h]] = where[h ^ 1u];
      }
      h = p->cw[h];
    } while (h != start);
    if (out->outer != NULL && out->outer[v] != PL_NONE) {
      out->outer[v] = where[out->outer[v]];
    }
  }
  return DS_OK;
}

/* ---- reading the graph ---------------------------------------------------
 */

/* the distinct non-loop edges of a view, as (lo, hi) pairs with lo < hi;
 * room for `extra` more edges is reserved after them */
static ds_status_t pl_collect(const ds_gview_t *g, size_t extra,
                              ds_context_t *ctx, size_t **out_u, size_t **out_v,
                              size_t *out_m) {
  size_t n = g->vertex_count;
  size_t total = 0;
  size_t v;
  size_t i;
  size_t k;
  size_t m = 0;
  size_t *lo;
  size_t *hi;
  size_t *su;
  size_t *sv;
  size_t *cnt;
  *out_u = NULL;
  *out_v = NULL;
  *out_m = 0;
  for (v = 0; v < n; v++) {
    size_t d = g->degree(g->graph, v);
    if (d > ((size_t)-1) / 4 - total) {
      return DS_ERR_OVERFLOW;
    }
    total += d;
  }
  if (extra > ((size_t)-1) / 4 - total) {
    return DS_ERR_OVERFLOW;
  }
  lo = (size_t *)pl_alloc(ctx, total, sizeof(size_t));
  hi = (size_t *)pl_alloc(ctx, total, sizeof(size_t));
  su = (size_t *)pl_alloc(ctx, total + extra, sizeof(size_t));
  sv = (size_t *)pl_alloc(ctx, total + extra, sizeof(size_t));
  cnt = (size_t *)pl_alloc(ctx, n + 1, sizeof(size_t));
  if (lo == NULL || hi == NULL || su == NULL || sv == NULL || cnt == NULL) {
    ds_context_free(ctx, lo);
    ds_context_free(ctx, hi);
    ds_context_free(ctx, su);
    ds_context_free(ctx, sv);
    ds_context_free(ctx, cnt);
    return DS_ERR_ALLOC;
  }
  k = 0;
  for (v = 0; v < n; v++) {
    size_t d = g->degree(g->graph, v);
    for (i = 0; i < d; i++) {
      size_t t = PL_NONE;
      double w = 1.0;
      g->edge(g->graph, v, i, &t, &w);
      if (t >= n) {
        ds_context_free(ctx, lo);
        ds_context_free(ctx, hi);
        ds_context_free(ctx, su);
        ds_context_free(ctx, sv);
        ds_context_free(ctx, cnt);
        return DS_ERR_INVALID;
      }
      if (t == v) {
        continue;
      }
      lo[k] = v < t ? v : t;
      hi[k] = v < t ? t : v;
      k++;
    }
  }
  /* counting sort by lo, then drop repeats with a stamp per hi */
  for (v = 0; v <= n; v++) {
    cnt[v] = 0;
  }
  for (i = 0; i < k; i++) {
    cnt[lo[i] + 1]++;
  }
  for (v = 0; v < n; v++) {
    cnt[v + 1] += cnt[v];
  }
  for (i = 0; i < k; i++) {
    size_t at = cnt[lo[i]]++;
    su[at] = lo[i];
    sv[at] = hi[i];
  }
  for (v = 0; v < n; v++) {
    cnt[v] = PL_NONE; /* now: the last lo seen with this hi */
  }
  for (i = 0; i < k; i++) {
    if (cnt[sv[i]] == su[i]) {
      continue;
    }
    cnt[sv[i]] = su[i];
    su[m] = su[i];
    sv[m] = sv[i];
    m++;
  }
  ds_context_free(ctx, lo);
  ds_context_free(ctx, hi);
  ds_context_free(ctx, cnt);
  *out_u = su;
  *out_v = sv;
  *out_m = m;
  return DS_OK;
}

static void pl_empty_embedding(ds_planar_embedding_t *e, ds_context_t *ctx) {
  if (e != NULL) {
    memset(e, 0, sizeof(*e));
    e->context = ctx;
  }
}

static ds_status_t pl_decide(const ds_gview_t *g, bool apex, bool *out_yes,
                             ds_planar_embedding_t *out_embedding,
                             ds_context_t *context) {
  pl_t p;
  size_t *eu;
  size_t *ev;
  size_t m;
  size_t n;
  size_t v;
  int rc;
  ds_status_t st;
  if (g == NULL || out_yes == NULL) {
    return DS_ERR_NULL;
  }
  *out_yes = false;
  pl_empty_embedding(out_embedding, context);
  n = g->vertex_count;
  if (n > ((size_t)-1) / 32) {
    return DS_ERR_OVERFLOW;
  }
  st = pl_collect(g, apex ? n : 0, context, &eu, &ev, &m);
  if (st != DS_OK) {
    return st;
  }
  if (apex) {
    for (v = 0; v < n; v++) {
      eu[m + v] = v;
      ev[m + v] = n;
    }
    m += n;
    n += 1;
  }
  if (n >= 3 && m > 3 * n - 6) {
    ds_context_free(context, eu);
    ds_context_free(context, ev);
    return DS_OK;
  }
  st = pl_init(&p, n, m, context);
  if (st != DS_OK) {
    ds_context_free(context, eu);
    ds_context_free(context, ev);
    return st;
  }
  memcpy(p.eu, eu, m * sizeof(size_t));
  memcpy(p.ev, ev, m * sizeof(size_t));
  ds_context_free(context, eu);
  ds_context_free(context, ev);
  p.n = n;
  p.m = m;
  rc = pl_run(&p);
  if (rc == 1 && out_embedding != NULL) {
    rc = pl_embed(&p);
    if (rc == 1) {
      st = pl_export(&p, apex ? n - 1 : n, out_embedding);
    }
  }
  pl_free(&p);
  if (rc < 0) {
    return DS_ERR_INTERNAL;
  }
  if (st != DS_OK) {
    return st;
  }
  *out_yes = rc == 1;
  return DS_OK;
}

ds_status_t ds_planarity_test(const ds_gview_t *g, bool *out_planar,
                              ds_planar_embedding_t *out_embedding,
                              ds_context_t *context) {
  return pl_decide(g, false, out_planar, out_embedding, context);
}

ds_status_t ds_outerplanarity_test(const ds_gview_t *g, bool *out_outerplanar,
                                   ds_planar_embedding_t *out_embedding,
                                   ds_context_t *context) {
  return pl_decide(g, true, out_outerplanar, out_embedding, context);
}

void ds_planar_embedding_destroy(ds_planar_embedding_t *e) {
  if (e == NULL) {
    return;
  }
  ds_context_free(e->context, e->offsets);
  ds_context_free(e->context, e->target);
  ds_context_free(e->context, e->twin);
  ds_context_free(e->context, e->outer);
  e->offsets = NULL;
  e->target = NULL;
  e->twin = NULL;
  e->outer = NULL;
  e->vertices = 0;
  e->edges = 0;
}

/* ---- faces and checks ----------------------------------------------------
 */

void ds_planar_embedding_view(const ds_planar_embedding_t *e,
                              ds_gview_csr_t *out_csr, ds_gview_t *out_view) {
  out_csr->vertex_count = e->vertices;
  out_csr->offsets = e->offsets;
  out_csr->targets = e->target;
  out_csr->weights = NULL;
  ds_gview_from_csr(out_csr, out_view);
}

size_t ds_planar_next(const ds_planar_embedding_t *e, size_t h) {
  size_t t = e->target[h];
  size_t q = e->twin[h];
  return q == e->offsets[t] ? e->offsets[t + 1] - 1 : q - 1;
}

size_t ds_planar_source(const ds_planar_embedding_t *e, size_t h) {
  size_t lo = 0;
  size_t hi = e->vertices; /* offsets[lo] <= h < offsets[hi] */
  while (hi - lo > 1) {
    size_t mid = lo + (hi - lo) / 2;
    if (e->offsets[mid] <= h) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return lo;
}

void ds_planar_faces(const ds_planar_embedding_t *e, size_t *out_face,
                     size_t *out_count) {
  size_t total = 2 * e->edges;
  size_t h;
  size_t faces = 0;
  for (h = 0; h < total; h++) {
    out_face[h] = PL_NONE;
  }
  for (h = 0; h < total; h++) {
    size_t x = h;
    if (out_face[h] != PL_NONE) {
      continue;
    }
    while (out_face[x] == PL_NONE) {
      out_face[x] = faces;
      x = ds_planar_next(e, x);
    }
    faces++;
  }
  if (out_count != NULL) {
    *out_count = faces;
  }
}

ds_status_t ds_planar_embedding_check(const ds_planar_embedding_t *e,
                                      bool *out_valid) {
  size_t n;
  size_t total;
  size_t v;
  size_t h;
  size_t *block;
  size_t *mark;
  size_t *face;
  size_t *comp;
  size_t *queue;
  size_t *cv;
  size_t *ce;
  size_t *cf;
  size_t faces;
  size_t comps = 0;
  bool ok = true;
  if (e == NULL || out_valid == NULL) {
    return DS_ERR_NULL;
  }
  *out_valid = false;
  n = e->vertices;
  total = 2 * e->edges;
  if (e->offsets == NULL || e->offsets[0] != 0 || e->offsets[n] != total) {
    return DS_OK;
  }
  for (v = 0; v < n; v++) {
    if (e->offsets[v] > e->offsets[v + 1]) {
      return DS_OK;
    }
  }
  if (n > ((size_t)-1) / 8 || total > ((size_t)-1) / 8 - 6 * n) {
    return DS_ERR_OVERFLOW;
  }
  block = (size_t *)pl_alloc(e->context, 6 * n + total, sizeof(size_t));
  if (block == NULL) {
    return DS_ERR_ALLOC;
  }
  mark = block;
  comp = mark + n;
  queue = comp + n;
  cv = queue + n;
  ce = cv + n;
  cf = ce + n;
  face = cf + n;
  for (v = 0; v < n; v++) {
    mark[v] = PL_NONE;
    comp[v] = PL_NONE;
  }
  /* twins pair up across the right vertices; no repeated neighbour */
  for (v = 0; v < n && ok; v++) {
    for (h = e->offsets[v]; h < e->offsets[v + 1]; h++) {
      size_t t = e->target[h];
      size_t q = e->twin[h];
      if (t >= n || t == v || q >= total || q == h || e->twin[q] != h ||
          q < e->offsets[t] || q >= e->offsets[t + 1] || e->target[q] != v ||
          mark[t] == v) {
        ok = false;
        break;
      }
      mark[t] = v;
    }
  }
  if (ok) {
    /* components by breadth-first search over the half-edges */
    for (v = 0; v < n; v++) {
      size_t head = 0;
      size_t tail = 0;
      if (comp[v] != PL_NONE || e->offsets[v] == e->offsets[v + 1]) {
        continue;
      }
      comp[v] = comps;
      cv[comps] = 0;
      ce[comps] = 0;
      cf[comps] = 0;
      queue[tail++] = v;
      while (head < tail) {
        size_t x = queue[head++];
        cv[comps]++;
        for (h = e->offsets[x]; h < e->offsets[x + 1]; h++) {
          size_t t = e->target[h];
          ce[comps]++;
          if (comp[t] == PL_NONE) {
            comp[t] = comps;
            queue[tail++] = t;
          }
        }
      }
      comps++;
    }
    ds_planar_faces(e, face, &faces);
    {
      /* faces are numbered in the order of their first half-edges: count
       * each one there, for the component of that half-edge */
      size_t seen = 0;
      for (h = 0; h < total && seen < faces; h++) {
        if (face[h] == seen) {
          cf[comp[ds_planar_source(e, h)]]++;
          seen++;
        }
      }
    }
    for (v = 0; v < comps; v++) {
      /* V - E + F = 2 with E counted twice in ce */
      if (cv[v] + cf[v] != ce[v] / 2 + 2) {
        ok = false;
      }
    }
  }
  ds_context_free(e->context, block);
  *out_valid = ok;
  return DS_OK;
}

/* ---- Kuratowski certificates ---------------------------------------------
 */

ds_status_t ds_kuratowski_classify(size_t vertices, const size_t *u,
                                   const size_t *v, size_t count,
                                   ds_kuratowski_kind_t *out_kind,
                                   ds_context_t *context) {
  size_t *block;
  size_t *deg;
  size_t *off;
  size_t *adj;
  size_t *bidx;
  size_t *cur;
  unsigned char *used;
  unsigned char pairs[6][6];
  size_t branch[6];
  size_t nb = 0;
  size_t want = 0;
  size_t traced = 0;
  size_t i;
  size_t x;
  bool ok = true;
  if (out_kind == NULL || (count > 0 && (u == NULL || v == NULL))) {
    return DS_ERR_NULL;
  }
  *out_kind = DS_KURATOWSKI_NONE;
  for (i = 0; i < count; i++) {
    if (u[i] >= vertices || v[i] >= vertices) {
      return DS_ERR_INVALID;
    }
  }
  if (count < 9) {
    return DS_OK;
  }
  if (vertices > ((size_t)-1) / 8 || count > ((size_t)-1) / 8) {
    return DS_ERR_OVERFLOW;
  }
  block =
      (size_t *)pl_alloc(context, 4 * vertices + 1 + 2 * count, sizeof(size_t));
  used = (unsigned char *)pl_alloc(context, count, 1);
  if (block == NULL || used == NULL) {
    ds_context_free(context, block);
    ds_context_free(context, used);
    return DS_ERR_ALLOC;
  }
  deg = block;
  off = deg + vertices;
  bidx = off + vertices + 1;
  cur = bidx + vertices;
  adj = cur + vertices;
  memset(used, 0, count);
  memset(pairs, 0, sizeof(pairs));
  for (x = 0; x < vertices; x++) {
    deg[x] = 0;
    bidx[x] = PL_NONE;
  }
  for (i = 0; i < count; i++) {
    if (u[i] == v[i]) {
      ok = false;
    }
    deg[u[i]]++;
    deg[v[i]]++;
  }
  for (x = 0; x < vertices && ok; x++) {
    if (deg[x] == 0 || deg[x] == 2) {
      continue;
    }
    if (deg[x] < 3 || nb == 6 || (want != 0 && deg[x] != want)) {
      ok = false;
      break;
    }
    want = deg[x];
    bidx[x] = nb;
    branch[nb++] = x;
  }
  if (ok && !((nb == 5 && want == 4) || (nb == 6 && want == 3))) {
    ok = false;
  }
  if (ok) {
    off[0] = 0;
    for (x = 0; x < vertices; x++) {
      off[x + 1] = off[x] + deg[x];
      cur[x] = off[x];
    }
    for (i = 0; i < count; i++) {
      adj[cur[u[i]]++] = i;
      adj[cur[v[i]]++] = i;
    }
    /* follow every path that leaves a branch vertex */
    for (i = 0; i < nb && ok; i++) {
      size_t b = branch[i];
      size_t k;
      for (k = off[b]; k < off[b + 1] && ok; k++) {
        size_t f = adj[k];
        size_t at;
        if (used[f]) {
          continue;
        }
        used[f] = 1;
        traced++;
        at = u[f] == b ? v[f] : u[f];
        while (bidx[at] == PL_NONE) {
          size_t g = adj[off[at]] == f ? adj[off[at] + 1] : adj[off[at]];
          if (used[g]) {
            ok = false;
            break;
          }
          used[g] = 1;
          traced++;
          f = g;
          at = u[g] == at ? v[g] : u[g];
        }
        if (!ok || at == b || pairs[i][bidx[at]]) {
          ok = false;
          break;
        }
        pairs[i][bidx[at]] = 1;
        pairs[bidx[at]][i] = 1;
      }
    }
    if (ok && traced != count) {
      ok = false; /* a cycle of degree-2 vertices apart from the rest */
    }
  }
  if (ok && nb == 5) {
    *out_kind = DS_KURATOWSKI_K5; /* five vertices, four distinct paths each */
  } else if (ok) {
    /* a cubic graph on six vertices is K3,3 exactly when it is bipartite */
    unsigned char colour[6];
    size_t a;
    size_t b;
    size_t side0 = 0;
    /* the neighbours of branch 0 on one side, everything else on the other
     * (in a bipartite cubic graph on six vertices that is the partition) */
    for (a = 0; a < 6; a++) {
      colour[a] = (unsigned char)(pairs[0][a] ? 1u : 0u);
      side0 += colour[a] == 0 ? 1u : 0u;
    }
    for (a = 0; a < 6 && ok; a++) {
      for (b = 0; b < 6; b++) {
        if (pairs[a][b] && colour[a] == colour[b]) {
          ok = false;
          break;
        }
      }
    }
    if (ok && side0 == 3) {
      *out_kind = DS_KURATOWSKI_K33;
    }
  }
  ds_context_free(context, block);
  ds_context_free(context, used);
  return DS_OK;
}

/* Certificates are found by shrinking the graph while it stays non-planar.
 * The graph is kept reduced: no vertex of degree one (its edge cannot lie
 * in a subdivision), no vertex of degree two (its two edges merge into one
 * standing for a path), no loops, and of parallel paths only the shortest.
 * None of this changes planarity. Random subsets of the reduced edges are
 * deleted when the rest stays non-planar; the deletion probability is
 * halved after a failure and doubled after a success. When fewer than one
 * deletion is expected, single edges are deleted until none can be. The
 * reduced graph is then minimal non-planar with minimum degree three: K5
 * or K3,3 itself, whose edges expand into the paths of a subdivision. The
 * generator is seeded with a constant, so results are reproducible. */

typedef struct kx {
  pl_t p;
  ds_context_t *ctx;
  size_t n;
  size_t m;
  const size_t *eu;
  const size_t *ev;
  size_t *block;
  size_t *sa; /* reduced edges: endpoints (original vertices), m */
  size_t *sb;
  size_t *head;  /* the path: first original edge, then next[] */
  size_t *tail;  /* m */
  size_t *next;  /* m */
  size_t *len;   /* m */
  size_t *alias; /* m: the reduced edge an edge was merged into */
  size_t *ids;   /* the live reduced edges, m */
  size_t c;
  size_t *adj;  /* 2m: incident reduced edges by local vertex */
  size_t *tab;  /* hash of endpoint pairs, tsize */
  size_t *tgen; /* generation of each slot, tsize */
  size_t tmask;
  size_t gen;    /* current hash generation */
  size_t *deg;   /* n, by local vertex */
  size_t *off;   /* n + 1 */
  size_t *stack; /* n */
  size_t *vert;  /* n: local -> original vertex */
  size_t *vmap;  /* n: original -> local, valid when stamp matches */
  size_t *stamp; /* n */
  size_t vgen;
  unsigned char *flags; /* m live + m del + m need + n inq */
  unsigned char *live;
  unsigned char *del;
  unsigned char *need;
  unsigned char *inq;
  size_t sp;
  unsigned long rng;
} kx_t;

static size_t kx_slot(const kx_t *k, size_t a, size_t b) {
  size_t x = a < b ? a : b;
  size_t y = a < b ? b : a;
  size_t h = x * (size_t)2654435761u;
  h ^= y + (size_t)0x9e3779b9u + (h << 6) + (h >> 2);
  h *= (size_t)0x85ebca6bu;
  h ^= h >> 13;
  return h & k->tmask;
}

static bool kx_used(const kx_t *k, size_t i) { return k->tgen[i] == k->gen; }

static size_t kx_find(const kx_t *k, size_t a, size_t b) {
  size_t i = kx_slot(k, a, b);
  while (kx_used(k, i)) {
    size_t s = k->tab[i];
    if ((k->sa[s] == a && k->sb[s] == b) || (k->sa[s] == b && k->sb[s] == a)) {
      return s;
    }
    i = (i + 1) & k->tmask;
  }
  return PL_NONE;
}

static void kx_insert(kx_t *k, size_t s) {
  size_t i = kx_slot(k, k->sa[s], k->sb[s]);
  while (kx_used(k, i)) {
    i = (i + 1) & k->tmask;
  }
  k->tab[i] = s;
  k->tgen[i] = k->gen;
}

/* remove s (still under its endpoints) by backward shifting */
static void kx_erase(kx_t *k, size_t s) {
  size_t i = kx_slot(k, k->sa[s], k->sb[s]);
  size_t j;
  while (kx_used(k, i) && k->tab[i] != s) {
    i = (i + 1) & k->tmask;
  }
  if (!kx_used(k, i)) {
    return;
  }
  j = i;
  for (;;) {
    size_t h;
    j = (j + 1) & k->tmask;
    if (!kx_used(k, j)) {
      break;
    }
    h = kx_slot(k, k->sa[k->tab[j]], k->sb[k->tab[j]]);
    /* move j back to i unless its home lies cyclically in (i, j] */
    if ((i <= j) ? (h <= i || h > j) : (h <= i && h > j)) {
      k->tab[i] = k->tab[j];
      i = j;
    }
  }
  k->tgen[i] = 0;
}

static size_t kx_root(kx_t *k, size_t s) {
  size_t r = s;
  while (k->alias[r] != r) {
    r = k->alias[r];
  }
  while (k->alias[s] != r) {
    size_t t = k->alias[s];
    k->alias[s] = r;
    s = t;
  }
  return r;
}

static void kx_push(kx_t *k, size_t x) {
  if (!k->inq[x] && k->deg[x] > 0 && k->deg[x] <= 2) {
    k->inq[x] = 1;
    k->stack[k->sp++] = x;
  }
}

static void kx_drop(kx_t *k, size_t s) {
  size_t a = k->vmap[k->sa[s]];
  size_t b = k->vmap[k->sb[s]];
  kx_erase(k, s);
  k->live[s] = 0;
  k->deg[a]--;
  k->deg[b]--;
  kx_push(k, a);
  kx_push(k, b);
}

/* s joins the hash, unless a parallel edge is there: the longer goes */
static void kx_place(kx_t *k, size_t s) {
  size_t t = kx_find(k, k->sa[s], k->sb[s]);
  if (t == PL_NONE) {
    kx_insert(k, s);
  } else if (k->len[s] < k->len[t]) {
    kx_drop(k, t);
    kx_insert(k, s);
  } else {
    size_t a = k->vmap[k->sa[s]];
    size_t b = k->vmap[k->sb[s]];
    k->live[s] = 0;
    k->deg[a]--;
    k->deg[b]--;
    kx_push(k, a);
    kx_push(k, b);
  }
}

/* drop the dead edges from ids, then reduce */
static void kx_reduce(kx_t *k) {
  size_t i;
  size_t j = 0;
  size_t nn = 0;
  size_t s;
  for (i = 0; i < k->c; i++) {
    if (k->live[k->ids[i]]) {
      k->ids[j++] = k->ids[i];
    }
  }
  k->c = j;
  k->vgen++;
  k->gen++;
  for (i = 0; i < k->c; i++) {
    size_t e[2];
    size_t t;
    s = k->ids[i];
    e[0] = k->sa[s];
    e[1] = k->sb[s];
    for (t = 0; t < 2; t++) {
      if (k->stamp[e[t]] != k->vgen) {
        k->stamp[e[t]] = k->vgen;
        k->vmap[e[t]] = nn;
        k->vert[nn] = e[t];
        k->deg[nn] = 0;
        k->inq[nn] = 0;
        nn++;
      }
    }
    k->deg[k->vmap[e[0]]]++;
    k->deg[k->vmap[e[1]]]++;
    k->alias[s] = s;
  }
  k->off[0] = 0;
  for (i = 0; i < nn; i++) {
    k->off[i + 1] = k->off[i] + k->deg[i];
  }
  for (i = 0; i < nn; i++) {
    k->stack[i] = k->off[i]; /* fill cursors */
  }
  for (i = 0; i < k->c; i++) {
    s = k->ids[i];
    k->adj[k->stack[k->vmap[k->sa[s]]]++] = s;
    k->adj[k->stack[k->vmap[k->sb[s]]]++] = s;
  }
  k->sp = 0;
  for (i = 0; i < k->c; i++) {
    s = k->ids[i];
    if (k->live[s]) {
      kx_place(k, s);
    }
  }
  for (i = 0; i < nn; i++) {
    kx_push(k, i);
  }
  while (k->sp > 0) {
    size_t v = k->stack[--k->sp];
    size_t got[2];
    size_t ng = 0;
    size_t x;
    k->inq[v] = 0;
    if (k->deg[v] == 0 || k->deg[v] > 2) {
      continue;
    }
    for (x = k->off[v]; x < k->off[v + 1] && ng < k->deg[v]; x++) {
      s = kx_root(k, k->adj[x]);
      if (k->live[s] && (k->vmap[k->sa[s]] == v || k->vmap[k->sb[s]] == v) &&
          (ng == 0 || got[0] != s)) {
        got[ng++] = s;
      }
    }
    if (ng != k->deg[v]) {
      continue; /* cannot happen in a consistent state */
    }
    if (ng == 1) {
      kx_drop(k, got[0]);
    } else {
      size_t s1 = got[0];
      size_t s2 = got[1];
      size_t ov = k->vert[v];
      size_t a = k->sa[s1] == ov ? k->sb[s1] : k->sa[s1];
      size_t b = k->sa[s2] == ov ? k->sb[s2] : k->sa[s2];
      kx_erase(k, s1);
      kx_erase(k, s2);
      k->next[k->tail[s1]] = k->head[s2];
      k->tail[s1] = k->tail[s2];
      k->len[s1] += k->len[s2];
      k->need[s1] = (unsigned char)(k->need[s1] | k->need[s2]);
      k->live[s2] = 0;
      k->alias[s2] = s1;
      k->sa[s1] = a;
      k->sb[s1] = b;
      k->deg[v] = 0;
      if (a == b) { /* a cycle hanging at a */
        k->live[s1] = 0;
        k->deg[k->vmap[a]] -= 2;
        kx_push(k, k->vmap[a]);
      } else {
        kx_place(k, s1);
      }
    }
  }
  j = 0;
  for (i = 0; i < k->c; i++) {
    if (k->live[k->ids[i]]) {
      k->ids[j++] = k->ids[i];
    }
  }
  k->c = j;
}

/* the live reduced edges outside the deletion set: 1 planar, 0 not */
static int kx_test(kx_t *k) {
  size_t i;
  size_t nn = 0;
  size_t mm = 0;
  k->vgen++;
  for (i = 0; i < k->c; i++) {
    size_t s = k->ids[i];
    size_t e[2];
    size_t t;
    if (k->del[s]) {
      continue;
    }
    e[0] = k->sa[s];
    e[1] = k->sb[s];
    for (t = 0; t < 2; t++) {
      if (k->stamp[e[t]] != k->vgen) {
        k->stamp[e[t]] = k->vgen;
        k->vmap[e[t]] = nn++;
      }
    }
    k->p.eu[mm] = k->vmap[e[0]];
    k->p.ev[mm] = k->vmap[e[1]];
    mm++;
  }
  k->p.n = nn;
  k->p.m = mm;
  return pl_run(&k->p);
}

static unsigned long kx_rand(kx_t *k) {
  unsigned long x = k->rng;
  x ^= (x << 13) & 0xffffffffUL;
  x ^= x >> 17;
  x ^= (x << 5) & 0xffffffffUL;
  k->rng = x & 0xffffffffUL;
  return k->rng;
}

static int kx_shrink(kx_t *k) {
  unsigned j = 1; /* delete each edge with probability 2^-j */
  size_t i;
  int rc;
  kx_reduce(k);
  while (j < 31 && ((size_t)1 << j) <= k->c) {
    unsigned long mask = (1UL << j) - 1;
    size_t chosen = 0;
    for (i = 0; i < k->c; i++) {
      if ((kx_rand(k) & mask) == 0 && !k->need[k->ids[i]]) {
        k->del[k->ids[i]] = 1;
        chosen++;
      }
    }
    if (chosen == 0) {
      j++;
      continue;
    }
    rc = kx_test(k);
    if (rc < 0) {
      return rc;
    }
    for (i = 0; i < k->c; i++) {
      size_t s = k->ids[i];
      if (k->del[s]) {
        k->del[s] = 0;
        if (rc == 0) {
          k->live[s] = 0;
        }
      }
    }
    if (rc == 0) {
      kx_reduce(k);
      j = j > 1 ? j - 1 : 1;
    } else {
      j++;
    }
  }
  /* single deletions until every edge is needed */
  for (;;) {
    bool removed = false;
    for (i = 0; i < k->c; i++) {
      size_t s = k->ids[i];
      if (k->need[s]) {
        continue;
      }
      k->del[s] = 1;
      rc = kx_test(k);
      k->del[s] = 0;
      if (rc < 0) {
        return rc;
      }
      if (rc == 0) {
        k->live[s] = 0;
        kx_reduce(k);
        removed = true;
        break;
      }
      k->need[s] = 1;
    }
    if (!removed) {
      return 1;
    }
  }
}

static void kx_free(kx_t *k) {
  pl_free(&k->p);
  ds_context_free(k->ctx, k->block);
  ds_context_free(k->ctx, k->flags);
  k->block = NULL;
  k->flags = NULL;
}

static ds_status_t kx_init(kx_t *k, size_t n, const size_t *eu,
                           const size_t *ev, size_t m, ds_context_t *ctx) {
  size_t tsize = 16;
  size_t total;
  size_t *b;
  size_t i;
  ds_status_t st;
  memset(k, 0, sizeof(*k));
  k->ctx = ctx;
  k->n = n;
  k->m = m;
  k->eu = eu;
  k->ev = ev;
  while (tsize < 2 * m) {
    tsize *= 2;
  }
  total = 10 * m + 2 * tsize + 6 * n + 1;
  st = pl_init(&k->p, n, m, ctx);
  if (st != DS_OK) {
    return st;
  }
  k->block = (size_t *)pl_alloc(ctx, total, sizeof(size_t));
  k->flags = (unsigned char *)pl_alloc(ctx, 3 * m + n, 1);
  if (k->block == NULL || k->flags == NULL) {
    kx_free(k);
    return DS_ERR_ALLOC;
  }
  b = k->block;
  k->sa = b;
  b += m;
  k->sb = b;
  b += m;
  k->head = b;
  b += m;
  k->tail = b;
  b += m;
  k->next = b;
  b += m;
  k->len = b;
  b += m;
  k->alias = b;
  b += m;
  k->ids = b;
  b += m;
  k->adj = b;
  b += 2 * m;
  k->tab = b;
  b += tsize;
  k->tgen = b;
  b += tsize;
  k->deg = b;
  b += n;
  k->off = b;
  b += n + 1;
  k->stack = b;
  b += n;
  k->vert = b;
  b += n;
  k->vmap = b;
  b += n;
  k->stamp = b;
  k->tmask = tsize - 1;
  k->live = k->flags;
  k->del = k->live + m;
  k->need = k->del + m;
  k->inq = k->need + m;
  memset(k->flags, 0, 3 * m + n);
  for (i = 0; i < tsize; i++) {
    k->tgen[i] = 0;
  }
  for (i = 0; i < n; i++) {
    k->stamp[i] = 0;
  }
  for (i = 0; i < m; i++) {
    k->sa[i] = eu[i];
    k->sb[i] = ev[i];
    k->head[i] = i;
    k->tail[i] = i;
    k->next[i] = PL_NONE;
    k->len[i] = 1;
    k->ids[i] = i;
    k->live[i] = 1;
  }
  k->c = m;
  k->gen = 0;
  k->vgen = 0;
  k->rng = 2463534242UL;
  return DS_OK;
}

ds_status_t ds_planarity_kuratowski(const ds_gview_t *g, size_t *out_u,
                                    size_t *out_v, size_t capacity,
                                    size_t *out_count,
                                    ds_kuratowski_kind_t *out_kind,
                                    ds_context_t *context) {
  kx_t k;
  size_t *eu;
  size_t *ev;
  size_t m;
  size_t n;
  size_t i;
  size_t total = 0;
  int rc;
  ds_status_t st;
  if (g == NULL || out_count == NULL ||
      (capacity > 0 && (out_u == NULL || out_v == NULL))) {
    return DS_ERR_NULL;
  }
  *out_count = 0;
  if (out_kind != NULL) {
    *out_kind = DS_KURATOWSKI_NONE;
  }
  n = g->vertex_count;
  if (n > ((size_t)-1) / 32) {
    return DS_ERR_OVERFLOW;
  }
  st = pl_collect(g, 0, context, &eu, &ev, &m);
  if (st != DS_OK) {
    return st;
  }
  if (m > ((size_t)-1) / 32) {
    ds_context_free(context, eu);
    ds_context_free(context, ev);
    return DS_ERR_OVERFLOW;
  }
  st = kx_init(&k, n, eu, ev, m, context);
  if (st != DS_OK) {
    ds_context_free(context, eu);
    ds_context_free(context, ev);
    return st;
  }
  rc = kx_test(&k);
  if (rc == 1) {
    st = DS_NOT_FOUND;
  } else if (rc == 0) {
    rc = kx_shrink(&k);
  }
  if (rc < 0) {
    st = DS_ERR_INTERNAL;
  }
  if (st == DS_OK) {
    for (i = 0; i < k.c; i++) {
      total += k.len[k.ids[i]];
    }
    *out_count = total;
    if (total > capacity) {
      st = DS_ERR_RANGE;
    } else {
      size_t at = 0;
      for (i = 0; i < k.c; i++) {
        size_t e;
        for (e = k.head[k.ids[i]]; e != PL_NONE; e = k.next[e]) {
          out_u[at] = eu[e];
          out_v[at] = ev[e];
          at++;
        }
      }
      if (out_kind != NULL) {
        st = ds_kuratowski_classify(n, out_u, out_v, total, out_kind, context);
      }
    }
  }
  kx_free(&k);
  ds_context_free(context, eu);
  ds_context_free(context, ev);
  return st;
}

/* ---- maximal planar subgraphs --------------------------------------------
 */

/* whether the kept edges plus cand[start .. end) form a planar graph */
static int pl_prefix(pl_t *p, size_t vertices, const size_t *u, const size_t *v,
                     const size_t *kept, size_t nk, const size_t *cand,
                     size_t start, size_t end) {
  size_t k = 0;
  size_t j;
  if (vertices >= 3 && nk + (end - start) > 3 * vertices - 6) {
    return 0;
  }
  for (j = 0; j < nk; j++) {
    p->eu[k] = u[kept[j]];
    p->ev[k] = v[kept[j]];
    k++;
  }
  for (j = start; j < end; j++) {
    p->eu[k] = u[cand[j]];
    p->ev[k] = v[cand[j]];
    k++;
  }
  p->n = vertices;
  p->m = k;
  return pl_run(p);
}

ds_status_t ds_planarity_maximal_subgraph(size_t vertices, const size_t *u,
                                          const size_t *v, size_t count,
                                          bool *keep, size_t *out_kept,
                                          ds_context_t *context) {
  pl_t p;
  size_t *cand;
  size_t *kept;
  size_t *stamp;
  size_t c = 0;
  size_t nk = 0;
  size_t start = 0;
  size_t i;
  ds_status_t st;
  if (count > 0 && (u == NULL || v == NULL || keep == NULL)) {
    return DS_ERR_NULL;
  }
  if (vertices > ((size_t)-1) / 32 || count > ((size_t)-1) / 32) {
    return DS_ERR_OVERFLOW;
  }
  for (i = 0; i < count; i++) {
    if (u[i] >= vertices || v[i] >= vertices) {
      return DS_ERR_INVALID;
    }
    keep[i] = false;
  }
  if (out_kept != NULL) {
    *out_kept = 0;
  }
  cand = (size_t *)pl_alloc(context, count, sizeof(size_t));
  kept = (size_t *)pl_alloc(context, count, sizeof(size_t));
  stamp = (size_t *)pl_alloc(context, count, sizeof(size_t));
  st = pl_init(&p, vertices, count, context);
  if (st != DS_OK || cand == NULL || kept == NULL || stamp == NULL) {
    if (st == DS_OK) {
      pl_free(&p);
    }
    ds_context_free(context, cand);
    ds_context_free(context, kept);
    ds_context_free(context, stamp);
    return st != DS_OK ? st : DS_ERR_ALLOC;
  }
  /* candidates: first occurrences of non-loop edges, in input order. The
   * repeats are found by bucketing on the smaller endpoint (p's arrays
   * serve as scratch). */
  {
    size_t *off = p.aoff; /* vertices + 1 */
    size_t *where = p.aedge;
    size_t *last = p.height;
    size_t x;
    for (x = 0; x <= vertices; x++) {
      off[x] = 0;
    }
    for (i = 0; i < count; i++) {
      size_t a = u[i] < v[i] ? u[i] : v[i];
      off[a + 1]++;
    }
    for (x = 0; x < vertices; x++) {
      off[x + 1] += off[x];
      p.pos[x] = off[x];
      last[x] = PL_NONE;
    }
    for (i = 0; i < count; i++) {
      size_t a = u[i] < v[i] ? u[i] : v[i];
      where[p.pos[a]++] = i;
    }
    for (i = 0; i < count; i++) {
      stamp[i] = 0; /* 1: a candidate */
    }
    for (x = 0; x < vertices; x++) {
      size_t k;
      for (k = off[x]; k < off[x + 1]; k++) {
        size_t j = where[k];
        size_t b = u[j] < v[j] ? v[j] : u[j];
        if (u[j] == v[j] || last[b] == x) {
          continue; /* loop, or a repeat (inputs are bucketed in order) */
        }
        last[b] = x;
        stamp[j] = 1;
      }
    }
    for (i = 0; i < count; i++) {
      if (stamp[i]) {
        cand[c++] = i;
      }
    }
  }
  /* greedy in input order: the longest planar run after the kept edges is
   * found by galloping, then bisection, and the edge after it is rejected,
   * so a run of d accepted edges costs O(log d) tests */
  while (start < c && st == DS_OK) {
    size_t lo = start; /* K + cand[start .. lo) is planar */
    size_t hi = c + 1; /* K + cand[start .. hi) is not (c + 1: unknown) */
    size_t step = 1;
    int rc;
    while (hi - lo > 1) {
      size_t probe;
      if (hi == c + 1) { /* still galloping */
        probe = lo + step <= c ? lo + step : c;
        step *= 2;
      } else {
        probe = lo + (hi - lo) / 2;
      }
      rc = pl_prefix(&p, vertices, u, v, kept, nk, cand, start, probe);
      if (rc < 0) {
        st = DS_ERR_INTERNAL;
        break;
      }
      if (rc == 1) {
        lo = probe;
        if (lo == c) {
          break;
        }
      } else {
        hi = probe;
      }
    }
    if (st != DS_OK) {
      break;
    }
    for (i = start; i < lo; i++) {
      kept[nk++] = cand[i];
      keep[cand[i]] = true;
    }
    start = lo + 1; /* cand[lo] (if any) is rejected */
  }
  if (out_kept != NULL) {
    *out_kept = nk;
  }
  pl_free(&p);
  ds_context_free(context, cand);
  ds_context_free(context, kept);
  ds_context_free(context, stamp);
  return st;
}
