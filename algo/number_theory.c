#include "number_theory.h"
#include "numeric.h"
#include <limits.h>

#define NT_MAX ((unsigned long)-1)
#define NT_BITS ((unsigned int)(CHAR_BIT * sizeof(unsigned long)))

static int nt_mul(unsigned long a, unsigned long b, unsigned long *out) {
  if (a != 0 && b > NT_MAX / a) {
    return 0;
  }
  *out = a * b;
  return 1;
}

static int nt_add(unsigned long a, unsigned long b, unsigned long *out) {
  if (b > NT_MAX - a) {
    return 0;
  }
  *out = a + b;
  return 1;
}

ds_status_t ds_crt(const unsigned long *residues, const unsigned long *moduli,
                   size_t count, unsigned long *out_x,
                   unsigned long *out_modulus) {
  unsigned long x = 0;
  unsigned long m = 1;
  unsigned long g;
  unsigned long a;
  unsigned long mi;
  unsigned long diff;
  unsigned long m_over_g;
  unsigned long mi_over_g;
  unsigned long inv;
  unsigned long t;
  unsigned long lcm;
  size_t i;
  if (out_x == NULL || out_modulus == NULL ||
      (count > 0 && (residues == NULL || moduli == NULL))) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < count; i++) {
    mi = moduli[i];
    if (mi == 0) {
      return DS_ERR_INVALID;
    }
    a = residues[i] % mi;
    g = ds_gcd(m, mi);
    /* need x + m * t = a (mod mi) */
    diff = (a + mi - x % mi) % mi;
    if (diff % g != 0) {
      return DS_NOT_FOUND;
    }
    m_over_g = m / g;
    mi_over_g = mi / g;
    if (!nt_mul(m_over_g, mi, &lcm)) {
      return DS_ERR_OVERFLOW;
    }
    if (mi_over_g == 1) {
      t = 0;
    } else {
      if (ds_modinv(m_over_g % mi_over_g, mi_over_g, &inv) != DS_OK) {
        return DS_ERR_INTERNAL;
      }
      t = ds_mulmod((diff / g) % mi_over_g, inv, mi_over_g);
    }
    /* x += m * t  (< lcm, so it fits) */
    x = x + ds_mulmod(m, t, lcm);
    if (x >= lcm) {
      x -= lcm;
    }
    m = lcm;
  }
  *out_x = x;
  *out_modulus = m;
  return DS_OK;
}

ds_status_t ds_fibonacci(unsigned long n, unsigned long *out) {
  /* F(n) leaves unsigned long within ~94 steps, so iterate */
  unsigned long a = 0;
  unsigned long b = 1;
  unsigned long t;
  unsigned long i;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n; i++) {
    if (!nt_add(a, b, &t)) {
      if (i + 1 == n) {
        a = b; /* F(n) = b still fits */
        break;
      }
      return DS_ERR_OVERFLOW;
    }
    a = b;
    b = t;
  }
  *out = a;
  return DS_OK;
}

unsigned long ds_fibonacci_mod(unsigned long n, unsigned long m) {
  unsigned long a = 0;
  unsigned long b = 1 % (m == 0 ? 1 : m);
  unsigned long c;
  unsigned long d;
  int bit;
  if (m <= 1) {
    return 0;
  }
  for (bit = (int)NT_BITS - 1; bit >= 0; bit--) {
    c = ds_mulmod(a, (2 * (b % m) % m + m - a) % m, m);
    d = (ds_mulmod(a, a, m) + ds_mulmod(b, b, m)) % m;
    if ((n >> bit) & 1UL) {
      a = d;
      b = (c + d) % m;
    } else {
      a = c;
      b = d;
    }
  }
  return a;
}

ds_status_t ds_kbonacci(unsigned int k, unsigned long n, unsigned long *out) {
  unsigned long window[64];
  unsigned long sum;
  unsigned long next;
  unsigned long i;
  unsigned int j;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  if (k < 2 || k > 64) {
    return DS_ERR_RANGE;
  }
  /* F(0..k-2) = 0, F(k-1) = 1 */
  if (n + 1 < (unsigned long)k) {
    *out = 0;
    return DS_OK;
  }
  for (j = 0; j < k; j++) {
    window[j] = j + 1 == k ? 1 : 0;
  }
  sum = 1;
  for (i = (unsigned long)k; i <= n; i++) {
    next = sum;
    /* slide: sum += next - oldest */
    if (!nt_add(sum, next, &sum)) {
      return DS_ERR_OVERFLOW;
    }
    sum -= window[i % k];
    window[i % k] = next;
  }
  *out = window[n % k];
  return DS_OK;
}

ds_status_t ds_ackermann(unsigned long m, unsigned long n, unsigned long *out) {
  unsigned long stack[256];
  size_t sp = 0;
  unsigned long steps = 0;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  /* closed forms keep the common cases instant */
  if (m == 0) {
    return nt_add(n, 1, out) ? DS_OK : DS_ERR_OVERFLOW;
  }
  if (m == 1) {
    return nt_add(n, 2, out) ? DS_OK : DS_ERR_OVERFLOW;
  }
  if (m == 2) {
    if (n > (NT_MAX - 3) / 2) {
      return DS_ERR_OVERFLOW;
    }
    *out = 2 * n + 3;
    return DS_OK;
  }
  if (m == 3) {
    if (n + 3 >= NT_BITS) {
      return DS_ERR_OVERFLOW;
    }
    *out = (1UL << (n + 3)) - 3;
    return DS_OK;
  }
  /* general case: iterate A(m, n) with an explicit stack of m's, using the
   * closed form whenever m <= 3 */
  stack[sp++] = m;
  while (sp > 0) {
    m = stack[--sp];
    if (++steps > 100000UL) {
      return DS_ERR_OVERFLOW;
    }
    if (m <= 3) {
      ds_status_t s = ds_ackermann(m, n, &n);
      if (s != DS_OK) {
        return s;
      }
    } else if (n == 0) {
      n = 1;
      stack[sp++] = m - 1;
    } else {
      if (sp + 2 > sizeof stack / sizeof stack[0]) {
        return DS_ERR_OVERFLOW;
      }
      stack[sp++] = m - 1;
      stack[sp++] = m;
      n--;
    }
  }
  *out = n;
  return DS_OK;
}

unsigned int ds_inverse_ackermann(unsigned long n) {
  /* A(0,0) = 1, A(1,1) = 3, A(2,2) = 7, A(3,3) = 61, A(4,4) is astronomic */
  if (n <= 1) {
    return 0;
  }
  if (n <= 3) {
    return 1;
  }
  if (n <= 7) {
    return 2;
  }
  if (n <= 61) {
    return 3;
  }
  return 4;
}

static long nt_floor_div(long a, long b) {
  long q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) {
    q--;
  }
  return q;
}

static long nt_mod(long a, long b) {
  long r = a % b;
  return r < 0 ? r + b : r;
}

static int nt_leap(long y) {
  return (nt_mod(y, 4) == 0 && nt_mod(y, 100) != 0) || nt_mod(y, 400) == 0;
}

int ds_day_of_week_doomsday(long year, int month, int day) {
  /* doomsday of each month (non-leap / leap January and February) */
  static const int dd[12] = {3, 28, 14, 4, 9, 6, 11, 8, 5, 10, 7, 12};
  long century = nt_floor_div(year, 100);
  long y = nt_mod(year, 100);
  long anchor = nt_mod(5 * nt_mod(century, 4) + 2, 7); /* Tuesday = 2 */
  long doomsday = nt_mod(anchor + y / 12 + y % 12 + (y % 12) / 4, 7);
  long ref = dd[(month - 1) % 12];
  if (nt_leap(year) && (month == 1 || month == 2)) {
    ref++;
  }
  return (int)nt_mod(doomsday + (long)day - ref, 7);
}

int ds_day_of_week_zeller(long year, int month, int day) {
  long m = month;
  long y = year;
  long k;
  long j;
  long h;
  if (m < 3) {
    m += 12;
    y -= 1;
  }
  k = nt_mod(y, 100);
  j = nt_floor_div(y, 100);
  /* h: 0 = Saturday, 1 = Sunday, ... */
  h = nt_mod((long)day + (13 * (m + 1)) / 5 + k + k / 4 + nt_floor_div(j, 4) +
                 5 * j,
             7);
  return (int)nt_mod(h + 6, 7);
}
