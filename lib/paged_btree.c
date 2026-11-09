#include "paged_btree.h"
#include "checked.h"
#include <limits.h>
#include <string.h>

#define PB_HDR 16U
#define PB_LEAF 1
#define PB_INNER 2
#define PB_MAGIC "DSBPT1\0\0"

static void pb_put64(unsigned char *p, unsigned long v) {
  int i;
  for (i = 0; i < 8; i++) {
    p[i] =
        (unsigned char)(i < (int)sizeof(unsigned long) ? (v >> (8 * i)) & 0xFFUL
                                                       : 0);
  }
}

static unsigned long pb_get64(const unsigned char *p) {
  unsigned long v = 0;
  int i;
  for (i = (int)(sizeof(unsigned long) < 8 ? sizeof(unsigned long) : 8);
       i-- > 0;) {
    v = (v << 8) | p[i];
  }
  return v;
}

static size_t pb_count(const unsigned char *page) {
  return (size_t)page[4] | ((size_t)page[5] << 8) | ((size_t)page[6] << 16) |
         ((size_t)page[7] << 24);
}

static void pb_set_count(unsigned char *page, size_t n) {
  page[4] = (unsigned char)(n & 0xFFU);
  page[5] = (unsigned char)((n >> 8) & 0xFFU);
  page[6] = (unsigned char)((n >> 16) & 0xFFU);
  page[7] = (unsigned char)((n >> 24) & 0xFFU);
}

static size_t pb_lsz(const ds_pbtree_t *t) { return 8 + t->value_size; }

static unsigned char *pb_lent(const ds_pbtree_t *t, unsigned char *page,
                              size_t i) {
  return page + PB_HDR + i * pb_lsz(t);
}

/* inner: child 0 at PB_HDR, then (key i, child i + 1) pairs */
static unsigned long pb_child(const unsigned char *page, size_t i) {
  return pb_get64(i == 0 ? page + PB_HDR
                         : page + PB_HDR + 8 + (i - 1) * 16 + 8);
}

static unsigned long pb_ikey(const unsigned char *page, size_t i) {
  return pb_get64(page + PB_HDR + 8 + i * 16);
}

static ds_status_t pb_get(ds_pbtree_t *t, unsigned long page,
                          unsigned char **buf) {
  void *d;
  ds_status_t st = ds_pager_get(t->pager, page, &d);
  if (st == DS_OK) {
    *buf = (unsigned char *)d;
  }
  return st;
}

static void pb_done(ds_pbtree_t *t, unsigned long page, bool dirty) {
  if (dirty) {
    (void)ds_pager_mark_dirty(t->pager, page);
  }
  (void)ds_pager_unpin(t->pager, page);
}

ds_status_t ds_pbtree_sync(ds_pbtree_t *tree) {
  unsigned char *m;
  ds_status_t st;
  if (tree == NULL || tree->pager == NULL) {
    return DS_ERR_NULL;
  }
  st = pb_get(tree, 0, &m);
  if (st != DS_OK) {
    return st;
  }
  memcpy(m, PB_MAGIC, 8);
  pb_put64(m + 8, (unsigned long)tree->value_size);
  pb_put64(m + 16, tree->root);
  pb_put64(m + 24, tree->height);
  pb_put64(m + 32, tree->count);
  pb_put64(m + 40, tree->first_leaf);
  m[48] = (unsigned char)sizeof(unsigned long); /* writer's key width */
  pb_done(tree, 0, true);
  return DS_OK;
}

ds_status_t ds_pbtree_open(ds_pbtree_t *tree, ds_pager_t *pager,
                           size_t value_size) {
  size_t ps;
  unsigned char *m;
  ds_status_t st;
  if (tree == NULL || pager == NULL || pager->store == NULL) {
    return DS_ERR_NULL;
  }
  memset(tree, 0, sizeof(*tree));
  ps = pager->store->page_size;
  if (ps < 64 || ps > 0xFFFFFFFFUL) {
    return DS_ERR_INVALID;
  }
  if (value_size > ps) {
    return DS_ERR_INVALID; /* also keeps 8 + value_size from wrapping */
  }
  tree->pager = pager;
  tree->value_size = value_size;
  tree->leaf_cap = (ps - PB_HDR) / pb_lsz(tree);
  tree->inner_cap = (ps - PB_HDR - 8) / 16;
  if (tree->leaf_cap < 3 || tree->inner_cap < 3) {
    return DS_ERR_INVALID;
  }
  if (ds_pager_page_count(pager) == 0) {
    unsigned long meta, leaf;
    void *d;
    st = ds_pager_new(pager, &meta, &d);
    if (st != DS_OK) {
      return st;
    }
    (void)ds_pager_unpin(pager, meta);
    st = ds_pager_new(pager, &leaf, &d);
    if (st != DS_OK) {
      return st;
    }
    ((unsigned char *)d)[0] = PB_LEAF;
    (void)ds_pager_unpin(pager, leaf);
    tree->root = leaf;
    tree->first_leaf = leaf;
    tree->height = 1;
    return ds_pbtree_sync(tree);
  }
  st = pb_get(tree, 0, &m);
  if (st != DS_OK) {
    return st;
  }
  if (memcmp(m, PB_MAGIC, 8) != 0 ||
      pb_get64(m + 8) != (unsigned long)value_size) {
    pb_done(tree, 0, false);
    return DS_ERR_INVALID;
  }
  /* keys and page numbers are 8-byte fields; a tree written where
   * unsigned long is wider may hold values this host cannot represent */
  if (m[48] > sizeof(unsigned long)) {
    pb_done(tree, 0, false);
    return DS_ERR_UNSUPPORTED;
  }
  tree->root = pb_get64(m + 16);
  tree->height = pb_get64(m + 24);
  tree->count = pb_get64(m + 32);
  tree->first_leaf = pb_get64(m + 40);
  pb_done(tree, 0, false);
  return DS_OK;
}

/* first leaf slot with key >= k */
static size_t pb_leaf_lower(const ds_pbtree_t *t, unsigned char *page,
                            unsigned long k) {
  size_t lo = 0, hi = pb_count(page);
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (pb_get64(pb_lent(t, page, mid)) < k) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

/* child index for k: the first key > k */
static size_t pb_route(unsigned char *page, unsigned long k) {
  size_t lo = 0, hi = pb_count(page);
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (pb_ikey(page, mid) <= k) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

/* leaf for key k (unpinned) */
static ds_status_t pb_find_leaf(ds_pbtree_t *t, unsigned long k,
                                unsigned long *leaf) {
  unsigned long page = t->root, level;
  ds_status_t st;
  for (level = t->height; level > 1; level--) {
    unsigned char *b;
    unsigned long next;
    st = pb_get(t, page, &b);
    if (st != DS_OK) {
      return st;
    }
    next = pb_child(b, pb_route(b, k));
    pb_done(t, page, false);
    page = next;
  }
  *leaf = page;
  return DS_OK;
}

#define PB_MAXH 64 /* deeper than any page count can reach */

/* split full leaf b (n entries, inserting key/value at pos) into b and the
 * new page nb; tmp holds n + 1 entries. Cannot fail. */
static unsigned long pb_split_leaf(ds_pbtree_t *t, unsigned char *b,
                                   unsigned char *nb, unsigned long np,
                                   size_t pos, unsigned long key,
                                   const void *value, unsigned char *tmp) {
  size_t n = pb_count(b), es = pb_lsz(t), half = (n + 1) / 2;
  memcpy(tmp, pb_lent(t, b, 0), pos * es);
  pb_put64(tmp + pos * es, key);
  memcpy(tmp + pos * es + 8, value, t->value_size);
  memcpy(tmp + (pos + 1) * es, pb_lent(t, b, pos), (n - pos) * es);
  memcpy(pb_lent(t, b, 0), tmp, half * es);
  pb_set_count(b, half);
  memset(nb, 0, PB_HDR);
  nb[0] = PB_LEAF;
  memcpy(pb_lent(t, nb, 0), tmp + half * es, (n + 1 - half) * es);
  pb_set_count(nb, n + 1 - half);
  memcpy(nb + 8, b + 8, 8); /* next link */
  pb_put64(b + 8, np);
  return pb_get64(tmp + half * es);
}

/* insert (sep, child) after child index idx of inner page b; when b is
 * full, split it into the new page nb (returns 1 and the separator that
 * moves up in *up). Cannot fail. */
static int pb_add_inner(ds_pbtree_t *t, unsigned char *b, unsigned char *nb,
                        size_t idx, unsigned long sep, unsigned long child,
                        unsigned char *tmp, unsigned long *up) {
  size_t n = pb_count(b), half, rest;
  if (n < t->inner_cap) {
    unsigned char *at = b + PB_HDR + 8 + idx * 16;
    memmove(at + 16, at, (n - idx) * 16);
    pb_put64(at, sep);
    pb_put64(at + 8, child);
    pb_set_count(b, n + 1);
    return 0;
  }
  memcpy(tmp, b + PB_HDR + 8, idx * 16);
  pb_put64(tmp + idx * 16, sep);
  pb_put64(tmp + idx * 16 + 8, child);
  memcpy(tmp + (idx + 1) * 16, b + PB_HDR + 8 + idx * 16, (n - idx) * 16);
  half = (n + 1) / 2; /* pair `half` moves up */
  memcpy(b + PB_HDR + 8, tmp, half * 16);
  pb_set_count(b, half);
  memset(nb, 0, PB_HDR);
  nb[0] = PB_INNER;
  memcpy(nb + PB_HDR, tmp + half * 16 + 8, 8); /* its child is child 0 */
  rest = n - half;
  memcpy(nb + PB_HDR + 8, tmp + (half + 1) * 16, rest * 16);
  pb_set_count(nb, rest);
  *up = pb_get64(tmp + half * 16);
  return 1;
}

/*
 * Insertion is prepare-then-commit. The descent records the path and the
 * fill of every node on it; if the leaf must split, the nodes that will
 * change are pinned, the new pages (and a new root, if the split reaches
 * it) are allocated and pinned, and the split buffer is allocated. Only
 * then are pages edited, in pinned memory, by steps that cannot fail. A
 * failure while preparing changes nothing reachable (pages already
 * appended stay unused). A split through k levels pins up to 2k + 2
 * frames.
 */
ds_status_t ds_pbtree_put(ds_pbtree_t *tree, unsigned long key,
                          const void *value) {
  unsigned long path[PB_MAXH], fresh[PB_MAXH + 1], up;
  size_t idx[PB_MAXH], cnt[PB_MAXH], pos, d, h, k, i, nfresh = 0, npinned = 0;
  unsigned char *buf[PB_MAXH], *fb[PB_MAXH + 1], *tmp = NULL;
  unsigned long pinned[PB_MAXH];
  ds_status_t st;
  int grow;
  if (tree == NULL || tree->pager == NULL ||
      (value == NULL && tree->value_size > 0)) {
    return DS_ERR_NULL;
  }
  h = (size_t)tree->height;
  if (h == 0 || h > PB_MAXH - 1) {
    return DS_ERR_STATE;
  }
  /* descent: path[0] is the root, path[h - 1] the leaf */
  path[0] = tree->root;
  for (d = 0; d + 1 < h; d++) {
    unsigned char *b;
    st = pb_get(tree, path[d], &b);
    if (st != DS_OK) {
      return st;
    }
    cnt[d] = pb_count(b);
    idx[d] = pb_route(b, key);
    path[d + 1] = pb_child(b, idx[d]);
    pb_done(tree, path[d], false);
  }
  st = pb_get(tree, path[h - 1], &buf[h - 1]);
  if (st != DS_OK) {
    return st;
  }
  pinned[npinned++] = path[h - 1];
  {
    unsigned char *b = buf[h - 1];
    size_t n = pb_count(b), es = pb_lsz(tree);
    pos = pb_leaf_lower(tree, b, key);
    if (pos < n && pb_get64(pb_lent(tree, b, pos)) == key) {
      memcpy(pb_lent(tree, b, pos) + 8, value, tree->value_size);
      pb_done(tree, path[h - 1], true);
      return DS_EXISTS;
    }
    if (n < tree->leaf_cap) {
      memmove(pb_lent(tree, b, pos + 1), pb_lent(tree, b, pos), (n - pos) * es);
      pb_put64(pb_lent(tree, b, pos), key);
      memcpy(pb_lent(tree, b, pos) + 8, value, tree->value_size);
      pb_set_count(b, n + 1);
      pb_done(tree, path[h - 1], true);
      tree->count++;
      return DS_OK;
    }
  }
  /* prepare: k levels split (the leaf, then every full ancestor) */
  k = 1;
  while (k < h && cnt[h - 1 - k] == tree->inner_cap) {
    k++;
  }
  grow = k == h; /* the root splits: a new root is needed */
  for (i = 1; i <= k && i < h; i++) { /* ancestors that change */
    d = h - 1 - i;
    st = pb_get(tree, path[d], &buf[d]);
    if (st != DS_OK) {
      goto unwind;
    }
    pinned[npinned++] = path[d];
  }
  for (i = 0; i < k + (grow ? 1U : 0U); i++) {
    void *data;
    st = ds_pager_new(tree->pager, &fresh[i], &data);
    if (st != DS_OK) {
      goto unwind;
    }
    fb[i] = (unsigned char *)data;
    nfresh++;
  }
  {
    size_t lbytes, ibytes;
    if (!ds_size_mul(tree->leaf_cap + 1, pb_lsz(tree), &lbytes) ||
        !ds_size_mul(tree->inner_cap + 1, 16, &ibytes)) {
      st = DS_ERR_OVERFLOW;
      goto unwind;
    }
    tmp = (unsigned char *)ds_context_alloc(tree->pager->context,
                                            lbytes > ibytes ? lbytes : ibytes);
    if (tmp == NULL) {
      st = DS_ERR_ALLOC;
      goto unwind;
    }
  }
  /* commit: nothing below can fail */
  up = pb_split_leaf(tree, buf[h - 1], fb[0], fresh[0], pos, key, value, tmp);
  for (i = 1; i < h; i++) {
    unsigned long upper;
    d = h - 1 - i;
    if (!pb_add_inner(tree, buf[d], i < k ? fb[i] : NULL, idx[d], up,
                      fresh[i - 1], tmp, &upper)) {
      break; /* absorbed without splitting */
    }
    up = upper;
  }
  if (grow) {
    unsigned char *r = fb[k];
    memset(r, 0, PB_HDR);
    r[0] = PB_INNER;
    pb_put64(r + PB_HDR, tree->root);
    pb_put64(r + PB_HDR + 8, up);
    pb_put64(r + PB_HDR + 16, fresh[k - 1]);
    pb_set_count(r, 1);
    tree->root = fresh[k];
    tree->height++;
  }
  tree->count++;
  ds_context_free(tree->pager->context, tmp);
  for (i = 0; i < npinned; i++) {
    pb_done(tree, pinned[i], true);
  }
  for (i = 0; i < nfresh; i++) {
    pb_done(tree, fresh[i], true);
  }
  return DS_OK;
unwind:
  ds_context_free(tree->pager->context, tmp);
  for (i = 0; i < npinned; i++) {
    pb_done(tree, pinned[i], false);
  }
  for (i = 0; i < nfresh; i++) {
    pb_done(tree, fresh[i], false); /* appended, never linked */
  }
  return st;
}

ds_status_t ds_pbtree_get(ds_pbtree_t *tree, unsigned long key, void *out) {
  unsigned long leaf;
  unsigned char *b;
  size_t pos;
  ds_status_t st;
  if (tree == NULL || tree->pager == NULL) {
    return DS_ERR_NULL;
  }
  st = pb_find_leaf(tree, key, &leaf);
  if (st != DS_OK) {
    return st;
  }
  st = pb_get(tree, leaf, &b);
  if (st != DS_OK) {
    return st;
  }
  pos = pb_leaf_lower(tree, b, key);
  if (pos < pb_count(b) && pb_get64(pb_lent(tree, b, pos)) == key) {
    if (out != NULL) {
      memcpy(out, pb_lent(tree, b, pos) + 8, tree->value_size);
    }
    st = DS_OK;
  } else {
    st = DS_NOT_FOUND;
  }
  pb_done(tree, leaf, false);
  return st;
}

ds_status_t ds_pbtree_remove(ds_pbtree_t *tree, unsigned long key) {
  unsigned long leaf;
  unsigned char *b;
  size_t pos, n, es;
  ds_status_t st;
  if (tree == NULL || tree->pager == NULL) {
    return DS_ERR_NULL;
  }
  st = pb_find_leaf(tree, key, &leaf);
  if (st != DS_OK) {
    return st;
  }
  st = pb_get(tree, leaf, &b);
  if (st != DS_OK) {
    return st;
  }
  n = pb_count(b);
  es = pb_lsz(tree);
  pos = pb_leaf_lower(tree, b, key);
  if (pos == n || pb_get64(pb_lent(tree, b, pos)) != key) {
    pb_done(tree, leaf, false);
    return DS_NOT_FOUND;
  }
  memmove(pb_lent(tree, b, pos), pb_lent(tree, b, pos + 1), (n - pos - 1) * es);
  pb_set_count(b, n - 1);
  pb_done(tree, leaf, true);
  tree->count--;
  return DS_OK;
}

unsigned long ds_pbtree_size(const ds_pbtree_t *tree) {
  return tree == NULL ? 0 : tree->count;
}

/* settle on a valid entry at or after (leaf, index) */
static ds_status_t pb_iter_settle(ds_pbtree_iter_t *it) {
  ds_pbtree_t *t = it->tree;
  while (it->leaf != 0) {
    unsigned char *b;
    unsigned long next;
    ds_status_t st = pb_get(t, it->leaf, &b);
    if (st != DS_OK) {
      it->leaf = 0;
      return st;
    }
    if (it->index < pb_count(b)) {
      it->key = pb_get64(pb_lent(t, b, it->index));
      memcpy(it->value, pb_lent(t, b, it->index) + 8, t->value_size);
      pb_done(t, it->leaf, false);
      return DS_OK;
    }
    next = pb_get64(b + 8);
    pb_done(t, it->leaf, false);
    it->leaf = next;
    it->index = 0;
  }
  return DS_OK;
}

ds_status_t ds_pbtree_iter_init(ds_pbtree_iter_t *it, ds_pbtree_t *tree,
                                unsigned long from) {
  unsigned char *b = NULL;
  ds_status_t st;
  if (it == NULL || tree == NULL || tree->pager == NULL) {
    return DS_ERR_NULL;
  }
  memset(it, 0, sizeof(*it));
  it->tree = tree;
  it->value = (unsigned char *)ds_context_alloc(
      tree->pager->context, tree->value_size == 0 ? 1 : tree->value_size);
  if (it->value == NULL) {
    return DS_ERR_ALLOC;
  }
  st = pb_find_leaf(tree, from, &it->leaf);
  if (st == DS_OK) {
    st = pb_get(tree, it->leaf, &b);
  }
  if (st != DS_OK) {
    it->leaf = 0;
    return st;
  }
  it->index = pb_leaf_lower(tree, b, from);
  pb_done(tree, it->leaf, false);
  return pb_iter_settle(it);
}

bool ds_pbtree_iter_valid(const ds_pbtree_iter_t *it) {
  return it != NULL && it->leaf != 0;
}

ds_status_t ds_pbtree_iter_next(ds_pbtree_iter_t *it) {
  if (it == NULL || it->leaf == 0) {
    return DS_OK;
  }
  it->index++;
  return pb_iter_settle(it);
}

void ds_pbtree_iter_destroy(ds_pbtree_iter_t *it) {
  if (it == NULL || it->tree == NULL) {
    return;
  }
  ds_context_free(it->tree->pager->context, it->value);
  memset(it, 0, sizeof(*it));
}
