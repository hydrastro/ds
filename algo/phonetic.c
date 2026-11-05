#include "phonetic.h"
#include <string.h>

#define PH_PAD 6

static int ph_alpha(unsigned char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static unsigned char ph_upper(unsigned char c) {
  return (unsigned char)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
}

static unsigned char ph_lower(unsigned char c) {
  return (unsigned char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
}

static int ph_vowel(unsigned char c) {
  return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U';
}

static void *ph_alloc(ds_context_t *ctx, size_t n) {
  if (n > ((size_t)-1) / 4 - 64) {
    return NULL;
  }
  return ds_context_alloc(ctx, 4 * n + 64);
}

/* Copy key into out (truncated to max_length when nonzero). */
static ds_status_t ph_emit(const unsigned char *key, size_t len,
                           size_t max_length, unsigned char *out,
                           size_t capacity, size_t *out_length) {
  if (max_length > 0 && len > max_length) {
    len = max_length;
  }
  *out_length = len;
  if (len > capacity) {
    return DS_ERR_OVERFLOW;
  }
  if (len > 0) {
    if (out == NULL) {
      return DS_ERR_NULL;
    }
    memcpy(out, key, len);
  }
  return DS_OK;
}

/* Uppercase letters of name into buf; returns their count. */
static size_t ph_letters(const unsigned char *name, size_t n,
                         unsigned char *buf) {
  size_t i;
  size_t k = 0;
  for (i = 0; i < n; i++) {
    if (ph_alpha(name[i])) {
      buf[k++] = ph_upper(name[i]);
    }
  }
  return k;
}

/* ---- Soundex -------------------------------------------------------- */

static char sx_code(unsigned char c) {
  switch (c) {
  case 'B':
  case 'F':
  case 'P':
  case 'V':
    return '1';
  case 'C':
  case 'G':
  case 'J':
  case 'K':
  case 'Q':
  case 'S':
  case 'X':
  case 'Z':
    return '2';
  case 'D':
  case 'T':
    return '3';
  case 'L':
    return '4';
  case 'M':
  case 'N':
    return '5';
  case 'R':
    return '6';
  case 'H':
  case 'W':
    return 'h'; /* transparent */
  default:
    return '0'; /* vowels separate */
  }
}

ds_status_t ds_soundex(const unsigned char *name, size_t n, unsigned char *out,
                       size_t capacity, size_t *out_length) {
  unsigned char key[4];
  size_t i;
  size_t k = 0;
  char last;
  char c;
  unsigned char u;
  if ((name == NULL && n > 0) || out_length == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n && !ph_alpha(name[i]); i++) {
  }
  if (i == n) {
    *out_length = 0;
    return DS_OK;
  }
  u = ph_upper(name[i]);
  key[k++] = u;
  last = sx_code(u);
  for (i++; i < n && k < 4; i++) {
    if (!ph_alpha(name[i])) {
      continue;
    }
    c = sx_code(ph_upper(name[i]));
    if (c == 'h') {
      continue;
    }
    if (c != '0' && c != last) {
      key[k++] = (unsigned char)c;
    }
    last = c;
  }
  while (k < 4) {
    key[k++] = '0';
  }
  return ph_emit(key, 4, 0, out, capacity, out_length);
}

/* ---- NYSIIS --------------------------------------------------------- */

static int ph_starts(const unsigned char *w, size_t len, const char *p) {
  size_t m = strlen(p);
  return len >= m && memcmp(w, p, m) == 0;
}

static int ph_ends(const unsigned char *w, size_t len, const char *p) {
  size_t m = strlen(p);
  return len >= m && memcmp(w + len - m, p, m) == 0;
}

static int ph_at(const unsigned char *w, size_t len, size_t i, const char *p) {
  size_t m = strlen(p);
  return i + m <= len && memcmp(w + i, p, m) == 0;
}

ds_status_t ds_nysiis(const unsigned char *name, size_t n, size_t max_length,
                      unsigned char *out, size_t capacity, size_t *out_length,
                      ds_context_t *context) {
  unsigned char *w;
  unsigned char *key;
  size_t len;
  size_t klen = 0;
  size_t i;
  size_t end;
  size_t skip = 0;
  size_t j;
  size_t r;
  ds_status_t st;
  if ((name == NULL && n > 0) || out_length == NULL) {
    return DS_ERR_NULL;
  }
  w = (unsigned char *)ph_alloc(context, n);
  if (w == NULL) {
    return DS_ERR_ALLOC;
  }
  key = w + n + PH_PAD;
  len = ph_letters(name, n, w);
  if (len == 0) {
    ds_context_free(context, w);
    *out_length = 0;
    return DS_OK;
  }
  if (ph_starts(w, len, "MAC")) {
    w[1] = 'C';
  } else if (ph_starts(w, len, "KN")) {
    w[0] = 'N';
  } else if (ph_starts(w, len, "K")) {
    w[0] = 'C';
  } else if (ph_starts(w, len, "PH") || ph_starts(w, len, "PF")) {
    w[0] = 'F';
    w[1] = 'F';
  } else if (ph_starts(w, len, "SCH")) {
    w[1] = 'S';
    w[2] = 'S';
  }
  if (ph_ends(w, len, "EE") || ph_ends(w, len, "IE")) {
    w[len - 2] = 'Y';
    len--;
  } else if (ph_ends(w, len, "DT") || ph_ends(w, len, "RT") ||
             ph_ends(w, len, "RD") || ph_ends(w, len, "NT") ||
             ph_ends(w, len, "ND")) {
    w[len - 2] = 'D';
    len--;
  }
  key[klen++] = w[0];
  end = len; /* the loop bound is fixed before the word shrinks */
  for (i = 1; i < end; i++) {
    if (i >= len) {
      continue;
    }
    if (skip > 0) {
      skip--;
      continue;
    }
    if (ph_at(w, len, i, "EV")) {
      w[i] = 'A';
      w[i + 1] = 'F';
      skip = 1;
    } else if (ph_vowel(w[i])) {
      w[i] = 'A';
    } else if (w[i] == 'Q') {
      w[i] = 'G';
    } else if (w[i] == 'Z') {
      w[i] = 'S';
    } else if (w[i] == 'M') {
      w[i] = 'N';
    } else if (ph_at(w, len, i, "KN")) {
      memmove(w + i, w + i + 1, len - i - 1);
      len--;
      w[i] = 'N';
    } else if (w[i] == 'K') {
      w[i] = 'C';
    } else if (ph_at(w, len, i, "SCH")) {
      w[i + 1] = 'S';
      w[i + 2] = 'S';
      skip = 2;
    } else if (ph_at(w, len, i, "SH")) {
      w[i + 1] = 'S';
      skip = 1;
    } else if (ph_at(w, len, i, "PH")) {
      w[i] = 'F';
      w[i + 1] = 'F';
      skip = 1;
    } else if (w[i] == 'H' &&
               (!ph_vowel(w[i - 1]) || i + 1 >= len || !ph_vowel(w[i + 1]))) {
      w[i] = w[i - 1];
    } else if (w[i] == 'W' && ph_vowel(w[i - 1])) {
      w[i] = w[i - 1];
    }
    if (skip == 0) {
      if (w[i] != key[klen - 1]) {
        key[klen++] = w[i];
      }
    } else {
      for (j = i; j <= i + skip && j < len; j++) {
        key[klen++] = w[j];
      }
    }
  }
  /* collapse repeats */
  r = 1;
  for (j = 1; j < klen; j++) {
    if (key[j] != key[r - 1]) {
      key[r++] = key[j];
    }
  }
  klen = r;
  if (klen > 0 && key[klen - 1] == 'S') {
    klen--;
  }
  if (klen >= 2 && key[klen - 2] == 'A' && key[klen - 1] == 'Y') {
    key[klen - 2] = 'Y';
    klen--;
  }
  if (klen > 0 && key[klen - 1] == 'A') {
    klen--;
  }
  st = ph_emit(key, klen, max_length, out, capacity, out_length);
  ds_context_free(context, w);
  return st;
}

/* ---- Metaphone ------------------------------------------------------ */

static int ph_frontv(unsigned char c) {
  return c == 'E' || c == 'I' || c == 'Y';
}

ds_status_t ds_metaphone(const unsigned char *name, size_t n, size_t max_length,
                         unsigned char *out, size_t capacity,
                         size_t *out_length, ds_context_t *context) {
  unsigned char *e;
  unsigned char *m;
  size_t len;
  size_t k = 0;
  size_t i;
  size_t el;
  unsigned char c;
  unsigned char nx;
  unsigned char n2;
  unsigned char pv;
  ds_status_t st;
  if ((name == NULL && n > 0) || out_length == NULL) {
    return DS_ERR_NULL;
  }
  e = (unsigned char *)ph_alloc(context, n);
  if (e == NULL) {
    return DS_ERR_ALLOC;
  }
  m = e + n + PH_PAD;
  len = ph_letters(name, n, e);
  if (len >= 2 && (ph_starts(e, len, "PN") || ph_starts(e, len, "AE") ||
                   ph_starts(e, len, "KN") || ph_starts(e, len, "GN") ||
                   ph_starts(e, len, "WR"))) {
    memmove(e, e + 1, len - 1);
    len--;
  } else if (len >= 1 && e[0] == 'X') {
    e[0] = 'S';
  } else if (ph_starts(e, len, "WH")) {
    memmove(e + 1, e + 2, len - 2);
    len--;
  }
  memset(e + len, 0, PH_PAD);
  el = len > 0 ? len - 1 : 0;
  for (i = 0; i < len; i++) {
    if (max_length > 0 && k >= max_length) {
      break;
    }
    c = e[i];
    nx = e[i + 1];
    n2 = nx ? e[i + 2] : 0;
    pv = i > 0 ? e[i - 1] : 0;
    if (c != 'G' && c != 'T' && i > 0 && pv == c) {
      continue;
    }
    if (ph_vowel(c)) {
      if (i == 0) {
        m[k++] = c;
      }
      continue;
    }
    switch (c) {
    case 'B':
      if (i != el || (i > 0 ? pv : e[el]) != 'M') {
        m[k++] = 'B';
      }
      break;
    case 'C':
      if (!(i > 0 && pv == 'S' && ph_frontv(nx))) {
        if (nx == 'I' && n2 == 'A') {
          m[k++] = 'X';
        } else if (ph_frontv(nx)) {
          m[k++] = 'S';
        } else if (i > 0 && pv == 'S' && nx == 'H') {
          m[k++] = 'K';
        } else if (nx == 'H') {
          m[k++] = (i == 0 && i + 1 < el && !ph_vowel(n2)) ? 'K' : 'X';
        } else {
          m[k++] = 'K';
        }
      }
      break;
    case 'D':
      m[k++] = (nx == 'G' && ph_frontv(n2)) ? 'J' : 'T';
      break;
    case 'G':
      if (nx == 'H' && i + 1 != el && ph_vowel(n2)) {
        break;
      }
      if (i > 0 &&
          ((i + 1 == el && nx == 'N') ||
           (i + 3 == el && nx == 'N' && n2 == 'E' && e[i + 3] == 'D'))) {
        break;
      }
      if (i > 1 && i + 1 <= el && pv == 'D' && ph_frontv(nx)) {
        break;
      }
      if (nx == 'G') {
        break;
      }
      if (ph_frontv(nx)) {
        m[k++] = (i == 0 || pv != 'G') ? 'J' : 'K';
      } else {
        m[k++] = 'K';
      }
      break;
    case 'H':
      if (i > 0 && ph_vowel(pv) && !ph_vowel(nx)) {
        break;
      }
      if (i > 0 &&
          (pv == 'C' || pv == 'G' || pv == 'P' || pv == 'S' || pv == 'T')) {
        break;
      }
      m[k++] = 'H';
      break;
    case 'F':
    case 'J':
    case 'L':
    case 'M':
    case 'N':
    case 'R':
      m[k++] = c;
      break;
    case 'K':
      if (!(i > 0 && pv == 'C')) {
        m[k++] = 'K';
      }
      break;
    case 'P':
      m[k++] = nx == 'H' ? 'F' : 'P';
      break;
    case 'Q':
      m[k++] = 'K';
      break;
    case 'S':
      if (i > 0 && i + 2 <= el && nx == 'I' && (n2 == 'O' || n2 == 'A')) {
        m[k++] = 'X';
      } else if (nx == 'H') {
        m[k++] = 'X';
      } else {
        m[k++] = 'S';
      }
      break;
    case 'T':
      if (i > 0 && i + 2 <= el && nx == 'I' && (n2 == 'A' || n2 == 'O')) {
        m[k++] = 'X';
      } else if (nx == 'H') {
        m[k++] = '0';
      } else if (!(nx == 'C' && n2 == 'H')) {
        if (pv != 'T') {
          m[k++] = 'T';
        }
      }
      break;
    case 'V':
      m[k++] = 'F';
      break;
    case 'W':
    case 'Y':
      if (ph_vowel(nx)) {
        m[k++] = c;
      }
      break;
    case 'X':
      m[k++] = 'K';
      m[k++] = 'S';
      break;
    case 'Z':
      m[k++] = 'S';
      break;
    default:
      break;
    }
  }
  st = ph_emit(m, k, max_length, out, capacity, out_length);
  ds_context_free(context, e);
  return st;
}

/* ---- Double Metaphone ----------------------------------------------- */

typedef struct dm {
  const unsigned char *w; /* uppercase, padded with spaces */
  size_t length;
  size_t total; /* with padding */
  unsigned char *p;
  unsigned char *s;
  size_t pl;
  size_t sl;
  int slavo;
} dm_t;

static unsigned char dm_get(const dm_t *d, long at) {
  if (at < 0 || (size_t)at >= d->total) {
    return 0;
  }
  return d->w[at];
}

static int dm_vowel(const dm_t *d, long at) {
  unsigned char c;
  if (at < 0 || (size_t)at >= d->length) {
    return 0;
  }
  c = d->w[at];
  return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' || c == 'Y';
}

/* StringAt(start, length, list...): list is a '|'-separated set of
 * strings of that length. */
static int dm_at(const dm_t *d, long start, size_t length, const char *list) {
  const char *p = list;
  if (start < 0) {
    return 0;
  }
  while (*p) {
    if ((size_t)start + length <= d->total &&
        memcmp(d->w + start, p, length) == 0) {
      return 1;
    }
    p += length;
    if (*p == '|') {
      p++;
    }
  }
  return 0;
}

static void dm_add2(dm_t *d, const char *main, const char *alt) {
  size_t i;
  for (i = 0; main[i]; i++) {
    d->p[d->pl++] = (unsigned char)main[i];
  }
  if (alt[0] == ' ') {
    return;
  }
  for (i = 0; alt[i]; i++) {
    d->s[d->sl++] = (unsigned char)alt[i];
  }
}

static void dm_add(dm_t *d, const char *both) {
  dm_add2(d, both, both);
}

static void dm_run(dm_t *d) {
  long cur = 0;
  long last = (long)d->length - 1;
  long len = (long)d->length;
  unsigned char c;
  if (dm_at(d, 0, 2, "GN|KN|PN|WR|PS")) {
    cur = 1;
  }
  if (dm_get(d, 0) == 'X') {
    dm_add(d, "S");
    cur = 1;
  }
  while (cur < len) {
    c = dm_get(d, cur);
    switch (c) {
    case 'A':
    case 'E':
    case 'I':
    case 'O':
    case 'U':
    case 'Y':
      if (cur == 0) {
        dm_add(d, "A");
      }
      cur += 1;
      break;
    case 'B':
      dm_add(d, "P");
      cur += dm_get(d, cur + 1) == 'B' ? 2 : 1;
      break;
    case 'C':
      if (cur > 1 && !dm_vowel(d, cur - 2) && dm_at(d, cur - 1, 3, "ACH") &&
          dm_get(d, cur + 2) != 'I' &&
          (dm_get(d, cur + 2) != 'E' ||
           dm_at(d, cur - 2, 6, "BACHER|MACHER"))) {
        dm_add(d, "K");
        cur += 2;
        break;
      }
      if (cur == 0 && dm_at(d, cur, 6, "CAESAR")) {
        dm_add(d, "S");
        cur += 2;
        break;
      }
      if (dm_at(d, cur, 4, "CHIA")) {
        dm_add(d, "K");
        cur += 2;
        break;
      }
      if (dm_at(d, cur, 2, "CH")) {
        if (cur > 0 && dm_at(d, cur, 4, "CHAE")) {
          dm_add2(d, "K", "X");
          cur += 2;
          break;
        }
        if (cur == 0 &&
            (dm_at(d, cur + 1, 5, "HARAC|HARIS") ||
             dm_at(d, cur + 1, 3, "HOR|HYM|HIA|HEM")) &&
            !dm_at(d, 0, 5, "CHORE")) {
          dm_add(d, "K");
          cur += 2;
          break;
        }
        if ((dm_at(d, 0, 4, "VAN |VON ") || dm_at(d, 0, 3, "SCH")) ||
            dm_at(d, cur - 2, 6, "ORCHES|ARCHIT|ORCHID") ||
            dm_at(d, cur + 2, 1, "T|S") ||
            ((dm_at(d, cur - 1, 1, "A|O|U|E") || cur == 0) &&
             dm_at(d, cur + 2, 1, "L|R|N|M|B|H|F|V|W| "))) {
          dm_add(d, "K");
        } else if (cur > 0) {
          if (dm_at(d, 0, 2, "MC")) {
            dm_add(d, "K");
          } else {
            dm_add2(d, "X", "K");
          }
        } else {
          dm_add(d, "X");
        }
        cur += 2;
        break;
      }
      if (dm_at(d, cur, 2, "CZ") && !dm_at(d, cur - 2, 4, "WICZ")) {
        dm_add2(d, "S", "X");
        cur += 2;
        break;
      }
      if (dm_at(d, cur + 1, 3, "CIA")) {
        dm_add(d, "X");
        cur += 3;
        break;
      }
      if (dm_at(d, cur, 2, "CC") && !(cur == 1 && dm_get(d, 0) == 'M')) {
        if (dm_at(d, cur + 2, 1, "I|E|H") && !dm_at(d, cur + 2, 2, "HU")) {
          if ((cur == 1 && dm_get(d, cur - 1) == 'A') ||
              dm_at(d, cur - 1, 5, "UCCEE|UCCES")) {
            dm_add(d, "KS");
          } else {
            dm_add(d, "X");
          }
          cur += 3;
          break;
        }
        dm_add(d, "K");
        cur += 2;
        break;
      }
      if (dm_at(d, cur, 2, "CK|CG|CQ")) {
        dm_add(d, "K");
        cur += 2;
        break;
      }
      if (dm_at(d, cur, 2, "CI|CE|CY")) {
        if (dm_at(d, cur, 3, "CIO|CIE|CIA")) {
          dm_add2(d, "S", "X");
        } else {
          dm_add(d, "S");
        }
        cur += 2;
        break;
      }
      dm_add(d, "K");
      if (dm_at(d, cur + 1, 2, " C| Q| G")) {
        cur += 3;
      } else if (dm_at(d, cur + 1, 1, "C|K|Q") &&
                 !dm_at(d, cur + 1, 2, "CE|CI")) {
        cur += 2;
      } else {
        cur += 1;
      }
      break;
    case 'D':
      if (dm_at(d, cur, 2, "DG")) {
        if (dm_at(d, cur + 2, 1, "I|E|Y")) {
          dm_add(d, "J");
          cur += 3;
        } else {
          dm_add(d, "TK");
          cur += 2;
        }
        break;
      }
      dm_add(d, "T");
      cur += dm_at(d, cur, 2, "DT|DD") ? 2 : 1;
      break;
    case 'F':
      dm_add(d, "F");
      cur += dm_get(d, cur + 1) == 'F' ? 2 : 1;
      break;
    case 'G':
      if (dm_get(d, cur + 1) == 'H') {
        if (cur > 0 && !dm_vowel(d, cur - 1)) {
          dm_add(d, "K");
          cur += 2;
          break;
        }
        if (cur < 3 && cur == 0) {
          dm_add(d, dm_get(d, cur + 2) == 'I' ? "J" : "K");
          cur += 2;
          break;
        }
        if ((cur > 1 && dm_at(d, cur - 2, 1, "B|H|D")) ||
            (cur > 2 && dm_at(d, cur - 3, 1, "B|H|D")) ||
            (cur > 3 && dm_at(d, cur - 4, 1, "B|H"))) {
          cur += 2;
          break;
        }
        if (cur > 2 && dm_get(d, cur - 1) == 'U' &&
            dm_at(d, cur - 3, 1, "C|G|L|R|T")) {
          dm_add(d, "F");
        } else if (cur > 0 && dm_get(d, cur - 1) != 'I') {
          dm_add(d, "K");
        }
        cur += 2;
        break;
      }
      if (dm_get(d, cur + 1) == 'N') {
        if (cur == 1 && dm_vowel(d, 0) && !d->slavo) {
          dm_add2(d, "KN", "N");
        } else if (!dm_at(d, cur + 2, 2, "EY") && dm_get(d, cur + 1) != 'Y' &&
                   !d->slavo) {
          dm_add2(d, "N", "KN");
        } else {
          dm_add(d, "KN");
        }
        cur += 2;
        break;
      }
      if (dm_at(d, cur + 1, 2, "LI") && !d->slavo) {
        dm_add2(d, "KL", "L");
        cur += 2;
        break;
      }
      if (cur == 0 &&
          (dm_get(d, cur + 1) == 'Y' ||
           dm_at(d, cur + 1, 2, "ES|EP|EB|EL|EY|IB|IL|IN|IE|EI|ER"))) {
        dm_add2(d, "K", "J");
        cur += 2;
        break;
      }
      if ((dm_at(d, cur + 1, 2, "ER") || dm_get(d, cur + 1) == 'Y') &&
          !dm_at(d, 0, 6, "DANGER|RANGER|MANGER") &&
          !dm_at(d, cur - 1, 1, "E|I") && !dm_at(d, cur - 1, 3, "RGY|OGY")) {
        dm_add2(d, "K", "J");
        cur += 2;
        break;
      }
      if (dm_at(d, cur + 1, 1, "E|I|Y") || dm_at(d, cur - 1, 4, "AGGI|OGGI")) {
        if (dm_at(d, 0, 4, "VAN |VON ") || dm_at(d, 0, 3, "SCH") ||
            dm_at(d, cur + 1, 2, "ET")) {
          dm_add(d, "K");
        } else if (dm_at(d, cur + 1, 4, "IER ")) {
          dm_add(d, "J");
        } else {
          dm_add2(d, "J", "K");
        }
        cur += 2;
        break;
      }
      dm_add(d, "K");
      cur += dm_get(d, cur + 1) == 'G' ? 2 : 1;
      break;
    case 'H':
      if ((cur == 0 || dm_vowel(d, cur - 1)) && dm_vowel(d, cur + 1)) {
        dm_add(d, "H");
        cur += 2;
      } else {
        cur += 1;
      }
      break;
    case 'J':
      if (dm_at(d, cur, 4, "JOSE") || dm_at(d, 0, 4, "SAN ")) {
        if ((cur == 0 && dm_get(d, cur + 4) == ' ') || dm_at(d, 0, 4, "SAN ")) {
          dm_add(d, "H");
        } else {
          dm_add2(d, "J", "H");
        }
        cur += 1;
        break;
      }
      if (cur == 0 && !dm_at(d, cur, 4, "JOSE")) {
        dm_add2(d, "J", "A");
      } else if (dm_vowel(d, cur - 1) && !d->slavo &&
                 (dm_get(d, cur + 1) == 'A' || dm_get(d, cur + 1) == 'O')) {
        dm_add2(d, "J", "H");
      } else if (cur == last) {
        dm_add2(d, "J", " ");
      } else if (!dm_at(d, cur + 1, 1, "L|T|K|S|N|M|B|Z") &&
                 !dm_at(d, cur - 1, 1, "S|K|L")) {
        dm_add(d, "J");
      }
      cur += dm_get(d, cur + 1) == 'J' ? 2 : 1;
      break;
    case 'K':
      dm_add(d, "K");
      cur += dm_get(d, cur + 1) == 'K' ? 2 : 1;
      break;
    case 'L':
      if (dm_get(d, cur + 1) == 'L') {
        if ((cur == len - 3 && dm_at(d, cur - 1, 4, "ILLO|ILLA|ALLE")) ||
            ((dm_at(d, last - 1, 2, "AS|OS") || dm_at(d, last, 1, "A|O")) &&
             dm_at(d, cur - 1, 4, "ALLE"))) {
          dm_add2(d, "L", " ");
          cur += 2;
          break;
        }
        cur += 2;
      } else {
        cur += 1;
      }
      dm_add(d, "L");
      break;
    case 'M':
      if ((dm_at(d, cur - 1, 3, "UMB") &&
           (cur + 1 == last || dm_at(d, cur + 2, 2, "ER"))) ||
          dm_get(d, cur + 1) == 'M') {
        cur += 2;
      } else {
        cur += 1;
      }
      dm_add(d, "M");
      break;
    case 'N':
      dm_add(d, "N");
      cur += dm_get(d, cur + 1) == 'N' ? 2 : 1;
      break;
    case 'P':
      if (dm_get(d, cur + 1) == 'H') {
        dm_add(d, "F");
        cur += 2;
        break;
      }
      cur += dm_at(d, cur + 1, 1, "P|B") ? 2 : 1;
      dm_add(d, "P");
      break;
    case 'Q':
      dm_add(d, "K");
      cur += dm_get(d, cur + 1) == 'Q' ? 2 : 1;
      break;
    case 'R':
      if (cur == last && !d->slavo && dm_at(d, cur - 2, 2, "IE") &&
          !dm_at(d, cur - 4, 2, "ME|MA")) {
        dm_add2(d, "", "R");
      } else {
        dm_add(d, "R");
      }
      cur += dm_get(d, cur + 1) == 'R' ? 2 : 1;
      break;
    case 'S':
      if (dm_at(d, cur - 1, 3, "ISL|YSL")) {
        cur += 1;
        break;
      }
      if (cur == 0 && dm_at(d, cur, 5, "SUGAR")) {
        dm_add2(d, "X", "S");
        cur += 1;
        break;
      }
      if (dm_at(d, cur, 2, "SH")) {
        if (dm_at(d, cur + 1, 4, "HEIM|HOEK|HOLM|HOLZ")) {
          dm_add(d, "S");
        } else {
          dm_add(d, "X");
        }
        cur += 2;
        break;
      }
      if (dm_at(d, cur, 3, "SIO|SIA") || dm_at(d, cur, 4, "SIAN")) {
        if (!d->slavo) {
          dm_add2(d, "S", "X");
        } else {
          dm_add(d, "S");
        }
        cur += 3;
        break;
      }
      if ((cur == 0 && dm_at(d, cur + 1, 1, "M|N|L|W")) ||
          dm_at(d, cur + 1, 1, "Z")) {
        dm_add2(d, "S", "X");
        cur += dm_at(d, cur + 1, 1, "Z") ? 2 : 1;
        break;
      }
      if (dm_at(d, cur, 2, "SC")) {
        if (dm_get(d, cur + 2) == 'H') {
          if (dm_at(d, cur + 3, 2, "OO|ER|EN|UY|ED|EM")) {
            if (dm_at(d, cur + 3, 2, "ER|EN")) {
              dm_add2(d, "X", "SK");
            } else {
              dm_add(d, "SK");
            }
            cur += 3;
            break;
          }
          if (cur == 0 && !dm_vowel(d, 3) && dm_get(d, 3) != 'W') {
            dm_add2(d, "X", "S");
          } else {
            dm_add(d, "X");
          }
          cur += 3;
          break;
        }
        if (dm_at(d, cur + 2, 1, "I|E|Y")) {
          dm_add(d, "S");
          cur += 3;
          break;
        }
        dm_add(d, "SK");
        cur += 3;
        break;
      }
      if (cur == last && dm_at(d, cur - 2, 2, "AI|OI")) {
        dm_add2(d, "", "S");
      } else {
        dm_add(d, "S");
      }
      cur += dm_at(d, cur + 1, 1, "S|Z") ? 2 : 1;
      break;
    case 'T':
      if (dm_at(d, cur, 4, "TION")) {
        dm_add(d, "X");
        cur += 3;
        break;
      }
      if (dm_at(d, cur, 3, "TIA|TCH")) {
        dm_add(d, "X");
        cur += 3;
        break;
      }
      if (dm_at(d, cur, 2, "TH") || dm_at(d, cur, 3, "TTH")) {
        if (dm_at(d, cur + 2, 2, "OM|AM") || dm_at(d, 0, 4, "VAN |VON ") ||
            dm_at(d, 0, 3, "SCH")) {
          dm_add(d, "T");
        } else {
          dm_add2(d, "0", "T");
        }
        cur += 2;
        break;
      }
      cur += dm_at(d, cur + 1, 1, "T|D") ? 2 : 1;
      dm_add(d, "T");
      break;
    case 'V':
      dm_add(d, "F");
      cur += dm_get(d, cur + 1) == 'V' ? 2 : 1;
      break;
    case 'W':
      if (dm_at(d, cur, 2, "WR")) {
        dm_add(d, "R");
        cur += 2;
        break;
      }
      if (cur == 0 && (dm_vowel(d, cur + 1) || dm_at(d, cur, 2, "WH"))) {
        if (dm_vowel(d, cur + 1)) {
          dm_add2(d, "A", "F");
        } else {
          dm_add(d, "A");
        }
      }
      if ((cur == last && dm_vowel(d, cur - 1)) ||
          dm_at(d, cur - 1, 5, "EWSKI|EWSKY|OWSKI|OWSKY") ||
          dm_at(d, 0, 3, "SCH")) {
        dm_add2(d, "", "F");
        cur += 1;
        break;
      }
      if (dm_at(d, cur, 4, "WICZ|WITZ")) {
        dm_add2(d, "TS", "FX");
        cur += 4;
        break;
      }
      cur += 1;
      break;
    case 'X':
      if (!(cur == last && (dm_at(d, cur - 3, 3, "IAU|EAU") ||
                            dm_at(d, cur - 2, 2, "AU|OU")))) {
        dm_add(d, "KS");
      }
      cur += dm_at(d, cur + 1, 1, "C|X") ? 2 : 1;
      break;
    case 'Z':
      if (dm_get(d, cur + 1) == 'H') {
        dm_add(d, "J");
        cur += 2;
        break;
      }
      if (dm_at(d, cur + 1, 2, "ZO|ZI|ZA") ||
          (d->slavo && (cur > 0 && dm_get(d, cur - 1) != 'T'))) {
        dm_add2(d, "S", "TS");
      } else {
        dm_add(d, "S");
      }
      cur += dm_get(d, cur + 1) == 'Z' ? 2 : 1;
      break;
    default:
      cur += 1;
      break;
    }
  }
}

ds_status_t ds_double_metaphone(const unsigned char *name, size_t n,
                                size_t max_length, unsigned char *primary,
                                unsigned char *secondary, size_t capacity,
                                size_t *out_primary_length,
                                size_t *out_secondary_length,
                                ds_context_t *context) {
  dm_t d;
  unsigned char *buf;
  unsigned char *w;
  size_t i;
  size_t k = 0;
  ds_status_t st;
  ds_status_t st2;
  if ((name == NULL && n > 0) || out_primary_length == NULL ||
      out_secondary_length == NULL) {
    return DS_ERR_NULL;
  }
  if (n > ((size_t)-1) / 8 - 64) {
    return DS_ERR_OVERFLOW;
  }
  buf = (unsigned char *)ds_context_alloc(context, 8 * n + 64);
  if (buf == NULL) {
    return DS_ERR_ALLOC;
  }
  w = buf;
  for (i = 0; i < n; i++) {
    if (ph_alpha(name[i]) || name[i] == ' ') {
      w[k++] = ph_upper(name[i]);
    }
  }
  d.length = k;
  for (i = 0; i < 5; i++) {
    w[k + i] = ' ';
  }
  d.total = k + 5;
  d.w = w;
  d.p = buf + n + 8;
  d.s = d.p + 3 * n + 8;
  d.pl = 0;
  d.sl = 0;
  d.slavo = 0;
  for (i = 0; i < k; i++) {
    if (w[i] == 'W' || w[i] == 'K' ||
        (i + 1 < k && w[i] == 'C' && w[i + 1] == 'Z')) {
      d.slavo = 1;
    }
  }
  dm_run(&d);
  if (max_length > 0) {
    if (d.pl > max_length) {
      d.pl = max_length;
    }
    if (d.sl > max_length) {
      d.sl = max_length;
    }
  }
  if (d.sl == d.pl && memcmp(d.p, d.s, d.pl) == 0) {
    d.sl = 0;
  }
  st = ph_emit(d.p, d.pl, 0, primary, capacity, out_primary_length);
  st2 = ph_emit(d.s, d.sl, 0, secondary, capacity, out_secondary_length);
  ds_context_free(context, buf);
  return st != DS_OK ? st : st2;
}

/* ---- Caverphone 2.0 ------------------------------------------------- */

typedef struct cv {
  unsigned char *w;
  size_t len;
} cv_t;

/* Replace every non-overlapping occurrence (left to right); the
 * replacement is never longer than the pattern. */
static void cv_replace(cv_t *c, const char *from, const char *to) {
  size_t fl = strlen(from);
  size_t tl = strlen(to);
  size_t i = 0;
  size_t o = 0;
  while (i < c->len) {
    if (i + fl <= c->len && memcmp(c->w + i, from, fl) == 0) {
      memcpy(c->w + o, to, tl);
      o += tl;
      i += fl;
    } else {
      c->w[o++] = c->w[i++];
    }
  }
  c->len = o;
}

static int cv_starts(const cv_t *c, const char *p) {
  size_t m = strlen(p);
  return c->len >= m && memcmp(c->w, p, m) == 0;
}

static void cv_prefix(cv_t *c, const char *from, const char *to) {
  if (cv_starts(c, from)) {
    memcpy(c->w, to, strlen(to)); /* same length */
  }
}

static void cv_squeeze(cv_t *c, unsigned char ch) {
  size_t i;
  size_t o = 0;
  for (i = 0; i < c->len; i++) {
    if (c->w[i] == ch && o > 0 && c->w[o - 1] == ph_upper(ch)) {
      continue;
    }
    c->w[o++] = c->w[i] == ch ? ph_upper(ch) : c->w[i];
  }
  c->len = o;
}

ds_status_t ds_caverphone(const unsigned char *name, size_t n,
                          unsigned char *out, size_t capacity,
                          size_t *out_length, ds_context_t *context) {
  cv_t c;
  unsigned char key[10];
  size_t i;
  const char *sq = "stpkfmn";
  if ((name == NULL && n > 0) || out_length == NULL) {
    return DS_ERR_NULL;
  }
  c.w = (unsigned char *)ph_alloc(context, n);
  if (c.w == NULL) {
    return DS_ERR_ALLOC;
  }
  c.len = 0;
  for (i = 0; i < n; i++) {
    if (ph_alpha(name[i])) {
      c.w[c.len++] = ph_lower(name[i]);
    }
  }
  if (c.len > 0 && c.w[c.len - 1] == 'e') {
    c.len--;
  }
  if (c.len > 0) {
    cv_prefix(&c, "cough", "cou2f");
    cv_prefix(&c, "rough", "rou2f");
    cv_prefix(&c, "tough", "tou2f");
    cv_prefix(&c, "enough", "enou2f");
    cv_prefix(&c, "trough", "trou2f");
    cv_prefix(&c, "gn", "2n");
    if (c.len >= 2 && c.w[c.len - 2] == 'm' && c.w[c.len - 1] == 'b') {
      c.w[c.len - 1] = '2';
    }
    cv_replace(&c, "cq", "2q");
    cv_replace(&c, "ci", "si");
    cv_replace(&c, "ce", "se");
    cv_replace(&c, "cy", "sy");
    cv_replace(&c, "tch", "2ch");
    cv_replace(&c, "c", "k");
    cv_replace(&c, "q", "k");
    cv_replace(&c, "x", "k");
    cv_replace(&c, "v", "f");
    cv_replace(&c, "dg", "2g");
    cv_replace(&c, "tio", "sio");
    cv_replace(&c, "tia", "sia");
    cv_replace(&c, "d", "t");
    cv_replace(&c, "ph", "fh");
    cv_replace(&c, "b", "p");
    cv_replace(&c, "sh", "s2");
    cv_replace(&c, "z", "s");
    if (c.w[0] == 'a' || c.w[0] == 'e' || c.w[0] == 'i' || c.w[0] == 'o' ||
        c.w[0] == 'u') {
      c.w[0] = 'A';
    }
    cv_replace(&c, "a", "3");
    cv_replace(&c, "e", "3");
    cv_replace(&c, "i", "3");
    cv_replace(&c, "o", "3");
    cv_replace(&c, "u", "3");
    cv_replace(&c, "j", "y");
    cv_prefix(&c, "y3", "Y3");
    if (c.len > 0 && c.w[0] == 'y') {
      c.w[0] = 'A';
    }
    cv_replace(&c, "y", "3");
    cv_replace(&c, "3gh3", "3kh3");
    cv_replace(&c, "gh", "22");
    cv_replace(&c, "g", "k");
    for (i = 0; sq[i]; i++) {
      cv_squeeze(&c, (unsigned char)sq[i]);
    }
    cv_replace(&c, "w3", "W3");
    cv_replace(&c, "wh3", "Wh3");
    if (c.len > 0 && c.w[c.len - 1] == 'w') {
      c.w[c.len - 1] = '3';
    }
    cv_replace(&c, "w", "2");
    if (c.len > 0 && c.w[0] == 'h') {
      c.w[0] = 'A';
    }
    cv_replace(&c, "h", "2");
    cv_replace(&c, "r3", "R3");
    if (c.len > 0 && c.w[c.len - 1] == 'r') {
      c.w[c.len - 1] = '3';
    }
    cv_replace(&c, "r", "2");
    cv_replace(&c, "l3", "L3");
    if (c.len > 0 && c.w[c.len - 1] == 'l') {
      c.w[c.len - 1] = '3';
    }
    cv_replace(&c, "l", "2");
    cv_replace(&c, "2", "");
    if (c.len > 0 && c.w[c.len - 1] == '3') {
      c.w[c.len - 1] = 'A';
    }
    cv_replace(&c, "3", "");
  }
  for (i = 0; i < 10; i++) {
    key[i] = i < c.len ? c.w[i] : (unsigned char)'1';
  }
  ds_context_free(context, c.w);
  return ph_emit(key, 10, 0, out, capacity, out_length);
}

ds_status_t ds_phonetic_code(ds_phonetic_algorithm_t algorithm,
                             const unsigned char *name, size_t n,
                             unsigned char *out, size_t capacity,
                             size_t *out_length, ds_context_t *context) {
  size_t sl;
  unsigned char *sec;
  ds_status_t st;
  switch (algorithm) {
  case DS_PHONETIC_SOUNDEX:
    return ds_soundex(name, n, out, capacity, out_length);
  case DS_PHONETIC_NYSIIS:
    return ds_nysiis(name, n, 0, out, capacity, out_length, context);
  case DS_PHONETIC_METAPHONE:
    return ds_metaphone(name, n, 0, out, capacity, out_length, context);
  case DS_PHONETIC_DOUBLE_METAPHONE:
    sec = (unsigned char *)ds_context_alloc(context, capacity + 1);
    if (sec == NULL) {
      return DS_ERR_ALLOC;
    }
    st = ds_double_metaphone(name, n, 0, out, sec, capacity, out_length, &sl,
                             context);
    ds_context_free(context, sec);
    /* only the primary key matters here */
    return st == DS_ERR_OVERFLOW && *out_length <= capacity ? DS_OK : st;
  case DS_PHONETIC_CAVERPHONE:
    return ds_caverphone(name, n, out, capacity, out_length, context);
  default:
    return DS_ERR_INVALID;
  }
}
