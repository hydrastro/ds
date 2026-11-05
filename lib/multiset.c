#include "multiset.h"
#include <string.h>

static unsigned char *ms_key(const ds_multiset_t *m, size_t i) {
  return m->keys + i * m->elem_size;
}

ds_status_t ds_multiset_init(ds_multiset_t *m, size_t elem_size,
                             ds_multiset_hash_func_t hash,
                             ds_multiset_equal_func_t equal, void *user,
                             ds_context_t *context) {
  if (m == NULL || hash == NULL || equal == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(m, 0, sizeof(*m));
  m->elem_size = elem_size;
  m->hash = hash;
  m->equal = equal;
  m->user = user;
  m->context = context;
  return DS_OK;
}

void ds_multiset_destroy(ds_multiset_t *m) {
  if (m == NULL) {
    return;
  }
  ds_context_free(m->context, m->keys);
  ds_context_free(m->context, m->counts);
  m->keys = NULL;
  m->counts = NULL;
  m->slots = m->distinct = m->total = 0;
}

void ds_multiset_clear(ds_multiset_t *m) {
  size_t i;
  if (m == NULL) {
    return;
  }
  for (i = 0; i < m->slots; i++) {
    m->counts[i] = 0;
  }
  m->distinct = m->total = 0;
}

static size_t ms_home(const ds_multiset_t *m, const void *elem) {
  size_t h = m->hash(elem, m->user);
  h ^= h >> 16;
  h *= (size_t)0x45d9f3bUL;
  h ^= h >> 16;
  return h & (m->slots - 1);
}

/* Slot of elem, or the empty slot where it would go. */
static size_t ms_find(const ds_multiset_t *m, const void *elem) {
  size_t i = ms_home(m, elem);
  while (m->counts[i] != 0 && !m->equal(ms_key(m, i), elem, m->user)) {
    i = (i + 1) & (m->slots - 1);
  }
  return i;
}

static ds_status_t ms_resize(ds_multiset_t *m, size_t slots) {
  unsigned char *ok = m->keys;
  size_t *oc = m->counts;
  size_t os = m->slots;
  size_t i;
  size_t j;
  if (slots > ((size_t)-1) / m->elem_size ||
      slots > ((size_t)-1) / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  m->keys = (unsigned char *)ds_context_alloc(m->context, slots * m->elem_size);
  m->counts = (size_t *)ds_context_calloc(m->context, slots, sizeof(size_t));
  if (m->keys == NULL || m->counts == NULL) {
    ds_context_free(m->context, m->keys);
    ds_context_free(m->context, m->counts);
    m->keys = ok;
    m->counts = oc;
    return DS_ERR_ALLOC;
  }
  m->slots = slots;
  for (i = 0; i < os; i++) {
    if (oc[i] != 0) {
      j = ms_find(m, ok + i * m->elem_size);
      memcpy(ms_key(m, j), ok + i * m->elem_size, m->elem_size);
      m->counts[j] = oc[i];
    }
  }
  ds_context_free(m->context, ok);
  ds_context_free(m->context, oc);
  return DS_OK;
}

ds_status_t ds_multiset_add(ds_multiset_t *m, const void *elem, size_t k) {
  size_t i;
  ds_status_t st;
  if (m == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (k == 0) {
    return DS_OK;
  }
  if (k > ((size_t)-1) - m->total) {
    return DS_ERR_OVERFLOW;
  }
  if (m->slots == 0 || (m->distinct + 1) * 4 > m->slots * 3) {
    if (m->slots > ((size_t)-1) / 2) {
      return DS_ERR_OVERFLOW;
    }
    st = ms_resize(m, m->slots == 0 ? 16 : m->slots * 2);
    if (st != DS_OK) {
      return st;
    }
  }
  i = ms_find(m, elem);
  if (m->counts[i] == 0) {
    memcpy(ms_key(m, i), elem, m->elem_size);
    m->distinct++;
  }
  m->counts[i] += k;
  m->total += k;
  return DS_OK;
}

/* Backward-shift deletion of slot i. */
static void ms_erase(ds_multiset_t *m, size_t i) {
  size_t j = i;
  size_t h;
  size_t mask = m->slots - 1;
  m->counts[i] = 0;
  for (;;) {
    j = (j + 1) & mask;
    if (m->counts[j] == 0) {
      return;
    }
    h = ms_home(m, ms_key(m, j));
    /* move j back to i unless its home lies cyclically in (i, j] */
    if ((i < j && (h <= i || h > j)) || (i > j && h <= i && h > j)) {
      memcpy(ms_key(m, i), ms_key(m, j), m->elem_size);
      m->counts[i] = m->counts[j];
      m->counts[j] = 0;
      i = j;
    }
  }
}

ds_status_t ds_multiset_remove(ds_multiset_t *m, const void *elem, size_t k,
                               size_t *out_removed) {
  size_t i;
  size_t r;
  if (m == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (out_removed != NULL) {
    *out_removed = 0;
  }
  if (m->slots == 0) {
    return DS_NOT_FOUND;
  }
  i = ms_find(m, elem);
  if (m->counts[i] == 0) {
    return DS_NOT_FOUND;
  }
  r = k < m->counts[i] ? k : m->counts[i];
  m->counts[i] -= r;
  m->total -= r;
  if (m->counts[i] == 0) {
    m->distinct--;
    ms_erase(m, i);
  }
  if (out_removed != NULL) {
    *out_removed = r;
  }
  return DS_OK;
}

size_t ds_multiset_count(const ds_multiset_t *m, const void *elem) {
  if (m == NULL || elem == NULL || m->slots == 0) {
    return 0;
  }
  return m->counts[ms_find(m, elem)];
}

size_t ds_multiset_distinct(const ds_multiset_t *m) {
  return m == NULL ? 0 : m->distinct;
}

size_t ds_multiset_total(const ds_multiset_t *m) {
  return m == NULL ? 0 : m->total;
}

void ds_multiset_foreach(const ds_multiset_t *m, ds_multiset_visit_func_t f,
                         void *user) {
  size_t i;
  if (m == NULL || f == NULL) {
    return;
  }
  for (i = 0; i < m->slots; i++) {
    if (m->counts[i] != 0 && !f(ms_key(m, i), m->counts[i], user)) {
      return;
    }
  }
}

static ds_status_t ms_check(const ds_multiset_t *m, const ds_multiset_t *o) {
  if (m == NULL || o == NULL) {
    return DS_ERR_NULL;
  }
  if (m == o || m->elem_size != o->elem_size) {
    return DS_ERR_INVALID;
  }
  return DS_OK;
}

ds_status_t ds_multiset_sum(ds_multiset_t *m, const ds_multiset_t *other) {
  size_t i;
  ds_status_t st = ms_check(m, other);
  for (i = 0; st == DS_OK && i < other->slots; i++) {
    if (other->counts[i] != 0) {
      st = ds_multiset_add(m, ms_key(other, i), other->counts[i]);
    }
  }
  return st;
}

ds_status_t ds_multiset_union(ds_multiset_t *m, const ds_multiset_t *other) {
  size_t i;
  size_t c;
  ds_status_t st = ms_check(m, other);
  for (i = 0; st == DS_OK && i < other->slots; i++) {
    if (other->counts[i] != 0) {
      c = ds_multiset_count(m, ms_key(other, i));
      if (other->counts[i] > c) {
        st = ds_multiset_add(m, ms_key(other, i), other->counts[i] - c);
      }
    }
  }
  return st;
}

ds_status_t ds_multiset_intersect(ds_multiset_t *m,
                                  const ds_multiset_t *other) {
  size_t i;
  size_t c;
  ds_status_t st = ms_check(m, other);
  if (st != DS_OK) {
    return st;
  }
  i = 0;
  while (i < m->slots) {
    if (m->counts[i] != 0) {
      c = ds_multiset_count(other, ms_key(m, i));
      if (c < m->counts[i]) {
        m->total -= m->counts[i] - c;
        m->counts[i] = c;
        if (c == 0) {
          /* erasing may shift a later entry into i: revisit i */
          m->distinct--;
          ms_erase(m, i);
          continue;
        }
      }
    }
    i++;
  }
  return DS_OK;
}

ds_status_t ds_multiset_subtract(ds_multiset_t *m, const ds_multiset_t *other) {
  size_t i;
  ds_status_t st = ms_check(m, other);
  for (i = 0; st == DS_OK && i < other->slots; i++) {
    if (other->counts[i] != 0) {
      st = ds_multiset_remove(m, ms_key(other, i), other->counts[i], NULL);
      if (st == DS_NOT_FOUND) {
        st = DS_OK;
      }
    }
  }
  return st;
}

bool ds_multiset_included(const ds_multiset_t *a, const ds_multiset_t *b) {
  size_t i;
  if (a == NULL || b == NULL) {
    return false;
  }
  for (i = 0; i < a->slots; i++) {
    if (a->counts[i] != 0 &&
        ds_multiset_count(b, ms_key(a, i)) < a->counts[i]) {
      return false;
    }
  }
  return true;
}
