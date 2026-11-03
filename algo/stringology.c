#include "stringology.h"
#include <string.h>

#define SG_NONE (~(size_t)0)

static void *sg_alloc(ds_context_t *context, size_t count, size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > (~(size_t)0) / size) {
    return NULL;
  }
  return ds_context_alloc(context, count * size);
}

void ds_prefix_function(const unsigned char *s, size_t n, size_t *out_pi) {
  size_t i;
  size_t k;
  if (n == 0) {
    return;
  }
  out_pi[0] = 0;
  for (i = 1; i < n; i++) {
    k = out_pi[i - 1];
    while (k > 0 && s[i] != s[k]) {
      k = out_pi[k - 1];
    }
    if (s[i] == s[k]) {
      k++;
    }
    out_pi[i] = k;
  }
}

void ds_z_function(const unsigned char *s, size_t n, size_t *out_z) {
  size_t i;
  size_t l = 0;
  size_t r = 0;
  size_t z;
  if (n == 0) {
    return;
  }
  out_z[0] = n;
  for (i = 1; i < n; i++) {
    z = 0;
    if (i < r) {
      z = r - i < out_z[i - l] ? r - i : out_z[i - l];
    }
    while (i + z < n && s[z] == s[i + z]) {
      z++;
    }
    out_z[i] = z;
    if (i + z > r) {
      l = i;
      r = i + z;
    }
  }
}

size_t ds_kmp_search(const unsigned char *text, size_t n,
                     const unsigned char *pattern, size_t m,
                     ds_match_func_t on_match, void *user,
                     ds_context_t *context) {
  size_t *pi;
  size_t i;
  size_t k = 0;
  size_t found = 0;
  if (text == NULL || pattern == NULL || m == 0 || m > n) {
    return 0;
  }
  pi = (size_t *)sg_alloc(context, m, sizeof(size_t));
  if (pi == NULL) {
    return 0;
  }
  ds_prefix_function(pattern, m, pi);
  for (i = 0; i < n; i++) {
    while (k > 0 && text[i] != pattern[k]) {
      k = pi[k - 1];
    }
    if (text[i] == pattern[k]) {
      k++;
    }
    if (k == m) {
      found++;
      if (on_match != NULL && !on_match(i + 1 - m, user)) {
        break;
      }
      k = pi[k - 1];
    }
  }
  ds_context_free(context, pi);
  return found;
}

/* Small modulus keeps every product below 2^32, so this is exact with a
 * 32-bit unsigned long; matches are verified byte by byte anyway. */
#define RK_MOD 65521UL
#define RK_BASE 251UL

size_t ds_rabin_karp_search(const unsigned char *text, size_t n,
                            const unsigned char *pattern, size_t m,
                            ds_match_func_t on_match, void *user) {
  unsigned long hp = 0;
  unsigned long ht = 0;
  unsigned long top = 1;
  size_t i;
  size_t found = 0;
  if (text == NULL || pattern == NULL || m == 0 || m > n) {
    return 0;
  }
  for (i = 0; i + 1 < m; i++) {
    top = top * RK_BASE % RK_MOD;
  }
  for (i = 0; i < m; i++) {
    hp = (hp * RK_BASE + pattern[i]) % RK_MOD;
    ht = (ht * RK_BASE + text[i]) % RK_MOD;
  }
  for (i = 0;; i++) {
    if (hp == ht && memcmp(text + i, pattern, m) == 0) {
      found++;
      if (on_match != NULL && !on_match(i, user)) {
        break;
      }
    }
    if (i + m >= n) {
      break;
    }
    ht = (ht + RK_MOD - text[i] * top % RK_MOD) % RK_MOD;
    ht = (ht * RK_BASE + text[i + m]) % RK_MOD;
  }
  return found;
}

ds_status_t ds_suffix_array(const unsigned char *s, size_t n, size_t *out_sa,
                            ds_context_t *context) {
  size_t *rank;
  size_t *tmp;
  size_t *cnt;
  size_t *swap;
  size_t buckets = n > 256 ? n + 1 : 257;
  size_t classes;
  size_t i;
  size_t k;
  size_t p;
  size_t a1;
  size_t a2;
  size_t b1;
  size_t b2;
  if ((s == NULL || out_sa == NULL) && n > 0) {
    return DS_ERR_NULL;
  }
  if (n == 0) {
    return DS_OK;
  }
  rank = (size_t *)sg_alloc(context, n, sizeof(size_t));
  tmp = (size_t *)sg_alloc(context, n, sizeof(size_t));
  cnt = (size_t *)sg_alloc(context, buckets, sizeof(size_t));
  if (rank == NULL || tmp == NULL || cnt == NULL) {
    ds_context_free(context, rank);
    ds_context_free(context, tmp);
    ds_context_free(context, cnt);
    return DS_ERR_ALLOC;
  }
  /* Round 0: counting sort by the first byte. */
  memset(cnt, 0, 257 * sizeof(size_t));
  for (i = 0; i < n; i++) {
    cnt[s[i] + 1]++;
  }
  for (i = 1; i < 257; i++) {
    cnt[i] += cnt[i - 1];
  }
  for (i = 0; i < n; i++) {
    out_sa[cnt[s[i]]++] = i;
  }
  rank[out_sa[0]] = 0;
  classes = 1;
  for (i = 1; i < n; i++) {
    if (s[out_sa[i]] != s[out_sa[i - 1]]) {
      classes++;
    }
    rank[out_sa[i]] = classes - 1;
  }
  /* Doubling: sort by (rank[i], rank[i + k]), both by counting sort. */
  for (k = 1; k < n && classes < n; k <<= 1) {
    p = 0;
    for (i = n - k; i < n; i++) {
      tmp[p++] = i; /* no second half: smallest second key */
    }
    for (i = 0; i < n; i++) {
      if (out_sa[i] >= k) {
        tmp[p++] = out_sa[i] - k;
      }
    }
    memset(cnt, 0, (classes + 1) * sizeof(size_t));
    for (i = 0; i < n; i++) {
      cnt[rank[tmp[i]] + 1]++;
    }
    for (i = 1; i <= classes; i++) {
      cnt[i] += cnt[i - 1];
    }
    for (i = 0; i < n; i++) {
      out_sa[cnt[rank[tmp[i]]]++] = tmp[i];
    }
    tmp[out_sa[0]] = 0;
    classes = 1;
    for (i = 1; i < n; i++) {
      a1 = rank[out_sa[i]];
      b1 = rank[out_sa[i - 1]];
      a2 = out_sa[i] + k < n ? rank[out_sa[i] + k] + 1 : 0;
      b2 = out_sa[i - 1] + k < n ? rank[out_sa[i - 1] + k] + 1 : 0;
      if (a1 != b1 || a2 != b2) {
        classes++;
      }
      tmp[out_sa[i]] = classes - 1;
    }
    swap = rank;
    rank = tmp;
    tmp = swap;
  }
  ds_context_free(context, rank);
  ds_context_free(context, tmp);
  ds_context_free(context, cnt);
  return DS_OK;
}

ds_status_t ds_lcp_array(const unsigned char *s, size_t n, const size_t *sa,
                         size_t *out_lcp, ds_context_t *context) {
  size_t *rank;
  size_t i;
  size_t j;
  size_t h = 0;
  if (n == 0) {
    return DS_OK;
  }
  if (s == NULL || sa == NULL || out_lcp == NULL) {
    return DS_ERR_NULL;
  }
  rank = (size_t *)sg_alloc(context, n, sizeof(size_t));
  if (rank == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    rank[sa[i]] = i;
  }
  out_lcp[0] = 0;
  for (i = 0; i < n; i++) {
    if (rank[i] == 0) {
      h = 0;
      continue;
    }
    j = sa[rank[i] - 1];
    while (i + h < n && j + h < n && s[i + h] == s[j + h]) {
      h++;
    }
    out_lcp[rank[i]] = h;
    if (h > 0) {
      h--;
    }
  }
  ds_context_free(context, rank);
  return DS_OK;
}

void ds_manacher(const unsigned char *s, size_t n, size_t *out_odd,
                 size_t *out_even, size_t *out_start, size_t *out_length) {
  size_t i;
  size_t l = 0;
  size_t r = 0; /* rightmost palindrome is [l, r) */
  size_t k;
  size_t best_start = 0;
  size_t best_len = n > 0 ? 1 : 0;
  for (i = 0; i < n; i++) {
    k = 1;
    if (i < r) {
      k = out_odd[l + r - 1 - i] < r - i ? out_odd[l + r - 1 - i] : r - i;
    }
    while (i >= k && i + k < n && s[i - k] == s[i + k]) {
      k++;
    }
    out_odd[i] = k;
    if (i + k > r) {
      l = i + 1 - k;
      r = i + k;
    }
    if (2 * k - 1 > best_len) {
      best_len = 2 * k - 1;
      best_start = i + 1 - k;
    }
  }
  l = 0;
  r = 0;
  for (i = 0; i < n; i++) {
    k = 0;
    if (i < r) {
      k = out_even[l + r - i] < r - i ? out_even[l + r - i] : r - i;
    }
    while (i >= k + 1 && i + k < n && s[i - k - 1] == s[i + k]) {
      k++;
    }
    out_even[i] = k;
    if (i + k > r) {
      l = i - k;
      r = i + k;
    }
    if (2 * k > best_len) {
      best_len = 2 * k;
      best_start = i - k;
    }
  }
  if (out_start != NULL) {
    *out_start = best_start;
  }
  if (out_length != NULL) {
    *out_length = best_len;
  }
}

ds_status_t ds_minimal_rotation(const unsigned char *s, size_t n,
                                size_t *out_index, ds_context_t *context) {
  size_t i = 0;
  size_t j;
  size_t k;
  size_t best = 0;
  (void)context; /* Duval's method on the doubled string needs no memory */
  if (out_index == NULL || (s == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  while (i < n) {
    best = i;
    j = i + 1;
    k = i;
    while (j < 2 * n && s[k % n] <= s[j % n]) {
      if (s[k % n] < s[j % n]) {
        k = i;
      } else {
        k++;
      }
      j++;
    }
    while (i <= k) {
      i += j - k;
    }
  }
  *out_index = best;
  return DS_OK;
}

size_t ds_lyndon_factorization(const unsigned char *s, size_t n,
                               bool (*on_factor)(size_t start, size_t length,
                                                 void *user),
                               void *user) {
  size_t i = 0;
  size_t j;
  size_t k;
  size_t factors = 0;
  while (i < n) {
    j = i + 1;
    k = i;
    while (j < n && s[k] <= s[j]) {
      if (s[k] < s[j]) {
        k = i;
      } else {
        k++;
      }
      j++;
    }
    while (i <= k) {
      factors++;
      if (on_factor != NULL && !on_factor(i, j - k, user)) {
        return factors;
      }
      i += j - k;
    }
  }
  return factors;
}

static size_t sg_min3(size_t a, size_t b, size_t c) {
  size_t m = a < b ? a : b;
  return m < c ? m : c;
}

static ds_status_t sg_edit(const unsigned char *a, size_t n,
                           const unsigned char *b, size_t m, bool transpose,
                           size_t *out, ds_context_t *context) {
  size_t *rows;
  size_t *prev2;
  size_t *prev;
  size_t *cur;
  size_t *t;
  size_t i;
  size_t j;
  size_t cost;
  if (out == NULL || (a == NULL && n > 0) || (b == NULL && m > 0)) {
    return DS_ERR_NULL;
  }
  rows = (size_t *)sg_alloc(context, 3 * (m + 1), sizeof(size_t));
  if (rows == NULL) {
    return DS_ERR_ALLOC;
  }
  prev2 = rows;
  prev = rows + (m + 1);
  cur = rows + 2 * (m + 1);
  for (j = 0; j <= m; j++) {
    prev[j] = j;
  }
  for (i = 1; i <= n; i++) {
    cur[0] = i;
    for (j = 1; j <= m; j++) {
      cost = a[i - 1] == b[j - 1] ? 0 : 1;
      cur[j] = sg_min3(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost);
      if (transpose && i > 1 && j > 1 && a[i - 1] == b[j - 2] &&
          a[i - 2] == b[j - 1] && prev2[j - 2] + 1 < cur[j]) {
        cur[j] = prev2[j - 2] + 1;
      }
    }
    t = prev2;
    prev2 = prev;
    prev = cur;
    cur = t;
  }
  *out = prev[m];
  ds_context_free(context, rows);
  return DS_OK;
}

ds_status_t ds_levenshtein(const unsigned char *a, size_t n,
                           const unsigned char *b, size_t m,
                           size_t *out_distance, ds_context_t *context) {
  return sg_edit(a, n, b, m, false, out_distance, context);
}

ds_status_t ds_osa_distance(const unsigned char *a, size_t n,
                            const unsigned char *b, size_t m,
                            size_t *out_distance, ds_context_t *context) {
  return sg_edit(a, n, b, m, true, out_distance, context);
}

ds_status_t ds_lcs_length(const unsigned char *a, size_t n,
                          const unsigned char *b, size_t m, size_t *out_length,
                          ds_context_t *context) {
  size_t *prev;
  size_t *cur;
  size_t *rows;
  size_t *t;
  size_t i;
  size_t j;
  if (out_length == NULL || (a == NULL && n > 0) || (b == NULL && m > 0)) {
    return DS_ERR_NULL;
  }
  rows = (size_t *)sg_alloc(context, 2 * (m + 1), sizeof(size_t));
  if (rows == NULL) {
    return DS_ERR_ALLOC;
  }
  prev = rows;
  cur = rows + (m + 1);
  memset(prev, 0, (m + 1) * sizeof(size_t));
  for (i = 1; i <= n; i++) {
    cur[0] = 0;
    for (j = 1; j <= m; j++) {
      if (a[i - 1] == b[j - 1]) {
        cur[j] = prev[j - 1] + 1;
      } else {
        cur[j] = prev[j] > cur[j - 1] ? prev[j] : cur[j - 1];
      }
    }
    t = prev;
    prev = cur;
    cur = t;
  }
  *out_length = prev[m];
  ds_context_free(context, rows);
  return DS_OK;
}

size_t ds_hamming_distance(const unsigned char *a, const unsigned char *b,
                           size_t n) {
  size_t i;
  size_t d = 0;
  for (i = 0; i < n; i++) {
    d += a[i] != b[i];
  }
  return d;
}
