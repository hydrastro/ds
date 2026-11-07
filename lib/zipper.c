#include "zipper.h"
#include <string.h>

#define ZE DS_ZIPPER_EMPTY

static ds_status_t zp_grow(ds_context_t *ctx, void **p, size_t elem,
                           size_t ncap) {
  void *q = ds_context_realloc(ctx, *p, ncap * elem);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *p = q;
  return DS_OK;
}

ds_status_t ds_zipper_store_init(ds_zipper_store_t *s, size_t elem_size,
                                 ds_context_t *context) {
  if (s == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(s, 0, sizeof(*s));
  s->elem_size = elem_size;
  s->node_free = ZE;
  s->frame_free = ZE;
  s->context = context;
  return DS_OK;
}

void ds_zipper_store_destroy(ds_zipper_store_t *s) {
  if (s == NULL) {
    return;
  }
  ds_context_free(s->context, s->value);
  ds_context_free(s->context, s->left);
  ds_context_free(s->context, s->right);
  ds_context_free(s->context, s->refs);
  ds_context_free(s->context, s->fvalue);
  ds_context_free(s->context, s->fsibling);
  ds_context_free(s->context, s->fup);
  ds_context_free(s->context, s->fdir);
  ds_context_free(s->context, s->frefs);
  ds_context_free(s->context, s->work);
  memset(s, 0, sizeof(*s));
}

size_t ds_zipper_store_nodes(const ds_zipper_store_t *s) {
  return s == NULL ? 0 : s->live_nodes;
}

size_t ds_zipper_store_frames(const ds_zipper_store_t *s) {
  return s == NULL ? 0 : s->live_frames;
}

void ds_zipper_tree_retain(ds_zipper_store_t *s, size_t tree) {
  if (s != NULL && tree != ZE) {
    s->refs[tree]++;
  }
}

static void zp_frame_retain(ds_zipper_store_t *s, size_t f) {
  if (f != ZE) {
    s->frefs[f]++;
  }
}

static bool zp_work_push(ds_zipper_store_t *s, size_t *top, size_t v) {
  if (*top == s->work_capacity) {
    size_t nc = s->work_capacity < 16 ? 64 : s->work_capacity * 2;
    if (zp_grow(s->context, (void **)&s->work, sizeof(size_t), nc) != DS_OK) {
      return false;
    }
    s->work_capacity = nc;
  }
  s->work[(*top)++] = v;
  return true;
}

/* Drop one reference to node t (tag 0) or frame f (tag 1); what reaches
 * zero is freed, children and parents in turn (work list, no recursion).
 * Entries are encoded as 2 * index + tag. */
static void zp_release(ds_zipper_store_t *s, size_t item) {
  size_t top = 0;
  if (!zp_work_push(s, &top, item)) {
    return; /* out of memory: leak rather than corrupt */
  }
  while (top > 0) {
    size_t e = s->work[--top];
    size_t idx = e / 2;
    if (e % 2 == 0) {
      if (--s->refs[idx] > 0) {
        continue;
      }
      if (s->left[idx] != ZE) {
        (void)zp_work_push(s, &top, 2 * s->left[idx]);
      }
      if (s->right[idx] != ZE) {
        (void)zp_work_push(s, &top, 2 * s->right[idx]);
      }
      s->left[idx] = s->node_free;
      s->node_free = idx;
      s->live_nodes--;
    } else {
      if (--s->frefs[idx] > 0) {
        continue;
      }
      if (s->fsibling[idx] != ZE) {
        (void)zp_work_push(s, &top, 2 * s->fsibling[idx]);
      }
      if (s->fup[idx] != ZE) {
        (void)zp_work_push(s, &top, 2 * s->fup[idx] + 1);
      }
      s->fup[idx] = s->frame_free;
      s->frame_free = idx;
      s->live_frames--;
    }
  }
}

void ds_zipper_tree_release(ds_zipper_store_t *s, size_t tree) {
  if (s != NULL && tree != ZE) {
    zp_release(s, 2 * tree);
  }
}

ds_status_t ds_zipper_tree_node(ds_zipper_store_t *s, const void *elem,
                                size_t left, size_t right, size_t *out) {
  size_t v;
  if (s == NULL || elem == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if ((left != ZE && left >= s->nodes) || (right != ZE && right >= s->nodes)) {
    return DS_ERR_RANGE;
  }
  if (s->node_free != ZE) {
    v = s->node_free;
    s->node_free = s->left[v];
  } else {
    if (s->nodes == s->node_capacity) {
      size_t nc = s->node_capacity < 8 ? 16 : s->node_capacity * 2;
      if (zp_grow(s->context, (void **)&s->value, s->elem_size, nc) ||
          zp_grow(s->context, (void **)&s->left, sizeof(size_t), nc) ||
          zp_grow(s->context, (void **)&s->right, sizeof(size_t), nc) ||
          zp_grow(s->context, (void **)&s->refs, sizeof(size_t), nc)) {
        return DS_ERR_ALLOC;
      }
      s->node_capacity = nc;
    }
    v = s->nodes++;
  }
  memcpy(s->value + v * s->elem_size, elem, s->elem_size);
  s->left[v] = left;
  s->right[v] = right;
  s->refs[v] = 1;
  ds_zipper_tree_retain(s, left);
  ds_zipper_tree_retain(s, right);
  s->live_nodes++;
  *out = v;
  return DS_OK;
}

const void *ds_zipper_tree_value(const ds_zipper_store_t *s, size_t tree) {
  return s == NULL || tree == ZE ? NULL : s->value + tree * s->elem_size;
}

size_t ds_zipper_tree_left(const ds_zipper_store_t *s, size_t tree) {
  return s == NULL || tree == ZE ? ZE : s->left[tree];
}

size_t ds_zipper_tree_right(const ds_zipper_store_t *s, size_t tree) {
  return s == NULL || tree == ZE ? ZE : s->right[tree];
}

size_t ds_zipper_tree_size(ds_zipper_store_t *s, size_t tree) {
  size_t top = 0;
  size_t n = 0;
  if (s == NULL || tree == ZE || !zp_work_push(s, &top, tree)) {
    return 0;
  }
  while (top > 0) {
    size_t v = s->work[--top];
    n++;
    if ((s->left[v] != ZE && !zp_work_push(s, &top, s->left[v])) ||
        (s->right[v] != ZE && !zp_work_push(s, &top, s->right[v]))) {
      return 0;
    }
  }
  return n;
}

static ds_status_t zp_frame(ds_zipper_store_t *s, unsigned char dir,
                            const void *elem, size_t sibling, size_t up,
                            size_t *out) {
  size_t f;
  if (s->frame_free != ZE) {
    f = s->frame_free;
    s->frame_free = s->fup[f];
  } else {
    if (s->frames == s->frame_capacity) {
      size_t nc = s->frame_capacity < 8 ? 16 : s->frame_capacity * 2;
      if (zp_grow(s->context, (void **)&s->fvalue, s->elem_size, nc) ||
          zp_grow(s->context, (void **)&s->fsibling, sizeof(size_t), nc) ||
          zp_grow(s->context, (void **)&s->fup, sizeof(size_t), nc) ||
          zp_grow(s->context, (void **)&s->fdir, 1, nc) ||
          zp_grow(s->context, (void **)&s->frefs, sizeof(size_t), nc)) {
        return DS_ERR_ALLOC;
      }
      s->frame_capacity = nc;
    }
    f = s->frames++;
  }
  memcpy(s->fvalue + f * s->elem_size, elem, s->elem_size);
  s->fdir[f] = dir;
  s->fsibling[f] = sibling;
  s->fup[f] = up;
  s->frefs[f] = 1;
  ds_zipper_tree_retain(s, sibling);
  zp_frame_retain(s, up);
  s->live_frames++;
  *out = f;
  return DS_OK;
}

ds_status_t ds_zipper_open(ds_zipper_store_t *s, size_t tree,
                           ds_zipper_t *out) {
  if (s == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  ds_zipper_tree_retain(s, tree);
  out->focus = tree;
  out->path = ZE;
  return DS_OK;
}

void ds_zipper_release(ds_zipper_store_t *s, ds_zipper_t z) {
  if (s == NULL) {
    return;
  }
  ds_zipper_tree_release(s, z.focus);
  if (z.path != ZE) {
    zp_release(s, 2 * z.path + 1);
  }
}

ds_status_t ds_zipper_copy(ds_zipper_store_t *s, ds_zipper_t z,
                           ds_zipper_t *out) {
  if (s == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  ds_zipper_tree_retain(s, z.focus);
  zp_frame_retain(s, z.path);
  *out = z;
  return DS_OK;
}

static ds_status_t zp_down(ds_zipper_store_t *s, ds_zipper_t z,
                           unsigned char dir, ds_zipper_t *out) {
  size_t f;
  size_t n = z.focus;
  ds_status_t st;
  if (s == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (n == ZE) {
    return DS_ERR_STATE;
  }
  st = zp_frame(s, dir, s->value + n * s->elem_size,
                dir == 0 ? s->right[n] : s->left[n], z.path, &f);
  if (st != DS_OK) {
    return st;
  }
  out->focus = dir == 0 ? s->left[n] : s->right[n];
  ds_zipper_tree_retain(s, out->focus);
  out->path = f;
  return DS_OK;
}

ds_status_t ds_zipper_down_left(ds_zipper_store_t *s, ds_zipper_t z,
                                ds_zipper_t *out) {
  return zp_down(s, z, 0, out);
}

ds_status_t ds_zipper_down_right(ds_zipper_store_t *s, ds_zipper_t z,
                                 ds_zipper_t *out) {
  return zp_down(s, z, 1, out);
}

ds_status_t ds_zipper_up(ds_zipper_store_t *s, ds_zipper_t z,
                         ds_zipper_t *out) {
  size_t f = z.path;
  size_t n;
  ds_status_t st;
  if (s == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (f == ZE) {
    return DS_ERR_STATE;
  }
  st = ds_zipper_tree_node(s, s->fvalue + f * s->elem_size,
                           s->fdir[f] == 0 ? z.focus : s->fsibling[f],
                           s->fdir[f] == 0 ? s->fsibling[f] : z.focus, &n);
  if (st != DS_OK) {
    return st;
  }
  out->focus = n;
  out->path = s->fup[f];
  zp_frame_retain(s, out->path);
  return DS_OK;
}

ds_status_t ds_zipper_close(ds_zipper_store_t *s, ds_zipper_t z,
                            size_t *out_tree) {
  ds_zipper_t cur;
  ds_status_t st;
  if (s == NULL || out_tree == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_zipper_copy(s, z, &cur);
  while (st == DS_OK && cur.path != ZE) {
    ds_zipper_t up;
    st = ds_zipper_up(s, cur, &up);
    ds_zipper_release(s, cur);
    if (st != DS_OK) {
      return st;
    }
    cur = up;
  }
  if (st != DS_OK) {
    return st;
  }
  *out_tree = cur.focus; /* the reference passes to the caller */
  return DS_OK;
}

const void *ds_zipper_value(const ds_zipper_store_t *s, ds_zipper_t z) {
  return ds_zipper_tree_value(s, z.focus);
}

ds_status_t ds_zipper_set(ds_zipper_store_t *s, ds_zipper_t z, const void *elem,
                          ds_zipper_t *out) {
  size_t n;
  ds_status_t st;
  if (s == NULL || elem == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (z.focus == ZE) {
    return DS_ERR_STATE;
  }
  st = ds_zipper_tree_node(s, elem, s->left[z.focus], s->right[z.focus], &n);
  if (st != DS_OK) {
    return st;
  }
  out->focus = n;
  out->path = z.path;
  zp_frame_retain(s, z.path);
  return DS_OK;
}

ds_status_t ds_zipper_replace(ds_zipper_store_t *s, ds_zipper_t z,
                              size_t subtree, ds_zipper_t *out) {
  if (s == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (subtree != ZE && subtree >= s->nodes) {
    return DS_ERR_RANGE;
  }
  ds_zipper_tree_retain(s, subtree);
  zp_frame_retain(s, z.path);
  out->focus = subtree;
  out->path = z.path;
  return DS_OK;
}

bool ds_zipper_at_root(ds_zipper_t z) { return z.path == ZE; }

size_t ds_zipper_depth(const ds_zipper_store_t *s, ds_zipper_t z) {
  size_t d = 0;
  size_t f;
  if (s == NULL) {
    return 0;
  }
  for (f = z.path; f != ZE; f = s->fup[f]) {
    d++;
  }
  return d;
}
