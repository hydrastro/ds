#include "sort_heap.h"
#include "kway_merge.h"
#include "sort.h"
#include <string.h>

#define AT(base, i, es) ((unsigned char *)(base) + (size_t)(i) * (es))
#define CAT(base, i, es) ((const unsigned char *)(base) + (size_t)(i) * (es))
#define SH_NONE (~(size_t)0)
#define SH_LEO_MAX 100

typedef struct sh_ctx {
  unsigned char *a;
  size_t es;
  ds_compare_func_t compare;
  void *user;
} sh_ctx_t;

static int sh_cmp(const sh_ctx_t *c, size_t i, size_t j) {
  return c->compare(CAT(c->a, i, c->es), CAT(c->a, j, c->es), c->user);
}

/* Total order (value, index): true when i comes before j. */
static int sh_less(const sh_ctx_t *c, size_t i, size_t j) {
  int r = sh_cmp(c, i, j);
  return r < 0 || (r == 0 && i < j);
}

static void sh_swap(const sh_ctx_t *c, size_t i, size_t j) {
  ds_elem_swap(AT(c->a, i, c->es), AT(c->a, j, c->es), c->es);
}

/* ---- smoothsort ----------------------------------------------------- */

typedef struct sh_smooth {
  sh_ctx_t c;
  size_t leo[SH_LEO_MAX];
  size_t orders[SH_LEO_MAX * 2];
  size_t roots[SH_LEO_MAX * 2];
  size_t heaps;
} sh_smooth_t;

/* Leonardo heap of order k rooted at r: right child root r - 1 (order
 * k - 2), left child root r - 1 - L(k - 2) (order k - 1). */
static void sh_sift(const sh_smooth_t *s, size_t r, size_t k) {
  size_t rr;
  size_t ll;
  size_t ch;
  size_t ck;
  while (k >= 2) {
    rr = r - 1;
    ll = r - 1 - s->leo[k - 2];
    if (sh_cmp(&s->c, ll, rr) > 0) {
      ch = ll;
      ck = k - 1;
    } else {
      ch = rr;
      ck = k - 2;
    }
    if (sh_cmp(&s->c, r, ch) >= 0) {
      return;
    }
    sh_swap(&s->c, r, ch);
    r = ch;
    k = ck;
  }
}

/* Restore the ascending order of roots up to heap h, then sift. */
static void sh_rectify(sh_smooth_t *s, size_t h) {
  size_t pos = s->roots[h];
  size_t ord = s->orders[h];
  size_t pr;
  size_t idx = h;
  while (idx > 0) {
    pr = s->roots[idx - 1];
    if (sh_cmp(&s->c, pr, pos) <= 0) {
      break;
    }
    if (ord >= 2 && (sh_cmp(&s->c, pr, pos - 1) <= 0 ||
                     sh_cmp(&s->c, pr, pos - 1 - s->leo[ord - 2]) <= 0)) {
      break;
    }
    sh_swap(&s->c, pr, pos);
    pos = pr;
    idx--;
    ord = s->orders[idx];
  }
  sh_sift(s, pos, ord);
}

void ds_smoothsort(void *base, size_t count, size_t elem_size,
                   ds_compare_func_t compare, void *user) {
  sh_smooth_t s;
  size_t i;
  size_t k;
  size_t h;
  if (base == NULL || compare == NULL || elem_size == 0 || count < 2) {
    return;
  }
  s.c.a = (unsigned char *)base;
  s.c.es = elem_size;
  s.c.compare = compare;
  s.c.user = user;
  s.leo[0] = 1;
  s.leo[1] = 1;
  for (k = 2; k < SH_LEO_MAX; k++) {
    if (s.leo[k - 1] > ((size_t)-1) - s.leo[k - 2] - 1) {
      s.leo[k] = (size_t)-1;
    } else {
      s.leo[k] = s.leo[k - 1] + s.leo[k - 2] + 1;
    }
  }
  s.heaps = 0;
  for (i = 0; i < count; i++) {
    h = s.heaps;
    if (h >= 2 && s.orders[h - 2] == s.orders[h - 1] + 1) {
      s.heaps--;
      s.orders[s.heaps - 1]++;
      s.roots[s.heaps - 1] = i;
    } else if (h >= 1 && s.orders[h - 1] == 1) {
      s.orders[h] = 0;
      s.roots[h] = i;
      s.heaps++;
    } else {
      s.orders[h] = 1;
      s.roots[h] = i;
      s.heaps++;
    }
    sh_rectify(&s, s.heaps - 1);
  }
  for (i = count - 1; i > 0; i--) {
    k = s.orders[s.heaps - 1];
    s.heaps--;
    if (k >= 2) {
      s.orders[s.heaps] = k - 1;
      s.roots[s.heaps] = i - 1 - s.leo[k - 2];
      s.heaps++;
      sh_rectify(&s, s.heaps - 1);
      s.orders[s.heaps] = k - 2;
      s.roots[s.heaps] = i - 1;
      s.heaps++;
      sh_rectify(&s, s.heaps - 1);
    }
  }
}

/* ---- adaptive heap sort --------------------------------------------- */

static void sh_heap_sift_down(const sh_ctx_t *c, size_t *heap, size_t n,
                              size_t i) {
  size_t ch;
  size_t t;
  for (;;) {
    ch = 2 * i + 1;
    if (ch >= n) {
      return;
    }
    if (ch + 1 < n && sh_less(c, heap[ch + 1], heap[ch])) {
      ch++;
    }
    if (!sh_less(c, heap[ch], heap[i])) {
      return;
    }
    t = heap[i];
    heap[i] = heap[ch];
    heap[ch] = t;
    i = ch;
  }
}

static void sh_heap_push(const sh_ctx_t *c, size_t *heap, size_t *n, size_t v) {
  size_t i = (*n)++;
  size_t p;
  heap[i] = v;
  while (i > 0) {
    p = (i - 1) / 2;
    if (!sh_less(c, heap[i], heap[p])) {
      return;
    }
    heap[i] = heap[p];
    heap[p] = v;
    i = p;
  }
}

static void *sh_alloc(ds_context_t *ctx, size_t count, size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > ((size_t)-1) / size) {
    return NULL;
  }
  return ds_context_alloc(ctx, count * size);
}

ds_status_t ds_adaptive_heap_sort(void *base, size_t count, size_t elem_size,
                                  ds_compare_func_t compare, void *user,
                                  ds_context_t *context) {
  sh_ctx_t c;
  size_t *left;
  size_t *right;
  size_t *stack;
  unsigned char *out;
  size_t top = 0;
  size_t i;
  size_t last;
  size_t hn = 0;
  size_t o = 0;
  size_t x;
  if (base == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  left = (size_t *)sh_alloc(context, 3 * count, sizeof(size_t));
  out = (unsigned char *)sh_alloc(context, count, elem_size);
  if (left == NULL || out == NULL) {
    ds_context_free(context, left);
    ds_context_free(context, out);
    return DS_ERR_ALLOC;
  }
  right = left + count;
  stack = right + count; /* Cartesian stack, then the heap */
  c.a = (unsigned char *)base;
  c.es = elem_size;
  c.compare = compare;
  c.user = user;
  /* Cartesian tree, minimum at the root, the leftmost minimum first */
  for (i = 0; i < count; i++) {
    left[i] = SH_NONE;
    right[i] = SH_NONE;
    last = SH_NONE;
    while (top > 0 && sh_cmp(&c, stack[top - 1], i) > 0) {
      last = stack[--top];
    }
    left[i] = last;
    if (top > 0) {
      right[stack[top - 1]] = i;
    }
    stack[top++] = i;
  }
  x = stack[0];
  sh_heap_push(&c, stack, &hn, x);
  while (hn > 0) {
    x = stack[0];
    stack[0] = stack[--hn];
    sh_heap_sift_down(&c, stack, hn, 0);
    memcpy(AT(out, o, elem_size), CAT(base, x, elem_size), elem_size);
    o++;
    if (left[x] != SH_NONE) {
      sh_heap_push(&c, stack, &hn, left[x]);
    }
    if (right[x] != SH_NONE) {
      sh_heap_push(&c, stack, &hn, right[x]);
    }
  }
  memcpy(base, out, count * elem_size);
  ds_context_free(context, left);
  ds_context_free(context, out);
  return DS_OK;
}

/* ---- dominance tree sort -------------------------------------------- */

static size_t sh_link(const sh_ctx_t *c, size_t *child, size_t *sibling,
                      size_t x, size_t y) {
  size_t w = sh_less(c, x, y) ? x : y;
  size_t l = w == x ? y : x;
  sibling[l] = child[w];
  child[w] = l;
  return w;
}

ds_status_t ds_dominance_tree_sort(void *base, size_t count, size_t elem_size,
                                   ds_compare_func_t compare, void *user,
                                   ds_context_t *context) {
  sh_ctx_t c;
  size_t *child;
  size_t *sibling;
  size_t *list;
  unsigned char *out;
  size_t n;
  size_t m;
  size_t i;
  size_t root;
  size_t o;
  size_t x;
  if (base == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  child = (size_t *)sh_alloc(context, 3 * count, sizeof(size_t));
  out = (unsigned char *)sh_alloc(context, count, elem_size);
  if (child == NULL || out == NULL) {
    ds_context_free(context, child);
    ds_context_free(context, out);
    return DS_ERR_ALLOC;
  }
  sibling = child + count;
  list = sibling + count;
  c.a = (unsigned char *)base;
  c.es = elem_size;
  c.compare = compare;
  c.user = user;
  for (i = 0; i < count; i++) {
    child[i] = SH_NONE;
    sibling[i] = SH_NONE;
    list[i] = i;
  }
  /* rounds of pairwise matches until one winner dominates everything */
  n = count;
  while (n > 1) {
    m = 0;
    for (i = 0; i + 1 < n; i += 2) {
      list[m++] = sh_link(&c, child, sibling, list[i], list[i + 1]);
    }
    if (n % 2 == 1) {
      list[m++] = list[n - 1];
    }
    n = m;
  }
  root = list[0];
  for (o = 0; o < count; o++) {
    memcpy(AT(out, o, elem_size), CAT(base, root, elem_size), elem_size);
    /* the extracted root's children play again: two-pass pairing */
    n = 0;
    for (x = child[root]; x != SH_NONE;) {
      size_t nx = sibling[x];
      sibling[x] = SH_NONE;
      list[n++] = x;
      x = nx;
    }
    if (n == 0) {
      continue;
    }
    m = 0;
    for (i = 0; i + 1 < n; i += 2) {
      list[m++] = sh_link(&c, child, sibling, list[i], list[i + 1]);
    }
    if (n % 2 == 1) {
      list[m++] = list[n - 1];
    }
    root = list[m - 1];
    for (i = m - 1; i-- > 0;) {
      root = sh_link(&c, child, sibling, list[i], root);
    }
  }
  memcpy(base, out, count * elem_size);
  ds_context_free(context, child);
  ds_context_free(context, out);
  return DS_OK;
}

/* ---- tournament sort ------------------------------------------------ */

ds_status_t ds_tournament_sort(void *base, size_t count, size_t elem_size,
                               ds_compare_func_t compare, void *user,
                               ds_context_t *context) {
  ds_tournament_config_t tc;
  ds_tournament_t t;
  size_t i;
  size_t w;
  ds_status_t st;
  if (base == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  tc.players = count;
  tc.elem_size = elem_size;
  tc.compare = compare;
  tc.user = user;
  tc.kind = DS_TOURNAMENT_LOSER;
  st = ds_tournament_init(&t, &tc, context);
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < count; i++) {
    memcpy(ds_tournament_slot(&t, i), CAT(base, i, elem_size), elem_size);
    ds_tournament_set_alive(&t, i, true);
  }
  ds_tournament_build(&t);
  for (i = 0; i < count; i++) {
    w = ds_tournament_winner(&t);
    memcpy(AT(base, i, elem_size), ds_tournament_slot(&t, w), elem_size);
    ds_tournament_set_alive(&t, w, false);
    ds_tournament_replay(&t, w);
  }
  ds_tournament_destroy(&t);
  return DS_OK;
}

/* ---- tree sort ------------------------------------------------------ */

ds_status_t ds_tree_sort(void *base, size_t count, size_t elem_size,
                         ds_compare_func_t compare, void *user, bool balanced,
                         ds_context_t *context) {
  sh_ctx_t c;
  size_t *left;
  size_t *right;
  size_t *path;
  unsigned long *prio = NULL;
  unsigned char *out;
  unsigned long seed = 2463534242UL;
  size_t root = SH_NONE;
  size_t depth;
  size_t i;
  size_t cur;
  size_t p;
  size_t g;
  size_t o;
  if (base == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  left = (size_t *)sh_alloc(context, 3 * count, sizeof(size_t));
  out = (unsigned char *)sh_alloc(context, count, elem_size);
  if (balanced) {
    prio = (unsigned long *)sh_alloc(context, count, sizeof(unsigned long));
  }
  if (left == NULL || out == NULL || (balanced && prio == NULL)) {
    ds_context_free(context, left);
    ds_context_free(context, out);
    ds_context_free(context, prio);
    return DS_ERR_ALLOC;
  }
  right = left + count;
  path = right + count;
  c.a = (unsigned char *)base;
  c.es = elem_size;
  c.compare = compare;
  c.user = user;
  for (i = 0; i < count; i++) {
    left[i] = SH_NONE;
    right[i] = SH_NONE;
    if (balanced) {
      seed ^= (seed << 13) & 0xFFFFFFFFUL;
      seed ^= seed >> 17;
      seed ^= (seed << 5) & 0xFFFFFFFFUL;
      prio[i] = seed & 0xFFFFFFFFUL;
    }
    if (root == SH_NONE) {
      root = i;
      continue;
    }
    /* equal keys go right, so the in-order walk is stable */
    depth = 0;
    cur = root;
    for (;;) {
      path[depth++] = cur;
      if (sh_cmp(&c, i, cur) < 0) {
        if (left[cur] == SH_NONE) {
          left[cur] = i;
          break;
        }
        cur = left[cur];
      } else {
        if (right[cur] == SH_NONE) {
          right[cur] = i;
          break;
        }
        cur = right[cur];
      }
    }
    if (!balanced) {
      continue;
    }
    /* treap: rotate i up while its priority beats its parent's */
    while (depth > 0 && prio[i] > prio[path[depth - 1]]) {
      p = path[--depth];
      if (left[p] == i) {
        left[p] = right[i];
        right[i] = p;
      } else {
        right[p] = left[i];
        left[i] = p;
      }
      if (depth == 0) {
        root = i;
      } else {
        g = path[depth - 1];
        if (left[g] == p) {
          left[g] = i;
        } else {
          right[g] = i;
        }
      }
    }
  }
  /* in-order walk */
  o = 0;
  depth = 0;
  cur = root;
  while (cur != SH_NONE || depth > 0) {
    while (cur != SH_NONE) {
      path[depth++] = cur;
      cur = left[cur];
    }
    cur = path[--depth];
    memcpy(AT(out, o, elem_size), CAT(base, cur, elem_size), elem_size);
    o++;
    cur = right[cur];
  }
  memcpy(base, out, count * elem_size);
  ds_context_free(context, left);
  ds_context_free(context, out);
  ds_context_free(context, prio);
  return DS_OK;
}

/* ---- JSort ---------------------------------------------------------- */

void ds_jsort(void *base, size_t count, size_t elem_size,
              ds_compare_func_t compare, void *user) {
  sh_ctx_t c;
  size_t i;
  size_t r;
  size_t ch;
  if (base == NULL || compare == NULL || elem_size == 0 || count < 2) {
    return;
  }
  c.a = (unsigned char *)base;
  c.es = elem_size;
  c.compare = compare;
  c.user = user;
  /* min-heap from the front: small items rise towards index 0 */
  for (i = count / 2; i-- > 0;) {
    r = i;
    for (;;) {
      ch = 2 * r + 1;
      if (ch >= count) {
        break;
      }
      if (ch + 1 < count && sh_cmp(&c, ch + 1, ch) < 0) {
        ch++;
      }
      if (sh_cmp(&c, ch, r) >= 0) {
        break;
      }
      sh_swap(&c, r, ch);
      r = ch;
    }
  }
  /* max-heap mirrored from the back: large items sink towards the end */
  for (i = count / 2; i-- > 0;) {
    r = i;
    for (;;) {
      ch = 2 * r + 1;
      if (ch >= count) {
        break;
      }
      if (ch + 1 < count &&
          sh_cmp(&c, count - 1 - (ch + 1), count - 1 - ch) > 0) {
        ch++;
      }
      if (sh_cmp(&c, count - 1 - ch, count - 1 - r) <= 0) {
        break;
      }
      sh_swap(&c, count - 1 - r, count - 1 - ch);
      r = ch;
    }
  }
  ds_insertion_sort(base, count, elem_size, compare, user);
}
