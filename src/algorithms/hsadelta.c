#include <dsa/algorithms/hsadelta.h>
#include <dsa/algorithms/sort.h>
#include <dsa/core/checked.h>
#include <dsa/structures/suffix_array.h>
#include <string.h>

typedef struct hsa_range {
  size_t begin, end;
} hsa_range_t;
typedef struct hsa_record {
  unsigned long hash;
  size_t block;
} hsa_record_t;
typedef struct hsa_index {
  const unsigned char *reference;
  size_t size, blocks, quick_bits;
  unsigned long *hashes, power;
  size_t *suffixes;
  unsigned char *quick;
  ds_map_t ranges;
  ds_hsadelta_config_t config;
  unsigned char digest[DS_SHA256_SIZE];
} hsa_index_t;
typedef struct hsa_match {
  size_t target, reference, length;
} hsa_match_t;
typedef struct hsa_query {
  const hsa_index_t *index;
  const unsigned char *target;
  size_t size, position, pending, cached;
  unsigned long *hashes;
} hsa_query_t;

static unsigned long hsa_hash(const hsa_index_t *s,
                              const unsigned char *bytes) {
  unsigned long h = 0;
  size_t i;
  if (s->config.hash != NULL)
    return s->config.hash(bytes, s->config.block_size, s->config.hash_user);
  for (i = 0; i < s->config.block_size; i++)
    h = h * 257UL + bytes[i] + 1UL;
  return h;
}
static unsigned long hsa_roll(const hsa_index_t *s, unsigned long h,
                              const unsigned char *old) {
  size_t p = s->config.block_size;
  if (s->config.hash == NULL)
    return (h - (old[0] + 1UL) * s->power) * 257UL + old[p] + 1UL;
  if (s->config.roll != NULL)
    return s->config.roll(h, old[0], old[p], p, s->config.hash_user);
  return hsa_hash(s, old + 1);
}
static int hsa_record_compare(const void *a, const void *b, void *user) {
  const hsa_record_t *x = (const hsa_record_t *)a;
  const hsa_record_t *y = (const hsa_record_t *)b;
  const hsa_index_t *s = (const hsa_index_t *)user;
  if (x->hash != y->hash)
    return x->hash < y->hash ? -1 : 1;
  return memcmp(s->reference + x->block * s->config.block_size,
                s->reference + y->block * s->config.block_size,
                s->config.block_size);
}
void ds_hsadelta_config_init(ds_hsadelta_config_t *c) {
  if (c != NULL) {
    memset(c, 0, sizeof(*c));
    c->block_size = 32;
  }
}
static void hsa_free(hsa_index_t *s, ds_context_t *context) {
  if (s != NULL) {
    ds_map_destroy(&s->ranges);
    ds_context_free(context, s->quick);
    ds_context_free(context, s->hashes);
    ds_context_free(context, s->suffixes);
    ds_context_free(context, s);
  }
}
ds_status_t ds_hsadelta_init(ds_hsadelta_index_t *out, const void *reference,
                             size_t size, const ds_hsadelta_config_t *config,
                             ds_context_t *context) {
  ds_hsadelta_config_t defaults;
  const ds_hsadelta_config_t *c = config;
  hsa_index_t *s;
  hsa_record_t *records = NULL;
  size_t *symbols = NULL;
  size_t i, j, alphabet = 0, blocks, quick_bits, bytes;
  ds_status_t st;
  if (out == NULL || (size > 0 && reference == NULL))
    return DS_ERR_NULL;
  if (c == NULL) {
    ds_hsadelta_config_init(&defaults);
    c = &defaults;
  }
  if (c->block_size == 0 || (c->hash == NULL && c->roll != NULL))
    return DS_ERR_INVALID;
  blocks = size / c->block_size;
  if (c->max_blocks != 0 && blocks > c->max_blocks)
    return DS_ERR_RANGE;
  if (blocks > (size_t)-1 / sizeof(hsa_record_t) ||
      blocks > (size_t)-1 / sizeof(size_t) ||
      blocks > (size_t)-1 / sizeof(unsigned long))
    return DS_ERR_OVERFLOW;
  quick_bits = c->quick_bits;
  if (quick_bits == 0) {
    if (!ds_size_mul(blocks, 16, &quick_bits))
      return DS_ERR_OVERFLOW;
    if (quick_bits < 64)
      quick_bits = 64;
  }
  s = (hsa_index_t *)ds_context_calloc(context, 1, sizeof(*s));
  if (s == NULL)
    return DS_ERR_ALLOC;
  s->reference = (const unsigned char *)reference;
  s->size = size;
  s->blocks = blocks;
  s->quick_bits = quick_bits;
  s->config = *c;
  s->power = 1;
  if (blocks > 0 && c->hash == NULL)
    for (i = 1; i < c->block_size; i++)
      s->power *= 257UL;
  st = ds_map_init(&s->ranges,
                   c->map_ops != NULL ? c->map_ops : ds_map_open_ops(),
                   sizeof(unsigned long), sizeof(hsa_range_t),
                   ds_map_hash_ulong, ds_map_equal_ulong, NULL, context);
  if (st != DS_OK)
    goto fail;
  if (blocks > 0) {
    bytes = quick_bits / 8 + (quick_bits % 8 != 0 ? 1 : 0);
    s->quick = (unsigned char *)ds_context_calloc(context, bytes, 1);
    s->hashes = (unsigned long *)ds_context_alloc(
        context, blocks * sizeof(unsigned long));
    s->suffixes = (size_t *)ds_context_alloc(context, blocks * sizeof(size_t));
    symbols = (size_t *)ds_context_alloc(context, blocks * sizeof(size_t));
    records = (hsa_record_t *)ds_context_alloc(context,
                                               blocks * sizeof(hsa_record_t));
    if (s->quick == NULL || s->hashes == NULL || s->suffixes == NULL ||
        symbols == NULL || records == NULL) {
      st = DS_ERR_ALLOC;
      goto fail;
    }
    for (i = 0; i < blocks; i++) {
      s->hashes[i] = hsa_hash(s, s->reference + i * c->block_size);
      records[i].hash = s->hashes[i];
      records[i].block = i;
    }
    ds_intro_sort(records, blocks, sizeof(*records), hsa_record_compare, s);
    for (i = 0; i < blocks; i++) {
      if (i == 0 || hsa_record_compare(&records[i - 1], &records[i], s) != 0)
        alphabet++;
      symbols[records[i].block] = alphabet - 1;
    }
    if (c->suffix != NULL)
      st = c->suffix(symbols, blocks, alphabet, s->suffixes, c->suffix_user,
                     context);
    else
      st = ds_sa_build_int(symbols, blocks, alphabet, s->suffixes, context);
    if (st != DS_OK) {
      if (st > DS_OK)
        st = DS_ERR_CALLBACK;
      goto fail;
    }
    memset(symbols, 0, blocks * sizeof(size_t));
    for (i = 0; i < blocks; i++) {
      if (s->suffixes[i] >= blocks || symbols[s->suffixes[i]] != 0) {
        st = DS_ERR_CALLBACK;
        goto fail;
      }
      symbols[s->suffixes[i]] = 1;
    }
    for (i = 0; i < blocks; i = j) {
      unsigned long h = s->hashes[s->suffixes[i]];
      size_t bit = h % quick_bits;
      hsa_range_t range;
      for (j = i + 1; j < blocks && s->hashes[s->suffixes[j]] == h; j++) {
      }
      range.begin = i;
      range.end = j;
      st = ds_map_put(&s->ranges, &h, &range);
      if (st != DS_OK) {
        if (st > DS_OK)
          st = DS_ERR_CALLBACK;
        goto fail;
      }
      s->quick[bit / 8] |= (unsigned char)(1u << (bit % 8));
    }
  }
  ds_sha256(reference, size, s->digest);
  ds_context_free(context, symbols);
  ds_context_free(context, records);
  out->impl = s;
  out->context = context != NULL ? context : ds_default_context();
  return DS_OK;
fail:
  ds_context_free(context, symbols);
  ds_context_free(context, records);
  hsa_free(s, context);
  return st;
}
void ds_hsadelta_destroy(ds_hsadelta_index_t *index) {
  if (index != NULL) {
    hsa_free((hsa_index_t *)index->impl, index->context);
    memset(index, 0, sizeof(*index));
  }
}
static int hsa_compare(hsa_query_t *q, size_t block) {
  const hsa_index_t *s = q->index;
  size_t j, p = s->config.block_size;
  size_t count = (q->size - q->position) / p, available = s->blocks - block;
  for (j = 0; j < count && j < available; j++) {
    unsigned long rh = s->hashes[block + j];
    int cmp;
    if (j == q->cached) {
      q->hashes[j] = hsa_hash(s, q->target + q->position + j * p);
      q->cached++;
    }
    if (rh != q->hashes[j])
      return rh < q->hashes[j] ? -1 : 1;
    cmp = memcmp(s->reference + (block + j) * p,
                 q->target + q->position + j * p, p);
    if (cmp != 0)
      return cmp;
  }
  return (available > count) - (available < count);
}
static bool hsa_better(const hsa_match_t *candidate, const hsa_match_t *best) {
  return candidate->length > best->length ||
         (candidate->length == best->length && candidate->length != 0 &&
          (candidate->target < best->target ||
           (candidate->target == best->target &&
            candidate->reference < best->reference)));
}
static void hsa_extend(hsa_query_t *q, size_t block, hsa_match_t *best) {
  const hsa_index_t *s = q->index;
  size_t p = s->config.block_size, at = block * p, forward = p, back = 0;
  hsa_match_t candidate;
  if (memcmp(s->reference + at, q->target + q->position, p) != 0)
    return;
  while (forward < s->size - at && forward < q->size - q->position &&
         s->reference[at + forward] == q->target[q->position + forward])
    forward++;
  while (back < at && back < q->position - q->pending &&
         s->reference[at - back - 1] == q->target[q->position - back - 1])
    back++;
  candidate.reference = at - back;
  candidate.target = q->position - back;
  candidate.length = forward + back;
  if (hsa_better(&candidate, best))
    *best = candidate;
}
static void hsa_find(hsa_query_t *q, unsigned long hash, hsa_match_t *best) {
  const hsa_index_t *s = q->index;
  hsa_range_t range;
  size_t lo, hi, bit = hash % s->quick_bits;
  void *stored;
  if ((s->quick[bit / 8] & (1u << (bit % 8))) == 0)
    return;
  stored = ds_map_get(&s->ranges, &hash);
  if (stored == NULL)
    return;
  memcpy(&range, stored, sizeof(range));
  q->hashes[0] = hash;
  q->cached = 1;
  lo = range.begin;
  hi = range.end;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (hsa_compare(q, s->suffixes[mid]) < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  if (lo < range.end)
    hsa_extend(q, s->suffixes[lo], best);
  if (lo > range.begin)
    hsa_extend(q, s->suffixes[lo - 1], best);
}
static ds_status_t hsa_emit_one(ds_hsadelta_emit_fn emit, void *user,
                                ds_hsadelta_kind_t kind, size_t offset,
                                size_t length, const unsigned char *literals,
                                ds_hsadelta_progress_t *progress) {
  ds_hsadelta_command_t command;
  ds_status_t st;
  if (length == 0)
    return DS_OK;
  command.kind = kind;
  command.offset = offset;
  command.length = length;
  st = emit(&command, literals, user);
  if (st == DS_OK) {
    progress->commands++;
    progress->target_bytes += length;
  }
  return st;
}
ds_status_t ds_hsadelta_emit(const ds_hsadelta_index_t *index,
                             const void *target, size_t size,
                             ds_hsadelta_emit_fn emit, void *user,
                             ds_hsadelta_progress_t *out_progress) {
  ds_hsadelta_progress_t progress;
  hsa_query_t q;
  const hsa_index_t *s;
  size_t p, last, minimum;
  unsigned long hash;
  ds_status_t st = DS_OK;
  memset(&progress, 0, sizeof(progress));
  memset(&q, 0, sizeof(q));
  if (index == NULL || emit == NULL || (size > 0 && target == NULL)) {
    st = DS_ERR_NULL;
    goto done;
  }
  s = (const hsa_index_t *)index->impl;
  if (s == NULL) {
    st = DS_ERR_STATE;
    goto done;
  }
  q.index = s;
  q.target = (const unsigned char *)target;
  q.size = size;
  p = s->config.block_size;
  minimum = s->config.minimum_match != 0 ? s->config.minimum_match : p;
  if (s->blocks > 0 && size >= p) {
    q.hashes = (unsigned long *)ds_context_alloc(
        index->context, s->blocks * sizeof(unsigned long));
    if (q.hashes == NULL) {
      st = DS_ERR_ALLOC;
      goto done;
    }
    last = size - p;
    hash = hsa_hash(s, q.target);
    while (q.position <= last) {
      hsa_match_t best;
      size_t window;
      memset(&best, 0, sizeof(best));
      hsa_find(&q, hash, &best);
      if (best.length < minimum) {
        if (q.position < last)
          hash = hsa_roll(s, hash, q.target + q.position);
        q.position++;
        continue;
      }
      window = last - q.position < p - 1 ? last : q.position + p - 1;
      while (q.position < window) {
        hash = hsa_roll(s, hash, q.target + q.position);
        q.position++;
        hsa_find(&q, hash, &best);
      }
      st = hsa_emit_one(emit, user, DS_HSA_INSERT, q.pending,
                        best.target - q.pending, q.target + q.pending,
                        &progress);
      if (st != DS_OK)
        goto done;
      st = hsa_emit_one(emit, user, DS_HSA_COPY, best.reference, best.length,
                        NULL, &progress);
      if (st != DS_OK)
        goto done;
      q.pending = best.target + best.length;
      q.position = q.pending;
      if (q.position <= last)
        hash = hsa_hash(s, q.target + q.position);
    }
  }
  if (q.pending < size)
    st = hsa_emit_one(emit, user, DS_HSA_INSERT, q.pending, size - q.pending,
                      q.target + q.pending, &progress);
done:
  if (index != NULL)
    ds_context_free(index->context, q.hashes);
  if (out_progress != NULL)
    *out_progress = progress;
  return st;
}
static ds_status_t hsa_collect(const ds_hsadelta_command_t *command,
                               const unsigned char *literals, void *user) {
  ds_hsadelta_script_t *script = (ds_hsadelta_script_t *)user;
  ds_hsadelta_command_t copy = *command;
  ds_status_t st;
  if (command->kind == DS_HSA_INSERT) {
    copy.offset = script->literals.size;
    st = ds_vec_append(&script->literals, literals, command->length);
    if (st != DS_OK)
      return st;
  }
  return ds_vec_push(&script->commands, &copy);
}
void ds_hsadelta_script_destroy(ds_hsadelta_script_t *s) {
  if (s != NULL) {
    ds_vec_destroy(&s->commands);
    ds_vec_destroy(&s->literals);
    memset(s, 0, sizeof(*s));
  }
}
ds_status_t ds_hsadelta_build(const ds_hsadelta_index_t *index,
                              const void *target, size_t size,
                              ds_hsadelta_script_t *out) {
  ds_hsadelta_script_t script;
  const hsa_index_t *s;
  ds_status_t st;
  if (index == NULL || out == NULL)
    return DS_ERR_NULL;
  s = (const hsa_index_t *)index->impl;
  if (s == NULL)
    return DS_ERR_STATE;
  memset(&script, 0, sizeof(script));
  st = ds_vec_init(&script.commands, sizeof(ds_hsadelta_command_t),
                   index->context);
  if (st == DS_OK)
    st = ds_vec_init(&script.literals, 1, index->context);
  if (st == DS_OK)
    st = ds_hsadelta_emit(index, target, size, hsa_collect, &script, NULL);
  if (st != DS_OK) {
    ds_hsadelta_script_destroy(&script);
    return st;
  }
  script.reference_size = s->size;
  script.target_size = size;
  memcpy(script.reference_digest, s->digest, DS_SHA256_SIZE);
  *out = script;
  return DS_OK;
}
ds_status_t ds_hsadelta_apply(const ds_hsadelta_script_t *s,
                              const void *reference, size_t size,
                              size_t max_output, void **out_data,
                              size_t *out_size, ds_context_t *context) {
  unsigned char digest[DS_SHA256_SIZE], *output = NULL;
  size_t i, total = 0;
  if (s == NULL || out_data == NULL || out_size == NULL ||
      (size > 0 && reference == NULL))
    return DS_ERR_NULL;
  if (size != s->reference_size ||
      s->commands.elem_size != sizeof(ds_hsadelta_command_t) ||
      s->literals.elem_size != 1 || s->commands.size > s->commands.capacity ||
      s->literals.size > s->literals.capacity ||
      s->commands.size > (size_t)-1 / sizeof(ds_hsadelta_command_t) ||
      (s->commands.size > 0 && s->commands.data == NULL) ||
      (s->literals.size > 0 && s->literals.data == NULL))
    return DS_ERR_INVALID;
  if (max_output != 0 && s->target_size > max_output)
    return DS_ERR_RANGE;
  ds_sha256(reference, size, digest);
  if (memcmp(digest, s->reference_digest, DS_SHA256_SIZE) != 0)
    return DS_ERR_INVALID;
  for (i = 0; i < s->commands.size; i++) {
    ds_hsadelta_command_t command;
    size_t available;
    memcpy(&command, s->commands.data + i * sizeof(command), sizeof(command));
    if (command.kind != DS_HSA_COPY && command.kind != DS_HSA_INSERT)
      return DS_ERR_INVALID;
    available = command.kind == DS_HSA_COPY ? size : s->literals.size;
    if (command.length == 0 || command.offset > available ||
        command.length > available - command.offset)
      return DS_ERR_INVALID;
    if (!ds_size_add(total, command.length, &total))
      return DS_ERR_OVERFLOW;
    if (total > s->target_size)
      return DS_ERR_INVALID;
  }
  if (total != s->target_size)
    return DS_ERR_INVALID;
  if (total == 0) {
    *out_data = NULL;
    *out_size = 0;
    return DS_OK;
  }
  output = (unsigned char *)ds_context_alloc(context, total);
  if (output == NULL)
    return DS_ERR_ALLOC;
  total = 0;
  for (i = 0; i < s->commands.size; i++) {
    ds_hsadelta_command_t command;
    const unsigned char *source;
    memcpy(&command, s->commands.data + i * sizeof(command), sizeof(command));
    source = command.kind == DS_HSA_COPY ? (const unsigned char *)reference
                                         : s->literals.data;
    memcpy(output + total, source + command.offset, command.length);
    total += command.length;
  }
  *out_data = output;
  *out_size = total;
  return DS_OK;
}
