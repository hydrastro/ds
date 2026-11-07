#include "rice_integral.h"
#include <math.h>

#define RI_PI 3.14159265358979323846

ds_status_t ds_rice_sum(ds_rice_func_t f, void *user, unsigned long a,
                        unsigned long n, double margin, size_t points,
                        double *out) {
  double c;
  double r;
  double lognf = 0.0;
  double sum_re = 0.0;
  double sum_im = 0.0;
  unsigned long j;
  size_t p;
  if (f == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!(margin > 0.0)) {
    margin = 0.5;
  }
  if (a > n || n > (1UL << 24) || !(margin < 1.0)) {
    return DS_ERR_INVALID;
  }
  if (points == 0) {
    points = 128 + 40 * (size_t)(n - a + 1);
  }
  c = ((double)a + (double)n) / 2.0;
  r = ((double)n - (double)a) / 2.0 + margin;
  for (j = 2; j <= n; j++) {
    lognf += log((double)j);
  }
  for (p = 0; p < points; p++) {
    double th = 2.0 * RI_PI * ((double)p + 0.5) / (double)points;
    double ct = cos(th);
    double sn = sin(th);
    double sr = c + r * ct;
    double si = r * sn;
    double logk = lognf;
    double argk = 0.0;
    double fr;
    double fi;
    double mag;
    double kr;
    double ki;
    double gr;
    double gi;
    for (j = 0; j <= n; j++) {
      double dr = sr - (double)j;
      logk -= 0.5 * log(dr * dr + si * si);
      argk -= atan2(si, dr);
    }
    mag = exp(logk);
    kr = mag * cos(argk);
    ki = mag * sin(argk);
    f(sr, si, &fr, &fi, user);
    /* g = f * K * (r e^{i th}) */
    gr = fr * kr - fi * ki;
    gi = fr * ki + fi * kr;
    sum_re += gr * r * ct - gi * r * sn;
    sum_im += gr * r * sn + gi * r * ct;
  }
  sum_re /= (double)points;
  *out = (n % 2 == 0) ? sum_re : -sum_re;
  (void)sum_im;
  return DS_OK;
}

ds_status_t ds_rice_sum_direct(ds_rice_func_t f, void *user, unsigned long a,
                               unsigned long n, double *out) {
  double sum = 0.0;
  double comp = 0.0;
  double binom = 1.0; /* C(n, k) */
  unsigned long k;
  if (f == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (a > n) {
    return DS_ERR_INVALID;
  }
  for (k = 0; k <= n; k++) {
    if (k >= a) {
      double fr;
      double fi;
      double term;
      double t;
      f((double)k, 0.0, &fr, &fi, user);
      term = (k % 2 == 0 ? binom : -binom) * fr;
      t = sum + term;
      comp += fabs(sum) >= fabs(term) ? (sum - t) + term : (term - t) + sum;
      sum = t;
    }
    binom = binom * (double)(n - k) / (double)(k + 1);
  }
  *out = sum + comp;
  return DS_OK;
}
