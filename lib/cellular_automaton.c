#include "cellular_automaton.h"
#include <limits.h>
#include <string.h>

#define CA_TABLE_MAX ((size_t)1 << 24)

static size_t ca_map(long x, size_t n, ds_ca_boundary_t b, bool *outside) {
  long w = (long)n;
  *outside = false;
  if (x >= 0 && x < w) {
    return (size_t)x;
  }
  switch (b) {
  case DS_CA_PERIODIC:
    x %= w;
    return (size_t)(x < 0 ? x + w : x);
  case DS_CA_REFLECT: {
    long m = 2 * w;
    x %= m;
    if (x < 0) {
      x += m;
    }
    return (size_t)(x >= w ? m - 1 - x : x);
  }
  case DS_CA_FIXED:
    break;
  }
  *outside = true;
  return 0;
}

/* ---------------------------------------------------------------- 1-D */

ds_status_t ds_ca1_init(ds_ca1_t *ca, size_t width, unsigned states,
                        size_t radius, ds_ca_boundary_t boundary,
                        ds_context_t *context) {
  if (ca == NULL) {
    return DS_ERR_NULL;
  }
  memset(ca, 0, sizeof(*ca));
  if (width == 0 || states < 2 || states > 256 ||
      width > (size_t)LONG_MAX / 4 || radius > (size_t)LONG_MAX / 4) {
    return DS_ERR_INVALID;
  }
  ca->cells = (unsigned char *)ds_context_calloc(context, width, 1);
  ca->next = (unsigned char *)ds_context_calloc(context, width, 1);
  ca->window = (unsigned char *)ds_context_alloc(context, 2 * radius + 1);
  if (ca->cells == NULL || ca->next == NULL || ca->window == NULL) {
    ds_context_free(context, ca->cells);
    ds_context_free(context, ca->next);
    ds_context_free(context, ca->window);
    memset(ca, 0, sizeof(*ca));
    return DS_ERR_ALLOC;
  }
  ca->width = width;
  ca->states = states;
  ca->radius = radius;
  ca->boundary = boundary;
  ca->kind = DS_CA_RULE_CALLBACK;
  ca->context = context;
  return DS_OK;
}

void ds_ca1_destroy(ds_ca1_t *ca) {
  if (ca == NULL) {
    return;
  }
  ds_context_free(ca->context, ca->cells);
  ds_context_free(ca->context, ca->next);
  ds_context_free(ca->context, ca->window);
  ds_context_free(ca->context, ca->table);
  memset(ca, 0, sizeof(*ca));
}

void ds_ca1_set_outside(ds_ca1_t *ca, unsigned char value) {
  if (ca != NULL && value < ca->states) {
    ca->outside = value;
  }
}

static ds_status_t ca1_table(ds_ca1_t *ca, size_t size,
                             const unsigned char *src, ds_ca_rule_kind_t k) {
  unsigned char *t;
  size_t i;
  for (i = 0; src != NULL && i < size; i++) {
    if (src[i] >= ca->states) {
      return DS_ERR_RANGE;
    }
  }
  t = (unsigned char *)ds_context_alloc(ca->context, size);
  if (t == NULL) {
    return DS_ERR_ALLOC;
  }
  if (src != NULL) {
    memcpy(t, src, size);
  }
  ds_context_free(ca->context, ca->table);
  ca->table = t;
  ca->table_size = size;
  ca->kind = k;
  return DS_OK;
}

static size_t ca1_full_size(const ds_ca1_t *ca) {
  size_t size = 1;
  size_t i;
  for (i = 0; i < 2 * ca->radius + 1; i++) {
    if (size > CA_TABLE_MAX / ca->states) {
      return 0;
    }
    size *= ca->states;
  }
  return size;
}

ds_status_t ds_ca1_set_table(ds_ca1_t *ca, const unsigned char *table) {
  size_t size;
  if (ca == NULL || table == NULL) {
    return DS_ERR_NULL;
  }
  size = ca1_full_size(ca);
  if (size == 0) {
    return DS_ERR_OVERFLOW;
  }
  return ca1_table(ca, size, table, DS_CA_RULE_TABLE);
}

ds_status_t ds_ca1_set_elementary(ds_ca1_t *ca, unsigned rule) {
  unsigned char t[8];
  unsigned i;
  if (ca == NULL) {
    return DS_ERR_NULL;
  }
  if (ca->states != 2 || ca->radius != 1) {
    return DS_ERR_STATE;
  }
  if (rule > 255) {
    return DS_ERR_RANGE;
  }
  for (i = 0; i < 8; i++) {
    t[i] = (unsigned char)((rule >> i) & 1u);
  }
  return ca1_table(ca, 8, t, DS_CA_RULE_TABLE);
}

static size_t ca1_sum_size(const ds_ca1_t *ca) {
  return (2 * ca->radius + 1) * (ca->states - 1) + 1;
}

ds_status_t ds_ca1_set_totalistic(ds_ca1_t *ca, const unsigned char *by_sum) {
  if (ca == NULL || by_sum == NULL) {
    return DS_ERR_NULL;
  }
  return ca1_table(ca, ca1_sum_size(ca), by_sum, DS_CA_RULE_TOTALISTIC);
}

ds_status_t ds_ca1_set_totalistic_code(ds_ca1_t *ca, unsigned long code) {
  size_t size;
  size_t s;
  ds_status_t st;
  unsigned char *t;
  if (ca == NULL) {
    return DS_ERR_NULL;
  }
  size = ca1_sum_size(ca);
  t = (unsigned char *)ds_context_alloc(ca->context, size);
  if (t == NULL) {
    return DS_ERR_ALLOC;
  }
  for (s = 0; s < size; s++) {
    t[s] = (unsigned char)(code % ca->states);
    code /= ca->states;
  }
  st = code != 0 ? DS_ERR_RANGE : ca1_table(ca, size, t, DS_CA_RULE_TOTALISTIC);
  ds_context_free(ca->context, t);
  return st;
}

ds_status_t ds_ca1_set_callback(ds_ca1_t *ca, ds_ca1_rule_fn rule, void *user) {
  if (ca == NULL || rule == NULL) {
    return DS_ERR_NULL;
  }
  ca->rule = rule;
  ca->user = user;
  ca->kind = DS_CA_RULE_CALLBACK;
  return DS_OK;
}

unsigned char ds_ca1_get(const ds_ca1_t *ca, size_t i) {
  return ca == NULL || i >= ca->width ? 0 : ca->cells[i];
}

ds_status_t ds_ca1_set(ds_ca1_t *ca, size_t i, unsigned char state) {
  if (ca == NULL) {
    return DS_ERR_NULL;
  }
  if (i >= ca->width || state >= ca->states) {
    return DS_ERR_RANGE;
  }
  ca->cells[i] = state;
  return DS_OK;
}

void ds_ca1_clear(ds_ca1_t *ca) {
  if (ca != NULL) {
    memset(ca->cells, 0, ca->width);
    ca->generation = 0;
  }
}

size_t ds_ca1_step(ds_ca1_t *ca) {
  size_t i;
  size_t j;
  size_t changed = 0;
  size_t span;
  unsigned char *swap;
  if (ca == NULL || (ca->kind == DS_CA_RULE_CALLBACK && ca->rule == NULL) ||
      (ca->kind != DS_CA_RULE_CALLBACK && ca->table == NULL)) {
    return 0;
  }
  span = 2 * ca->radius + 1;
  for (i = 0; i < ca->width; i++) {
    size_t acc = 0;
    unsigned char v;
    for (j = 0; j < span; j++) {
      bool out;
      size_t at = ca_map((long)i - (long)ca->radius + (long)j, ca->width,
                         ca->boundary, &out);
      ca->window[j] = out ? ca->outside : ca->cells[at];
    }
    switch (ca->kind) {
    case DS_CA_RULE_TABLE:
      for (j = 0; j < span; j++) {
        acc = acc * ca->states + ca->window[j];
      }
      v = ca->table[acc];
      break;
    case DS_CA_RULE_TOTALISTIC:
      for (j = 0; j < span; j++) {
        acc += ca->window[j];
      }
      v = ca->table[acc];
      break;
    case DS_CA_RULE_CALLBACK:
    default:
      v = ca->rule(ca->window, span, ca->user);
      if (v >= ca->states) {
        v = (unsigned char)(v % ca->states);
      }
      break;
    }
    ca->next[i] = v;
    if (v != ca->cells[i]) {
      changed++;
    }
  }
  swap = ca->cells;
  ca->cells = ca->next;
  ca->next = swap;
  ca->generation++;
  return changed;
}

void ds_ca1_run(ds_ca1_t *ca, unsigned long generations) {
  unsigned long g;
  for (g = 0; g < generations; g++) {
    (void)ds_ca1_step(ca);
  }
}

/* ---------------------------------------------------------------- 2-D */

static size_t ca2_max_neighbors(ds_ca_neighborhood_t nb, size_t r) {
  return nb == DS_CA_MOORE ? (2 * r + 1) * (2 * r + 1) - 1 : 2 * r * (r + 1);
}

ds_status_t ds_ca2_init(ds_ca2_t *ca, size_t width, size_t height,
                        unsigned states, ds_ca_neighborhood_t neighborhood,
                        size_t range, ds_ca_boundary_t boundary,
                        ds_context_t *context) {
  size_t cells;
  size_t m;
  if (ca == NULL) {
    return DS_ERR_NULL;
  }
  memset(ca, 0, sizeof(*ca));
  if (width == 0 || height == 0 || states < 2 || states > 256 || range == 0 ||
      range > 1024 || width > (size_t)LONG_MAX / 4 ||
      height > (size_t)LONG_MAX / 4) {
    return DS_ERR_INVALID;
  }
  if (width > (size_t)-1 / height) {
    return DS_ERR_OVERFLOW;
  }
  cells = width * height;
  m = ca2_max_neighbors(neighborhood, range);
  ca->cells = (unsigned char *)ds_context_calloc(context, cells, 1);
  ca->next = (unsigned char *)ds_context_calloc(context, cells, 1);
  ca->birth = (bool *)ds_context_calloc(context, 2 * (m + 1), sizeof(bool));
  if (ca->cells == NULL || ca->next == NULL || ca->birth == NULL) {
    ds_context_free(context, ca->cells);
    ds_context_free(context, ca->next);
    ds_context_free(context, ca->birth);
    memset(ca, 0, sizeof(*ca));
    return DS_ERR_ALLOC;
  }
  ca->survive = ca->birth + m + 1;
  ca->width = width;
  ca->height = height;
  ca->states = states;
  ca->neighborhood = neighborhood;
  ca->range = range;
  ca->boundary = boundary;
  ca->max_neighbors = m;
  ca->kind = DS_CA_RULE_TOTALISTIC;
  ca->context = context;
  if (m >= 3) {
    ca->birth[3] = true;
    ca->survive[2] = true;
    ca->survive[3] = true;
  }
  return DS_OK;
}

void ds_ca2_destroy(ds_ca2_t *ca) {
  if (ca == NULL) {
    return;
  }
  ds_context_free(ca->context, ca->cells);
  ds_context_free(ca->context, ca->next);
  ds_context_free(ca->context, ca->birth);
  memset(ca, 0, sizeof(*ca));
}

void ds_ca2_set_outside(ds_ca2_t *ca, unsigned char value) {
  if (ca != NULL && value < ca->states) {
    ca->outside = value;
  }
}

ds_status_t ds_ca2_set_life(ds_ca2_t *ca, const bool *birth,
                            const bool *survive) {
  if (ca == NULL || birth == NULL || survive == NULL) {
    return DS_ERR_NULL;
  }
  memmove(ca->birth, birth, (ca->max_neighbors + 1) * sizeof(bool));
  memmove(ca->survive, survive, (ca->max_neighbors + 1) * sizeof(bool));
  ca->kind = DS_CA_RULE_TOTALISTIC;
  return DS_OK;
}

static char ca_upper(char c) {
  return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

/* digits of one B or S part into flags; returns the end or NULL */
static const char *ca_counts(const char *p, bool *flags, size_t max,
                             ds_status_t *st) {
  while (*p >= '0' && *p <= '9') {
    size_t n = (size_t)(*p - '0');
    if (n > max) {
      *st = DS_ERR_RANGE;
      return NULL;
    }
    flags[n] = true;
    p++;
  }
  return p;
}

static const char *ca_number(const char *p, unsigned long *out) {
  unsigned long v = 0;
  if (!(*p >= '0' && *p <= '9')) {
    return NULL;
  }
  while (*p >= '0' && *p <= '9') {
    if (v > 100000ul) {
      return NULL;
    }
    v = v * 10 + (unsigned long)(*p - '0');
    p++;
  }
  *out = v;
  return p;
}

ds_status_t ds_ca2_parse_rule(ds_ca2_t *ca, const char *rule) {
  bool *b;
  bool *s;
  const char *p;
  size_t m;
  ds_status_t st = DS_OK;
  if (ca == NULL || rule == NULL) {
    return DS_ERR_NULL;
  }
  m = ca->max_neighbors;
  b = (bool *)ds_context_calloc(ca->context, 2 * (m + 1), sizeof(bool));
  if (b == NULL) {
    return DS_ERR_ALLOC;
  }
  s = b + m + 1;
  p = rule;
  if (ca_upper(*p) == 'B' || ca_upper(*p) == 'S' || ca_upper(*p) == 'C' ||
      ca_upper(*p) == 'G') {
    unsigned seen = 0;
    while (st == DS_OK && *p != '\0') {
      char tag = ca_upper(*p++);
      unsigned bit = tag == 'B' ? 1u : tag == 'S' ? 2u : 4u;
      if ((tag != 'B' && tag != 'S' && tag != 'C' && tag != 'G') ||
          (seen & bit) != 0) {
        st = DS_ERR_INVALID;
        break;
      }
      seen |= bit;
      if (tag == 'C' || tag == 'G') {
        unsigned long c = 0;
        p = ca_number(p, &c);
        if (p == NULL || c != ca->states) {
          st = DS_ERR_INVALID;
          break;
        }
      } else {
        p = ca_counts(p, tag == 'B' ? b : s, m, &st);
        if (p == NULL) {
          break;
        }
      }
      if (*p == '/') {
        p++;
        if (*p == '\0') {
          st = DS_ERR_INVALID;
        }
      } else if (*p != '\0') {
        st = DS_ERR_INVALID;
      }
    }
  } else {
    /* S/B or S/B/C */
    p = ca_counts(p, s, m, &st);
    if (p != NULL && *p == '/') {
      p = ca_counts(p + 1, b, m, &st);
      if (p != NULL && *p == '/') {
        unsigned long c = 0;
        p = ca_number(p + 1, &c);
        if (p == NULL || c != ca->states) {
          st = st == DS_OK ? DS_ERR_INVALID : st;
          p = NULL;
        }
      }
      if (p != NULL && *p != '\0') {
        st = DS_ERR_INVALID;
      }
    } else if (st == DS_OK) {
      st = DS_ERR_INVALID;
    }
  }
  if (st == DS_OK) {
    st = ds_ca2_set_life(ca, b, s);
  }
  ds_context_free(ca->context, b);
  return st;
}

ds_status_t ds_ca2_set_callback(ds_ca2_t *ca, ds_ca2_rule_fn rule, void *user) {
  if (ca == NULL || rule == NULL) {
    return DS_ERR_NULL;
  }
  ca->rule = rule;
  ca->user = user;
  ca->kind = DS_CA_RULE_CALLBACK;
  return DS_OK;
}

unsigned char ds_ca2_get(const ds_ca2_t *ca, size_t x, size_t y) {
  if (ca == NULL || x >= ca->width || y >= ca->height) {
    return 0;
  }
  return ca->cells[y * ca->width + x];
}

unsigned char ds_ca2_at(const ds_ca2_t *ca, long x, long y) {
  bool ox;
  bool oy;
  size_t mx;
  size_t my;
  if (ca == NULL) {
    return 0;
  }
  mx = ca_map(x, ca->width, ca->boundary, &ox);
  my = ca_map(y, ca->height, ca->boundary, &oy);
  return ox || oy ? ca->outside : ca->cells[my * ca->width + mx];
}

ds_status_t ds_ca2_set(ds_ca2_t *ca, size_t x, size_t y, unsigned char state) {
  if (ca == NULL) {
    return DS_ERR_NULL;
  }
  if (x >= ca->width || y >= ca->height || state >= ca->states) {
    return DS_ERR_RANGE;
  }
  ca->cells[y * ca->width + x] = state;
  return DS_OK;
}

void ds_ca2_clear(ds_ca2_t *ca) {
  if (ca != NULL) {
    memset(ca->cells, 0, ca->width * ca->height);
    ca->generation = 0;
  }
}

size_t ds_ca2_live_neighbors(const ds_ca2_t *ca, size_t x, size_t y) {
  long r;
  long dx;
  long dy;
  size_t n = 0;
  if (ca == NULL || x >= ca->width || y >= ca->height) {
    return 0;
  }
  r = (long)ca->range;
  for (dy = -r; dy <= r; dy++) {
    for (dx = -r; dx <= r; dx++) {
      long ax = dx < 0 ? -dx : dx;
      long ay = dy < 0 ? -dy : dy;
      if ((dx == 0 && dy == 0) ||
          (ca->neighborhood == DS_CA_VON_NEUMANN && ax + ay > r)) {
        continue;
      }
      if (ds_ca2_at(ca, (long)x + dx, (long)y + dy) == 1) {
        n++;
      }
    }
  }
  return n;
}

size_t ds_ca2_population(const ds_ca2_t *ca) {
  size_t i;
  size_t n = 0;
  if (ca == NULL) {
    return 0;
  }
  for (i = 0; i < ca->width * ca->height; i++) {
    if (ca->cells[i] != 0) {
      n++;
    }
  }
  return n;
}

size_t ds_ca2_step(ds_ca2_t *ca) {
  size_t x;
  size_t y;
  size_t changed = 0;
  unsigned char *swap;
  if (ca == NULL || (ca->kind == DS_CA_RULE_CALLBACK && ca->rule == NULL)) {
    return 0;
  }
  for (y = 0; y < ca->height; y++) {
    for (x = 0; x < ca->width; x++) {
      size_t i = y * ca->width + x;
      unsigned char s = ca->cells[i];
      unsigned char v;
      if (ca->kind == DS_CA_RULE_CALLBACK) {
        v = ca->rule(ca, x, y, ca->user);
        if (v >= ca->states) {
          v = (unsigned char)(v % ca->states);
        }
      } else if (s == 0) {
        v = ca->birth[ds_ca2_live_neighbors(ca, x, y)] ? 1 : 0;
      } else if (s == 1) {
        v = ca->survive[ds_ca2_live_neighbors(ca, x, y)]
                ? 1
                : (unsigned char)(ca->states > 2 ? 2 : 0);
      } else {
        v = (unsigned)s + 1 >= ca->states ? 0 : (unsigned char)(s + 1);
      }
      ca->next[i] = v;
      if (v != s) {
        changed++;
      }
    }
  }
  swap = ca->cells;
  ca->cells = ca->next;
  ca->next = swap;
  ca->generation++;
  return changed;
}

void ds_ca2_run(ds_ca2_t *ca, unsigned long generations) {
  unsigned long g;
  for (g = 0; g < generations; g++) {
    (void)ds_ca2_step(ca);
  }
}
