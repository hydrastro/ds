#include "robust_predicates.h"
#include <string.h>

#define RP_EPS (1.0 / 9007199254740992.0) /* 2^-53 */
#define RP_SPLITTER 134217729.0           /* 2^27 + 1 */
#define RP_NZ(x) ((x) < 0.0 || (x) > 0.0)
#define RP_ABS(x) ((x) < 0.0 ? -(x) : (x))

static double rp_ccw_bound(void) { return (3.0 + 16.0 * RP_EPS) * RP_EPS; }
static double rp_o3d_bound(void) { return (7.0 + 56.0 * RP_EPS) * RP_EPS; }
static double rp_icc_bound(void) { return (10.0 + 96.0 * RP_EPS) * RP_EPS; }
static double rp_isp_bound(void) {
  return 2.0 * (16.0 + 224.0 * RP_EPS) * RP_EPS;
}

void ds_two_sum(double a, double b, double *x, double *y) {
  double s = a + b;
  double bv = s - a;
  double av = s - bv;
  double br = b - bv;
  double ar = a - av;
  *x = s;
  *y = ar + br;
}

void ds_two_diff(double a, double b, double *x, double *y) {
  double s = a - b;
  double bv = a - s;
  double av = s + bv;
  double br = bv - b;
  double ar = a - av;
  *x = s;
  *y = ar + br;
}

static void rp_fast_two_sum(double a, double b, double *x, double *y) {
  double s = a + b;
  double bv = s - a;
  *x = s;
  *y = b - bv;
}

static void rp_split(double a, double *hi, double *lo) {
  double c = RP_SPLITTER * a;
  double big = c - a;
  *hi = c - big;
  *lo = a - *hi;
}

void ds_two_product(double a, double b, double *x, double *y) {
  double p = a * b, ahi, alo, bhi, blo, e1, e2, e3;
  rp_split(a, &ahi, &alo);
  rp_split(b, &bhi, &blo);
  e1 = p - ahi * bhi;
  e2 = e1 - alo * bhi;
  e3 = e2 - ahi * blo;
  *x = p;
  *y = alo * blo - e3;
}

size_t ds_expansion_sum(const double *e, size_t elen, const double *f,
                        size_t flen, double *h) {
  size_t i = 0, j = 0, k = 0;
  double q, nq, hh, now;
  if (elen == 0 && flen == 0) {
    h[0] = 0.0;
    return 1;
  }
  /* take the smaller-magnitude head first */
  if (j == flen || (i < elen && RP_ABS(e[i]) <= RP_ABS(f[j]))) {
    q = e[i++];
  } else {
    q = f[j++];
  }
  while (i < elen || j < flen) {
    if (j == flen || (i < elen && RP_ABS(e[i]) <= RP_ABS(f[j]))) {
      now = e[i++];
    } else {
      now = f[j++];
    }
    ds_two_sum(q, now, &nq, &hh);
    q = nq;
    if (RP_NZ(hh)) {
      h[k++] = hh;
    }
  }
  if (RP_NZ(q) || k == 0) {
    h[k++] = q;
  }
  return k;
}

size_t ds_expansion_scale(const double *e, size_t elen, double b, double *h) {
  size_t i, k = 0;
  double q, hh, p1, p0, s;
  if (elen == 0) {
    h[0] = 0.0;
    return 1;
  }
  ds_two_product(e[0], b, &q, &hh);
  if (RP_NZ(hh)) {
    h[k++] = hh;
  }
  for (i = 1; i < elen; i++) {
    ds_two_product(e[i], b, &p1, &p0);
    ds_two_sum(q, p0, &s, &hh);
    if (RP_NZ(hh)) {
      h[k++] = hh;
    }
    rp_fast_two_sum(p1, s, &q, &hh);
    if (RP_NZ(hh)) {
      h[k++] = hh;
    }
  }
  if (RP_NZ(q) || k == 0) {
    h[k++] = q;
  }
  return k;
}

size_t ds_expansion_product(const double *e, size_t elen, const double *f,
                            size_t flen, double *h, double *scratch) {
  double *tmp, *other, *cur, *nxt, *sw;
  size_t clen, i;
  if (elen == 0 || flen == 0) {
    h[0] = 0.0;
    return 1;
  }
  tmp = scratch;
  other = scratch + 2 * elen;
  /* ping-pong so that the last sum lands in h */
  cur = (flen - 1) % 2 == 0 ? h : other;
  nxt = cur == h ? other : h;
  clen = ds_expansion_scale(e, elen, f[0], cur);
  for (i = 1; i < flen; i++) {
    size_t tlen = ds_expansion_scale(e, elen, f[i], tmp);
    clen = ds_expansion_sum(cur, clen, tmp, tlen, nxt);
    sw = cur;
    cur = nxt;
    nxt = sw;
  }
  return clen;
}

void ds_expansion_negate(const double *e, size_t elen, double *h) {
  size_t i;
  for (i = 0; i < elen; i++) {
    h[i] = -e[i];
  }
}

size_t ds_expansion_compress(const double *e, size_t elen, double *h) {
  size_t bottom, top = 0, i;
  double q, nq, small;
  if (elen == 0) {
    h[0] = 0.0;
    return 1;
  }
  bottom = elen - 1;
  q = e[bottom];
  for (i = elen - 1; i-- > 0;) {
    rp_fast_two_sum(q, e[i], &nq, &small);
    if (RP_NZ(small)) {
      h[bottom--] = nq;
      q = small;
    } else {
      q = nq;
    }
  }
  for (i = bottom + 1; i < elen; i++) {
    rp_fast_two_sum(h[i], q, &nq, &small);
    if (RP_NZ(small)) {
      h[top++] = small;
    }
    q = nq;
  }
  h[top] = q;
  return top + 1;
}

int ds_expansion_sign(const double *e, size_t elen) {
  size_t i = elen;
  while (i-- > 0) {
    if (e[i] > 0.0) {
      return 1;
    }
    if (e[i] < 0.0) {
      return -1;
    }
  }
  return 0;
}

double ds_expansion_estimate(const double *e, size_t elen) {
  size_t i;
  double q = 0.0, top = 0.0;
  int s = ds_expansion_sign(e, elen);
  for (i = 0; i < elen; i++) {
    q += e[i];
    if (RP_NZ(e[i])) {
      top = e[i];
    }
  }
  if ((s > 0 && !(q > 0.0)) || (s < 0 && !(q < 0.0))) {
    return top; /* rounding cancelled the sum: fall back to the head */
  }
  return s == 0 ? 0.0 : q;
}

/* ---- exact helpers ---- */

static size_t rp_diff(double a, double b, double *h) {
  double x, y;
  ds_two_diff(a, b, &x, &y);
  if (RP_NZ(y)) {
    h[0] = y;
    h[1] = x;
    return 2;
  }
  h[0] = x;
  return 1;
}

/* h = a*b - c*d for 2-component inputs; h holds 16 */
static size_t rp_minor2(const double *a, size_t al, const double *b, size_t bl,
                        const double *c, size_t cl, const double *d, size_t dl,
                        double *h) {
  double p[8], q[8], scr[12];
  size_t pl = ds_expansion_product(a, al, b, bl, p, scr);
  size_t ql = ds_expansion_product(c, cl, d, dl, q, scr);
  ds_expansion_negate(q, ql, q);
  return ds_expansion_sum(p, pl, q, ql, h);
}

/* h = x * m (x 2-component, m up to 16) ; h holds 64 */
static size_t rp_scale2(const double *x, size_t xl, const double *m, size_t ml,
                        double neg, double *h) {
  double scr[2 * 16 * 2 + 2 * 16];
  size_t n = ds_expansion_product(m, ml, x, xl, h, scr);
  if (neg < 0.0) {
    ds_expansion_negate(h, n, h);
  }
  return n;
}

double ds_orient2d_exact(const double *a, const double *b, const double *c) {
  double acx[2], acy[2], bcx[2], bcy[2], h[16];
  size_t l1 = rp_diff(a[0], c[0], acx), l2 = rp_diff(a[1], c[1], acy);
  size_t l3 = rp_diff(b[0], c[0], bcx), l4 = rp_diff(b[1], c[1], bcy);
  size_t n = rp_minor2(acx, l1, bcy, l4, acy, l2, bcx, l3, h);
  return ds_expansion_estimate(h, n);
}

double ds_orient2d(const double *a, const double *b, const double *c) {
  double dl = (a[0] - c[0]) * (b[1] - c[1]);
  double dr = (a[1] - c[1]) * (b[0] - c[0]);
  double det = dl - dr, sum;
  if (dl > 0.0) {
    if (!(dr > 0.0)) {
      return det;
    }
    sum = dl + dr;
  } else if (dl < 0.0) {
    if (!(dr < 0.0)) {
      return det;
    }
    sum = -dl - dr;
  } else {
    return ds_orient2d_exact(a, b, c);
  }
  if (det >= rp_ccw_bound() * sum || -det >= rp_ccw_bound() * sum) {
    return det;
  }
  return ds_orient2d_exact(a, b, c);
}

typedef struct rp_d3 {
  double v[3][2];
  size_t n[3];
} rp_d3_t;

static void rp_diff3(const double *p, const double *o, rp_d3_t *d) {
  int i;
  for (i = 0; i < 3; i++) {
    d->n[i] = rp_diff(p[i], o[i], d->v[i]);
  }
}

#define RP_X(d, i) (d).v[i], (d).n[i]

double ds_orient3d_exact(const double *a, const double *b, const double *c,
                         const double *d) {
  rp_d3_t A, B, C;
  double m[16], t1[64], t2[64], t3[64], s12[128], s[192];
  size_t ml, l1, l2, l3, l12, ls;
  rp_diff3(a, d, &A);
  rp_diff3(b, d, &B);
  rp_diff3(c, d, &C);
  ml = rp_minor2(RP_X(B, 1), RP_X(C, 2), RP_X(B, 2), RP_X(C, 1), m);
  l1 = rp_scale2(RP_X(A, 0), m, ml, 1.0, t1);
  ml = rp_minor2(RP_X(C, 1), RP_X(A, 2), RP_X(C, 2), RP_X(A, 1), m);
  l2 = rp_scale2(RP_X(B, 0), m, ml, 1.0, t2);
  ml = rp_minor2(RP_X(A, 1), RP_X(B, 2), RP_X(A, 2), RP_X(B, 1), m);
  l3 = rp_scale2(RP_X(C, 0), m, ml, 1.0, t3);
  l12 = ds_expansion_sum(t1, l1, t2, l2, s12);
  ls = ds_expansion_sum(s12, l12, t3, l3, s);
  return ds_expansion_estimate(s, ls);
}

double ds_orient3d(const double *a, const double *b, const double *c,
                   const double *d) {
  double adx = a[0] - d[0], ady = a[1] - d[1], adz = a[2] - d[2];
  double bdx = b[0] - d[0], bdy = b[1] - d[1], bdz = b[2] - d[2];
  double cdx = c[0] - d[0], cdy = c[1] - d[1], cdz = c[2] - d[2];
  double bdxcdy = bdx * cdy, cdxbdy = cdx * bdy;
  double cdxady = cdx * ady, adxcdy = adx * cdy;
  double adxbdy = adx * bdy, bdxady = bdx * ady;
  double det = adz * (bdxcdy - cdxbdy) + bdz * (cdxady - adxcdy) +
               cdz * (adxbdy - bdxady);
  double perm = (RP_ABS(bdxcdy) + RP_ABS(cdxbdy)) * RP_ABS(adz) +
                (RP_ABS(cdxady) + RP_ABS(adxcdy)) * RP_ABS(bdz) +
                (RP_ABS(adxbdy) + RP_ABS(bdxady)) * RP_ABS(cdz);
  double bound = rp_o3d_bound() * perm;
  if (det > bound || -det > bound) {
    return det;
  }
  return ds_orient3d_exact(a, b, c, d);
}

/* h = x^2 + y^2 for 2-component inputs; h holds 16 */
static size_t rp_lift2(const double *x, size_t xl, const double *y, size_t yl,
                       double *h) {
  double p[8], q[8], scr[12];
  size_t pl = ds_expansion_product(x, xl, x, xl, p, scr);
  size_t ql = ds_expansion_product(y, yl, y, yl, q, scr);
  return ds_expansion_sum(p, pl, q, ql, h);
}

double ds_incircle_exact(const double *a, const double *b, const double *c,
                         const double *d) {
  double ad[2][2], bd[2][2], cd[2][2];
  size_t an[2], bn[2], cn[2];
  double lift[16], m[16], t1[512], t2[512], t3[512], s12[1024], s[1536];
  double scr[2 * 16 * 16 + 2 * 16];
  size_t ll, ml, l1, l2, l3, l12, ls;
  int i;
  for (i = 0; i < 2; i++) {
    an[i] = rp_diff(a[i], d[i], ad[i]);
    bn[i] = rp_diff(b[i], d[i], bd[i]);
    cn[i] = rp_diff(c[i], d[i], cd[i]);
  }
  ll = rp_lift2(ad[0], an[0], ad[1], an[1], lift);
  ml = rp_minor2(bd[0], bn[0], cd[1], cn[1], cd[0], cn[0], bd[1], bn[1], m);
  l1 = ds_expansion_product(m, ml, lift, ll, t1, scr);
  ll = rp_lift2(bd[0], bn[0], bd[1], bn[1], lift);
  ml = rp_minor2(cd[0], cn[0], ad[1], an[1], ad[0], an[0], cd[1], cn[1], m);
  l2 = ds_expansion_product(m, ml, lift, ll, t2, scr);
  ll = rp_lift2(cd[0], cn[0], cd[1], cn[1], lift);
  ml = rp_minor2(ad[0], an[0], bd[1], bn[1], bd[0], bn[0], ad[1], an[1], m);
  l3 = ds_expansion_product(m, ml, lift, ll, t3, scr);
  l12 = ds_expansion_sum(t1, l1, t2, l2, s12);
  ls = ds_expansion_sum(s12, l12, t3, l3, s);
  return ds_expansion_estimate(s, ls);
}

double ds_incircle(const double *a, const double *b, const double *c,
                   const double *d) {
  double adx = a[0] - d[0], ady = a[1] - d[1];
  double bdx = b[0] - d[0], bdy = b[1] - d[1];
  double cdx = c[0] - d[0], cdy = c[1] - d[1];
  double bdxcdy = bdx * cdy, cdxbdy = cdx * bdy;
  double cdxady = cdx * ady, adxcdy = adx * cdy;
  double adxbdy = adx * bdy, bdxady = bdx * ady;
  double alift = adx * adx + ady * ady;
  double blift = bdx * bdx + bdy * bdy;
  double clift = cdx * cdx + cdy * cdy;
  double det = alift * (bdxcdy - cdxbdy) + blift * (cdxady - adxcdy) +
               clift * (adxbdy - bdxady);
  double perm = (RP_ABS(bdxcdy) + RP_ABS(cdxbdy)) * alift +
                (RP_ABS(cdxady) + RP_ABS(adxcdy)) * blift +
                (RP_ABS(adxbdy) + RP_ABS(bdxady)) * clift;
  double bound = rp_icc_bound() * perm;
  if (det > bound || -det > bound) {
    return det;
  }
  return ds_incircle_exact(a, b, c, d);
}

/* 3x3 minor z1*m23 - z2*m13 + z3*m12 style: h = s1*x*m1 + s2*y*m2 + s3*z*m3,
 * h holds 192 */
static size_t rp_minor3(const double *x, size_t xl, const double *m1, size_t l1,
                        double s1, const double *y, size_t yl, const double *m2,
                        size_t l2, double s2, const double *z, size_t zl,
                        const double *m3, size_t l3, double s3, double *h) {
  double t1[64], t2[64], t3[64], s12[128];
  size_t n1 = rp_scale2(x, xl, m1, l1, s1, t1);
  size_t n2 = rp_scale2(y, yl, m2, l2, s2, t2);
  size_t n3 = rp_scale2(z, zl, m3, l3, s3, t3);
  size_t n12 = ds_expansion_sum(t1, n1, t2, n2, s12);
  return ds_expansion_sum(s12, n12, t3, n3, h);
}

/* h = x^2 + y^2 + z^2; h holds 24 */
static size_t rp_lift3(const rp_d3_t *p, double *h) {
  double xy[16], z[8], scr[12];
  size_t a = rp_lift2(p->v[0], p->n[0], p->v[1], p->n[1], xy);
  size_t b = ds_expansion_product(p->v[2], p->n[2], p->v[2], p->n[2], z, scr);
  return ds_expansion_sum(xy, a, z, b, h);
}

static ds_status_t rp_insphere_exact(const double *a, const double *b,
                                     const double *c, const double *d,
                                     const double *e, double *out,
                                     ds_context_t *context) {
  rp_d3_t A, B, C, D;
  double ab[16], bc[16], cd[16], da[16], ac[16], bd[16];
  size_t nab, nbc, ncd, nda, nac, nbd;
  double minor[4][192], lift[4][24];
  size_t mn[4], ln[4], tn[4], tot = 0, scr_need = 0, i;
  double sign[4];
  double *buf, *t[4], *scr, *s12, *s34, *res;
  size_t n12, n34, n;
  rp_diff3(a, e, &A);
  rp_diff3(b, e, &B);
  rp_diff3(c, e, &C);
  rp_diff3(d, e, &D);
  nab = rp_minor2(RP_X(A, 0), RP_X(B, 1), RP_X(B, 0), RP_X(A, 1), ab);
  nbc = rp_minor2(RP_X(B, 0), RP_X(C, 1), RP_X(C, 0), RP_X(B, 1), bc);
  ncd = rp_minor2(RP_X(C, 0), RP_X(D, 1), RP_X(D, 0), RP_X(C, 1), cd);
  nda = rp_minor2(RP_X(D, 0), RP_X(A, 1), RP_X(A, 0), RP_X(D, 1), da);
  nac = rp_minor2(RP_X(A, 0), RP_X(C, 1), RP_X(C, 0), RP_X(A, 1), ac);
  nbd = rp_minor2(RP_X(B, 0), RP_X(D, 1), RP_X(D, 0), RP_X(B, 1), bd);
  /* abc = aez*bc - bez*ac + cez*ab */
  mn[0] = rp_minor3(RP_X(A, 2), bc, nbc, 1.0, RP_X(B, 2), ac, nac, -1.0,
                    RP_X(C, 2), ab, nab, 1.0, minor[0]);
  /* bcd = bez*cd - cez*bd + dez*bc */
  mn[1] = rp_minor3(RP_X(B, 2), cd, ncd, 1.0, RP_X(C, 2), bd, nbd, -1.0,
                    RP_X(D, 2), bc, nbc, 1.0, minor[1]);
  /* cda = cez*da + dez*ac + aez*cd */
  mn[2] = rp_minor3(RP_X(C, 2), da, nda, 1.0, RP_X(D, 2), ac, nac, 1.0,
                    RP_X(A, 2), cd, ncd, 1.0, minor[2]);
  /* dab = dez*ab + aez*bd + bez*da */
  mn[3] = rp_minor3(RP_X(D, 2), ab, nab, 1.0, RP_X(A, 2), bd, nbd, 1.0,
                    RP_X(B, 2), da, nda, 1.0, minor[3]);
  /* det = dlift*abc - clift*dab + blift*cda - alift*bcd: pair minor i
   * with the lift of the point it omits */
  ln[0] = rp_lift3(&D, lift[0]); /* with abc, + */
  ln[1] = rp_lift3(&A, lift[1]); /* with bcd, - */
  ln[2] = rp_lift3(&B, lift[2]); /* with cda, + */
  ln[3] = rp_lift3(&C, lift[3]); /* with dab, - */
  sign[0] = 1.0;
  sign[1] = -1.0;
  sign[2] = 1.0;
  sign[3] = -1.0;
  for (i = 0; i < 4; i++) {
    size_t need;
    mn[i] = ds_expansion_compress(minor[i], mn[i], minor[i]);
    tn[i] = 2 * mn[i] * ln[i];
    tot += tn[i];
    need = 2 * mn[i] * ln[i] + 2 * mn[i];
    if (need > scr_need) {
      scr_need = need;
    }
  }
  buf = (double *)ds_context_alloc(context,
                                   (2 * tot + tot + scr_need) * sizeof(double));
  if (buf == NULL) {
    return DS_ERR_ALLOC;
  }
  t[0] = buf;
  for (i = 1; i < 4; i++) {
    t[i] = t[i - 1] + tn[i - 1];
  }
  s12 = t[3] + tn[3];        /* t0 + t2 */
  s34 = s12 + tn[0] + tn[2]; /* t1 + t3 */
  res = s34 + tn[1] + tn[3];
  scr = res + tot;
  for (i = 0; i < 4; i++) {
    tn[i] = ds_expansion_product(minor[i], mn[i], lift[i], ln[i], t[i], scr);
    if (sign[i] < 0.0) {
      ds_expansion_negate(t[i], tn[i], t[i]);
    }
  }
  n12 = ds_expansion_sum(t[0], tn[0], t[2], tn[2], s12);
  n34 = ds_expansion_sum(t[1], tn[1], t[3], tn[3], s34);
  n = ds_expansion_sum(s12, n12, s34, n34, res);
  *out = ds_expansion_estimate(res, n);
  ds_context_free(context, buf);
  return DS_OK;
}

ds_status_t ds_insphere(const double *a, const double *b, const double *c,
                        const double *d, const double *e, double *out,
                        ds_context_t *context) {
  double aex, aey, aez, bex, bey, bez, cex, cey, cez, dex, dey, dez;
  double ab, bc, cd, da, ac, bd, abc, bcd, cda, dab;
  double abp, bcp, cdp, dap, acp, bdp, abcp, bcdp, cdap, dabp;
  double al, bl, cl, dl, det, perm, bound;
  if (a == NULL || b == NULL || c == NULL || d == NULL || e == NULL ||
      out == NULL) {
    return DS_ERR_NULL;
  }
  aex = a[0] - e[0];
  aey = a[1] - e[1];
  aez = a[2] - e[2];
  bex = b[0] - e[0];
  bey = b[1] - e[1];
  bez = b[2] - e[2];
  cex = c[0] - e[0];
  cey = c[1] - e[1];
  cez = c[2] - e[2];
  dex = d[0] - e[0];
  dey = d[1] - e[1];
  dez = d[2] - e[2];
  ab = aex * bey - bex * aey;
  bc = bex * cey - cex * bey;
  cd = cex * dey - dex * cey;
  da = dex * aey - aex * dey;
  ac = aex * cey - cex * aey;
  bd = bex * dey - dex * bey;
  abp = RP_ABS(aex * bey) + RP_ABS(bex * aey);
  bcp = RP_ABS(bex * cey) + RP_ABS(cex * bey);
  cdp = RP_ABS(cex * dey) + RP_ABS(dex * cey);
  dap = RP_ABS(dex * aey) + RP_ABS(aex * dey);
  acp = RP_ABS(aex * cey) + RP_ABS(cex * aey);
  bdp = RP_ABS(bex * dey) + RP_ABS(dex * bey);
  abc = aez * bc - bez * ac + cez * ab;
  bcd = bez * cd - cez * bd + dez * bc;
  cda = cez * da + dez * ac + aez * cd;
  dab = dez * ab + aez * bd + bez * da;
  abcp = RP_ABS(aez) * bcp + RP_ABS(bez) * acp + RP_ABS(cez) * abp;
  bcdp = RP_ABS(bez) * cdp + RP_ABS(cez) * bdp + RP_ABS(dez) * bcp;
  cdap = RP_ABS(cez) * dap + RP_ABS(dez) * acp + RP_ABS(aez) * cdp;
  dabp = RP_ABS(dez) * abp + RP_ABS(aez) * bdp + RP_ABS(bez) * dap;
  al = aex * aex + aey * aey + aez * aez;
  bl = bex * bex + bey * bey + bez * bez;
  cl = cex * cex + cey * cey + cez * cez;
  dl = dex * dex + dey * dey + dez * dez;
  det = (dl * abc - cl * dab) + (bl * cda - al * bcd);
  perm = dl * abcp + cl * dabp + bl * cdap + al * bcdp;
  bound = rp_isp_bound() * perm;
  if (det > bound || -det > bound) {
    *out = det;
    return DS_OK;
  }
  return rp_insphere_exact(a, b, c, d, e, out, context);
}
