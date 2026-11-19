#include <dsa/algorithms/sort.h>
#include <dsa/core/checked.h>
#include <dsa/structures/bst.h>
#include <dsa/structures/jellyfish.h>
#include <dsa/structures/list.h>
#include <string.h>

typedef struct jf_item {
  ds_list_node_t link;
  unsigned long key;
} jf_item_t;
typedef struct jf_body {
  ds_bst_node_t link;
  size_t rank, item;
} jf_body_t;
typedef struct jf_tentacle {
  ds_list_t list;
  ds_list_node_t nil;
} jf_tentacle_t;
typedef struct jf_impl {
  unsigned char *items;
  size_t count, body_count;
  jf_body_t *body;
  jf_tentacle_t *tentacles;
  ds_bst_node_t nil, *root;
  ds_ordered_t ordered;
} jf_impl_t;

static jf_item_t *jf_item(const ds_jellyfish_t *t, const jf_impl_t *s,
                          size_t i) {
  return (jf_item_t *)(void *)(s->items + i * t->stride);
}
static void *jf_value(const ds_jellyfish_t *t, jf_item_t *item) {
  return t->value_size == 0 ? (void *)&item->key
                            : (unsigned char *)(void *)item + t->value_offset;
}
static int jf_compare(const void *a, const void *b, void *user) {
  const jf_item_t *x = (const jf_item_t *)a, *y = (const jf_item_t *)b;
  (void)user;
  return (x->key > y->key) - (x->key < y->key);
}
static size_t jf_sqrt(size_t n) {
  size_t lo = 1, hi = n, result = 0;
  while (lo <= hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (mid <= n / mid) {
      result = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return result;
}
static ds_bst_node_t *jf_balance(jf_impl_t *s, size_t lo, size_t hi,
                                 ds_bst_node_t *parent) {
  size_t mid;
  ds_bst_node_t *node;
  if (lo == hi) {
    return &s->nil;
  }
  mid = lo + (hi - lo) / 2;
  node = &s->body[mid].link;
  node->parent = parent;
  node->left = jf_balance(s, lo, mid, node);
  node->right = jf_balance(s, mid + 1, hi, node);
  return node;
}
static void jf_free(const ds_jellyfish_t *t, jf_impl_t *s) {
  if (s != NULL) {
    ds_ordered_destroy(&s->ordered);
    ds_context_free(t->context, s->items);
    ds_context_free(t->context, s->body);
    ds_context_free(t->context, s->tentacles);
    ds_context_free(t->context, s);
  }
}
/* Takes ownership of items even on failure; items are already sorted. */
static ds_status_t jf_build(const ds_jellyfish_t *t, unsigned char *items,
                            size_t count, jf_impl_t **out) {
  jf_impl_t *s;
  size_t bodies = jf_sqrt(count), buckets = bodies + 1, i, j, at = 0;
  size_t each = (count - bodies) / buckets, extra = (count - bodies) % buckets;
  ds_status_t st = DS_OK;
  s = (jf_impl_t *)ds_context_calloc(t->context, 1, sizeof(*s));
  if (s == NULL) {
    ds_context_free(t->context, items);
    return DS_ERR_ALLOC;
  }
  s->items = items;
  s->count = count;
  s->body_count = bodies;
  if (bodies > (size_t)-1 / sizeof(jf_body_t) ||
      buckets > (size_t)-1 / sizeof(jf_tentacle_t)) {
    st = DS_ERR_OVERFLOW;
    goto fail;
  }
  if (bodies > 0) {
    s->body =
        (jf_body_t *)ds_context_calloc(t->context, bodies, sizeof(jf_body_t));
  }
  s->tentacles = (jf_tentacle_t *)ds_context_calloc(t->context, buckets,
                                                    sizeof(jf_tentacle_t));
  if ((bodies > 0 && s->body == NULL) || s->tentacles == NULL) {
    st = DS_ERR_ALLOC;
    goto fail;
  }
  if (t->body_ops != NULL) {
    st = ds_ordered_init(&s->ordered, t->body_ops, sizeof(size_t), t->context);
    if (st != DS_OK) {
      goto fail;
    }
  }
  for (i = 0; i < buckets; i++) {
    ds_list_t *list = &s->tentacles[i].list;
    list->nil = &s->tentacles[i].nil;
    list->nil->next = list->nil;
    list->head = list->tail = list->nil;
    for (j = 0; j < each + (i < extra ? 1 : 0); j++) {
      ds_list_append(list, &jf_item(t, s, at++)->link);
    }
    if (i < bodies) {
      s->body[i].rank = i;
      s->body[i].item = at;
      if (t->body_ops != NULL) {
        st = ds_ordered_insert(&s->ordered, jf_item(t, s, at)->key, &i);
        if (st != DS_OK) {
          if (st > DS_OK) {
            st = DS_ERR_CALLBACK;
          }
          goto fail;
        }
      }
      at++;
    }
  }
  s->nil.left = s->nil.right = s->nil.parent = &s->nil;
  s->root = jf_balance(s, 0, bodies, &s->nil);
  *out = s;
  return DS_OK;
fail:
  jf_free(t, s);
  return st;
}
static ds_status_t jf_allocate(const ds_jellyfish_t *t, size_t count,
                               unsigned char **out) {
  size_t bytes;
  if (count == 0 || t->stride == 0) {
    return DS_ERR_STATE;
  }
  if (!ds_size_mul(count, t->stride, &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  *out =
      bytes > 0 ? (unsigned char *)ds_context_alloc(t->context, bytes) : NULL;
  return *out == NULL ? DS_ERR_ALLOC : DS_OK;
}
static void jf_write(const ds_jellyfish_t *t, unsigned char *at,
                     unsigned long key, const void *value) {
  jf_item_t *item = (jf_item_t *)(void *)at;
  memset(at, 0, t->stride);
  item->key = key;
  if (t->value_size > 0 && value != NULL) {
    memcpy(at + t->value_offset, value, t->value_size);
  }
}
void ds_jellyfish_config_init(ds_jellyfish_config_t *c) {
  if (c != NULL) {
    memset(c, 0, sizeof(*c));
  }
}
ds_status_t ds_jellyfish_init(ds_jellyfish_t *out,
                              const ds_jellyfish_config_t *c) {
  ds_jellyfish_t t;
  jf_impl_t *s;
  ds_status_t st;
  size_t bytes;
  if (out == NULL || c == NULL) {
    return DS_ERR_NULL;
  }
  memset(&t, 0, sizeof(t));
  t.context = c->context;
  t.body_ops = c->body_ops;
  t.value_size = c->value_size;
  if (!ds_size_align(sizeof(jf_item_t), DS_MAX_ALIGN, &t.value_offset) ||
      !ds_size_add(t.value_offset, c->value_size, &bytes) ||
      !ds_size_align(bytes, DS_MAX_ALIGN, &t.stride)) {
    return DS_ERR_OVERFLOW;
  }
  st = jf_build(&t, NULL, 0, &s);
  if (st != DS_OK) {
    return st;
  }
  t.impl = s;
  *out = t;
  return DS_OK;
}
void ds_jellyfish_destroy(ds_jellyfish_t *t) {
  if (t != NULL) {
    jf_free(t, (jf_impl_t *)t->impl);
    memset(t, 0, sizeof(*t));
  }
}
static ds_status_t jf_publish(ds_jellyfish_t *t, unsigned char *items,
                              size_t count) {
  jf_impl_t *stage;
  ds_status_t st = jf_build(t, items, count, &stage);
  if (st == DS_OK) {
    jf_free(t, (jf_impl_t *)t->impl);
    t->impl = stage;
  }
  return st;
}
ds_status_t ds_jellyfish_assign(ds_jellyfish_t *t, const unsigned long *keys,
                                const void *values, size_t count) {
  unsigned char *items;
  size_t i;
  ds_status_t st;
  if (t == NULL || (count > 0 && keys == NULL)) {
    return DS_ERR_NULL;
  }
  if (t->impl == NULL) {
    return DS_ERR_STATE;
  }
  if (count == 0) {
    return jf_publish(t, NULL, 0);
  }
  st = jf_allocate(t, count, &items);
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < count; i++) {
    jf_write(t, items + i * t->stride, keys[i],
             values != NULL ? (const unsigned char *)values + i * t->value_size
                            : NULL);
  }
  ds_intro_sort(items, count, t->stride, jf_compare, NULL);
  for (i = 1; i < count; i++) {
    if (jf_compare(items + (i - 1) * t->stride, items + i * t->stride, NULL) ==
        0) {
      ds_context_free(t->context, items);
      return DS_ERR_INVALID;
    }
  }
  return jf_publish(t, items, count);
}
static size_t jf_rank(const ds_jellyfish_t *t, const jf_impl_t *s,
                      unsigned long key) {
  size_t lo = 0, hi = s->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (jf_item(t, s, mid)->key < key) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}
ds_status_t ds_jellyfish_put(ds_jellyfish_t *t, unsigned long key,
                             const void *value) {
  jf_impl_t *s;
  unsigned char *items;
  size_t pos;
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  s = (jf_impl_t *)t->impl;
  if (s == NULL) {
    return DS_ERR_STATE;
  }
  pos = jf_rank(t, s, key);
  if (pos < s->count && jf_item(t, s, pos)->key == key) {
    if (t->value_size > 0) {
      void *stored = jf_value(t, jf_item(t, s, pos));
      if (value != NULL) {
        memmove(stored, value, t->value_size);
      } else {
        memset(stored, 0, t->value_size);
      }
    }
    return DS_EXISTS;
  }
  if (s->count == (size_t)-1) {
    return DS_ERR_OVERFLOW;
  }
  st = jf_allocate(t, s->count + 1, &items);
  if (st != DS_OK) {
    return st;
  }
  if (pos > 0) {
    memcpy(items, s->items, pos * t->stride);
  }
  jf_write(t, items + pos * t->stride, key, value);
  if (pos < s->count) {
    memcpy(items + (pos + 1) * t->stride, s->items + pos * t->stride,
           (s->count - pos) * t->stride);
  }
  return jf_publish(t, items, s->count + 1);
}
ds_status_t ds_jellyfish_remove(ds_jellyfish_t *t, unsigned long key) {
  jf_impl_t *s;
  unsigned char *items;
  size_t pos;
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  s = (jf_impl_t *)t->impl;
  if (s == NULL) {
    return DS_ERR_STATE;
  }
  pos = jf_rank(t, s, key);
  if (pos == s->count || jf_item(t, s, pos)->key != key) {
    return DS_NOT_FOUND;
  }
  if (s->count == 1) {
    return jf_publish(t, NULL, 0);
  }
  st = jf_allocate(t, s->count - 1, &items);
  if (st != DS_OK) {
    return st;
  }
  if (pos > 0) {
    memcpy(items, s->items, pos * t->stride);
  }
  if (pos + 1 < s->count) {
    memcpy(items + pos * t->stride, s->items + (pos + 1) * t->stride,
           (s->count - pos - 1) * t->stride);
  }
  return jf_publish(t, items, s->count - 1);
}
ds_status_t ds_jellyfish_get(const ds_jellyfish_t *t, unsigned long key,
                             void **out) {
  const jf_impl_t *s;
  const ds_list_t *list;
  ds_list_node_t *node;
  size_t gap = 0;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  s = (const jf_impl_t *)t->impl;
  if (s == NULL) {
    return DS_ERR_STATE;
  }
  if (t->body_ops != NULL && s->body_count > 0) {
    unsigned long floor;
    ds_status_t st = ds_ordered_floor(&s->ordered, key, &floor);
    if (st == DS_OK) {
      size_t rank;
      void *slot = ds_ordered_find(&s->ordered, floor);
      if (slot == NULL) {
        return DS_ERR_STATE;
      }
      memcpy(&rank, slot, sizeof(rank));
      if (rank >= s->body_count) {
        return DS_ERR_STATE;
      }
      if (floor == key) {
        *out = jf_value(t, jf_item(t, s, s->body[rank].item));
        return DS_OK;
      }
      gap = rank + 1;
    } else if (st != DS_NOT_FOUND) {
      return st;
    }
  } else {
    ds_bst_node_t *body = s->root;
    while (body != &s->nil) {
      const jf_body_t *b = (const jf_body_t *)(const void *)body;
      jf_item_t *item = jf_item(t, s, b->item);
      if (key == item->key) {
        *out = jf_value(t, item);
        return DS_OK;
      }
      if (key < item->key) {
        body = body->left;
      } else {
        gap = b->rank + 1;
        body = body->right;
      }
    }
  }
  list = &s->tentacles[gap].list;
  for (node = list->head; node != list->nil; node = node->next) {
    jf_item_t *item = (jf_item_t *)(void *)node;
    if (item->key == key) {
      *out = jf_value(t, item);
      return DS_OK;
    }
    if (item->key > key) {
      break;
    }
  }
  return DS_NOT_FOUND;
}
size_t ds_jellyfish_size(const ds_jellyfish_t *t) {
  const jf_impl_t *s = t != NULL ? (const jf_impl_t *)t->impl : NULL;
  return s != NULL ? s->count : 0;
}
ds_status_t ds_jellyfish_shape(const ds_jellyfish_t *t,
                               ds_jellyfish_shape_t *out) {
  const jf_impl_t *s;
  ds_jellyfish_shape_t shape;
  size_t i;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  s = (const jf_impl_t *)t->impl;
  if (s == NULL) {
    return DS_ERR_STATE;
  }
  memset(&shape, 0, sizeof(shape));
  shape.items = s->count;
  shape.body_items = s->body_count;
  shape.tentacles = s->body_count + 1;
  for (i = 0; i < shape.tentacles; i++) {
    if (s->tentacles[i].list.size > shape.longest_tentacle) {
      shape.longest_tentacle = s->tentacles[i].list.size;
    }
  }
  if (t->body_ops == NULL) {
    for (i = s->body_count; i > 0; i >>= 1) {
      shape.binary_body_height++;
    }
  }
  *out = shape;
  return DS_OK;
}
ds_status_t ds_jellyfish_for_each(const ds_jellyfish_t *t,
                                  ds_ordered_visit_fn visit, void *user) {
  const jf_impl_t *s;
  size_t i;
  if (t == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  s = (const jf_impl_t *)t->impl;
  if (s == NULL) {
    return DS_ERR_STATE;
  }
  for (i = 0; i < s->count; i++) {
    jf_item_t *item = jf_item(t, s, i);
    ds_status_t st = visit(item->key, jf_value(t, item), user);
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
}
ds_status_t ds_jellyfish_tentacle(const ds_jellyfish_t *t, size_t index,
                                  ds_ordered_visit_fn visit, void *user) {
  const jf_impl_t *s;
  const ds_list_t *list;
  ds_list_node_t *node;
  if (t == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  s = (const jf_impl_t *)t->impl;
  if (s == NULL) {
    return DS_ERR_STATE;
  }
  if (index > s->body_count) {
    return DS_ERR_RANGE;
  }
  list = &s->tentacles[index].list;
  for (node = list->head; node != list->nil; node = node->next) {
    jf_item_t *item = (jf_item_t *)(void *)node;
    ds_status_t st = visit(item->key, jf_value(t, item), user);
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
}
