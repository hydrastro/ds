#include "association_list.h"
#include <string.h>

#define AL_END DS_ALIST_END

ds_status_t ds_alist_init(ds_alist_t *l, size_t key_size, size_t value_size,
                          ds_alist_equal_func_t equal, void *user,
                          ds_context_t *context) {
  if (l == NULL || equal == NULL) {
    return DS_ERR_NULL;
  }
  if (key_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(l, 0, sizeof(*l));
  l->head = AL_END;
  l->free_list = AL_END;
  l->key_size = key_size;
  l->value_size = value_size;
  l->equal = equal;
  l->user = user;
  l->context = context;
  return DS_OK;
}

void ds_alist_destroy(ds_alist_t *l) {
  if (l == NULL) {
    return;
  }
  ds_context_free(l->context, l->keys);
  ds_context_free(l->context, l->values);
  ds_context_free(l->context, l->next);
  l->keys = l->values = NULL;
  l->next = NULL;
  l->size = l->slots = l->capacity = 0;
  l->head = l->free_list = AL_END;
}

void ds_alist_clear(ds_alist_t *l) {
  if (l != NULL) {
    l->size = l->slots = 0;
    l->head = l->free_list = AL_END;
  }
}

size_t ds_alist_size(const ds_alist_t *l) { return l == NULL ? 0 : l->size; }

static unsigned char *al_key(const ds_alist_t *l, size_t v) {
  return l->keys + v * l->key_size;
}

static unsigned char *al_val(const ds_alist_t *l, size_t v) {
  return l->values + v * l->value_size;
}

static ds_status_t al_node(ds_alist_t *l, size_t *out) {
  size_t cap;
  void *p;
  if (l->free_list != AL_END) {
    *out = l->free_list;
    l->free_list = l->next[*out];
    return DS_OK;
  }
  if (l->slots == l->capacity) {
    cap = l->capacity < 8 ? 8 : l->capacity * 2;
    if (cap <= l->capacity || cap > ((size_t)-1) / l->key_size ||
        (l->value_size > 0 && cap > ((size_t)-1) / l->value_size) ||
        cap > ((size_t)-1) / sizeof(size_t)) {
      return DS_ERR_OVERFLOW;
    }
    p = ds_context_realloc(l->context, l->keys, cap * l->key_size);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    l->keys = (unsigned char *)p;
    p = ds_context_realloc(l->context, l->values,
                           l->value_size > 0 ? cap * l->value_size : 1);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    l->values = (unsigned char *)p;
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

ds_status_t ds_alist_push(ds_alist_t *l, const void *key, const void *value) {
  size_t v;
  ds_status_t st;
  if (l == NULL || key == NULL || (value == NULL && l->value_size > 0)) {
    return DS_ERR_NULL;
  }
  st = al_node(l, &v);
  if (st != DS_OK) {
    return st;
  }
  memcpy(al_key(l, v), key, l->key_size);
  if (l->value_size > 0) {
    memcpy(al_val(l, v), value, l->value_size);
  }
  l->next[v] = l->head;
  l->head = v;
  l->size++;
  return DS_OK;
}

static void al_unlink(ds_alist_t *l, size_t prev, size_t v) {
  if (prev == AL_END) {
    l->head = l->next[v];
  } else {
    l->next[prev] = l->next[v];
  }
  l->next[v] = l->free_list;
  l->free_list = v;
  l->size--;
}

ds_status_t ds_alist_pop(ds_alist_t *l, void *out_key, void *out_value) {
  size_t v;
  if (l == NULL) {
    return DS_ERR_NULL;
  }
  if (l->head == AL_END) {
    return DS_EMPTY;
  }
  v = l->head;
  if (out_key != NULL) {
    memcpy(out_key, al_key(l, v), l->key_size);
  }
  if (out_value != NULL && l->value_size > 0) {
    memcpy(out_value, al_val(l, v), l->value_size);
  }
  al_unlink(l, AL_END, v);
  return DS_OK;
}

static size_t al_find(const ds_alist_t *l, const void *key, size_t from,
                      size_t *out_prev) {
  size_t prev = *out_prev;
  size_t v;
  for (v = from; v != AL_END; prev = v, v = l->next[v]) {
    if (l->equal(al_key(l, v), key, l->user)) {
      *out_prev = prev;
      return v;
    }
  }
  return AL_END;
}

void *ds_alist_lookup(const ds_alist_t *l, const void *key) {
  size_t prev = AL_END;
  size_t v;
  if (l == NULL || key == NULL) {
    return NULL;
  }
  v = al_find(l, key, l->head, &prev);
  return v == AL_END ? NULL : al_val(l, v);
}

size_t ds_alist_lookup_all(const ds_alist_t *l, const void *key,
                           ds_alist_visit_func_t f, void *user) {
  size_t v;
  size_t n = 0;
  if (l == NULL || key == NULL) {
    return 0;
  }
  for (v = l->head; v != AL_END; v = l->next[v]) {
    if (l->equal(al_key(l, v), key, l->user)) {
      n++;
      if (f != NULL && !f(al_key(l, v), al_val(l, v), user)) {
        break;
      }
    }
  }
  return n;
}

ds_status_t ds_alist_put(ds_alist_t *l, const void *key, const void *value) {
  size_t prev = AL_END;
  size_t v;
  if (l == NULL || key == NULL || (value == NULL && l->value_size > 0)) {
    return DS_ERR_NULL;
  }
  v = al_find(l, key, l->head, &prev);
  if (v == AL_END) {
    return ds_alist_push(l, key, value);
  }
  if (l->value_size > 0) {
    memcpy(al_val(l, v), value, l->value_size);
  }
  return DS_EXISTS;
}

ds_status_t ds_alist_get(const ds_alist_t *l, const void *key,
                         void *out_value) {
  const void *p;
  if (l == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  p = ds_alist_lookup(l, key);
  if (p == NULL) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && l->value_size > 0) {
    memcpy(out_value, p, l->value_size);
  }
  return DS_OK;
}

ds_status_t ds_alist_remove(ds_alist_t *l, const void *key, bool all) {
  size_t prev = AL_END;
  size_t v;
  size_t next;
  bool any = false;
  if (l == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  v = al_find(l, key, l->head, &prev);
  while (v != AL_END) {
    any = true;
    next = l->next[v];
    al_unlink(l, prev, v);
    if (!all) {
      break;
    }
    v = al_find(l, key, next, &prev);
  }
  return any ? DS_OK : DS_NOT_FOUND;
}

void ds_alist_foreach(const ds_alist_t *l, ds_alist_visit_func_t f,
                      void *user) {
  size_t v;
  if (l == NULL || f == NULL) {
    return;
  }
  for (v = l->head; v != AL_END; v = l->next[v]) {
    if (!f(al_key(l, v), al_val(l, v), user)) {
      return;
    }
  }
}
