#include "merkle_tree.h"
#include <string.h>

static const unsigned char mk_leaf_tag = 0x00;
static const unsigned char mk_node_tag = 0x01;

static unsigned long mk_fmix(unsigned long h) {
  h &= 0xFFFFFFFFUL;
  h ^= h >> 16;
  h = (h * 0x85EBCA6BUL) & 0xFFFFFFFFUL;
  h ^= h >> 13;
  h = (h * 0xC2B2AE35UL) & 0xFFFFFFFFUL;
  h ^= h >> 16;
  return h;
}

void ds_merkle_tree_default_digest(const void *const *parts,
                                   const size_t *lengths, size_t count,
                                   unsigned char *out, void *user) {
  static const unsigned long bases[4] = {0x811C9DC5UL, 0x01000193UL,
                                         0x9E3779B9UL, 0x85EBCA6BUL};
  unsigned long lane[4];
  unsigned long total = 0;
  size_t p;
  size_t i;
  unsigned int k;
  const unsigned char *d;
  (void)user;
  for (k = 0; k < 4; k++) {
    lane[k] = bases[k];
  }
  for (p = 0; p < count; p++) {
    d = (const unsigned char *)parts[p];
    for (i = 0; i < lengths[p]; i++) {
      for (k = 0; k < 4; k++) {
        lane[k] =
            ((lane[k] ^ (d[i] + k)) * (0x01000193UL + 2 * k)) & 0xFFFFFFFFUL;
      }
    }
    total += (unsigned long)lengths[p];
  }
  for (k = 0; k < 4; k++) {
    lane[k] = mk_fmix(lane[k] ^ lane[(k + 1) & 3U] ^ total ^ k);
    out[4 * k] = (unsigned char)(lane[k] & 0xFFU);
    out[4 * k + 1] = (unsigned char)((lane[k] >> 8) & 0xFFU);
    out[4 * k + 2] = (unsigned char)((lane[k] >> 16) & 0xFFU);
    out[4 * k + 3] = (unsigned char)((lane[k] >> 24) & 0xFFU);
  }
}

static void mk_leaf(const ds_merkle_tree_config_t *c, const void *data,
                    size_t length, unsigned char *out) {
  const void *parts[2];
  size_t lengths[2];
  parts[0] = &mk_leaf_tag;
  lengths[0] = 1;
  parts[1] = data;
  lengths[1] = length;
  c->digest(parts, lengths, 2, out, c->user);
}

static void mk_node(const ds_merkle_tree_config_t *c, const unsigned char *l,
                    const unsigned char *r, unsigned char *out) {
  const void *parts[3];
  size_t lengths[3];
  unsigned char tmp[DS_MERKLE_MAX_DIGEST];
  parts[0] = &mk_node_tag;
  lengths[0] = 1;
  parts[1] = l;
  lengths[1] = c->digest_size;
  parts[2] = r;
  lengths[2] = c->digest_size;
  c->digest(parts, lengths, 3, tmp, c->user); /* out may alias l or r */
  memcpy(out, tmp, c->digest_size);
}

static unsigned char *mk_at(const ds_merkle_tree_t *t, size_t level, size_t i) {
  return t->levels[level].data + i * t->config.digest_size;
}

/* Recompute node j of level `level` + 1 from level `level`. */
static void mk_recompute(ds_merkle_tree_t *t, size_t level, size_t j) {
  size_t w = t->levels[level].size;
  unsigned char *dst = mk_at(t, level + 1, j);
  if (2 * j + 1 < w) {
    mk_node(&t->config, mk_at(t, level, 2 * j), mk_at(t, level, 2 * j + 1),
            dst);
  } else if (t->config.odd == DS_MERKLE_DUPLICATE) {
    mk_node(&t->config, mk_at(t, level, 2 * j), mk_at(t, level, 2 * j), dst);
  } else {
    memcpy(dst, mk_at(t, level, 2 * j), t->config.digest_size);
  }
}

void ds_merkle_tree_config_init(ds_merkle_tree_config_t *config) {
  config->digest_size = 16;
  config->digest = ds_merkle_tree_default_digest;
  config->user = NULL;
  config->odd = DS_MERKLE_PROMOTE;
  config->context = NULL;
}

ds_status_t ds_merkle_tree_init(ds_merkle_tree_t *tree,
                                const ds_merkle_tree_config_t *config) {
  size_t i;
  ds_status_t status;
  if (tree == NULL || config == NULL || config->digest == NULL) {
    return DS_ERR_NULL;
  }
  if (config->digest_size == 0 || config->digest_size > DS_MERKLE_MAX_DIGEST ||
      (config->odd != DS_MERKLE_PROMOTE &&
       config->odd != DS_MERKLE_DUPLICATE)) {
    return DS_ERR_INVALID;
  }
  tree->config = *config;
  tree->level_count = 0;
  for (i = 0; i < DS_MERKLE_MAX_LEVELS; i++) {
    status =
        ds_vec_init(&tree->levels[i], config->digest_size, config->context);
    if (status != DS_OK) {
      while (i-- > 0) {
        ds_vec_destroy(&tree->levels[i]);
      }
      return status;
    }
  }
  return DS_OK;
}

void ds_merkle_tree_destroy(ds_merkle_tree_t *tree) {
  size_t i;
  if (tree == NULL) {
    return;
  }
  for (i = 0; i < DS_MERKLE_MAX_LEVELS; i++) {
    ds_vec_destroy(&tree->levels[i]);
  }
  tree->level_count = 0;
}

static ds_status_t mk_rebuild_above(ds_merkle_tree_t *t) {
  size_t level = 0;
  size_t w;
  size_t j;
  ds_status_t status;
  t->level_count = t->levels[0].size == 0 ? 0 : 1;
  while (t->levels[level].size > 1) {
    w = t->levels[level].size;
    status = ds_vec_resize(&t->levels[level + 1], (w + 1) / 2, NULL);
    if (status != DS_OK) {
      return status;
    }
    for (j = 0; j < (w + 1) / 2; j++) {
      mk_recompute(t, level, j);
    }
    level++;
  }
  t->level_count = level + 1;
  return DS_OK;
}

ds_status_t ds_merkle_tree_build(ds_merkle_tree_t *tree, size_t count,
                                 ds_merkle_leaf_func_t leaf, void *user) {
  size_t i;
  const void *data;
  size_t length;
  ds_status_t status;
  if (leaf == NULL && count > 0) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < DS_MERKLE_MAX_LEVELS; i++) {
    ds_vec_clear(&tree->levels[i]);
  }
  status = ds_vec_resize(&tree->levels[0], count, NULL);
  if (status != DS_OK) {
    return status;
  }
  for (i = 0; i < count; i++) {
    leaf(i, &data, &length, user);
    mk_leaf(&tree->config, data, length, mk_at(tree, 0, i));
  }
  return mk_rebuild_above(tree);
}

/* Recompute the path above level-0 node i. */
static ds_status_t mk_fix_path(ds_merkle_tree_t *t, size_t i) {
  size_t level = 0;
  size_t w;
  ds_status_t status;
  while (t->levels[level].size > 1) {
    w = t->levels[level].size;
    if (level + 1 >= DS_MERKLE_MAX_LEVELS) {
      return DS_ERR_OVERFLOW;
    }
    if (t->levels[level + 1].size < (w + 1) / 2) {
      status = ds_vec_resize(&t->levels[level + 1], (w + 1) / 2, NULL);
      if (status != DS_OK) {
        return status;
      }
    }
    i >>= 1;
    mk_recompute(t, level, i);
    level++;
  }
  t->level_count = level + 1;
  return DS_OK;
}

ds_status_t ds_merkle_tree_append(ds_merkle_tree_t *tree, const void *data,
                                  size_t length) {
  unsigned char *slot = (unsigned char *)ds_vec_emplace(&tree->levels[0]);
  if (slot == NULL) {
    return DS_ERR_ALLOC;
  }
  mk_leaf(&tree->config, data, length, slot);
  return mk_fix_path(tree, tree->levels[0].size - 1);
}

ds_status_t ds_merkle_tree_update(ds_merkle_tree_t *tree, size_t index,
                                  const void *data, size_t length) {
  if (index >= tree->levels[0].size) {
    return DS_ERR_RANGE;
  }
  mk_leaf(&tree->config, data, length, mk_at(tree, 0, index));
  return mk_fix_path(tree, index);
}

size_t ds_merkle_tree_leaf_count(const ds_merkle_tree_t *tree) {
  return tree->levels[0].size;
}

ds_status_t ds_merkle_tree_root(const ds_merkle_tree_t *tree,
                                unsigned char *out) {
  if (tree->level_count == 0) {
    return DS_EMPTY;
  }
  memcpy(out, mk_at(tree, tree->level_count - 1, 0), tree->config.digest_size);
  return DS_OK;
}

const unsigned char *ds_merkle_tree_leaf_digest(const ds_merkle_tree_t *tree,
                                                size_t index) {
  return index < tree->levels[0].size ? mk_at(tree, 0, index) : NULL;
}

ds_status_t ds_merkle_tree_proof(const ds_merkle_tree_t *tree, size_t index,
                                 unsigned char *out, size_t capacity,
                                 size_t *out_count) {
  size_t level;
  size_t w;
  size_t count = 0;
  size_t sibling;
  size_t ds = tree->config.digest_size;
  if (index >= tree->levels[0].size) {
    return DS_ERR_RANGE;
  }
  for (level = 0; level + 1 < tree->level_count; level++) {
    w = tree->levels[level].size;
    if (index & 1U) {
      sibling = index - 1;
    } else if (index + 1 < w) {
      sibling = index + 1;
    } else {
      index >>= 1; /* unpaired: promoted or paired with itself */
      continue;
    }
    if (count >= capacity) {
      return DS_ERR_OVERFLOW;
    }
    memcpy(out + count * ds, mk_at(tree, level, sibling), ds);
    count++;
    index >>= 1;
  }
  *out_count = count;
  return DS_OK;
}

bool ds_merkle_tree_verify(const ds_merkle_tree_config_t *config,
                           const void *data, size_t length, size_t index,
                           size_t leaf_count, const unsigned char *proof,
                           size_t proof_count, const unsigned char *root) {
  unsigned char h[DS_MERKLE_MAX_DIGEST];
  size_t w = leaf_count;
  size_t k = 0;
  size_t ds = config->digest_size;
  if (index >= leaf_count || ds == 0 || ds > DS_MERKLE_MAX_DIGEST) {
    return false;
  }
  mk_leaf(config, data, length, h);
  while (w > 1) {
    if (index & 1U) {
      if (k >= proof_count) {
        return false;
      }
      mk_node(config, proof + k * ds, h, h);
      k++;
    } else if (index + 1 < w) {
      if (k >= proof_count) {
        return false;
      }
      mk_node(config, h, proof + k * ds, h);
      k++;
    } else if (config->odd == DS_MERKLE_DUPLICATE) {
      mk_node(config, h, h, h);
    }
    index >>= 1;
    w = (w + 1) / 2;
  }
  return k == proof_count && memcmp(h, root, ds) == 0;
}
