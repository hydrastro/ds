#include "sort_misc.h"
#include "kway_merge.h"
#include "select.h"
#include "sort.h"
#include <stdlib.h>
#include <string.h>

#define AT(base, i, es) ((unsigned char *)(base) + (size_t)(i) * (es))
#define CAT(base, i, es) ((const unsigned char *)(base) + (size_t)(i) * (es))
#define QS_STACK 256
#define QS_DEFAULT_CUTOFF 16
#define DPQ_INSERTION 27
#define ADAPTIVE_STACK 128

static size_t sm_floor_log2(size_t x) {
  size_t r = 0;
  while (x > 1) {
    x >>= 1;
    r++;
  }
  return r;
}

static unsigned long sm_xorshift(void *state) {
  unsigned long *x = (unsigned long *)state;
  *x ^= (*x << 13) & 0xFFFFFFFFUL;
  *x ^= *x >> 17;
  *x ^= (*x << 5) & 0xFFFFFFFFUL;
  return *x & 0xFFFFFFFFUL;
}

/* Uniform index in [0, bound) by rejection from 32-bit draws. */
static size_t sm_bounded(ds_random_func_t random, void *state, size_t bound) {
  unsigned long x;
  unsigned long limit;
  size_t hi;
  if (bound <= 1) {
    return 0;
  }
  if (bound - 1 <= 0xFFFFFFFFUL) {
    limit = 0xFFFFFFFFUL -
            (0xFFFFFFFFUL % (unsigned long)bound + 1UL) % (unsigned long)bound;
    do {
      x = random(state) & 0xFFFFFFFFUL;
    } while (x > limit);
    return (size_t)(x % (unsigned long)bound);
  }
  hi = (size_t)(random(state) & 0xFFFFFFFFUL);
  hi = (hi << 16) << 16;
  hi |= (size_t)(random(state) & 0xFFFFFFFFUL);
  return hi % bound;
}

/* ---- quicksort ------------------------------------------------------ */

typedef struct qs_ctx {
  unsigned char *a;
  size_t es;
  ds_compare_func_t compare;
  void *user;
  size_t p; /* tracked pivot position */
} qs_ctx_t;

static int qs_cmp(const qs_ctx_t *c, size_t i, size_t j) {
  return c->compare(CAT(c->a, i, c->es), CAT(c->a, j, c->es), c->user);
}

static void qs_swap(qs_ctx_t *c, size_t i, size_t j) {
  if (i == j) {
    return;
  }
  ds_elem_swap(AT(c->a, i, c->es), AT(c->a, j, c->es), c->es);
  if (c->p == i) {
    c->p = j;
  } else if (c->p == j) {
    c->p = i;
  }
}

static size_t qs_median3(const qs_ctx_t *c, size_t x, size_t y, size_t z) {
  if (qs_cmp(c, x, y) < 0) {
    if (qs_cmp(c, y, z) < 0) {
      return y;
    }
    return qs_cmp(c, x, z) < 0 ? z : x;
  }
  if (qs_cmp(c, x, z) < 0) {
    return x;
  }
  return qs_cmp(c, y, z) < 0 ? z : y;
}

static size_t qs_pivot(qs_ctx_t *c, size_t lo, size_t hi,
                       const ds_quicksort_config_t *cfg, ds_random_func_t rnd,
                       void *rstate) {
  size_t len = hi - lo;
  size_t mid = lo + len / 2;
  size_t s;
  switch (cfg->pivot) {
  case DS_QS_PIVOT_FIRST:
    return lo;
  case DS_QS_PIVOT_LAST:
    return hi - 1;
  case DS_QS_PIVOT_MIDDLE:
    return mid;
  case DS_QS_PIVOT_NINTHER:
    if (len >= 40) {
      s = len / 8;
      return qs_median3(c, qs_median3(c, lo, lo + s, lo + 2 * s),
                        qs_median3(c, mid - s, mid, mid + s),
                        qs_median3(c, hi - 1 - 2 * s, hi - 1 - s, hi - 1));
    }
    return qs_median3(c, lo, mid, hi - 1);
  case DS_QS_PIVOT_RANDOM:
    return lo + sm_bounded(rnd, rstate, len);
  case DS_QS_PIVOT_MEDIAN:
    ds_select_median_of_medians(c->a + lo * c->es, len, c->es, (len - 1) / 2,
                                c->compare, c->user);
    return lo + (len - 1) / 2;
  case DS_QS_PIVOT_MEDIAN3:
  default:
    return qs_median3(c, lo, mid, hi - 1);
  }
}

/* Partition [lo, hi) around position p into [lo, *le) and [*rs, hi), both
 * strictly smaller than the input. */
static void qs_partition(qs_ctx_t *c, size_t lo, size_t hi, size_t p,
                         ds_quicksort_partition_t scheme, size_t *le,
                         size_t *rs) {
  size_t i;
  size_t j;
  size_t store;
  size_t lt;
  size_t gt;
  int cr;
  c->p = p;
  if (scheme == DS_QS_PARTITION_LOMUTO) {
    qs_swap(c, p, hi - 1);
    store = lo;
    for (i = lo; i < hi - 1; i++) {
      if (qs_cmp(c, i, hi - 1) < 0) {
        qs_swap(c, i, store);
        store++;
      }
    }
    qs_swap(c, store, hi - 1);
    *le = store;
    *rs = store + 1;
    return;
  }
  if (scheme == DS_QS_PARTITION_THREE_WAY) {
    qs_swap(c, lo, p);
    lt = lo;
    gt = hi - 1;
    i = lo + 1;
    while (i <= gt) {
      cr = qs_cmp(c, i, lt);
      if (cr < 0) {
        qs_swap(c, lt, i);
        lt++;
        i++;
      } else if (cr > 0) {
        qs_swap(c, i, gt);
        if (gt == 0) {
          break;
        }
        gt--;
      } else {
        i++;
      }
    }
    *le = lt;
    *rs = gt + 1;
    return;
  }
  /* Hoare, pivot first */
  qs_swap(c, lo, p);
  i = lo;
  j = hi - 1;
  for (;;) {
    while (qs_cmp(c, i, c->p) < 0) {
      i++;
    }
    while (qs_cmp(c, j, c->p) > 0) {
      j--;
    }
    if (i >= j) {
      break;
    }
    qs_swap(c, i, j);
    i++;
    j--;
  }
  *le = j + 1;
  *rs = j + 1;
}

void ds_quicksort_config_default(ds_quicksort_config_t *config) {
  if (config == NULL) {
    return;
  }
  config->pivot = DS_QS_PIVOT_MEDIAN3;
  config->partition = DS_QS_PARTITION_HOARE;
  config->cutoff = QS_DEFAULT_CUTOFF;
  config->small_sort = NULL;
  config->introspective = true;
  config->random = NULL;
  config->random_state = NULL;
}

void ds_quicksort(void *base, size_t count, size_t elem_size,
                  ds_compare_func_t compare, void *user,
                  const ds_quicksort_config_t *config) {
  ds_quicksort_config_t cfg;
  qs_ctx_t c;
  size_t stack_lo[QS_STACK];
  size_t stack_hi[QS_STACK];
  size_t stack_depth[QS_STACK];
  size_t top = 0;
  size_t lo;
  size_t hi;
  size_t depth;
  size_t limit;
  size_t len;
  size_t le;
  size_t rs;
  size_t p;
  ds_sort_func_t small;
  ds_random_func_t rnd;
  void *rstate;
  unsigned long own = 88172645UL;
  if (base == NULL || compare == NULL || elem_size == 0 || count < 2) {
    return;
  }
  if (config == NULL) {
    ds_quicksort_config_default(&cfg);
  } else {
    cfg = *config;
  }
  small = cfg.small_sort != NULL ? cfg.small_sort : ds_insertion_sort;
  rnd = cfg.random != NULL ? cfg.random : sm_xorshift;
  rstate = cfg.random != NULL ? cfg.random_state : &own;
  c.a = (unsigned char *)base;
  c.es = elem_size;
  c.compare = compare;
  c.user = user;
  limit = 2 * sm_floor_log2(count) + 2;
  stack_lo[0] = 0;
  stack_hi[0] = count;
  stack_depth[0] = 0;
  top = 1;
  while (top > 0) {
    top--;
    lo = stack_lo[top];
    hi = stack_hi[top];
    depth = stack_depth[top];
    for (;;) {
      len = hi - lo;
      if (len < 2) {
        break;
      }
      if (cfg.cutoff > 0 && len <= cfg.cutoff) {
        small(c.a + lo * elem_size, len, elem_size, compare, user);
        break;
      }
      if (cfg.introspective && depth > limit) {
        ds_heap_sort(c.a + lo * elem_size, len, elem_size, compare, user);
        break;
      }
      p = qs_pivot(&c, lo, hi, &cfg, rnd, rstate);
      qs_partition(&c, lo, hi, p, cfg.partition, &le, &rs);
      depth++;
      if (top == QS_STACK) {
        /* cannot happen with the smaller-first order; stay safe */
        ds_heap_sort(c.a + lo * elem_size, len, elem_size, compare, user);
        break;
      }
      if (le - lo < hi - rs) {
        stack_lo[top] = rs;
        stack_hi[top] = hi;
        stack_depth[top] = depth;
        top++;
        hi = le;
      } else {
        stack_lo[top] = lo;
        stack_hi[top] = le;
        stack_depth[top] = depth;
        top++;
        lo = rs;
      }
    }
  }
}

void ds_quicksort_plain(void *base, size_t count, size_t elem_size,
                        ds_compare_func_t compare, void *user) {
  ds_quicksort_config_t cfg;
  ds_quicksort_config_default(&cfg);
  cfg.pivot = DS_QS_PIVOT_FIRST;
  cfg.cutoff = 0;
  cfg.introspective = false;
  ds_quicksort(base, count, elem_size, compare, user, &cfg);
}

void ds_q_sort(void *base, size_t count, size_t elem_size,
               ds_compare_func_t compare, void *user) {
  ds_quicksort_config_t cfg;
  ds_quicksort_config_default(&cfg);
  cfg.pivot = DS_QS_PIVOT_MIDDLE;
  cfg.introspective = false;
  ds_quicksort(base, count, elem_size, compare, user, &cfg);
}

void ds_qm_sort(void *base, size_t count, size_t elem_size,
                ds_compare_func_t compare, void *user) {
  ds_quicksort_config_t cfg;
  ds_quicksort_config_default(&cfg);
  cfg.introspective = false;
  ds_quicksort(base, count, elem_size, compare, user, &cfg);
}

void ds_balanced_quicksort(void *base, size_t count, size_t elem_size,
                           ds_compare_func_t compare, void *user) {
  ds_quicksort_config_t cfg;
  ds_quicksort_config_default(&cfg);
  cfg.pivot = DS_QS_PIVOT_MEDIAN;
  cfg.introspective = false;
  ds_quicksort(base, count, elem_size, compare, user, &cfg);
}

void ds_dual_pivot_quicksort(void *base, size_t count, size_t elem_size,
                             ds_compare_func_t compare, void *user) {
  qs_ctx_t c;
  size_t stack_lo[QS_STACK];
  size_t stack_hi[QS_STACK];
  size_t stack_depth[QS_STACK];
  size_t top;
  size_t lo;
  size_t hi; /* inclusive */
  size_t depth;
  size_t limit;
  size_t len;
  size_t third;
  size_t l;
  size_t g;
  size_t k;
  size_t seg_lo[3];
  size_t seg_hi[3]; /* half-open */
  size_t nseg;
  size_t s;
  size_t smallest;
  if (base == NULL || compare == NULL || elem_size == 0 || count < 2) {
    return;
  }
  c.a = (unsigned char *)base;
  c.es = elem_size;
  c.compare = compare;
  c.user = user;
  c.p = (size_t)-1;
  limit = 2 * sm_floor_log2(count) + 2;
  stack_lo[0] = 0;
  stack_hi[0] = count;
  stack_depth[0] = 0;
  top = 1;
  while (top > 0) {
    top--;
    lo = stack_lo[top];
    len = stack_hi[top] - lo;
    depth = stack_depth[top];
    for (;;) {
      if (len < DPQ_INSERTION) {
        if (len > 1) {
          ds_insertion_sort(c.a + lo * elem_size, len, elem_size, compare,
                            user);
        }
        break;
      }
      if (depth > limit || top + 2 > QS_STACK) {
        ds_heap_sort(c.a + lo * elem_size, len, elem_size, compare, user);
        break;
      }
      hi = lo + len - 1;
      third = len / 3;
      qs_swap(&c, lo, lo + third);
      qs_swap(&c, hi, hi - third);
      if (qs_cmp(&c, lo, hi) > 0) {
        qs_swap(&c, lo, hi);
      }
      l = lo + 1;
      g = hi - 1;
      k = l;
      while (k <= g) {
        if (qs_cmp(&c, k, lo) < 0) {
          qs_swap(&c, k, l);
          l++;
        } else if (qs_cmp(&c, k, hi) >= 0) {
          while (qs_cmp(&c, g, hi) > 0 && k < g) {
            g--;
          }
          qs_swap(&c, k, g);
          g--;
          if (qs_cmp(&c, k, lo) < 0) {
            qs_swap(&c, k, l);
            l++;
          }
        }
        k++;
      }
      l--;
      g++;
      qs_swap(&c, lo, l);
      qs_swap(&c, hi, g);
      depth++;
      nseg = 0;
      seg_lo[nseg] = lo;
      seg_hi[nseg] = l;
      nseg++;
      if (qs_cmp(&c, l, g) < 0) {
        seg_lo[nseg] = l + 1;
        seg_hi[nseg] = g;
        nseg++;
      }
      seg_lo[nseg] = g + 1;
      seg_hi[nseg] = hi + 1;
      nseg++;
      smallest = 0;
      for (s = 1; s < nseg; s++) {
        if (seg_hi[s] - seg_lo[s] < seg_hi[smallest] - seg_lo[smallest]) {
          smallest = s;
        }
      }
      for (s = 0; s < nseg; s++) {
        if (s != smallest && seg_hi[s] - seg_lo[s] > 1) {
          stack_lo[top] = seg_lo[s];
          stack_hi[top] = seg_hi[s];
          stack_depth[top] = depth;
          top++;
        }
      }
      lo = seg_lo[smallest];
      len = seg_hi[smallest] - seg_lo[smallest];
    }
  }
}

void ds_dutch_national_flag(void *base, size_t count, size_t elem_size,
                            ds_classify_func_t classify, void *user,
                            size_t *out_first1, size_t *out_first2) {
  unsigned char *a = (unsigned char *)base;
  size_t lo = 0;
  size_t mid = 0;
  size_t hi = count; /* [hi, count) is class 2 */
  int k;
  if (base == NULL || classify == NULL) {
    count = 0;
    hi = 0;
  }
  while (mid < hi) {
    k = classify(AT(a, mid, elem_size), user);
    if (k == 0) {
      ds_elem_swap(AT(a, lo, elem_size), AT(a, mid, elem_size), elem_size);
      lo++;
      mid++;
    } else if (k == 1) {
      mid++;
    } else {
      hi--;
      ds_elem_swap(AT(a, mid, elem_size), AT(a, hi, elem_size), elem_size);
    }
  }
  if (out_first1 != NULL) {
    *out_first1 = lo;
  }
  if (out_first2 != NULL) {
    *out_first2 = hi;
  }
}

/* ---- merge sorts ---------------------------------------------------- */

static void sm_merge2(const unsigned char *src, unsigned char *dst, size_t lo,
                      size_t mid, size_t hi, size_t es,
                      ds_compare_func_t compare, void *user) {
  size_t i = lo;
  size_t j = mid;
  size_t o = lo;
  while (i < mid && j < hi) {
    if (compare(CAT(src, j, es), CAT(src, i, es), user) < 0) {
      memcpy(AT(dst, o, es), CAT(src, j, es), es);
      j++;
    } else {
      memcpy(AT(dst, o, es), CAT(src, i, es), es);
      i++;
    }
    o++;
  }
  if (i < mid) {
    memcpy(AT(dst, o, es), CAT(src, i, es), (mid - i) * es);
    o += mid - i;
  }
  if (j < hi) {
    memcpy(AT(dst, o, es), CAT(src, j, es), (hi - j) * es);
  }
}

ds_status_t ds_two_way_merge_sort(void *base, size_t count, size_t elem_size,
                                  ds_compare_func_t compare, void *user,
                                  void *scratch) {
  unsigned char *src = (unsigned char *)base;
  unsigned char *dst = (unsigned char *)scratch;
  unsigned char *tmp;
  size_t run;
  size_t lo;
  size_t mid;
  size_t hi;
  if (base == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  if (scratch == NULL) {
    return DS_ERR_NULL;
  }
  for (run = 1; run<count; run = run> count / 2 ? count : run * 2) {
    for (lo = 0; lo < count; lo += 2 * run) {
      mid = count - lo > run ? lo + run : count;
      hi = count - mid > run ? mid + run : count;
      sm_merge2(src, dst, lo, mid, hi, elem_size, compare, user);
      if (count - lo <= 2 * run) {
        break;
      }
    }
    tmp = src;
    src = dst;
    dst = tmp;
  }
  if (src != (unsigned char *)base) {
    memcpy(base, src, count * elem_size);
  }
  return DS_OK;
}

ds_status_t ds_kway_merge_sort(void *base, size_t count, size_t elem_size,
                               size_t ways, ds_compare_func_t compare,
                               void *user, void *scratch,
                               ds_context_t *context) {
  ds_tournament_config_t tc;
  ds_tournament_t t;
  unsigned char *src = (unsigned char *)base;
  unsigned char *dst = (unsigned char *)scratch;
  unsigned char *tmp;
  size_t *pos;
  size_t *end;
  size_t run;
  size_t group;
  size_t start;
  size_t r;
  size_t o;
  size_t w;
  ds_status_t st;
  if (base == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 || ways < 2) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  if (scratch == NULL) {
    return DS_ERR_NULL;
  }
  if (ways == 2) {
    return ds_two_way_merge_sort(base, count, elem_size, compare, user,
                                 scratch);
  }
  if (ways > count) {
    ways = count;
  }
  tc.players = ways;
  tc.elem_size = elem_size;
  tc.compare = compare;
  tc.user = user;
  tc.kind = DS_TOURNAMENT_LOSER;
  st = ds_tournament_init(&t, &tc, context);
  if (st != DS_OK) {
    return st;
  }
  pos = (size_t *)ds_context_alloc(context, 2 * ways * sizeof(size_t));
  if (pos == NULL) {
    ds_tournament_destroy(&t);
    return DS_ERR_ALLOC;
  }
  end = pos + ways;
  run = 1;
  while (run < count) {
    group = run <= count / ways ? run * ways : count;
    for (start = 0; start < count;) {
      for (r = 0; r < ways; r++) {
        size_t s0 = r * run;
        if (s0 >= count - start) {
          pos[r] = end[r] = 0;
          ds_tournament_set_alive(&t, r, false);
          continue;
        }
        pos[r] = start + s0;
        end[r] = count - pos[r] > run ? pos[r] + run : count;
        memcpy(ds_tournament_slot(&t, r), CAT(src, pos[r], elem_size),
               elem_size);
        pos[r]++;
        ds_tournament_set_alive(&t, r, true);
      }
      ds_tournament_build(&t);
      o = start;
      for (;;) {
        w = ds_tournament_winner(&t);
        if (w == DS_TOURNAMENT_NONE) {
          break;
        }
        memcpy(AT(dst, o, elem_size), ds_tournament_slot(&t, w), elem_size);
        o++;
        if (pos[w] < end[w]) {
          memcpy(ds_tournament_slot(&t, w), CAT(src, pos[w], elem_size),
                 elem_size);
          pos[w]++;
        } else {
          ds_tournament_set_alive(&t, w, false);
        }
        ds_tournament_replay(&t, w);
      }
      if (count - start <= group) {
        break;
      }
      start += group;
    }
    tmp = src;
    src = dst;
    dst = tmp;
    run = group;
  }
  if (src != (unsigned char *)base) {
    memcpy(base, src, count * elem_size);
  }
  ds_context_free(context, pos);
  ds_tournament_destroy(&t);
  return DS_OK;
}

ds_status_t ds_three_way_merge_sort(void *base, size_t count, size_t elem_size,
                                    ds_compare_func_t compare, void *user,
                                    void *scratch, ds_context_t *context) {
  return ds_kway_merge_sort(base, count, elem_size, 3, compare, user, scratch,
                            context);
}

/* ---- adaptive (natural) merge sort ---------------------------------- */

typedef struct ad_ctx {
  unsigned char *a;
  unsigned char *tmp;
  size_t es;
  ds_compare_func_t compare;
  void *user;
} ad_ctx_t;

static int ad_cmp(const ad_ctx_t *c, const void *x, const void *y) {
  return c->compare(x, y, c->user);
}

/* First index in [0, len) of run whose element is > key (upper bound),
 * found by galloping from the left. */
static size_t ad_gallop_right(const ad_ctx_t *c, const void *key,
                              const unsigned char *run, size_t len) {
  size_t lo = 0;
  size_t hi = 1;
  size_t mid;
  while (hi <= len && ad_cmp(c, key, CAT(run, hi - 1, c->es)) >= 0) {
    lo = hi;
    hi = hi > len / 2 ? len + 1 : 2 * hi;
  }
  if (hi > len) {
    hi = len;
  } else {
    hi = hi - 1;
  }
  /* answer in [lo, hi] */
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    if (ad_cmp(c, key, CAT(run, mid, c->es)) >= 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

/* First index whose element is >= key (lower bound), galloping. */
static size_t ad_gallop_left(const ad_ctx_t *c, const void *key,
                             const unsigned char *run, size_t len) {
  size_t lo = 0;
  size_t hi = 1;
  size_t mid;
  while (hi <= len && ad_cmp(c, CAT(run, hi - 1, c->es), key) < 0) {
    lo = hi;
    hi = hi > len / 2 ? len + 1 : 2 * hi;
  }
  if (hi > len) {
    hi = len;
  } else {
    hi = hi - 1;
  }
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    if (ad_cmp(c, CAT(run, mid, c->es), key) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

static void ad_merge(const ad_ctx_t *c, size_t s1, size_t l1, size_t l2) {
  size_t es = c->es;
  unsigned char *a = c->a;
  size_t k;
  size_t i;
  size_t j;
  size_t o;
  size_t s2 = s1 + l1;
  /* elements of run 1 not greater than run2[0] are already in place */
  k = ad_gallop_right(c, CAT(a, s2, es), CAT(a, s1, es), l1);
  s1 += k;
  l1 -= k;
  if (l1 == 0) {
    return;
  }
  /* elements of run 2 not less than run1[last] are already in place */
  l2 = ad_gallop_left(c, CAT(a, s2 - 1, es), CAT(a, s2, es), l2);
  if (l2 == 0) {
    return;
  }
  if (l1 <= l2) {
    memcpy(c->tmp, CAT(a, s1, es), l1 * es);
    i = 0;
    j = s2;
    o = s1;
    while (i < l1 && j < s2 + l2) {
      if (ad_cmp(c, CAT(a, j, es), CAT(c->tmp, i, es)) < 0) {
        memcpy(AT(a, o, es), CAT(a, j, es), es);
        j++;
      } else {
        memcpy(AT(a, o, es), CAT(c->tmp, i, es), es);
        i++;
      }
      o++;
    }
    if (i < l1) {
      memcpy(AT(a, o, es), CAT(c->tmp, i, es), (l1 - i) * es);
    }
  } else {
    memcpy(c->tmp, CAT(a, s2, es), l2 * es);
    i = s2; /* one past the next run-1 element */
    j = l2; /* one past the next run-2 element (in tmp) */
    o = s2 + l2;
    while (i > s1 && j > 0) {
      o--;
      if (ad_cmp(c, CAT(c->tmp, j - 1, es), CAT(a, i - 1, es)) < 0) {
        memcpy(AT(a, o, es), CAT(a, i - 1, es), es);
        i--;
      } else {
        memcpy(AT(a, o, es), CAT(c->tmp, j - 1, es), es);
        j--;
      }
    }
    if (j > 0) {
      memcpy(AT(a, s1, es), c->tmp, j * es);
    }
  }
}

static size_t ad_minrun(size_t n) {
  size_t r = 0;
  while (n >= 64) {
    r |= n & 1u;
    n >>= 1;
  }
  return n + r;
}

static void ad_binary_insertion(const ad_ctx_t *c, size_t lo, size_t sorted,
                                size_t hi) {
  size_t es = c->es;
  size_t i;
  size_t l;
  size_t r;
  size_t m;
  unsigned char *a = c->a;
  for (i = sorted; i < hi; i++) {
    l = lo;
    r = i;
    while (l < r) {
      m = l + (r - l) / 2;
      if (ad_cmp(c, CAT(a, i, es), CAT(a, m, es)) < 0) {
        r = m;
      } else {
        l = m + 1;
      }
    }
    if (l < i) {
      ds_elem_rotate(a, l, i, i + 1, es);
    }
  }
}

void ds_adaptive_sort(void *base, size_t count, size_t elem_size,
                      ds_compare_func_t compare, void *user, void *scratch) {
  ad_ctx_t c;
  size_t run_start[ADAPTIVE_STACK];
  size_t run_len[ADAPTIVE_STACK];
  size_t n = 0;
  size_t minrun;
  size_t i = 0;
  size_t len;
  size_t force;
  size_t at;
  if (base == NULL || compare == NULL || elem_size == 0 || count < 2) {
    return;
  }
  if (scratch == NULL) {
    ds_inplace_stable_sort(base, count, elem_size, compare, user);
    return;
  }
  c.a = (unsigned char *)base;
  c.tmp = (unsigned char *)scratch;
  c.es = elem_size;
  c.compare = compare;
  c.user = user;
  minrun = ad_minrun(count);
  while (i < count) {
    len = 1;
    if (i + 1 < count) {
      if (ad_cmp(&c, CAT(c.a, i + 1, elem_size), CAT(c.a, i, elem_size)) < 0) {
        len = 2;
        while (i + len < count &&
               ad_cmp(&c, CAT(c.a, i + len, elem_size),
                      CAT(c.a, i + len - 1, elem_size)) < 0) {
          len++;
        }
        ds_elem_reverse(AT(c.a, i, elem_size), len, elem_size);
      } else {
        len = 2;
        while (i + len < count &&
               ad_cmp(&c, CAT(c.a, i + len, elem_size),
                      CAT(c.a, i + len - 1, elem_size)) >= 0) {
          len++;
        }
      }
    }
    if (len < minrun) {
      force = count - i < minrun ? count - i : minrun;
      ad_binary_insertion(&c, i, i + len, i + force);
      len = force;
    }
    run_start[n] = i;
    run_len[n] = len;
    n++;
    i += len;
    /* restore the run-length invariants */
    while (n > 1) {
      at = n - 2;
      if ((n >= 3 && run_len[n - 3] <= run_len[n - 2] + run_len[n - 1]) ||
          (n >= 4 && run_len[n - 4] <= run_len[n - 3] + run_len[n - 2])) {
        if (run_len[n - 3] < run_len[n - 1]) {
          at = n - 3;
        }
      } else if (run_len[n - 2] > run_len[n - 1]) {
        break;
      }
      ad_merge(&c, run_start[at], run_len[at], run_len[at + 1]);
      run_len[at] += run_len[at + 1];
      if (at + 2 < n) {
        run_start[at + 1] = run_start[at + 2];
        run_len[at + 1] = run_len[at + 2];
      }
      n--;
    }
  }
  while (n > 1) {
    at = n - 2;
    if (n >= 3 && run_len[n - 3] < run_len[n - 1]) {
      at = n - 3;
    }
    ad_merge(&c, run_start[at], run_len[at], run_len[at + 1]);
    run_len[at] += run_len[at + 1];
    if (at + 2 < n) {
      run_start[at + 1] = run_start[at + 2];
      run_len[at + 1] = run_len[at + 2];
    }
    n--;
  }
}

/* ---- strand sort ---------------------------------------------------- */

ds_status_t ds_strand_sort(void *base, size_t count, size_t elem_size,
                           ds_compare_func_t compare, void *user,
                           void *scratch) {
  unsigned char *a = (unsigned char *)base;
  unsigned char *s = (unsigned char *)scratch;
  size_t es = elem_size;
  size_t remaining = count; /* unsorted items in [0, remaining) */
  size_t rlen = 0;          /* result in [count - rlen, count) */
  size_t slen;
  size_t keep;
  size_t i;
  size_t si;
  size_t ri;
  size_t o;
  if (base == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (es == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  if (scratch == NULL) {
    return DS_ERR_NULL;
  }
  while (remaining > 0) {
    /* pull a non-decreasing strand into scratch, compacting the rest */
    memcpy(s, a, es);
    slen = 1;
    keep = 0;
    for (i = 1; i < remaining; i++) {
      if (compare(CAT(a, i, es), CAT(s, slen - 1, es), user) >= 0) {
        memcpy(AT(s, slen, es), CAT(a, i, es), es);
        slen++;
      } else {
        if (keep != i) {
          memcpy(AT(a, keep, es), CAT(a, i, es), es);
        }
        keep++;
      }
    }
    /* merge strand and result into [keep, count); ties keep the result
     * (earlier strands) first */
    si = 0;
    ri = count - rlen;
    o = keep;
    while (si < slen && ri < count) {
      if (compare(CAT(s, si, es), CAT(a, ri, es), user) < 0) {
        memcpy(AT(a, o, es), CAT(s, si, es), es);
        si++;
      } else {
        if (o != ri) {
          memcpy(AT(a, o, es), CAT(a, ri, es), es);
        }
        ri++;
      }
      o++;
    }
    if (si < slen) {
      memcpy(AT(a, o, es), CAT(s, si, es), (slen - si) * es);
    }
    rlen += slen;
    remaining = keep;
  }
  return DS_OK;
}

void ds_bingo_sort(void *base, size_t count, size_t elem_size,
                   ds_compare_func_t compare, void *user) {
  qs_ctx_t c;
  size_t remaining = count; /* unsorted prefix length */
  size_t i;
  size_t m;
  if (base == NULL || compare == NULL || elem_size == 0) {
    return;
  }
  c.a = (unsigned char *)base;
  c.es = elem_size;
  c.compare = compare;
  c.user = user;
  while (remaining > 1) {
    m = 0;
    for (i = 1; i < remaining; i++) {
      if (qs_cmp(&c, i, m) > 0) {
        m = i;
      }
    }
    c.p = m;
    /* move every copy of the maximum to the end of the unsorted part */
    for (i = remaining; i-- > 0;) {
      if (qs_cmp(&c, i, c.p) == 0) {
        qs_swap(&c, i, remaining - 1);
        remaining--;
      }
    }
  }
}

/* ---- joke sorts ----------------------------------------------------- */

bool ds_lucky_sort(void *base, size_t count, size_t elem_size,
                   ds_compare_func_t compare, void *user) {
  if (base == NULL || compare == NULL) {
    return count < 2;
  }
  return ds_is_sorted(base, count, elem_size, compare, user);
}

bool ds_bogosort(void *base, size_t count, size_t elem_size,
                 ds_compare_func_t compare, void *user, ds_random_func_t random,
                 void *random_state, unsigned long max_steps) {
  unsigned long steps = 0;
  unsigned long own = 1234567UL;
  size_t i;
  if (base == NULL || compare == NULL) {
    return count < 2;
  }
  if (random == NULL) {
    random = sm_xorshift;
    random_state = &own;
  }
  while (!ds_is_sorted(base, count, elem_size, compare, user)) {
    if (max_steps != 0 && steps >= max_steps) {
      return false;
    }
    for (i = count; i > 1; i--) {
      ds_elem_swap(AT(base, i - 1, elem_size),
                   AT(base, sm_bounded(random, random_state, i), elem_size),
                   elem_size);
    }
    steps++;
  }
  return true;
}

bool ds_bozo_sort(void *base, size_t count, size_t elem_size,
                  ds_compare_func_t compare, void *user,
                  ds_random_func_t random, void *random_state,
                  unsigned long max_steps) {
  unsigned long steps = 0;
  unsigned long own = 7654321UL;
  if (base == NULL || compare == NULL) {
    return count < 2;
  }
  if (random == NULL) {
    random = sm_xorshift;
    random_state = &own;
  }
  while (!ds_is_sorted(base, count, elem_size, compare, user)) {
    if (max_steps != 0 && steps >= max_steps) {
      return false;
    }
    ds_elem_swap(AT(base, sm_bounded(random, random_state, count), elem_size),
                 AT(base, sm_bounded(random, random_state, count), elem_size),
                 elem_size);
    steps++;
  }
  return true;
}

bool ds_permutation_sort(void *base, size_t count, size_t elem_size,
                         ds_compare_func_t compare, void *user,
                         unsigned long max_steps, ds_context_t *context) {
  size_t *c;
  size_t i = 1;
  unsigned long steps = 0;
  bool sorted;
  if (base == NULL || compare == NULL) {
    return count < 2;
  }
  if (ds_is_sorted(base, count, elem_size, compare, user)) {
    return true;
  }
  c = (size_t *)ds_context_calloc(context, count, sizeof(size_t));
  if (c == NULL) {
    return false;
  }
  sorted = false;
  /* Heap's algorithm: every permutation by one swap each */
  while (i < count) {
    if (c[i] < i) {
      if (max_steps != 0 && steps >= max_steps) {
        break;
      }
      ds_elem_swap(AT(base, (i % 2 == 0) ? 0 : c[i], elem_size),
                   AT(base, i, elem_size), elem_size);
      steps++;
      if (ds_is_sorted(base, count, elem_size, compare, user)) {
        sorted = true;
        break;
      }
      c[i]++;
      i = 1;
    } else {
      c[i] = 0;
      i++;
    }
  }
  ds_context_free(context, c);
  return sorted;
}

static int sm_cmp_ulong(const void *a, const void *b) {
  unsigned long x = *(const unsigned long *)a;
  unsigned long y = *(const unsigned long *)b;
  return (x > y) - (x < y);
}

bool ds_taco_sort_ulong(unsigned long *values, size_t count, unsigned bits,
                        ds_random_func_t random, void *random_state,
                        unsigned long max_steps, ds_context_t *context) {
  unsigned long *target;
  unsigned long mask;
  unsigned long steps = 0;
  unsigned long own = 7654321UL;
  bool done;
  if (values == NULL || count < 2) {
    return true;
  }
  if (bits == 0 || bits > 32) {
    return false;
  }
  mask = bits == 32 ? 0xFFFFFFFFUL : (1UL << bits) - 1UL;
  if (random == NULL) {
    random = sm_xorshift;
    random_state = &own;
  }
  target =
      (unsigned long *)ds_context_alloc(context, count * sizeof(unsigned long));
  if (target == NULL) {
    return false;
  }
  memcpy(target, values, count * sizeof(unsigned long));
  qsort(target, count, sizeof(unsigned long), sm_cmp_ulong);
  for (;;) {
    done = memcmp(values, target, count * sizeof(unsigned long)) == 0;
    if (done || (max_steps != 0 && steps >= max_steps)) {
      break;
    }
    values[sm_bounded(random, random_state, count)] =
        random(random_state) & mask;
    steps++;
  }
  ds_context_free(context, target);
  return done;
}
