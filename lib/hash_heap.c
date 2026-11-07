#include "hash_heap.h"
#include <string.h>

static unsigned char *hh_key(const ds_hash_heap_t *h, size_t i) {
  return h->keys + i * h->key_size;
}

static unsigned char *hh_prio(const ds_hash_heap_t *h, size_t i) {
  return h->prios + i * h->prio_size;
}

ds_status_t ds_hash_heap_init(ds_hash_heap_t *h, size_t key_size,
                              size_t prio_size, ds_hash_heap_hash_func_t hash,
                              ds_hash_heap_equal_func_t equal,
                              ds_hash_heap_compare_func_t compare, void *user,
                              ds_context_t *context) {
  if (h == NULL || hash == NULL || equal == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (key_size == 0 || prio_size == 0 || key_size > ((size_t)-1) - prio_size) {
    return DS_ERR_INVALID;
  }
  memset(h, 0, sizeof(*h));
  h->key_size = key_size;
  h->prio_size = prio_size;
  h->hash = hash;
  h->equal = equal;
  h->compare = compare;
  h->user = user;
  h->context = context;
  h->tmp = (unsigned char *)ds_context_alloc(context, key_size + prio_size);
  return h->tmp == NULL ? DS_ERR_ALLOC : DS_OK;
}

void ds_hash_heap_destroy(ds_hash_heap_t *h) {
  if (h == NULL) {
    return;
  }
  ds_context_free(h->context, h->keys);
  ds_context_free(h->context, h->prios);
  ds_context_free(h->context, h->table);
  ds_context_free(h->context, h->tmp);
  h->keys = h->prios = h->tmp = NULL;
  h->table = NULL;
  h->size = h->capacity = h->slots = 0;
}

size_t ds_hash_heap_size(const ds_hash_heap_t *h) {
  return h == NULL ? 0 : h->size;
}

static size_t hh_home(const ds_hash_heap_t *h, const void *key) {
  size_t x = h->hash(key, h->user);
  x ^= x >> 16;
  x *= (size_t)0x45d9f3bUL;
  x ^= x >> 16;
  return x & (h->slots - 1);
}

/* Table slot of key, or the empty slot where it would go. */
static size_t hh_slot(const ds_hash_heap_t *h, const void *key) {
  size_t i = hh_home(h, key);
  while (h->table[i] != 0 &&
         !h->equal(hh_key(h, h->table[i] - 1), key, h->user)) {
    i = (i + 1) & (h->slots - 1);
  }
  return i;
}

static void hh_set_pos(ds_hash_heap_t *h, size_t pos) {
  h->table[hh_slot(h, hh_key(h, pos))] = pos + 1;
}

static void hh_swap(ds_hash_heap_t *h, size_t a, size_t b) {
  unsigned char *t = h->tmp;
  /* find both table slots while the keys are still in place */
  size_t sa = hh_slot(h, hh_key(h, a));
  size_t sb = hh_slot(h, hh_key(h, b));
  memcpy(t, hh_key(h, a), h->key_size);
  memcpy(t + h->key_size, hh_prio(h, a), h->prio_size);
  memcpy(hh_key(h, a), hh_key(h, b), h->key_size);
  memcpy(hh_prio(h, a), hh_prio(h, b), h->prio_size);
  memcpy(hh_key(h, b), t, h->key_size);
  memcpy(hh_prio(h, b), t + h->key_size, h->prio_size);
  h->table[sa] = b + 1;
  h->table[sb] = a + 1;
}

static bool hh_less(const ds_hash_heap_t *h, size_t a, size_t b) {
  return h->compare(hh_prio(h, a), hh_prio(h, b), h->user) < 0;
}

static void hh_up(ds_hash_heap_t *h, size_t i) {
  while (i > 0 && hh_less(h, i, (i - 1) / 2)) {
    hh_swap(h, i, (i - 1) / 2);
    i = (i - 1) / 2;
  }
}

static void hh_down(ds_hash_heap_t *h, size_t i) {
  size_t c;
  for (;;) {
    c = 2 * i + 1;
    if (c >= h->size) {
      return;
    }
    if (c + 1 < h->size && hh_less(h, c + 1, c)) {
      c++;
    }
    if (!hh_less(h, c, i)) {
      return;
    }
    hh_swap(h, i, c);
    i = c;
  }
}

static ds_status_t hh_rehash(ds_hash_heap_t *h, size_t slots) {
  size_t *nt;
  size_t i;
  if (slots > ((size_t)-1) / sizeof(size_t)) {
    return DS_ERR_OVERFLOW;
  }
  nt = (size_t *)ds_context_calloc(h->context, slots, sizeof(size_t));
  if (nt == NULL) {
    return DS_ERR_ALLOC;
  }
  ds_context_free(h->context, h->table);
  h->table = nt;
  h->slots = slots;
  for (i = 0; i < h->size; i++) {
    hh_set_pos(h, i);
  }
  return DS_OK;
}

/* Backward-shift deletion of table slot i. */
static void hh_erase_slot(ds_hash_heap_t *h, size_t i) {
  size_t j = i;
  size_t home;
  size_t mask = h->slots - 1;
  h->table[i] = 0;
  for (;;) {
    j = (j + 1) & mask;
    if (h->table[j] == 0) {
      return;
    }
    home = hh_home(h, hh_key(h, h->table[j] - 1));
    if ((i < j && (home <= i || home > j)) ||
        (i > j && home <= i && home > j)) {
      h->table[i] = h->table[j];
      h->table[j] = 0;
      i = j;
    }
  }
}

ds_status_t ds_hash_heap_push(ds_hash_heap_t *h, const void *key,
                              const void *prio) {
  size_t s;
  size_t pos;
  size_t cap;
  void *p;
  ds_status_t st;
  if (h == NULL || key == NULL || prio == NULL) {
    return DS_ERR_NULL;
  }
  if (h->slots > 0) {
    s = hh_slot(h, key);
    if (h->table[s] != 0) {
      pos = h->table[s] - 1;
      memcpy(hh_prio(h, pos), prio, h->prio_size);
      hh_up(h, pos);
      hh_down(h, h->table[hh_slot(h, key)] - 1);
      return DS_EXISTS;
    }
  }
  if (h->size == h->capacity) {
    cap = h->capacity < 8 ? 8 : h->capacity * 2;
    if (cap <= h->capacity || cap > ((size_t)-1) / h->key_size ||
        cap > ((size_t)-1) / h->prio_size) {
      return DS_ERR_OVERFLOW;
    }
    p = ds_context_realloc(h->context, h->keys, cap * h->key_size);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    h->keys = (unsigned char *)p;
    p = ds_context_realloc(h->context, h->prios, cap * h->prio_size);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    h->prios = (unsigned char *)p;
    h->capacity = cap;
  }
  if (h->slots == 0 || (h->size + 1) * 4 > h->slots * 3) {
    st = hh_rehash(h, h->slots == 0 ? 16 : h->slots * 2);
    if (st != DS_OK) {
      return st;
    }
  }
  pos = h->size++;
  memcpy(hh_key(h, pos), key, h->key_size);
  memcpy(hh_prio(h, pos), prio, h->prio_size);
  hh_set_pos(h, pos);
  hh_up(h, pos);
  return DS_OK;
}

ds_status_t ds_hash_heap_peek(const ds_hash_heap_t *h, void *out_key,
                              void *out_prio) {
  if (h == NULL) {
    return DS_ERR_NULL;
  }
  if (h->size == 0) {
    return DS_EMPTY;
  }
  if (out_key != NULL) {
    memcpy(out_key, hh_key(h, 0), h->key_size);
  }
  if (out_prio != NULL) {
    memcpy(out_prio, hh_prio(h, 0), h->prio_size);
  }
  return DS_OK;
}

/* Remove the entry at heap position pos. */
static void hh_remove_at(ds_hash_heap_t *h, size_t pos) {
  size_t last = h->size - 1;
  hh_erase_slot(h, hh_slot(h, hh_key(h, pos)));
  if (pos != last) {
    memcpy(hh_key(h, pos), hh_key(h, last), h->key_size);
    memcpy(hh_prio(h, pos), hh_prio(h, last), h->prio_size);
    h->size--;
    hh_set_pos(h, pos);
    /* restore order in whichever direction; if it rose, the parent
     * that came down to pos is already in place */
    hh_up(h, pos);
    hh_down(h, pos);
  } else {
    h->size--;
  }
}

ds_status_t ds_hash_heap_pop(ds_hash_heap_t *h, void *out_key, void *out_prio) {
  ds_status_t st = ds_hash_heap_peek(h, out_key, out_prio);
  if (st != DS_OK) {
    return st;
  }
  hh_remove_at(h, 0);
  return DS_OK;
}

ds_status_t ds_hash_heap_priority(const ds_hash_heap_t *h, const void *key,
                                  void *out_prio) {
  size_t s;
  if (h == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  if (h->slots == 0) {
    return DS_NOT_FOUND;
  }
  s = hh_slot(h, key);
  if (h->table[s] == 0) {
    return DS_NOT_FOUND;
  }
  if (out_prio != NULL) {
    memcpy(out_prio, hh_prio(h, h->table[s] - 1), h->prio_size);
  }
  return DS_OK;
}

ds_status_t ds_hash_heap_remove(ds_hash_heap_t *h, const void *key,
                                void *out_prio) {
  size_t s;
  size_t pos;
  if (h == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  if (h->slots == 0) {
    return DS_NOT_FOUND;
  }
  s = hh_slot(h, key);
  if (h->table[s] == 0) {
    return DS_NOT_FOUND;
  }
  pos = h->table[s] - 1;
  if (out_prio != NULL) {
    memcpy(out_prio, hh_prio(h, pos), h->prio_size);
  }
  hh_remove_at(h, pos);
  return DS_OK;
}

bool ds_hash_heap_contains(const ds_hash_heap_t *h, const void *key) {
  return ds_hash_heap_priority(h, key, NULL) == DS_OK;
}
