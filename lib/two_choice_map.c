#include "two_choice_map.h"
#include <string.h>

ds_status_t ds_two_choice_map_init(ds_two_choice_map_t *m, size_t key_size,
                                   size_t value_size,
                                   ds_choice_hash_func_t hash,
                                   ds_choice_equal_func_t equal, void *user,
                                   size_t choices, size_t bucket_size,
                                   ds_choice_policy_t policy,
                                   ds_context_t *context) {
  if (m == NULL || hash == NULL || equal == NULL) {
    return DS_ERR_NULL;
  }
  if (choices == 0) {
    choices = 2;
  }
  if (bucket_size == 0) {
    bucket_size = 4;
  }
  if (key_size == 0 || choices < 2 || choices > 64 || bucket_size > 4096 ||
      (int)policy < 0 || policy > DS_CHOICE_LEFT) {
    return DS_ERR_INVALID;
  }
  memset(m, 0, sizeof(*m));
  m->key_size = key_size;
  m->value_size = value_size;
  m->hash = hash;
  m->equal = equal;
  m->user = user;
  m->choices = choices;
  m->bucket_size = bucket_size;
  m->policy = policy;
  m->rng = 88172645UL;
  m->context = context;
  return DS_OK;
}

void ds_two_choice_map_destroy(ds_two_choice_map_t *m) {
  if (m == NULL) {
    return;
  }
  ds_context_free(m->context, m->keys);
  ds_context_free(m->context, m->values);
  ds_context_free(m->context, m->load);
  m->keys = m->values = NULL;
  m->load = NULL;
  m->buckets = m->size = 0;
}

void ds_two_choice_map_clear(ds_two_choice_map_t *m) {
  size_t i;
  if (m == NULL) {
    return;
  }
  for (i = 0; i < m->buckets; i++) {
    m->load[i] = 0;
  }
  m->size = 0;
}

size_t ds_two_choice_map_size(const ds_two_choice_map_t *m) {
  return m == NULL ? 0 : m->size;
}

static unsigned char *tc_key(const ds_two_choice_map_t *m, size_t b, size_t j) {
  return m->keys + (b * m->bucket_size + j) * m->key_size;
}

static unsigned char *tc_val(const ds_two_choice_map_t *m, size_t b, size_t j) {
  return m->values + (b * m->bucket_size + j) * m->value_size;
}

/* Candidate bucket i of key (buckets is a multiple of choices). */
static size_t tc_bucket(const ds_two_choice_map_t *m, const void *key,
                        size_t i) {
  size_t h = m->hash(key, i, m->user);
  size_t group;
  h ^= h >> 15;
  h *= (size_t)0x2c1b3c6dUL;
  h ^= h >> 12;
  if (m->policy == DS_CHOICE_LEFT) {
    group = m->buckets / m->choices;
    return i * group + h % group;
  }
  return h % m->buckets;
}

static bool tc_locate(const ds_two_choice_map_t *m, const void *key,
                      size_t *out_b, size_t *out_j) {
  size_t i;
  size_t b;
  size_t j;
  if (m->buckets == 0) {
    return false;
  }
  for (i = 0; i < m->choices; i++) {
    b = tc_bucket(m, key, i);
    for (j = 0; j < m->load[b]; j++) {
      if (m->equal(tc_key(m, b, j), key, m->user)) {
        *out_b = b;
        *out_j = j;
        return true;
      }
    }
  }
  return false;
}

static unsigned long tc_rand(ds_two_choice_map_t *m) {
  unsigned long x = m->rng;
  x ^= (x << 13) & 0xFFFFFFFFUL;
  x ^= x >> 17;
  x ^= (x << 5) & 0xFFFFFFFFUL;
  m->rng = x & 0xFFFFFFFFUL;
  return m->rng;
}

/* Least loaded candidate with room, or buckets if all are full. */
static size_t tc_choose(ds_two_choice_map_t *m, const void *key) {
  size_t i;
  size_t b;
  size_t best = m->buckets;
  size_t ties = 0;
  for (i = 0; i < m->choices; i++) {
    b = tc_bucket(m, key, i);
    if (m->load[b] >= m->bucket_size) {
      continue;
    }
    if (best == m->buckets || m->load[b] < m->load[best]) {
      best = b;
      ties = 1;
    } else if (m->load[b] == m->load[best] && b != best &&
               m->policy == DS_CHOICE_GREEDY) {
      /* reservoir choice among equally loaded buckets */
      ties++;
      if (tc_rand(m) % ties == 0) {
        best = b;
      }
    }
  }
  return best;
}

static void tc_place(ds_two_choice_map_t *m, size_t b, const void *key,
                     const void *value) {
  size_t j = m->load[b]++;
  memcpy(tc_key(m, b, j), key, m->key_size);
  if (m->value_size > 0) {
    memcpy(tc_val(m, b, j), value, m->value_size);
  }
}

static ds_status_t tc_rehash(ds_two_choice_map_t *m, size_t buckets) {
  ds_two_choice_map_t n;
  size_t cells;
  size_t b;
  size_t j;
  size_t t;
  int tries = 0;
  for (;;) {
    n = *m;
    if (buckets > ((size_t)-1) / m->bucket_size) {
      return DS_ERR_OVERFLOW;
    }
    cells = buckets * m->bucket_size;
    if (cells > ((size_t)-1) / m->key_size ||
        (m->value_size > 0 && cells > ((size_t)-1) / m->value_size)) {
      return DS_ERR_OVERFLOW;
    }
    n.buckets = buckets;
    n.keys = (unsigned char *)ds_context_alloc(m->context, cells * m->key_size);
    n.values = (unsigned char *)ds_context_alloc(
        m->context, m->value_size > 0 ? cells * m->value_size : 1);
    n.load = (size_t *)ds_context_calloc(m->context, buckets, sizeof(size_t));
    if (n.keys == NULL || n.values == NULL || n.load == NULL) {
      ds_context_free(m->context, n.keys);
      ds_context_free(m->context, n.values);
      ds_context_free(m->context, n.load);
      return DS_ERR_ALLOC;
    }
    for (b = 0; b < m->buckets; b++) {
      for (j = 0; j < m->load[b]; j++) {
        t = tc_choose(&n, tc_key(m, b, j));
        if (t == n.buckets) {
          goto retry;
        }
        tc_place(&n, t, tc_key(m, b, j), tc_val(m, b, j));
      }
    }
    ds_context_free(m->context, m->keys);
    ds_context_free(m->context, m->values);
    ds_context_free(m->context, m->load);
    m->keys = n.keys;
    m->values = n.values;
    m->load = n.load;
    m->buckets = buckets;
    m->rng = n.rng;
    return DS_OK;
  retry:
    ds_context_free(m->context, n.keys);
    ds_context_free(m->context, n.values);
    ds_context_free(m->context, n.load);
    /* a hash that keeps colliding cannot be fixed by growing forever */
    if (++tries > 8 || buckets > ((size_t)-1) / 2) {
      return DS_ERR_OVERFLOW;
    }
    buckets *= 2;
  }
}

ds_status_t ds_two_choice_map_put(ds_two_choice_map_t *m, const void *key,
                                  const void *value) {
  size_t b;
  size_t j;
  int grown = 0;
  ds_status_t st;
  if (m == NULL || key == NULL || (value == NULL && m->value_size > 0)) {
    return DS_ERR_NULL;
  }
  if (tc_locate(m, key, &b, &j)) {
    if (m->value_size > 0) {
      memcpy(tc_val(m, b, j), value, m->value_size);
    }
    return DS_EXISTS;
  }
  if (m->buckets == 0) {
    st = tc_rehash(m, m->choices * 4);
    if (st != DS_OK) {
      return st;
    }
  }
  for (;;) {
    b = m->size * 4 >= m->buckets * m->bucket_size * 3 ? m->buckets
                                                       : tc_choose(m, key);
    if (b != m->buckets) {
      break;
    }
    if (++grown > 8 || m->buckets > ((size_t)-1) / 2) {
      return DS_ERR_OVERFLOW;
    }
    st = tc_rehash(m, m->buckets * 2);
    if (st != DS_OK) {
      return st;
    }
  }
  tc_place(m, b, key, value);
  m->size++;
  return DS_OK;
}

void *ds_two_choice_map_get(const ds_two_choice_map_t *m, const void *key) {
  size_t b;
  size_t j;
  if (m == NULL || key == NULL || !tc_locate(m, key, &b, &j)) {
    return NULL;
  }
  return tc_val(m, b, j);
}

ds_status_t ds_two_choice_map_remove(ds_two_choice_map_t *m, const void *key,
                                     void *out_value) {
  size_t b;
  size_t j;
  size_t last;
  if (m == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  if (!tc_locate(m, key, &b, &j)) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && m->value_size > 0) {
    memcpy(out_value, tc_val(m, b, j), m->value_size);
  }
  last = --m->load[b];
  if (j != last) {
    memcpy(tc_key(m, b, j), tc_key(m, b, last), m->key_size);
    if (m->value_size > 0) {
      memcpy(tc_val(m, b, j), tc_val(m, b, last), m->value_size);
    }
  }
  m->size--;
  return DS_OK;
}

size_t ds_two_choice_map_max_load(const ds_two_choice_map_t *m) {
  size_t b;
  size_t mx = 0;
  if (m == NULL) {
    return 0;
  }
  for (b = 0; b < m->buckets; b++) {
    if (m->load[b] > mx) {
      mx = m->load[b];
    }
  }
  return mx;
}

void ds_two_choice_map_foreach(const ds_two_choice_map_t *m,
                               bool (*visit)(const void *key, const void *value,
                                             void *user),
                               void *user) {
  size_t b;
  size_t j;
  if (m == NULL || visit == NULL) {
    return;
  }
  for (b = 0; b < m->buckets; b++) {
    for (j = 0; j < m->load[b]; j++) {
      if (!visit(tc_key(m, b, j), tc_val(m, b, j), user)) {
        return;
      }
    }
  }
}
