#include "sorted_list.h"
#include <string.h>

static ds_status_t sl_grow_bytes(ds_context_t *ctx, unsigned char **data,
                                 size_t *capacity, size_t need,
                                 size_t elem_size) {
  size_t cap = *capacity < 8 ? 8 : *capacity;
  unsigned char *d;
  while (cap < need) {
    if (cap > ((size_t)-1) / 2) {
      return DS_ERR_OVERFLOW;
    }
    cap *= 2;
  }
  if (cap > ((size_t)-1) / elem_size) {
    return DS_ERR_OVERFLOW;
  }
  d = (unsigned char *)ds_context_realloc(ctx, *data, cap * elem_size);
  if (d == NULL) {
    return DS_ERR_ALLOC;
  }
  *data = d;
  *capacity = cap;
  return DS_OK;
}

/* ---- sorted array -------------------------------------------------------- */

static unsigned char *sa_at(const ds_sorted_array_t *a, size_t i) {
  return a->data + i * a->elem_size;
}

ds_status_t ds_sorted_array_init(ds_sorted_array_t *a, size_t elem_size,
                                 ds_sorted_compare_func_t compare, void *user,
                                 bool unique, ds_context_t *context) {
  if (a == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(a, 0, sizeof(*a));
  a->elem_size = elem_size;
  a->compare = compare;
  a->user = user;
  a->unique = unique;
  a->context = context;
  return DS_OK;
}

ds_status_t ds_sorted_array_init_buffer(ds_sorted_array_t *a, size_t elem_size,
                                        ds_sorted_compare_func_t compare,
                                        void *user, bool unique, void *buffer,
                                        size_t capacity) {
  ds_status_t st =
      ds_sorted_array_init(a, elem_size, compare, user, unique, NULL);
  if (st != DS_OK) {
    return st;
  }
  if (buffer == NULL && capacity > 0) {
    return DS_ERR_NULL;
  }
  a->data = (unsigned char *)buffer;
  a->capacity = capacity;
  a->fixed = true;
  return DS_OK;
}

void ds_sorted_array_destroy(ds_sorted_array_t *a) {
  if (a == NULL) {
    return;
  }
  if (!a->fixed) {
    ds_context_free(a->context, a->data);
  }
  a->data = NULL;
  a->size = a->capacity = 0;
}

void ds_sorted_array_clear(ds_sorted_array_t *a) {
  if (a != NULL) {
    a->size = 0;
  }
}

size_t ds_sorted_array_size(const ds_sorted_array_t *a) {
  return a == NULL ? 0 : a->size;
}

static size_t sa_bound(const ds_sorted_array_t *a, const void *key,
                       bool upper) {
  size_t lo = 0;
  size_t hi = a->size;
  size_t mid;
  int c;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    c = a->compare(sa_at(a, mid), key, a->user);
    if (c < 0 || (upper && c == 0)) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

size_t ds_sorted_array_lower_bound(const ds_sorted_array_t *a,
                                   const void *key) {
  return a == NULL || key == NULL ? 0 : sa_bound(a, key, false);
}

size_t ds_sorted_array_upper_bound(const ds_sorted_array_t *a,
                                   const void *key) {
  return a == NULL || key == NULL ? 0 : sa_bound(a, key, true);
}

static ds_status_t sa_reserve(ds_sorted_array_t *a, size_t need) {
  if (need <= a->capacity) {
    return DS_OK;
  }
  if (a->fixed) {
    return DS_ERR_OVERFLOW;
  }
  return sl_grow_bytes(a->context, &a->data, &a->capacity, need, a->elem_size);
}

ds_status_t ds_sorted_array_insert(ds_sorted_array_t *a, const void *elem,
                                   size_t *out_index) {
  size_t i;
  ds_status_t st;
  if (a == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  i = sa_bound(a, elem, true);
  if (a->unique && i > 0 && a->compare(sa_at(a, i - 1), elem, a->user) == 0) {
    if (out_index != NULL) {
      *out_index = i - 1;
    }
    return DS_EXISTS;
  }
  st = sa_reserve(a, a->size + 1);
  if (st != DS_OK) {
    return st;
  }
  memmove(sa_at(a, i + 1), sa_at(a, i), (a->size - i) * a->elem_size);
  memcpy(sa_at(a, i), elem, a->elem_size);
  a->size++;
  if (out_index != NULL) {
    *out_index = i;
  }
  return DS_OK;
}

ds_status_t ds_sorted_array_find(const ds_sorted_array_t *a, const void *key,
                                 size_t *out_index) {
  size_t i;
  if (a == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  i = sa_bound(a, key, false);
  if (i == a->size || a->compare(sa_at(a, i), key, a->user) != 0) {
    return DS_NOT_FOUND;
  }
  if (out_index != NULL) {
    *out_index = i;
  }
  return DS_OK;
}

ds_status_t ds_sorted_array_remove_at(ds_sorted_array_t *a, size_t index,
                                      void *out) {
  if (a == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= a->size) {
    return DS_ERR_RANGE;
  }
  if (out != NULL) {
    memcpy(out, sa_at(a, index), a->elem_size);
  }
  memmove(sa_at(a, index), sa_at(a, index + 1),
          (a->size - index - 1) * a->elem_size);
  a->size--;
  return DS_OK;
}

ds_status_t ds_sorted_array_remove(ds_sorted_array_t *a, const void *key,
                                   void *out) {
  size_t i;
  ds_status_t st = ds_sorted_array_find(a, key, &i);
  if (st != DS_OK) {
    return st;
  }
  return ds_sorted_array_remove_at(a, i, out);
}

const void *ds_sorted_array_at(const ds_sorted_array_t *a, size_t index) {
  if (a == NULL || index >= a->size) {
    return NULL;
  }
  return sa_at(a, index);
}

ds_status_t ds_sorted_array_merge(ds_sorted_array_t *a,
                                  const ds_sorted_array_t *src) {
  size_t i;
  size_t j;
  size_t k;
  size_t total;
  int c;
  ds_status_t st;
  if (a == NULL || src == NULL) {
    return DS_ERR_NULL;
  }
  if (a == src || a->elem_size != src->elem_size) {
    return DS_ERR_INVALID;
  }
  if (src->size > ((size_t)-1) - a->size) {
    return DS_ERR_OVERFLOW;
  }
  total = a->size + src->size;
  st = sa_reserve(a, total);
  if (st != DS_OK) {
    return st;
  }
  /* merge from the back; unique arrays skip duplicates afterwards */
  i = a->size;
  j = src->size;
  k = total;
  while (j > 0) {
    if (i > 0) {
      c = a->compare(sa_at(a, i - 1), src->data + (j - 1) * src->elem_size,
                     a->user);
    } else {
      c = -1;
    }
    k--;
    if (c > 0) {
      memcpy(sa_at(a, k), sa_at(a, i - 1), a->elem_size);
      i--;
    } else {
      memcpy(sa_at(a, k), src->data + (j - 1) * src->elem_size, a->elem_size);
      j--;
    }
  }
  a->size = total;
  if (a->unique && total > 1) {
    k = 1;
    for (i = 1; i < total; i++) {
      if (a->compare(sa_at(a, k - 1), sa_at(a, i), a->user) != 0) {
        if (k != i) {
          memcpy(sa_at(a, k), sa_at(a, i), a->elem_size);
        }
        k++;
      }
    }
    a->size = k;
  }
  return DS_OK;
}

/* ---- ordered linked list ------------------------------------------------- */

#define SL_END DS_SORTED_LIST_END

static unsigned char *ol_at(const ds_sorted_list_t *l, size_t v) {
  return l->data + v * l->elem_size;
}

ds_status_t ds_sorted_list_init(ds_sorted_list_t *l, size_t elem_size,
                                ds_sorted_compare_func_t compare, void *user,
                                bool unique, ds_context_t *context) {
  if (l == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(l, 0, sizeof(*l));
  l->head = SL_END;
  l->free_list = SL_END;
  l->elem_size = elem_size;
  l->compare = compare;
  l->user = user;
  l->unique = unique;
  l->context = context;
  return DS_OK;
}

void ds_sorted_list_destroy(ds_sorted_list_t *l) {
  if (l == NULL) {
    return;
  }
  ds_context_free(l->context, l->data);
  ds_context_free(l->context, l->next);
  l->data = NULL;
  l->next = NULL;
  l->size = l->slots = l->capacity = 0;
  l->head = l->free_list = SL_END;
}

void ds_sorted_list_clear(ds_sorted_list_t *l) {
  if (l != NULL) {
    l->size = l->slots = 0;
    l->head = l->free_list = SL_END;
  }
}

size_t ds_sorted_list_size(const ds_sorted_list_t *l) {
  return l == NULL ? 0 : l->size;
}

static ds_status_t ol_node(ds_sorted_list_t *l, size_t *out) {
  size_t cap;
  void *p;
  if (l->free_list != SL_END) {
    *out = l->free_list;
    l->free_list = l->next[*out];
    return DS_OK;
  }
  if (l->slots == l->capacity) {
    cap = l->capacity < 8 ? 8 : l->capacity * 2;
    if (cap <= l->capacity || cap > ((size_t)-1) / l->elem_size ||
        cap > ((size_t)-1) / sizeof(size_t)) {
      return DS_ERR_OVERFLOW;
    }
    p = ds_context_realloc(l->context, l->data, cap * l->elem_size);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    l->data = (unsigned char *)p;
    p = ds_context_realloc(l->context, l->next, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    l->next = (size_t *)p;
    l->capacity = cap;
  }
  *out = l->slots++;
  return DS_OK;
}

ds_status_t ds_sorted_list_insert(ds_sorted_list_t *l, const void *elem) {
  size_t prev = SL_END;
  size_t v;
  size_t node;
  int c = -1;
  ds_status_t st;
  if (l == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  for (v = l->head; v != SL_END; prev = v, v = l->next[v]) {
    c = l->compare(ol_at(l, v), elem, l->user);
    if (c > 0 || (c == 0 && l->unique)) {
      break;
    }
  }
  if (v != SL_END && c == 0) {
    return DS_EXISTS;
  }
  st = ol_node(l, &node);
  if (st != DS_OK) {
    return st;
  }
  memcpy(ol_at(l, node), elem, l->elem_size);
  l->next[node] = v;
  if (prev == SL_END) {
    l->head = node;
  } else {
    l->next[prev] = node;
  }
  l->size++;
  return DS_OK;
}

static size_t ol_search(const ds_sorted_list_t *l, const void *key,
                        size_t *out_prev) {
  size_t prev = SL_END;
  size_t v;
  int c;
  for (v = l->head; v != SL_END; prev = v, v = l->next[v]) {
    c = l->compare(ol_at(l, v), key, l->user);
    if (c == 0) {
      *out_prev = prev;
      return v;
    }
    if (c > 0) {
      break; /* ordered: stop early */
    }
  }
  return SL_END;
}

ds_status_t ds_sorted_list_find(const ds_sorted_list_t *l, const void *key,
                                size_t *out_node) {
  size_t prev;
  size_t v;
  if (l == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  v = ol_search(l, key, &prev);
  if (v == SL_END) {
    return DS_NOT_FOUND;
  }
  if (out_node != NULL) {
    *out_node = v;
  }
  return DS_OK;
}

static void ol_unlink(ds_sorted_list_t *l, size_t prev, size_t v, void *out) {
  if (out != NULL) {
    memcpy(out, ol_at(l, v), l->elem_size);
  }
  if (prev == SL_END) {
    l->head = l->next[v];
  } else {
    l->next[prev] = l->next[v];
  }
  l->next[v] = l->free_list;
  l->free_list = v;
  l->size--;
}

ds_status_t ds_sorted_list_remove(ds_sorted_list_t *l, const void *key,
                                  void *out) {
  size_t prev;
  size_t v;
  if (l == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  v = ol_search(l, key, &prev);
  if (v == SL_END) {
    return DS_NOT_FOUND;
  }
  ol_unlink(l, prev, v, out);
  return DS_OK;
}

ds_status_t ds_sorted_list_pop_front(ds_sorted_list_t *l, void *out) {
  if (l == NULL) {
    return DS_ERR_NULL;
  }
  if (l->head == SL_END) {
    return DS_EMPTY;
  }
  ol_unlink(l, SL_END, l->head, out);
  return DS_OK;
}

size_t ds_sorted_list_first(const ds_sorted_list_t *l) {
  return l == NULL ? SL_END : l->head;
}

size_t ds_sorted_list_next(const ds_sorted_list_t *l, size_t node) {
  if (l == NULL || node >= l->slots) {
    return SL_END;
  }
  return l->next[node];
}

const void *ds_sorted_list_get(const ds_sorted_list_t *l, size_t node) {
  if (l == NULL || node >= l->slots) {
    return NULL;
  }
  return ol_at(l, node);
}

ds_status_t ds_sorted_list_merge(ds_sorted_list_t *l, ds_sorted_list_t *src) {
  size_t prev = SL_END;
  size_t v;
  size_t node;
  int c;
  ds_status_t st;
  if (l == NULL || src == NULL) {
    return DS_ERR_NULL;
  }
  if (l == src || l->elem_size != src->elem_size) {
    return DS_ERR_INVALID;
  }
  v = l->head;
  /* one ordered pass: src's head is inserted at or after prev */
  while (src->head != SL_END) {
    const unsigned char *e = ol_at(src, src->head);
    c = -1;
    while (v != SL_END) {
      c = l->compare(ol_at(l, v), e, l->user);
      if (c > 0 || (c == 0 && l->unique)) {
        break;
      }
      prev = v;
      v = l->next[v];
    }
    if (l->unique &&
        ((v != SL_END && c == 0) ||
         (prev != SL_END && l->compare(ol_at(l, prev), e, l->user) == 0))) {
      ol_unlink(src, SL_END, src->head, NULL);
      continue;
    }
    st = ol_node(l, &node);
    if (st != DS_OK) {
      return st;
    }
    ol_unlink(src, SL_END, src->head, ol_at(l, node));
    l->next[node] = v;
    if (prev == SL_END) {
      l->head = node;
    } else {
      l->next[prev] = node;
    }
    prev = node;
    l->size++;
  }
  return DS_OK;
}

/* ---- unsorted list ------------------------------------------------------- */

static unsigned char *ul_at(const ds_unsorted_list_t *l, size_t i) {
  return l->data + i * l->elem_size;
}

ds_status_t ds_unsorted_list_init(ds_unsorted_list_t *l, size_t elem_size,
                                  ds_sorted_compare_func_t equal, void *user,
                                  ds_context_t *context) {
  if (l == NULL || equal == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(l, 0, sizeof(*l));
  l->elem_size = elem_size;
  l->equal = equal;
  l->user = user;
  l->context = context;
  return DS_OK;
}

ds_status_t ds_unsorted_list_init_buffer(ds_unsorted_list_t *l,
                                         size_t elem_size,
                                         ds_sorted_compare_func_t equal,
                                         void *user, void *buffer,
                                         size_t capacity) {
  ds_status_t st = ds_unsorted_list_init(l, elem_size, equal, user, NULL);
  if (st != DS_OK) {
    return st;
  }
  if (buffer == NULL && capacity > 0) {
    return DS_ERR_NULL;
  }
  l->data = (unsigned char *)buffer;
  l->capacity = capacity;
  l->fixed = true;
  return DS_OK;
}

void ds_unsorted_list_destroy(ds_unsorted_list_t *l) {
  if (l == NULL) {
    return;
  }
  if (!l->fixed) {
    ds_context_free(l->context, l->data);
  }
  l->data = NULL;
  l->size = l->capacity = 0;
}

void ds_unsorted_list_clear(ds_unsorted_list_t *l) {
  if (l != NULL) {
    l->size = 0;
  }
}

size_t ds_unsorted_list_size(const ds_unsorted_list_t *l) {
  return l == NULL ? 0 : l->size;
}

ds_status_t ds_unsorted_list_add(ds_unsorted_list_t *l, const void *elem) {
  ds_status_t st;
  if (l == NULL || elem == NULL) {
    return DS_ERR_NULL;
  }
  if (l->size == l->capacity) {
    if (l->fixed) {
      return DS_ERR_OVERFLOW;
    }
    st = sl_grow_bytes(l->context, &l->data, &l->capacity, l->size + 1,
                       l->elem_size);
    if (st != DS_OK) {
      return st;
    }
  }
  memcpy(ul_at(l, l->size), elem, l->elem_size);
  l->size++;
  return DS_OK;
}

ds_status_t ds_unsorted_list_find(const ds_unsorted_list_t *l, const void *key,
                                  size_t *out_index) {
  size_t i;
  if (l == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < l->size; i++) {
    if (l->equal(ul_at(l, i), key, l->user) == 0) {
      if (out_index != NULL) {
        *out_index = i;
      }
      return DS_OK;
    }
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_unsorted_list_remove_at(ds_unsorted_list_t *l, size_t index,
                                       void *out) {
  if (l == NULL) {
    return DS_ERR_NULL;
  }
  if (index >= l->size) {
    return DS_ERR_RANGE;
  }
  if (out != NULL) {
    memcpy(out, ul_at(l, index), l->elem_size);
  }
  l->size--;
  if (index != l->size) {
    memcpy(ul_at(l, index), ul_at(l, l->size), l->elem_size);
  }
  return DS_OK;
}

ds_status_t ds_unsorted_list_remove(ds_unsorted_list_t *l, const void *key,
                                    void *out) {
  size_t i;
  ds_status_t st = ds_unsorted_list_find(l, key, &i);
  if (st != DS_OK) {
    return st;
  }
  return ds_unsorted_list_remove_at(l, i, out);
}

const void *ds_unsorted_list_at(const ds_unsorted_list_t *l, size_t index) {
  if (l == NULL || index >= l->size) {
    return NULL;
  }
  return ul_at(l, index);
}
