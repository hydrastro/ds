#include "zipper.h"
#include "checked.h"
#include <string.h>

#define ZE DS_ZIPPER_EMPTY

static ds_status_t zp_grow(ds_context_t *ctx, void **p, size_t elem,
                           size_t ncap) {
  size_t bytes;
  void *q;
  if (!ds_size_mul(ncap, elem, &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  q = ds_context_realloc(ctx, *p, bytes);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *p = q;
  return DS_OK;
}

/* the next capacity of a store array (DS_ERR_OVERFLOW past half the
 * address space) */
static ds_status_t zp_next_capacity(size_t cap, size_t *out) {
  if (cap > ((size_t)-1) / 2) {
    return DS_ERR_OVERFLOW;
  }
  *out = cap < 8 ? 16 : cap * 2;
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
  ds_context_free(s->context, s->count);
  ds_context_free(s->context, s->fvalue);
  ds_context_free(s->context, s->fsibling);
  ds_context_free(s->context, s->fup);
  ds_context_free(s->context, s->fdir);
  ds_context_free(s->context, s->frefs);
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

/* Drop one reference to node t (tag 0) or frame f (tag 1); what reaches
 * zero is freed, children and parents in turn, without recursion and
 * without allocating: a node or frame whose count reached zero is no
 * longer referenced, so its count field links it into a pending stack
 * until it is processed. Release therefore cannot fail. Entries are
 * encoded as 2 * index + tag. */
static void zp_release(ds_zipper_store_t *s, size_t item) {
  size_t nh = ZE; /* pending nodes, linked through refs */
  size_t fh = ZE; /* pending frames, linked through frefs */
  size_t idx = item / 2;
  if (item % 2 == 0) {
    if (--s->refs[idx] == 0) {
      s->refs[idx] = nh;
      nh = idx;
    }
  } else if (--s->frefs[idx] == 0) {
    s->frefs[idx] = fh;
    fh = idx;
  }
  while (nh != ZE || fh != ZE) {
    if (nh != ZE) {
      size_t c[2];
      int k;
      idx = nh;
      nh = s->refs[idx];
      c[0] = s->left[idx];
      c[1] = s->right[idx];
      for (k = 0; k < 2; k++) {
        if (c[k] != ZE && --s->refs[c[k]] == 0) {
          s->refs[c[k]] = nh;
          nh = c[k];
        }
      }
      s->refs[idx] = 0;
      s->left[idx] = s->node_free;
      s->node_free = idx;
      s->live_nodes--;
    } else {
      size_t sib;
      size_t up;
      idx = fh;
      fh = s->frefs[idx];
      sib = s->fsibling[idx];
      up = s->fup[idx];
      if (sib != ZE && --s->refs[sib] == 0) {
        s->refs[sib] = nh;
        nh = sib;
      }
      if (up != ZE && --s->frefs[up] == 0) {
        s->frefs[up] = fh;
        fh = up;
      }
      s->frefs[idx] = 0;
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

/* grow every node array to nc slots; capacities only change when all
 * succeed, and a partial growth is harmless (realloc keeps contents) */
static ds_status_t zp_reserve_nodes(ds_zipper_store_t *s, size_t nc) {
  ds_status_t st;
  if (nc <= s->node_capacity) {
    return DS_OK;
  }
  st = zp_grow(s->context, (void **)&s->value, s->elem_size, nc);
  if (st == DS_OK) {
    st = zp_grow(s->context, (void **)&s->left, sizeof(size_t), nc);
  }
  if (st == DS_OK) {
    st = zp_grow(s->context, (void **)&s->right, sizeof(size_t), nc);
  }
  if (st == DS_OK) {
    st = zp_grow(s->context, (void **)&s->refs, sizeof(size_t), nc);
  }
  if (st == DS_OK) {
    st = zp_grow(s->context, (void **)&s->count, sizeof(size_t), nc);
  }
  if (st == DS_OK) {
    s->node_capacity = nc;
  }
  return st;
}

static ds_status_t zp_reserve_frames(ds_zipper_store_t *s, size_t nc) {
  ds_status_t st;
  if (nc <= s->frame_capacity) {
    return DS_OK;
  }
  st = zp_grow(s->context, (void **)&s->fvalue, s->elem_size, nc);
  if (st == DS_OK) {
    st = zp_grow(s->context, (void **)&s->fsibling, sizeof(size_t), nc);
  }
  if (st == DS_OK) {
    st = zp_grow(s->context, (void **)&s->fup, sizeof(size_t), nc);
  }
  if (st == DS_OK) {
    st = zp_grow(s->context, (void **)&s->fdir, 1, nc);
  }
  if (st == DS_OK) {
    st = zp_grow(s->context, (void **)&s->frefs, sizeof(size_t), nc);
  }
  if (st == DS_OK) {
    s->frame_capacity = nc;
  }
  return st;
}

ds_status_t ds_zipper_store_reserve(ds_zipper_store_t *s, size_t nodes,
                                    size_t frames) {
  ds_status_t st;
  if (s == NULL) {
    return DS_ERR_NULL;
  }
  st = zp_reserve_nodes(s, nodes);
  return st == DS_OK ? zp_reserve_frames(s, frames) : st;
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
      size_t nc;
      ds_status_t st = zp_next_capacity(s->node_capacity, &nc);
      if (st == DS_OK) {
        st = zp_reserve_nodes(s, nc);
      }
      if (st != DS_OK) {
        return st;
      }
    }
    v = s->nodes++;
  }
  memcpy(s->value + v * s->elem_size, elem, s->elem_size);
  s->left[v] = left;
  s->right[v] = right;
  s->refs[v] = 1;
  /* subtree size with multiplicity, saturating: shared subtrees count
   * once per occurrence and a DAG may exceed SIZE_MAX */
  {
    size_t n = 1;
    size_t i;
    size_t ch[2];
    ch[0] = left;
    ch[1] = right;
    for (i = 0; i < 2; i++) {
      if (ch[i] != ZE) {
        n = s->count[ch[i]] > ((size_t)-1) - n ? (size_t)-1
                                               : n + s->count[ch[i]];
      }
    }
    s->count[v] = n;
  }
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
  return s == NULL || tree == ZE ? 0 : s->count[tree];
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
      size_t nc;
      ds_status_t st = zp_next_capacity(s->frame_capacity, &nc);
      if (st == DS_OK) {
        st = zp_reserve_frames(s, nc);
      }
      if (st != DS_OK) {
        return st;
      }
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
