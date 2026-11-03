#include "select.h"
#include "sort.h"
#include <math.h>
#include <stddef.h>

#define AT(base, i, es) ((unsigned char *)(base) + (size_t)(i) * (es))
#define SEL_FR_CUTOFF 600

typedef struct sel_ctx {
  unsigned char *a;
  size_t es;
  ds_compare_func_t compare;
  void *user;
  ptrdiff_t p; /* current position of the pivot element */
} sel_ctx_t;

static int sel_cmp(const sel_ctx_t *c, ptrdiff_t i, ptrdiff_t j) {
  return c->compare(AT(c->a, i, c->es), AT(c->a, j, c->es), c->user);
}

/* Swap two positions, following the pivot. */
static void sel_swap(sel_ctx_t *c, ptrdiff_t i, ptrdiff_t j) {
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

static void sel_init(sel_ctx_t *c, void *base, size_t es,
                     ds_compare_func_t compare, void *user) {
  c->a = (unsigned char *)base;
  c->es = es;
  c->compare = compare;
  c->user = user;
  c->p = 0;
}

static void sel_insertion(sel_ctx_t *c, ptrdiff_t lo, ptrdiff_t hi) {
  ptrdiff_t i;
  ptrdiff_t j;
  for (i = lo + 1; i <= hi; i++) {
    for (j = i; j > lo && sel_cmp(c, j - 1, j) > 0; j--) {
      sel_swap(c, j - 1, j);
    }
  }
}

void ds_select_find(void *base, size_t count, size_t elem_size, size_t k,
                    ds_compare_func_t compare, void *user) {
  sel_ctx_t c;
  ptrdiff_t l = 0;
  ptrdiff_t r;
  ptrdiff_t i;
  ptrdiff_t j;
  ptrdiff_t kk = (ptrdiff_t)k;
  if (base == NULL || compare == NULL || k >= count) {
    return;
  }
  sel_init(&c, base, elem_size, compare, user);
  r = (ptrdiff_t)count - 1;
  while (l < r) {
    c.p = kk;
    i = l;
    j = r;
    do {
      while (sel_cmp(&c, i, c.p) < 0) {
        i++;
      }
      while (sel_cmp(&c, c.p, j) < 0) {
        j--;
      }
      if (i <= j) {
        sel_swap(&c, i, j);
        i++;
        j--;
      }
    } while (i <= j);
    if (j < kk) {
      l = i;
    }
    if (kk < i) {
      r = j;
    }
  }
}

void ds_select_modifind(void *base, size_t count, size_t elem_size, size_t k,
                        ds_compare_func_t compare, void *user) {
  sel_ctx_t c;
  ptrdiff_t l = 0;
  ptrdiff_t r;
  ptrdiff_t i;
  ptrdiff_t j;
  ptrdiff_t kk = (ptrdiff_t)k;
  if (base == NULL || compare == NULL || k >= count) {
    return;
  }
  sel_init(&c, base, elem_size, compare, user);
  r = (ptrdiff_t)count - 1;
  while (l < r) {
    c.p = kk;
    i = l;
    j = r;
    while (j >= kk && i <= kk) {
      while (sel_cmp(&c, i, c.p) < 0) {
        i++;
      }
      while (sel_cmp(&c, c.p, j) < 0) {
        j--;
      }
      sel_swap(&c, i, j);
      i++;
      j--;
    }
    if (j < kk) {
      l = i;
    }
    if (kk < i) {
      r = j;
    }
  }
}

static void sel_floyd_rivest(sel_ctx_t *c, ptrdiff_t l, ptrdiff_t r,
                             ptrdiff_t k) {
  ptrdiff_t i;
  ptrdiff_t j;
  ptrdiff_t nl;
  ptrdiff_t nr;
  double n;
  double ii;
  double z;
  double s;
  double sd;
  double fl;
  double fr;
  while (r > l) {
    if (r - l > SEL_FR_CUTOFF) {
      n = (double)(r - l + 1);
      ii = (double)(k - l + 1);
      z = log(n);
      s = 0.5 * exp(2.0 * z / 3.0);
      sd = 0.5 * sqrt(z * s * (n - s) / n);
      if (ii < n / 2.0) {
        sd = -sd;
      }
      fl = floor((double)k - ii * s / n + sd);
      fr = floor((double)k + (n - ii) * s / n + sd);
      nl = fl > (double)l ? (ptrdiff_t)fl : l;
      nr = fr < (double)r ? (ptrdiff_t)fr : r;
      if (nl > k) {
        nl = k;
      }
      if (nr < k) {
        nr = k;
      }
      sel_floyd_rivest(c, nl, nr, k);
    }
    c->p = k;
    i = l;
    j = r;
    sel_swap(c, l, k);
    if (sel_cmp(c, r, c->p) > 0) {
      sel_swap(c, r, l);
    }
    while (i < j) {
      sel_swap(c, i, j);
      i++;
      j--;
      while (sel_cmp(c, i, c->p) < 0) {
        i++;
      }
      while (sel_cmp(c, j, c->p) > 0) {
        j--;
      }
    }
    if (sel_cmp(c, l, c->p) == 0) {
      sel_swap(c, l, j);
    } else {
      j++;
      sel_swap(c, j, r);
    }
    if (j <= k) {
      l = j + 1;
    }
    if (k <= j) {
      r = j - 1;
    }
  }
}

void ds_select_floyd_rivest(void *base, size_t count, size_t elem_size,
                            size_t k, ds_compare_func_t compare, void *user) {
  sel_ctx_t c;
  if (base == NULL || compare == NULL || k >= count) {
    return;
  }
  sel_init(&c, base, elem_size, compare, user);
  sel_floyd_rivest(&c, 0, (ptrdiff_t)count - 1, (ptrdiff_t)k);
}

/* Three-way partition of [l, r] around the element at p: afterwards
 * [l, lt) < pivot, [lt, gt] == pivot, (gt, r] > pivot. */
static void sel_partition3(sel_ctx_t *c, ptrdiff_t l, ptrdiff_t r, ptrdiff_t p,
                           ptrdiff_t *out_lt, ptrdiff_t *out_gt) {
  ptrdiff_t lt = l;
  ptrdiff_t gt = r;
  ptrdiff_t i = l + 1;
  int cr;
  c->p = p;
  sel_swap(c, l, p);
  while (i <= gt) {
    cr = sel_cmp(c, i, lt);
    if (cr < 0) {
      sel_swap(c, lt, i);
      lt++;
      i++;
    } else if (cr > 0) {
      sel_swap(c, i, gt);
      gt--;
    } else {
      i++;
    }
  }
  *out_lt = lt;
  *out_gt = gt;
}

static void sel_mom(sel_ctx_t *c, ptrdiff_t l, ptrdiff_t r, ptrdiff_t k) {
  ptrdiff_t g;
  ptrdiff_t e;
  ptrdiff_t m;
  ptrdiff_t lt;
  ptrdiff_t gt;
  for (;;) {
    if (r - l < 5) {
      sel_insertion(c, l, r);
      return;
    }
    m = 0;
    for (g = l; g <= r; g += 5) {
      e = g + 4 < r ? g + 4 : r;
      sel_insertion(c, g, e);
      sel_swap(c, l + m, g + (e - g) / 2);
      m++;
    }
    sel_mom(c, l, l + m - 1, l + (m - 1) / 2);
    sel_partition3(c, l, r, l + (m - 1) / 2, &lt, &gt);
    if (k < lt) {
      r = lt - 1;
    } else if (k > gt) {
      l = gt + 1;
    } else {
      return;
    }
  }
}

void ds_select_median_of_medians(void *base, size_t count, size_t elem_size,
                                 size_t k, ds_compare_func_t compare,
                                 void *user) {
  sel_ctx_t c;
  if (base == NULL || compare == NULL || k >= count) {
    return;
  }
  sel_init(&c, base, elem_size, compare, user);
  sel_mom(&c, 0, (ptrdiff_t)count - 1, (ptrdiff_t)k);
}

static unsigned long sel_default_random(void *state) {
  unsigned long *x = (unsigned long *)state;
  /* 32-bit xorshift */
  *x ^= (*x << 13) & 0xFFFFFFFFUL;
  *x ^= *x >> 17;
  *x ^= (*x << 5) & 0xFFFFFFFFUL;
  return *x & 0xFFFFFFFFUL;
}

/* Uniform value in [0, bound) from 32-bit draws (rejection sampling). */
static size_t sel_bounded(ds_random_func_t random, void *state, size_t bound) {
  unsigned long x;
  unsigned long limit;
  size_t hi;
  if (bound <= 1) {
    return 0;
  }
  if ((bound - 1) <= 0xFFFFFFFFUL) {
    limit = 0xFFFFFFFFUL -
            (0xFFFFFFFFUL % (unsigned long)bound + 1UL) % (unsigned long)bound;
    do {
      x = random(state) & 0xFFFFFFFFUL;
    } while (x > limit);
    return (size_t)(x % (unsigned long)bound);
  }
  /* wider than 32 bits: combine draws (bias below 2^-32 per step) */
  hi = (size_t)(random(state) & 0xFFFFFFFFUL);
  hi = (hi << 16) << 16;
  hi |= (size_t)(random(state) & 0xFFFFFFFFUL);
  return hi % bound;
}

void ds_select_randomized(void *base, size_t count, size_t elem_size, size_t k,
                          ds_compare_func_t compare, void *user,
                          ds_random_func_t random, void *random_state) {
  sel_ctx_t c;
  ptrdiff_t l = 0;
  ptrdiff_t r;
  ptrdiff_t kk = (ptrdiff_t)k;
  ptrdiff_t lt;
  ptrdiff_t gt;
  ptrdiff_t p;
  unsigned long own = 2463534242UL;
  if (base == NULL || compare == NULL || k >= count) {
    return;
  }
  if (random == NULL) {
    random = sel_default_random;
    random_state = &own;
  }
  sel_init(&c, base, elem_size, compare, user);
  r = (ptrdiff_t)count - 1;
  while (l < r) {
    p = l + (ptrdiff_t)sel_bounded(random, random_state, (size_t)(r - l + 1));
    sel_partition3(&c, l, r, p, &lt, &gt);
    if (kk < lt) {
      r = lt - 1;
    } else if (kk > gt) {
      l = gt + 1;
    } else {
      return;
    }
  }
}

ds_status_t ds_select(void *base, size_t count, size_t elem_size, size_t k,
                      ds_compare_func_t compare, void *user,
                      const ds_select_config_t *config) {
  ds_select_algorithm_t alg =
      config == NULL ? DS_SELECT_INTRO : config->algorithm;
  if (base == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (k >= count) {
    return DS_ERR_RANGE;
  }
  switch (alg) {
  case DS_SELECT_INTRO:
    ds_nth_element(base, count, elem_size, k, compare, user);
    break;
  case DS_SELECT_FIND:
    ds_select_find(base, count, elem_size, k, compare, user);
    break;
  case DS_SELECT_MODIFIND:
    ds_select_modifind(base, count, elem_size, k, compare, user);
    break;
  case DS_SELECT_FLOYD_RIVEST:
    ds_select_floyd_rivest(base, count, elem_size, k, compare, user);
    break;
  case DS_SELECT_MEDIAN_OF_MEDIANS:
    ds_select_median_of_medians(base, count, elem_size, k, compare, user);
    break;
  case DS_SELECT_RANDOMIZED:
    ds_select_randomized(base, count, elem_size, k, compare, user,
                         config->random, config->random_state);
    break;
  default:
    return DS_ERR_INVALID;
  }
  return DS_OK;
}

size_t ds_select_median(void *base, size_t count, size_t elem_size,
                        ds_compare_func_t compare, void *user) {
  size_t k;
  if (count == 0) {
    return 0;
  }
  k = (count - 1) / 2;
  ds_nth_element(base, count, elem_size, k, compare, user);
  return k;
}

ds_status_t ds_select_mode_sorted(const void *base, size_t count,
                                  size_t elem_size, ds_compare_func_t compare,
                                  void *user, size_t *out_index,
                                  size_t *out_frequency) {
  const unsigned char *a = (const unsigned char *)base;
  size_t i;
  size_t run_start = 0;
  size_t best = 0;
  size_t best_len = 0;
  if (base == NULL || compare == NULL || out_index == NULL ||
      out_frequency == NULL) {
    return DS_ERR_NULL;
  }
  if (count == 0) {
    return DS_EMPTY;
  }
  for (i = 1; i <= count; i++) {
    if (i == count ||
        compare(a + run_start * elem_size, a + i * elem_size, user) != 0) {
      if (i - run_start > best_len) {
        best_len = i - run_start;
        best = run_start;
      }
      run_start = i;
    }
  }
  *out_index = best;
  *out_frequency = best_len;
  return DS_OK;
}

ds_status_t ds_select_mode(void *base, size_t count, size_t elem_size,
                           ds_compare_func_t compare, void *user,
                           size_t *out_index, size_t *out_frequency) {
  if (base == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  ds_intro_sort(base, count, elem_size, compare, user);
  return ds_select_mode_sorted(base, count, elem_size, compare, user, out_index,
                               out_frequency);
}
