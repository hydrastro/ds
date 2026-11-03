#include "combinatorics.h"
#include <limits.h>
#include <string.h>

#define CB_MAX ((unsigned long)-1)
#define CB_BITS ((unsigned int)(CHAR_BIT * sizeof(unsigned long)))

static unsigned long cb_gcd(unsigned long a, unsigned long b) {
  unsigned long t;
  while (b != 0) {
    t = a % b;
    a = b;
    b = t;
  }
  return a;
}

static bool cb_mul(unsigned long a, unsigned long b, unsigned long *out) {
  if (a != 0 && b > CB_MAX / a) {
    return false;
  }
  *out = a * b;
  return true;
}

static bool cb_add(unsigned long a, unsigned long b, unsigned long *out) {
  if (b > CB_MAX - a) {
    return false;
  }
  *out = a + b;
  return true;
}

ds_status_t ds_binomial(unsigned long n, unsigned long k, unsigned long *out) {
  unsigned long r = 1;
  unsigned long i;
  unsigned long g;
  unsigned long m;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  if (k > n) {
    *out = 0;
    return DS_OK;
  }
  if (k > n - k) {
    k = n - k;
  }
  for (i = 1; i <= k; i++) {
    /* r = r * (n - k + i) / i, kept exact: i / gcd(r, i) divides m */
    g = cb_gcd(r, i);
    r /= g;
    m = (n - k + i) / (i / g);
    if (!cb_mul(r, m, &r)) {
      return DS_ERR_OVERFLOW;
    }
  }
  *out = r;
  return DS_OK;
}

bool ds_next_combination(size_t *c, size_t k, size_t n) {
  size_t i = k;
  size_t j;
  while (i > 0) {
    i--;
    if (c[i] < n - k + i) {
      c[i]++;
      for (j = i + 1; j < k; j++) {
        c[j] = c[j - 1] + 1;
      }
      return true;
    }
  }
  return false;
}

ds_status_t ds_combination_rank(const size_t *c, size_t k, size_t n,
                                unsigned long *out_rank) {
  unsigned long rank = 0;
  unsigned long b;
  size_t i;
  size_t j;
  size_t prev = 0;
  ds_status_t s;
  if (out_rank == NULL || (c == NULL && k > 0)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < k; i++) {
    if (c[i] >= n || (i > 0 && c[i] <= c[i - 1])) {
      return DS_ERR_INVALID;
    }
    for (j = i == 0 ? 0 : prev + 1; j < c[i]; j++) {
      s = ds_binomial((unsigned long)(n - 1 - j), (unsigned long)(k - 1 - i),
                      &b);
      if (s != DS_OK || !cb_add(rank, b, &rank)) {
        return DS_ERR_OVERFLOW;
      }
    }
    prev = c[i];
  }
  *out_rank = rank;
  return DS_OK;
}

ds_status_t ds_combination_unrank(unsigned long rank, size_t k, size_t n,
                                  size_t *out_c) {
  size_t i;
  size_t j = 0;
  unsigned long b;
  ds_status_t s;
  if (out_c == NULL && k > 0) {
    return DS_ERR_NULL;
  }
  if (k > n) {
    return DS_ERR_RANGE;
  }
  for (i = 0; i < k; i++) {
    for (;;) {
      if (j >= n) {
        return DS_ERR_RANGE;
      }
      s = ds_binomial((unsigned long)(n - 1 - j), (unsigned long)(k - 1 - i),
                      &b);
      if (s != DS_OK) {
        return s;
      }
      if (rank < b) {
        break;
      }
      rank -= b;
      j++;
    }
    out_c[i] = j++;
  }
  return rank == 0 ? DS_OK : DS_ERR_RANGE;
}

ds_status_t ds_factorial(unsigned long n, unsigned long *out) {
  unsigned long r = 1;
  unsigned long i;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 2; i <= n; i++) {
    if (!cb_mul(r, i, &r)) {
      return DS_ERR_OVERFLOW;
    }
  }
  *out = r;
  return DS_OK;
}

ds_status_t ds_permutation_rank(const size_t *perm, size_t n,
                                unsigned long *out_rank) {
  unsigned long rank = 0;
  unsigned long f;
  size_t i;
  size_t j;
  size_t smaller;
  if (out_rank == NULL || (perm == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n; i++) {
    if (perm[i] >= n) {
      return DS_ERR_INVALID;
    }
    smaller = 0;
    for (j = i + 1; j < n; j++) {
      if (perm[j] == perm[i]) {
        return DS_ERR_INVALID;
      }
      smaller += perm[j] < perm[i];
    }
    if (ds_factorial((unsigned long)(n - 1 - i), &f) != DS_OK ||
        !cb_mul(f, (unsigned long)smaller, &f) || !cb_add(rank, f, &rank)) {
      return DS_ERR_OVERFLOW;
    }
  }
  *out_rank = rank;
  return DS_OK;
}

ds_status_t ds_permutation_unrank(unsigned long rank, size_t n,
                                  size_t *out_perm) {
  size_t i;
  size_t j;
  size_t q;
  unsigned long f;
  if (out_perm == NULL && n > 0) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n; i++) {
    out_perm[i] = i;
  }
  for (i = 0; i < n; i++) {
    if (ds_factorial((unsigned long)(n - 1 - i), &f) != DS_OK) {
      /* (n - 1 - i)! exceeds every representable rank: digit is 0 */
      continue;
    }
    q = (size_t)(rank / f);
    rank %= f;
    if (i + q >= n) {
      return DS_ERR_RANGE;
    }
    /* move out_perm[i + q] to position i */
    {
      size_t v = out_perm[i + q];
      for (j = i + q; j > i; j--) {
        out_perm[j] = out_perm[j - 1];
      }
      out_perm[i] = v;
    }
  }
  return rank == 0 ? DS_OK : DS_ERR_RANGE;
}

void ds_sjt_init(ds_sjt_t *s, size_t n, size_t *perm, size_t *pos,
                 signed char *dir) {
  size_t i;
  s->perm = perm;
  s->pos = pos;
  s->dir = dir;
  s->n = n;
  for (i = 0; i < n; i++) {
    perm[i] = i;
    pos[i] = i;
    dir[i] = -1;
  }
}

bool ds_sjt_next(ds_sjt_t *s, size_t *out_i) {
  size_t v;
  size_t best = (size_t)-1;
  size_t p;
  size_t q;
  size_t other;
  /* largest mobile element: points at a smaller neighbour */
  for (v = s->n; v-- > 0;) {
    p = s->pos[v];
    if (s->dir[v] < 0) {
      if (p > 0 && s->perm[p - 1] < v) {
        best = v;
        break;
      }
    } else if (p + 1 < s->n && s->perm[p + 1] < v) {
      best = v;
      break;
    }
  }
  if (best == (size_t)-1) {
    return false;
  }
  p = s->pos[best];
  q = s->dir[best] < 0 ? p - 1 : p + 1;
  other = s->perm[q];
  s->perm[q] = best;
  s->perm[p] = other;
  s->pos[best] = q;
  s->pos[other] = p;
  for (v = best + 1; v < s->n; v++) {
    s->dir[v] = (signed char)-s->dir[v];
  }
  if (out_i != NULL) {
    *out_i = p < q ? p : q;
  }
  return true;
}

ds_status_t ds_derangement_count(unsigned long n, unsigned long *out) {
  unsigned long a = 1; /* D(0) */
  unsigned long b = 0; /* D(1) */
  unsigned long c;
  unsigned long i;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  if (n == 0) {
    *out = 1;
    return DS_OK;
  }
  for (i = 2; i <= n; i++) {
    if (!cb_add(a, b, &c) || !cb_mul(i - 1, c, &c)) {
      return DS_ERR_OVERFLOW;
    }
    a = b;
    b = c;
  }
  *out = b;
  return DS_OK;
}

ds_status_t ds_random_derangement(size_t *out, size_t n,
                                  ds_random_func_t random, void *state) {
  size_t i;
  size_t j;
  size_t t;
  bool ok;
  if (out == NULL || random == NULL) {
    return DS_ERR_NULL;
  }
  if (n == 1) {
    return DS_ERR_RANGE;
  }
  do {
    for (i = 0; i < n; i++) {
      out[i] = i;
    }
    for (i = n; i > 1; i--) {
      j = (size_t)(random(state) % (unsigned long)i);
      t = out[i - 1];
      out[i - 1] = out[j];
      out[j] = t;
    }
    ok = true;
    for (i = 0; i < n && ok; i++) {
      ok = out[i] != i;
    }
  } while (!ok);
  return DS_OK;
}

ds_status_t ds_catalan(unsigned long n, unsigned long *out) {
  unsigned long c = 1;
  unsigned long i;
  unsigned long g;
  unsigned long num;
  unsigned long den;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n; i++) {
    /* C(i + 1) = C(i) * 2(2i + 1) / (i + 2) */
    num = 2 * (2 * i + 1);
    den = i + 2;
    g = cb_gcd(c, den);
    c /= g;
    den /= g;
    num /= den; /* exact: the result is an integer */
    if (!cb_mul(c, num, &c)) {
      return DS_ERR_OVERFLOW;
    }
  }
  *out = c;
  return DS_OK;
}

ds_status_t ds_partition_count(unsigned long n, unsigned long *out) {
  unsigned long p[512];
  unsigned long i;
  unsigned long k;
  unsigned long g1;
  unsigned long g2;
  unsigned long plus;
  unsigned long minus;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  if (n >= 512) {
    return DS_ERR_OVERFLOW; /* p(416) already exceeds 2^64 */
  }
  p[0] = 1;
  for (i = 1; i <= n; i++) {
    plus = 0;
    minus = 0;
    for (k = 1;; k++) {
      g1 = k * (3 * k - 1) / 2;
      g2 = k * (3 * k + 1) / 2;
      if (g1 > i) {
        break;
      }
      if (k & 1UL) {
        if (!cb_add(plus, p[i - g1], &plus) ||
            (g2 <= i && !cb_add(plus, p[i - g2], &plus))) {
          return DS_ERR_OVERFLOW;
        }
      } else {
        if (!cb_add(minus, p[i - g1], &minus) ||
            (g2 <= i && !cb_add(minus, p[i - g2], &minus))) {
          return DS_ERR_OVERFLOW;
        }
      }
    }
    p[i] = plus - minus;
  }
  *out = p[n];
  return DS_OK;
}

ds_status_t ds_bell(unsigned long n, unsigned long *out) {
  unsigned long row[64];
  unsigned long next[64];
  unsigned long i;
  unsigned long j;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  if (n >= 63) {
    return DS_ERR_OVERFLOW;
  }
  row[0] = 1; /* Bell triangle */
  for (i = 1; i <= n; i++) {
    next[0] = row[i - 1];
    for (j = 1; j <= i; j++) {
      if (!cb_add(next[j - 1], row[j - 1], &next[j])) {
        return DS_ERR_OVERFLOW;
      }
    }
    memcpy(row, next, (i + 1) * sizeof(unsigned long));
  }
  *out = row[0];
  return DS_OK;
}

bool ds_next_partition(size_t *parts, size_t *count) {
  size_t i = *count;
  size_t rem;
  size_t val;
  size_t x;
  while (i > 0 && parts[i - 1] == 1) {
    i--;
  }
  if (i == 0) {
    return false;
  }
  i--;
  rem = *count - i; /* the trailing ones plus the unit taken from parts[i] */
  val = --parts[i];
  *count = i + 1;
  while (rem > 0) {
    x = val < rem ? val : rem;
    parts[(*count)++] = x;
    rem -= x;
  }
  return true;
}

bool ds_next_set_partition(size_t *a, size_t n) {
  size_t i;
  size_t j;
  size_t mx;
  for (i = n; i-- > 1;) {
    mx = 0;
    for (j = 0; j < i; j++) {
      if (a[j] > mx) {
        mx = a[j];
      }
    }
    if (a[i] <= mx) {
      a[i]++;
      for (j = i + 1; j < n; j++) {
        a[j] = 0;
      }
      return true;
    }
  }
  return false;
}

ds_status_t ds_n_queens(size_t n, size_t *cols, ds_queens_visit_func_t visit,
                        void *user, unsigned long *out_count) {
  unsigned long avail[CHAR_BIT * sizeof(unsigned long) + 1];
  unsigned long colmask[CHAR_BIT * sizeof(unsigned long) + 1];
  unsigned long d1[CHAR_BIT * sizeof(unsigned long) + 1];
  unsigned long d2[CHAR_BIT * sizeof(unsigned long) + 1];
  unsigned long full;
  unsigned long bit;
  unsigned long count = 0;
  size_t r = 0;
  size_t c;
  ds_status_t s;
  if (cols == NULL && n > 0) {
    return DS_ERR_NULL;
  }
  if (n > CB_BITS) {
    return DS_ERR_RANGE;
  }
  if (n == 0) {
    if (out_count != NULL) {
      *out_count = 1;
    }
    return DS_OK;
  }
  full = n == CB_BITS ? CB_MAX : (1UL << n) - 1;
  colmask[0] = d1[0] = d2[0] = 0;
  avail[0] = full;
  for (;;) {
    if (avail[r] == 0) {
      if (r == 0) {
        break;
      }
      r--;
      continue;
    }
    bit = avail[r] & (~avail[r] + 1);
    avail[r] &= ~bit;
    c = 0;
    while ((bit >> c) != 1UL) {
      c++;
    }
    cols[r] = c;
    if (r + 1 == n) {
      count++;
      if (visit != NULL) {
        s = visit(cols, n, user);
        if (s != DS_OK) {
          if (out_count != NULL) {
            *out_count = count;
          }
          return s == DS_STOP ? DS_OK : s;
        }
      }
      continue;
    }
    colmask[r + 1] = colmask[r] | bit;
    d1[r + 1] = ((d1[r] | bit) << 1) & full;
    d2[r + 1] = (d2[r] | bit) >> 1;
    avail[r + 1] = full & ~(colmask[r + 1] | d1[r + 1] | d2[r + 1]);
    r++;
  }
  if (out_count != NULL) {
    *out_count = count;
  }
  return DS_OK;
}

static const int cb_kdr[8] = {2, 1, -1, -2, -2, -1, 1, 2};
static const int cb_kdc[8] = {1, 2, 2, 1, -1, -2, -2, -1};

static size_t cb_knight_degree(size_t rows, size_t cols, const size_t *order,
                               size_t sq) {
  size_t r = sq / cols;
  size_t c = sq % cols;
  size_t d = 0;
  int m;
  long nr;
  long nc;
  for (m = 0; m < 8; m++) {
    nr = (long)r + cb_kdr[m];
    nc = (long)c + cb_kdc[m];
    if (nr >= 0 && nc >= 0 && nr < (long)rows && nc < (long)cols &&
        order[(size_t)nr * cols + (size_t)nc] == (size_t)-1) {
      d++;
    }
  }
  return d;
}

static double cb_center_dist(size_t rows, size_t cols, size_t sq) {
  double dr = (double)(sq / cols) - ((double)rows - 1) / 2;
  double dc = (double)(sq % cols) - ((double)cols - 1) / 2;
  return dr * dr + dc * dc;
}

static bool cb_knight_adjacent(size_t cols, size_t a, size_t b) {
  long dr = (long)(a / cols) - (long)(b / cols);
  long dc = (long)(a % cols) - (long)(b % cols);
  return (dr * dr + dc * dc) == 5;
}

static unsigned long cb_xorshift(unsigned long *x) {
  unsigned long v = *x & 0xFFFFFFFFUL;
  v ^= (v << 13) & 0xFFFFFFFFUL;
  v ^= v >> 17;
  v ^= (v << 5) & 0xFFFFFFFFUL;
  *x = v;
  return v;
}

/*
 * One Warnsdorff search with backtracking from sq: candidates ordered by
 * onward degree, ties broken by distance from the centre (rng NULL) or at
 * random. Stops after `limit` placements (0: unlimited). Returns DS_OK,
 * DS_NOT_FOUND (exhausted) or DS_STOP (limit reached).
 */
static ds_status_t cb_kt_search(size_t rows, size_t cols, size_t start,
                                size_t *order, size_t *scratch,
                                unsigned long limit, unsigned long *rng) {
  size_t total = rows * cols;
  size_t depth = 0;
  size_t sq = start;
  size_t i;
  size_t j;
  size_t nc;
  size_t *cand;
  unsigned long steps = 0;
  double key[8];
  int m;
  for (i = 0; i < total; i++) {
    order[i] = (size_t)-1;
  }
  order[sq] = 0;
  scratch[8] = (size_t)-1;
  scratch[9] = sq;
  for (;;) {
    cand = scratch + 10 * depth;
    if (depth + 1 == total) {
      return DS_OK;
    }
    if (cand[8] == (size_t)-1) {
      nc = 0;
      for (m = 0; m < 8; m++) {
        long r2 = (long)(sq / cols) + cb_kdr[m];
        long c2 = (long)(sq % cols) + cb_kdc[m];
        if (r2 >= 0 && c2 >= 0 && r2 < (long)rows && c2 < (long)cols &&
            order[(size_t)r2 * cols + (size_t)c2] == (size_t)-1) {
          size_t v = (size_t)r2 * cols + (size_t)c2;
          size_t deg = cb_knight_degree(rows, cols, order, v);
          double k = (double)deg * 1e6;
          k -= rng != NULL ? (double)(cb_xorshift(rng) % 1000UL)
                           : cb_center_dist(rows, cols, v);
          /* insertion by key */
          j = nc;
          while (j > 0 && key[j - 1] > k) {
            key[j] = key[j - 1];
            cand[j] = cand[j - 1];
            j--;
          }
          key[j] = k;
          cand[j] = v;
          nc++;
        }
      }
      for (i = nc; i < 8; i++) {
        cand[i] = (size_t)-1;
      }
      cand[8] = 0;
    }
    if (cand[8] < 8 && cand[cand[8]] != (size_t)-1) {
      size_t next = cand[cand[8]++];
      if (limit != 0 && ++steps > limit) {
        return DS_STOP;
      }
      order[next] = depth + 1;
      depth++;
      scratch[10 * depth + 8] = (size_t)-1;
      scratch[10 * depth + 9] = next;
      sq = next;
      continue;
    }
    order[sq] = (size_t)-1;
    if (depth == 0) {
      return DS_NOT_FOUND;
    }
    depth--;
    sq = scratch[10 * depth + 9];
  }
}

/* Turn a Hamiltonian path (order[] numbering) into a cycle by Posa
 * rotations: while the end is not a knight's move from the start, pick a
 * neighbour w of the end inside the path and reverse the part after w. */
static bool cb_kt_close(size_t rows, size_t cols, size_t *order, size_t *path,
                        unsigned long *rng) {
  size_t total = rows * cols;
  size_t i;
  size_t end;
  size_t lo;
  size_t hi;
  size_t t;
  size_t nbr[8];
  size_t nn;
  unsigned long iter;
  int m;
  for (i = 0; i < total; i++) {
    path[order[i]] = i;
  }
  for (iter = 0; iter < 2000UL * (unsigned long)total; iter++) {
    end = path[total - 1];
    if (cb_knight_adjacent(cols, end, path[0])) {
      for (i = 0; i < total; i++) {
        order[path[i]] = i;
      }
      return true;
    }
    if ((cb_xorshift(rng) & 15UL) == 0) {
      lo = 0; /* swap the ends */
      hi = total - 1;
      while (lo < hi) {
        t = path[lo];
        path[lo] = path[hi];
        path[hi] = t;
        lo++;
        hi--;
      }
      for (i = 0; i < total; i++) {
        order[path[i]] = i;
      }
      continue;
    }
    nn = 0;
    for (m = 0; m < 8; m++) {
      long r2 = (long)(end / cols) + cb_kdr[m];
      long c2 = (long)(end % cols) + cb_kdc[m];
      if (r2 >= 0 && c2 >= 0 && r2 < (long)rows && c2 < (long)cols) {
        size_t w = (size_t)r2 * cols + (size_t)c2;
        if (order[w] + 2 < total) {
          nbr[nn++] = w;
        }
      }
    }
    if (nn == 0) {
      continue;
    }
    lo = order[nbr[cb_xorshift(rng) % (unsigned long)nn]] + 1;
    hi = total - 1;
    while (lo < hi) {
      t = path[lo];
      path[lo] = path[hi];
      path[hi] = t;
      order[path[lo]] = lo;
      order[path[hi]] = hi;
      lo++;
      hi--;
    }
  }
  return false;
}

ds_status_t ds_knights_tour(size_t rows, size_t cols, size_t start_r,
                            size_t start_c, bool closed, size_t *out_order,
                            size_t *scratch) {
  size_t total;
  size_t i;
  size_t start;
  size_t mn = rows < cols ? rows : cols;
  size_t mx = rows < cols ? cols : rows;
  unsigned long rng = 0x2545F491UL;
  unsigned int attempt;
  ds_status_t s = DS_NOT_FOUND;
  if (out_order == NULL || scratch == NULL) {
    return DS_ERR_NULL;
  }
  if (rows == 0 || cols == 0 || start_r >= rows || start_c >= cols ||
      rows > ((size_t)-1) / 10 / cols) {
    return DS_ERR_RANGE;
  }
  total = rows * cols;
  start = start_r * cols + start_c;
  if (total == 1) {
    out_order[0] = 0;
    return closed ? DS_NOT_FOUND : DS_OK;
  }
  /* boards known to have no tour (Schwenk; Conrad et al.) */
  if (closed && ((total & 1U) || mn == 1 || mn == 2 || mn == 4 ||
                 (mn == 3 && (mx == 4 || mx == 6 || mx == 8)))) {
    return DS_NOT_FOUND;
  }
  if (mn == 1 || mn == 2 || (mn == 3 && (mx == 3 || mx == 5 || mx == 6)) ||
      (mn == 4 && mx == 4)) {
    return DS_NOT_FOUND;
  }
  if ((total & 1U) && ((start_r + start_c) & 1U)) {
    return DS_NOT_FOUND; /* must start on the majority colour */
  }
  /* Bounded Warnsdorff searches with random tie-breaking, then (small
   * boards only) an exhaustive one. */
  for (attempt = 0; attempt < 64; attempt++) {
    s = cb_kt_search(rows, cols, start, out_order, scratch,
                     4UL * (unsigned long)total + 64,
                     attempt == 0 ? NULL : &rng);
    if (s == DS_OK) {
      if (!closed) {
        return DS_OK;
      }
      /* a Hamiltonian cycle may start anywhere: close the path, then
       * renumber from the requested start */
      if (cb_kt_close(rows, cols, out_order, scratch, &rng)) {
        size_t off = out_order[start];
        for (i = 0; i < total; i++) {
          out_order[i] = (out_order[i] + total - off) % total;
        }
        return DS_OK;
      }
    } else if (s == DS_NOT_FOUND) {
      break;
    }
  }
  if (s != DS_NOT_FOUND && total <= 64 && !closed) {
    s = cb_kt_search(rows, cols, start, out_order, scratch, 0, NULL);
  }
  return s == DS_OK && !closed ? DS_OK : DS_NOT_FOUND;
}

void ds_hanoi(unsigned int disks, ds_hanoi_move_func_t move, void *user) {
  unsigned long m;
  unsigned long count;
  unsigned int from;
  unsigned int to;
  unsigned int disk;
  if (move == NULL || disks == 0 || disks >= CB_BITS) {
    return;
  }
  count = (1UL << disks) - 1;
  for (m = 1; m <= count; m++) {
    disk = 0;
    while (((m >> disk) & 1UL) == 0) {
      disk++;
    }
    from = (unsigned int)((m & (m - 1)) % 3);
    to = (unsigned int)(((m | (m - 1)) + 1) % 3);
    if ((disks & 1U) == 0) { /* this formula ends on peg 1 for even n */
      from = from == 0 ? 0 : 3 - from;
      to = to == 0 ? 0 : 3 - to;
    }
    move(disk + 1, from, to, user);
  }
}

ds_status_t ds_collatz(unsigned long n, unsigned long *out_steps,
                       unsigned long *out, size_t capacity) {
  unsigned long steps = 0;
  if (n == 0) {
    return DS_ERR_RANGE;
  }
  for (;;) {
    if (out != NULL && steps < capacity) {
      out[steps] = n;
    }
    if (n == 1) {
      break;
    }
    if (n & 1UL) {
      if (n > (CB_MAX - 1) / 3) {
        return DS_ERR_OVERFLOW;
      }
      n = 3 * n + 1;
    } else {
      n >>= 1;
    }
    steps++;
  }
  if (out_steps != NULL) {
    *out_steps = steps;
  }
  return DS_OK;
}
