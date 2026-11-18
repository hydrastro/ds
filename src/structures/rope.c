#include "core/owner_alloc.h"
#include <dsa/structures/rope.h>
#include <stdlib.h>
#include <string.h>

#define ROPE_LEAF_BYTES 64

/* Nodes are immutable after construction, apart from reference counts.
 * Edits prepare a new balanced path and publish one root pointer. */
static void rope_release(ds_rope_t *r, ds_rope_node_t *n) {
  if (n == NULL || --n->refs != 0)
    return;
  if (n->is_leaf)
    DS_OWNER_FREE(r, n->data);
  else {
    rope_release(r, n->left);
    rope_release(r, n->right);
  }
  DS_OWNER_FREE(r, n);
}

static ds_status_t rope_retain(ds_rope_node_t *n, ds_rope_node_t **out) {
  if (n != NULL) {
    if (n->refs == (size_t)-1)
      return DS_ERR_OVERFLOW;
    n->refs++;
  }
  *out = n;
  return DS_OK;
}

static size_t rope_height(const ds_rope_node_t *n) {
  return n == NULL ? 0 : n->depth;
}

static ds_status_t rope_leaf(ds_rope_t *r, const char *s, size_t len,
                             ds_rope_node_t **out) {
  ds_rope_node_t *n;
  if (len == 0) {
    *out = NULL;
    return DS_OK;
  }
  if (len == (size_t)-1)
    return DS_ERR_OVERFLOW;
  n = (ds_rope_node_t *)DS_OWNER_ALLOC(r, sizeof(*n));
  if (n == NULL)
    return DS_ERR_ALLOC;
  memset(n, 0, sizeof(*n));
  n->data = (char *)DS_OWNER_ALLOC(r, len + 1);
  if (n->data == NULL) {
    DS_OWNER_FREE(r, n);
    return DS_ERR_ALLOC;
  }
  memcpy(n->data, s, len);
  n->data[len] = '\0';
  n->weight = n->length = len;
  n->depth = n->leaves = n->refs = 1;
  n->is_leaf = true;
  *out = n;
  return DS_OK;
}

/* Builds an internal node over borrowed children. */
static ds_status_t rope_inner(ds_rope_t *r, ds_rope_node_t *left,
                              ds_rope_node_t *right, ds_rope_node_t **out) {
  ds_rope_node_t *n;
  ds_status_t st;
  if (left == NULL)
    return rope_retain(right, out);
  if (right == NULL)
    return rope_retain(left, out);
  if (right->length > (size_t)-1 - left->length ||
      right->leaves > (size_t)-1 - left->leaves)
    return DS_ERR_OVERFLOW;
  n = (ds_rope_node_t *)DS_OWNER_ALLOC(r, sizeof(*n));
  if (n == NULL)
    return DS_ERR_ALLOC;
  memset(n, 0, sizeof(*n));
  st = rope_retain(left, &n->left);
  if (st == DS_OK)
    st = rope_retain(right, &n->right);
  if (st != DS_OK) {
    rope_release(r, n->left);
    DS_OWNER_FREE(r, n);
    return st;
  }
  n->weight = left->length;
  n->length = left->length + right->length;
  n->depth = 1 + (left->depth > right->depth ? left->depth : right->depth);
  n->leaves = left->leaves + right->leaves;
  n->refs = 1;
  *out = n;
  return DS_OK;
}

static ds_status_t rope_balance(ds_rope_t *r, ds_rope_node_t *l,
                                ds_rope_node_t *q, ds_rope_node_t **out) {
  ds_rope_node_t *a = NULL, *b = NULL;
  ds_status_t st;
  size_t lh = rope_height(l), rh = rope_height(q);
  if (lh > rh + 1) {
    if (rope_height(l->left) >= rope_height(l->right)) {
      st = rope_inner(r, l->right, q, &a);
      if (st == DS_OK)
        st = rope_inner(r, l->left, a, out);
    } else {
      ds_rope_node_t *m = l->right;
      st = rope_inner(r, l->left, m->left, &a);
      if (st == DS_OK)
        st = rope_inner(r, m->right, q, &b);
      if (st == DS_OK)
        st = rope_inner(r, a, b, out);
    }
  } else if (rh > lh + 1) {
    if (rope_height(q->right) >= rope_height(q->left)) {
      st = rope_inner(r, l, q->left, &a);
      if (st == DS_OK)
        st = rope_inner(r, a, q->right, out);
    } else {
      ds_rope_node_t *m = q->left;
      st = rope_inner(r, l, m->left, &a);
      if (st == DS_OK)
        st = rope_inner(r, m->right, q->right, &b);
      if (st == DS_OK)
        st = rope_inner(r, a, b, out);
    }
  } else
    st = rope_inner(r, l, q, out);
  rope_release(r, a);
  rope_release(r, b);
  return st;
}

static ds_status_t rope_join(ds_rope_t *r, ds_rope_node_t *a, ds_rope_node_t *b,
                             ds_rope_node_t **out) {
  ds_rope_node_t *part = NULL;
  ds_status_t st;
  if (a == NULL)
    return rope_retain(b, out);
  if (b == NULL)
    return rope_retain(a, out);
  if (b->length > (size_t)-1 - a->length)
    return DS_ERR_OVERFLOW;
  if (a->is_leaf && b->is_leaf && a->length + b->length <= ROPE_LEAF_BYTES) {
    char bytes[ROPE_LEAF_BYTES];
    memcpy(bytes, a->data, a->length);
    memcpy(bytes + a->length, b->data, b->length);
    return rope_leaf(r, bytes, a->length + b->length, out);
  }
  if (a->depth > b->depth + 1) {
    st = rope_join(r, a->right, b, &part);
    if (st == DS_OK)
      st = rope_balance(r, a->left, part, out);
  } else if (b->depth > a->depth + 1) {
    st = rope_join(r, a, b->left, &part);
    if (st == DS_OK)
      st = rope_balance(r, part, b->right, out);
  } else
    st = rope_inner(r, a, b, out);
  rope_release(r, part);
  return st;
}

static ds_status_t rope_build(ds_rope_t *r, const char *s, size_t len,
                              ds_rope_node_t **out) {
  ds_rope_node_t *l = NULL, *q = NULL;
  ds_status_t st;
  size_t mid;
  if (len <= ROPE_LEAF_BYTES)
    return rope_leaf(r, s, len, out);
  mid = len / 2;
  st = rope_build(r, s, mid, &l);
  if (st == DS_OK)
    st = rope_build(r, s + mid, len - mid, &q);
  if (st == DS_OK)
    st = rope_inner(r, l, q, out);
  rope_release(r, l);
  rope_release(r, q);
  return st;
}

/* Splits a borrowed root, returning two owned roots. */
static ds_status_t rope_split(ds_rope_t *r, ds_rope_node_t *n, size_t at,
                              ds_rope_node_t **left, ds_rope_node_t **right) {
  ds_rope_node_t *a = NULL, *b = NULL, *l = NULL, *q = NULL;
  ds_status_t st;
  if (n == NULL) {
    *left = *right = NULL;
    return DS_OK;
  }
  if (at == 0) {
    *left = NULL;
    return rope_retain(n, right);
  }
  if (at >= n->length) {
    *right = NULL;
    return rope_retain(n, left);
  }
  if (n->is_leaf) {
    st = rope_leaf(r, n->data, at, &l);
    if (st == DS_OK)
      st = rope_leaf(r, n->data + at, n->length - at, &q);
  } else if (at < n->weight) {
    st = rope_split(r, n->left, at, &l, &b);
    if (st == DS_OK)
      st = rope_join(r, b, n->right, &q);
  } else if (at > n->weight) {
    st = rope_split(r, n->right, at - n->weight, &a, &q);
    if (st == DS_OK)
      st = rope_join(r, n->left, a, &l);
  } else {
    st = rope_retain(n->left, &l);
    if (st == DS_OK)
      st = rope_retain(n->right, &q);
  }
  rope_release(r, a);
  rope_release(r, b);
  if (st == DS_OK) {
    *left = l;
    *right = q;
  } else {
    rope_release(r, l);
    rope_release(r, q);
  }
  return st;
}

ds_rope_t *ds_rope_create(void) { return ds_rope_create_alloc(malloc, free); }

static ds_rope_t *rope_create_internal(void *(*allocator)(size_t),
                                       void (*deallocator)(void *),
                                       ds_context_t *context) {
  ds_rope_t *r;
  if (allocator == NULL || deallocator == NULL)
    return NULL;
  r = (ds_rope_t *)DS_OWNER_ALLOCATE(context, allocator, sizeof(*r));
  if (r != NULL) {
    r->root = NULL;
    r->context = context;
    r->allocator = allocator;
    r->deallocator = deallocator;
  }
  return r;
}

ds_rope_t *ds_rope_create_alloc(void *(*allocator)(size_t),
                                void (*deallocator)(void *)) {
  return rope_create_internal(allocator, deallocator, NULL);
}
ds_status_t ds_rope_create_context(ds_context_t *context, ds_rope_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = rope_create_internal(malloc, free,
                              context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

ds_rope_t *ds_rope_from_string(const char *s, size_t len) {
  ds_rope_t *r;
  if (s == NULL && len != 0)
    return NULL;
  r = ds_rope_create();
  if (r != NULL && rope_build(r, s, len, &r->root) != DS_OK) {
    DS_OWNER_FREE(r, r);
    return NULL;
  }
  return r;
}

size_t ds_rope_length(ds_rope_t *r) {
  return r == NULL || r->root == NULL ? 0 : r->root->length;
}

char ds_rope_char_at(ds_rope_t *r, size_t index) {
  ds_rope_node_t *n;
  if (r == NULL || index >= ds_rope_length(r))
    return '\0';
  n = r->root;
  while (!n->is_leaf) {
    if (index < n->weight)
      n = n->left;
    else {
      index -= n->weight;
      n = n->right;
    }
  }
  return n->data[index];
}

ds_status_t ds_rope_append_ex(ds_rope_t *r, const char *s, size_t len) {
  ds_rope_node_t *fresh = NULL, *root = NULL;
  ds_status_t st;
  if (r == NULL || (s == NULL && len != 0))
    return DS_ERR_NULL;
  if (len == 0)
    return DS_OK;
  if (len > (size_t)-1 - ds_rope_length(r))
    return DS_ERR_OVERFLOW;
  st = rope_build(r, s, len, &fresh);
  if (st == DS_OK)
    st = rope_join(r, r->root, fresh, &root);
  rope_release(r, fresh);
  if (st == DS_OK) {
    rope_release(r, r->root);
    r->root = root;
  }
  return st;
}

void ds_rope_append(ds_rope_t *r, const char *s, size_t len) {
  (void)ds_rope_append_ex(r, s, len);
}

ds_status_t ds_rope_insert_ex(ds_rope_t *r, size_t index, const char *s,
                              size_t len) {
  ds_rope_node_t *l = NULL, *q = NULL, *mid = NULL, *lm = NULL, *root = NULL;
  ds_status_t st;
  if (r == NULL || (s == NULL && len != 0))
    return DS_ERR_NULL;
  if (len == 0)
    return DS_OK;
  if (index >= ds_rope_length(r))
    return ds_rope_append_ex(r, s, len);
  if (len > (size_t)-1 - ds_rope_length(r))
    return DS_ERR_OVERFLOW;
  st = rope_build(r, s, len, &mid);
  if (st == DS_OK)
    st = rope_split(r, r->root, index, &l, &q);
  if (st == DS_OK)
    st = rope_join(r, l, mid, &lm);
  if (st == DS_OK)
    st = rope_join(r, lm, q, &root);
  rope_release(r, l);
  rope_release(r, q);
  rope_release(r, mid);
  rope_release(r, lm);
  if (st == DS_OK) {
    rope_release(r, r->root);
    r->root = root;
  }
  return st;
}

void ds_rope_insert(ds_rope_t *r, size_t index, const char *s, size_t len) {
  (void)ds_rope_insert_ex(r, index, s, len);
}

ds_status_t ds_rope_delete_ex(ds_rope_t *r, size_t index, size_t count) {
  ds_rope_node_t *l = NULL, *rest = NULL, *mid = NULL, *q = NULL, *root = NULL;
  ds_status_t st;
  size_t total;
  if (r == NULL)
    return DS_ERR_NULL;
  total = ds_rope_length(r);
  if (count == 0 || index >= total)
    return DS_OK;
  if (count > total - index)
    count = total - index;
  st = rope_split(r, r->root, index, &l, &rest);
  if (st == DS_OK)
    st = rope_split(r, rest, count, &mid, &q);
  if (st == DS_OK)
    st = rope_join(r, l, q, &root);
  rope_release(r, l);
  rope_release(r, rest);
  rope_release(r, mid);
  rope_release(r, q);
  if (st == DS_OK) {
    rope_release(r, r->root);
    r->root = root;
  }
  return st;
}

void ds_rope_delete(ds_rope_t *r, size_t index, size_t count) {
  (void)ds_rope_delete_ex(r, index, count);
}

static size_t rope_substring(const ds_rope_node_t *n, size_t at, size_t count,
                             char *out) {
  size_t written;
  if (n == NULL || count == 0)
    return 0;
  if (n->is_leaf) {
    if (count > n->length - at)
      count = n->length - at;
    memcpy(out, n->data + at, count);
    return count;
  }
  if (at >= n->weight)
    return rope_substring(n->right, at - n->weight, count, out);
  written = rope_substring(n->left, at, count, out);
  if (written < count)
    written += rope_substring(n->right, 0, count - written, out + written);
  return written;
}

size_t ds_rope_substring(ds_rope_t *r, size_t index, size_t count, char *out) {
  size_t total = ds_rope_length(r);
  if (out == NULL || index >= total || count == 0)
    return 0;
  if (count > total - index)
    count = total - index;
  return rope_substring(r->root, index, count, out);
}

char *ds_rope_to_string(ds_rope_t *r) {
  size_t len;
  char *out;
  if (r == NULL)
    return NULL;
  len = ds_rope_length(r);
  if (len == (size_t)-1)
    return NULL;
  out = (char *)DS_OWNER_ALLOC(r, len + 1);
  if (out == NULL)
    return NULL;
  (void)rope_substring(r->root, 0, len, out);
  out[len] = '\0';
  return out;
}

ds_status_t ds_rope_concat_ex(ds_rope_t *r, ds_rope_t *other) {
  ds_rope_node_t *root = NULL;
  ds_status_t st;
  if (r == NULL || other == NULL)
    return DS_ERR_NULL;
  if (r == other || ((r->context != NULL || other->context != NULL)
                         ? (r->context == NULL || other->context == NULL ||
                            !ds_context_compatible(r->context, other->context))
                         : (r->allocator != other->allocator ||
                            r->deallocator != other->deallocator)))
    return DS_ERR_INVALID;
  st = rope_join(r, r->root, other->root, &root);
  if (st == DS_OK) {
    rope_release(r, r->root);
    rope_release(other, other->root);
    r->root = root;
    other->root = NULL;
  }
  return st;
}

void ds_rope_concat(ds_rope_t *r, ds_rope_t *other) {
  (void)ds_rope_concat_ex(r, other);
}

/* All edits maintain AVL balance already. Explicit rebalance also repacks
 * fragments into nearly full leaves, preparing the whole replacement. */
ds_status_t ds_rope_rebalance_ex(ds_rope_t *r) {
  char *bytes;
  ds_rope_node_t *root = NULL;
  ds_status_t st;
  if (r == NULL)
    return DS_ERR_NULL;
  if (r->root == NULL || r->root->is_leaf)
    return DS_OK;
  bytes = ds_rope_to_string(r);
  if (bytes == NULL)
    return ds_rope_length(r) == (size_t)-1 ? DS_ERR_OVERFLOW : DS_ERR_ALLOC;
  st = rope_build(r, bytes, ds_rope_length(r), &root);
  DS_OWNER_FREE(r, bytes);
  if (st == DS_OK) {
    rope_release(r, r->root);
    r->root = root;
  }
  return st;
}

void ds_rope_rebalance(ds_rope_t *r) { (void)ds_rope_rebalance_ex(r); }

void ds_rope_destroy(ds_rope_t *r) {
  if (r == NULL)
    return;
  rope_release(r, r->root);
  DS_OWNER_FREE(r, r);
}

ds_status_t ds_rope_char_at_ex(ds_rope_t *rope, size_t index, char *out) {
  if (rope == NULL || out == NULL)
    return DS_ERR_NULL;
  if (index >= ds_rope_length(rope))
    return DS_ERR_RANGE;
  *out = ds_rope_char_at(rope, index);
  return DS_OK;
}
ds_status_t ds_rope_to_string_ex(ds_rope_t *rope, char **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (rope == NULL)
    return DS_ERR_NULL;
  if (ds_rope_length(rope) == (size_t)-1)
    return DS_ERR_OVERFLOW;
  *out = ds_rope_to_string(rope);
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}
