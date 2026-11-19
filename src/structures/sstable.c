#include <dsa/core/checked.h>
#include <dsa/core/stage.h>
#include <dsa/structures/sstable.h>
#include <limits.h>
#include <string.h>

#define SST_MAGIC_V1 0x53535442UL /* version 1 */
#define SST_MAGIC_V2 0x32545353UL /* bytes "SST2" */
#define SST_MAGIC_V3 0x33545353UL /* bytes "SST3" */
#define SST_FOOTER_V1 28
#define SST_FOOTER_V2 32
#define SST_FOOTER_V3 36
#define SST_MAXLEN DS_SSTABLE_MAX_LEN

/* ---- encoding helpers
 * -------------------------------------------------------
 */

static void sst_put_u32(unsigned char *p, unsigned long v) {
  p[0] = (unsigned char)(v & 0xFFUL);
  p[1] = (unsigned char)((v >> 8) & 0xFFUL);
  p[2] = (unsigned char)((v >> 16) & 0xFFUL);
  p[3] = (unsigned char)((v >> 24) & 0xFFUL);
}

static unsigned long sst_get_u32(const unsigned char *p) {
  return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
         ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* version 1 checksum */
static unsigned long sst_fnv(const unsigned char *p, size_t n) {
  unsigned long h = 2166136261UL;
  size_t i;
  for (i = 0; i < n; i++) {
    h ^= p[i];
    h = (h * 16777619UL) & 0xFFFFFFFFUL;
  }
  return h;
}

/* CRC-32C (Castagnoli, reflected 0x82F63B78), a generated constant table */
static const unsigned long sst_crc_table[256] = {
    0x00000000UL, 0xF26B8303UL, 0xE13B70F7UL, 0x1350F3F4UL, 0xC79A971FUL,
    0x35F1141CUL, 0x26A1E7E8UL, 0xD4CA64EBUL, 0x8AD958CFUL, 0x78B2DBCCUL,
    0x6BE22838UL, 0x9989AB3BUL, 0x4D43CFD0UL, 0xBF284CD3UL, 0xAC78BF27UL,
    0x5E133C24UL, 0x105EC76FUL, 0xE235446CUL, 0xF165B798UL, 0x030E349BUL,
    0xD7C45070UL, 0x25AFD373UL, 0x36FF2087UL, 0xC494A384UL, 0x9A879FA0UL,
    0x68EC1CA3UL, 0x7BBCEF57UL, 0x89D76C54UL, 0x5D1D08BFUL, 0xAF768BBCUL,
    0xBC267848UL, 0x4E4DFB4BUL, 0x20BD8EDEUL, 0xD2D60DDDUL, 0xC186FE29UL,
    0x33ED7D2AUL, 0xE72719C1UL, 0x154C9AC2UL, 0x061C6936UL, 0xF477EA35UL,
    0xAA64D611UL, 0x580F5512UL, 0x4B5FA6E6UL, 0xB93425E5UL, 0x6DFE410EUL,
    0x9F95C20DUL, 0x8CC531F9UL, 0x7EAEB2FAUL, 0x30E349B1UL, 0xC288CAB2UL,
    0xD1D83946UL, 0x23B3BA45UL, 0xF779DEAEUL, 0x05125DADUL, 0x1642AE59UL,
    0xE4292D5AUL, 0xBA3A117EUL, 0x4851927DUL, 0x5B016189UL, 0xA96AE28AUL,
    0x7DA08661UL, 0x8FCB0562UL, 0x9C9BF696UL, 0x6EF07595UL, 0x417B1DBCUL,
    0xB3109EBFUL, 0xA0406D4BUL, 0x522BEE48UL, 0x86E18AA3UL, 0x748A09A0UL,
    0x67DAFA54UL, 0x95B17957UL, 0xCBA24573UL, 0x39C9C670UL, 0x2A993584UL,
    0xD8F2B687UL, 0x0C38D26CUL, 0xFE53516FUL, 0xED03A29BUL, 0x1F682198UL,
    0x5125DAD3UL, 0xA34E59D0UL, 0xB01EAA24UL, 0x42752927UL, 0x96BF4DCCUL,
    0x64D4CECFUL, 0x77843D3BUL, 0x85EFBE38UL, 0xDBFC821CUL, 0x2997011FUL,
    0x3AC7F2EBUL, 0xC8AC71E8UL, 0x1C661503UL, 0xEE0D9600UL, 0xFD5D65F4UL,
    0x0F36E6F7UL, 0x61C69362UL, 0x93AD1061UL, 0x80FDE395UL, 0x72966096UL,
    0xA65C047DUL, 0x5437877EUL, 0x4767748AUL, 0xB50CF789UL, 0xEB1FCBADUL,
    0x197448AEUL, 0x0A24BB5AUL, 0xF84F3859UL, 0x2C855CB2UL, 0xDEEEDFB1UL,
    0xCDBE2C45UL, 0x3FD5AF46UL, 0x7198540DUL, 0x83F3D70EUL, 0x90A324FAUL,
    0x62C8A7F9UL, 0xB602C312UL, 0x44694011UL, 0x5739B3E5UL, 0xA55230E6UL,
    0xFB410CC2UL, 0x092A8FC1UL, 0x1A7A7C35UL, 0xE811FF36UL, 0x3CDB9BDDUL,
    0xCEB018DEUL, 0xDDE0EB2AUL, 0x2F8B6829UL, 0x82F63B78UL, 0x709DB87BUL,
    0x63CD4B8FUL, 0x91A6C88CUL, 0x456CAC67UL, 0xB7072F64UL, 0xA457DC90UL,
    0x563C5F93UL, 0x082F63B7UL, 0xFA44E0B4UL, 0xE9141340UL, 0x1B7F9043UL,
    0xCFB5F4A8UL, 0x3DDE77ABUL, 0x2E8E845FUL, 0xDCE5075CUL, 0x92A8FC17UL,
    0x60C37F14UL, 0x73938CE0UL, 0x81F80FE3UL, 0x55326B08UL, 0xA759E80BUL,
    0xB4091BFFUL, 0x466298FCUL, 0x1871A4D8UL, 0xEA1A27DBUL, 0xF94AD42FUL,
    0x0B21572CUL, 0xDFEB33C7UL, 0x2D80B0C4UL, 0x3ED04330UL, 0xCCBBC033UL,
    0xA24BB5A6UL, 0x502036A5UL, 0x4370C551UL, 0xB11B4652UL, 0x65D122B9UL,
    0x97BAA1BAUL, 0x84EA524EUL, 0x7681D14DUL, 0x2892ED69UL, 0xDAF96E6AUL,
    0xC9A99D9EUL, 0x3BC21E9DUL, 0xEF087A76UL, 0x1D63F975UL, 0x0E330A81UL,
    0xFC588982UL, 0xB21572C9UL, 0x407EF1CAUL, 0x532E023EUL, 0xA145813DUL,
    0x758FE5D6UL, 0x87E466D5UL, 0x94B49521UL, 0x66DF1622UL, 0x38CC2A06UL,
    0xCAA7A905UL, 0xD9F75AF1UL, 0x2B9CD9F2UL, 0xFF56BD19UL, 0x0D3D3E1AUL,
    0x1E6DCDEEUL, 0xEC064EEDUL, 0xC38D26C4UL, 0x31E6A5C7UL, 0x22B65633UL,
    0xD0DDD530UL, 0x0417B1DBUL, 0xF67C32D8UL, 0xE52CC12CUL, 0x1747422FUL,
    0x49547E0BUL, 0xBB3FFD08UL, 0xA86F0EFCUL, 0x5A048DFFUL, 0x8ECEE914UL,
    0x7CA56A17UL, 0x6FF599E3UL, 0x9D9E1AE0UL, 0xD3D3E1ABUL, 0x21B862A8UL,
    0x32E8915CUL, 0xC083125FUL, 0x144976B4UL, 0xE622F5B7UL, 0xF5720643UL,
    0x07198540UL, 0x590AB964UL, 0xAB613A67UL, 0xB831C993UL, 0x4A5A4A90UL,
    0x9E902E7BUL, 0x6CFBAD78UL, 0x7FAB5E8CUL, 0x8DC0DD8FUL, 0xE330A81AUL,
    0x115B2B19UL, 0x020BD8EDUL, 0xF0605BEEUL, 0x24AA3F05UL, 0xD6C1BC06UL,
    0xC5914FF2UL, 0x37FACCF1UL, 0x69E9F0D5UL, 0x9B8273D6UL, 0x88D28022UL,
    0x7AB90321UL, 0xAE7367CAUL, 0x5C18E4C9UL, 0x4F48173DUL, 0xBD23943EUL,
    0xF36E6F75UL, 0x0105EC76UL, 0x12551F82UL, 0xE03E9C81UL, 0x34F4F86AUL,
    0xC69F7B69UL, 0xD5CF889DUL, 0x27A40B9EUL, 0x79B737BAUL, 0x8BDCB4B9UL,
    0x988C474DUL, 0x6AE7C44EUL, 0xBE2DA0A5UL, 0x4C4623A6UL, 0x5F16D052UL,
    0xAD7D5351UL};

/* c is the running register: start with 0xFFFFFFFF, finish with ^ */
static unsigned long sst_crc_update(unsigned long c, const unsigned char *p,
                                    size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    c = sst_crc_table[(c ^ p[i]) & 0xFFUL] ^ (c >> 8);
  }
  return c & 0xFFFFFFFFUL;
}

unsigned long ds_sstable_crc32c(unsigned long crc, const void *p, size_t n) {
  if (p == NULL || n == 0) {
    return crc & 0xFFFFFFFFUL;
  }
  return sst_crc_update((crc & 0xFFFFFFFFUL) ^ 0xFFFFFFFFUL,
                        (const unsigned char *)p, n) ^
         0xFFFFFFFFUL;
}

static unsigned long sst_key_hash(const unsigned char *p, size_t n) {
  unsigned long h = sst_fnv(p, n);
  h ^= h >> 16;
  h = (h * 0x85EBCA6BUL) & 0xFFFFFFFFUL;
  h ^= h >> 13;
  h = (h * 0xC2B2AE35UL) & 0xFFFFFFFFUL;
  h ^= h >> 16;
  return h;
}

/* the hash AMQ filters take: as wide as unsigned long */
static unsigned long sst_amq_hash(const unsigned char *p, size_t n) {
#if ULONG_MAX > 0xFFFFFFFFUL
  unsigned long h = 0xCBF29CE484222325UL;
  size_t i;
  for (i = 0; i < n; i++) {
    h ^= p[i];
    h *= 0x100000001B3UL;
  }
  h ^= h >> 33;
  h *= 0xFF51AFD7ED558CCDUL;
  h ^= h >> 33;
  h *= 0xC4CEB9FE1A85EC53UL;
  h ^= h >> 33;
  return h;
#else
  return sst_key_hash(p, n);
#endif
}

/* a key hash folded and mixed to 32 bits (the Bloom filter's) */
static unsigned long sst_mix32(unsigned long h) {
#if ULONG_MAX > 0xFFFFFFFFUL
  h ^= h >> 32;
#endif
  h &= 0xFFFFFFFFUL;
  h ^= h >> 16;
  h = (h * 0x85EBCA6BUL) & 0xFFFFFFFFUL;
  h ^= h >> 13;
  h = (h * 0xC2B2AE35UL) & 0xFFFFFFFFUL;
  h ^= h >> 16;
  return h;
}

/* a key hash mixed to the width of unsigned long (AMQ filters') */
static unsigned long sst_mix_wide(unsigned long h) {
#if ULONG_MAX > 0xFFFFFFFFUL
  h ^= h >> 33;
  h *= 0xFF51AFD7ED558CCDUL;
  h ^= h >> 33;
  h *= 0xC4CEB9FE1A85EC53UL;
  h ^= h >> 33;
  return h;
#else
  return sst_mix32(h);
#endif
}

/* the hash a filter takes for a key: the comparator's key hash, mixed, for
 * a keyed filter, otherwise a hash of the bytes */
static unsigned long sst_hash_for(bool amq, const ds_sstable_key_hash_t *kh,
                                  const unsigned char *p, size_t n) {
  if (kh != NULL) {
    unsigned long h = kh->hash(p, n, kh->user);
    return amq ? sst_mix_wide(h) : sst_mix32(h);
  }
  return amq ? sst_amq_hash(p, n) : sst_key_hash(p, n);
}

static unsigned long sst_filter_hash(const ds_sstable_builder_t *b,
                                     const unsigned char *p, size_t n) {
  return sst_hash_for(b->filter_ops != NULL, b->keyed ? &b->key_hash : NULL, p,
                      n);
}

static int sst_bytewise_cmp(const unsigned char *a, size_t na,
                            const unsigned char *b, size_t nb, void *user) {
  size_t n = na < nb ? na : nb;
  int c = n == 0 ? 0 : memcmp(a, b, n);
  (void)user;
  if (c != 0) {
    return c;
  }
  return (na > nb) - (na < nb);
}

static const ds_sstable_comparator_t sst_bytewise = {sst_bytewise_cmp, NULL,
                                                     0UL};

const ds_sstable_comparator_t *ds_sstable_bytewise(void) {
  return &sst_bytewise;
}

static int sst_cmp(const ds_sstable_comparator_t *c, const unsigned char *a,
                   size_t na, const unsigned char *b, size_t nb) {
  return c->compare(a, na, b, nb, c->user);
}

/* NULL means bytewise; a comparator needs compare, and id 0 is reserved */
static bool sst_cmp_valid(const ds_sstable_comparator_t *c) {
  return c == NULL || (c->compare != NULL && !DS_ABOVE_U32(c->id) &&
                       (c->id != 0 || c->compare == sst_bytewise_cmp));
}

static ds_status_t sst_grow(ds_context_t *ctx, unsigned char **buf,
                            size_t *cap, size_t need) {
  size_t nc;
  void *q;
  if (need <= *cap) {
    return DS_OK;
  }
  nc = *cap < 256 ? 256 : *cap;
  while (nc < need) {
    if (nc > ((size_t)-1) / 2) {
      nc = need;
      break;
    }
    nc *= 2;
  }
  q = ds_context_realloc(ctx, *buf, nc);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *buf = (unsigned char *)q;
  *cap = nc;
  return DS_OK;
}

static ds_status_t sst_put(ds_context_t *ctx, unsigned char **buf, size_t *len,
                           size_t *cap, const void *p, size_t n) {
  size_t need;
  ds_status_t st;
  if (!ds_size_add(*len, n, &need)) {
    return DS_ERR_OVERFLOW;
  }
  st = sst_grow(ctx, buf, cap, need);
  if (st != DS_OK) {
    return st;
  }
  if (n > 0) {
    memcpy(*buf + *len, p, n);
  }
  *len += n;
  return DS_OK;
}

static ds_status_t sst_put_varint(ds_context_t *ctx, unsigned char **buf,
                                  size_t *len, size_t *cap, unsigned long v) {
  unsigned char tmp[10];
  size_t n = 0;
  do {
    unsigned char c = (unsigned char)(v & 0x7FUL);
    v >>= 7;
    tmp[n++] = (unsigned char)(c | (v != 0 ? 0x80 : 0));
  } while (v != 0 && n < sizeof(tmp));
  return sst_put(ctx, buf, len, cap, tmp, n);
}

/* decode a varint of at most 32 bits at *pos below end; false when it is
 * truncated or longer than 32 bits */
static bool sst_get_varint(const unsigned char *p, size_t end, size_t *pos,
                           unsigned long *out) {
  unsigned long v = 0;
  unsigned shift = 0;
  while (*pos < end && shift <= 28) {
    unsigned char c = p[(*pos)++];
    if (shift == 28 && (c & 0xF0) != 0) {
      return false;
    }
    v |= (unsigned long)(c & 0x7F) << shift;
    if ((c & 0x80) == 0) {
      *out = v;
      return true;
    }
    shift += 7;
  }
  return false;
}

/* [off, off + size) inside [0, limit) */
static bool sst_range(size_t off, size_t size, size_t limit) {
  return off <= limit && size <= limit - off;
}

/* base + rel when it is below limit */
static bool sst_at(size_t base, unsigned long rel, size_t limit, size_t *out) {
  if (base >= limit || rel >= (unsigned long)(limit - base)) {
    return false;
  }
  *out = base + (size_t)rel;
  return true;
}

/* ---- builder
 * ----------------------------------------------------------------
 */

void ds_sstable_options_init(ds_sstable_options_t *options) {
  if (options != NULL) {
    memset(options, 0, sizeof(*options));
  }
}

/* an AMQ backend a table can carry: it saves images and loads them */
static bool sst_amq_usable(const ds_amq_ops_t *ops) {
  return ops->save != NULL && ops->load != NULL && ops->id >= 1 &&
         ops->id <= 255;
}

ds_status_t ds_sstable_builder_init_opts(ds_sstable_builder_t *b,
                                         const ds_sstable_options_t *options,
                                         ds_context_t *context) {
  ds_sstable_options_t def;
  if (b == NULL) {
    return DS_ERR_NULL;
  }
  if (options == NULL) {
    ds_sstable_options_init(&def);
    options = &def;
  }
  if (options->bits_per_key > 32 || !sst_cmp_valid(options->comparator) ||
      (options->filter != NULL && options->bits_per_key != 0)) {
    return DS_ERR_INVALID;
  }
  if (options->filter != NULL && !sst_amq_usable(options->filter)) {
    return DS_ERR_UNSUPPORTED;
  }
  memset(b, 0, sizeof(*b));
  b->block_size = options->block_size == 0 ? 4096 : options->block_size;
  b->restart_interval =
      options->restart_interval == 0 ? 16 : options->restart_interval;
  b->bits_per_key = options->bits_per_key;
  b->filter_ops = options->filter;
  b->filter_fp_bits = options->filter_fp_bits;
  b->cmp = options->comparator == NULL ? sst_bytewise : *options->comparator;
  /* Filters hash bytes, which a custom order may not respect (it can
   * equate different spellings of a key): such tables keep a filter only
   * with a key hash consistent with the order. */
  if (b->cmp.id != 0) {
    if (options->key_hash != NULL && options->key_hash->hash != NULL) {
      b->keyed = true;
      b->key_hash = *options->key_hash;
    } else {
      b->bits_per_key = 0;
      b->filter_ops = NULL;
    }
  }
  b->context = context != NULL ? context : ds_default_context();
  return DS_OK;
}

ds_status_t ds_sstable_builder_init_cmp(ds_sstable_builder_t *b,
                                        size_t block_size,
                                        size_t restart_interval,
                                        size_t bits_per_key,
                                        const ds_sstable_comparator_t *cmp,
                                        ds_context_t *context) {
  ds_sstable_options_t o;
  ds_sstable_options_init(&o);
  o.block_size = block_size;
  o.restart_interval = restart_interval;
  o.bits_per_key = bits_per_key;
  o.comparator = cmp;
  return ds_sstable_builder_init_opts(b, &o, context);
}

ds_status_t ds_sstable_builder_init(ds_sstable_builder_t *b, size_t block_size,
                                    size_t restart_interval,
                                    size_t bits_per_key,
                                    ds_context_t *context) {
  return ds_sstable_builder_init_cmp(b, block_size, restart_interval,
                                     bits_per_key, NULL, context);
}

void ds_sstable_builder_destroy(ds_sstable_builder_t *b) {
  if (b == NULL) {
    return;
  }
  ds_context_free(b->context, b->buf);
  ds_context_free(b->context, b->restarts);
  ds_context_free(b->context, b->last_key);
  ds_context_free(b->context, b->index);
  ds_context_free(b->context, b->hashes);
  memset(b, 0, sizeof(*b));
}

static ds_status_t sst_flush_block(ds_sstable_builder_t *b) {
  unsigned char u[4];
  size_t i;
  size_t size;
  ds_status_t st = DS_OK;
  if (b->block_entries == 0) {
    return DS_OK;
  }
  for (i = 0; i < b->restart_count && st == DS_OK; i++) {
    sst_put_u32(u, b->restarts[i]);
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, u, 4);
  }
  if (st == DS_OK) {
    sst_put_u32(u, (unsigned long)b->restart_count);
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, u, 4);
  }
  size = b->len - b->block_start;
  /* index entry: full last key, value = offset, size */
  if (st == DS_OK) {
    st =
        sst_put_varint(b->context, &b->index, &b->index_len, &b->index_cap, 0);
  }
  if (st == DS_OK) {
    st = sst_put_varint(b->context, &b->index, &b->index_len, &b->index_cap,
                        (unsigned long)b->last_len);
  }
  if (st == DS_OK) {
    /* the value: two varints, measured first */
    unsigned char vbuf[20];
    size_t vl = 0;
    unsigned char *vb = vbuf;
    size_t vc = sizeof(vbuf);
    st = sst_put_varint(NULL, &vb, &vl, &vc, (unsigned long)b->block_start);
    if (st == DS_OK) {
      st = sst_put_varint(NULL, &vb, &vl, &vc, (unsigned long)size);
    }
    if (st == DS_OK) {
      st = sst_put_varint(b->context, &b->index, &b->index_len, &b->index_cap,
                          (unsigned long)vl << 1);
    }
    if (st == DS_OK) {
      st = sst_put(b->context, &b->index, &b->index_len, &b->index_cap,
                   b->last_key, b->last_len);
    }
    if (st == DS_OK) {
      st = sst_put(b->context, &b->index, &b->index_len, &b->index_cap, vbuf,
                   vl);
    }
  }
  b->index_entries++;
  b->block_start = b->len;
  b->block_entries = 0;
  b->restart_count = 0;
  return st;
}

static ds_status_t sst_reserve_longs(ds_context_t *ctx, unsigned long **arr,
                                     size_t *cap, size_t first) {
  size_t nc = *cap < first / 2 ? first : *cap * 2;
  size_t bytes;
  void *q;
  if (*cap > ((size_t)-1) / 2 ||
      !ds_size_mul(nc, sizeof(unsigned long), &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  q = ds_context_realloc(ctx, *arr, bytes);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *arr = (unsigned long *)q;
  *cap = nc;
  return DS_OK;
}

static ds_status_t sst_add(ds_sstable_builder_t *b, const void *key,
                           size_t key_len, const void *value, size_t value_len,
                           bool tombstone) {
  const unsigned char *k = (const unsigned char *)key;
  size_t shared = 0;
  size_t need;
  ds_status_t st;
  if (b == NULL || (key == NULL && key_len > 0) ||
      (value == NULL && value_len > 0)) {
    return DS_ERR_NULL;
  }
  if (b->error != DS_OK) {
    return b->error;
  }
  /* the entry plus its varints and the block's restart and index growth
   * must keep the table below SST_MAXLEN; value_len << 1 fits 32 bits */
  if (value_len > SST_MAXLEN / 2 || !ds_size_add(key_len, value_len, &need) ||
      !ds_size_add(need, 64, &need) || need > SST_MAXLEN ||
      b->len > SST_MAXLEN - need) {
    return DS_ERR_OVERFLOW;
  }
  if (b->has_last &&
      sst_cmp(&b->cmp, b->last_key, b->last_len, k, key_len) >= 0) {
    return DS_ERR_INVALID;
  }
  if (b->block_entries % b->restart_interval == 0) {
    if (b->restart_count == b->restart_cap) {
      st = sst_reserve_longs(b->context, &b->restarts, &b->restart_cap, 16);
      if (st != DS_OK) {
        return st;
      }
    }
    b->restarts[b->restart_count++] = (unsigned long)(b->len - b->block_start);
  } else {
    size_t lim = b->last_len < key_len ? b->last_len : key_len;
    while (shared < lim && b->last_key[shared] == k[shared]) {
      shared++;
    }
  }
  if (b->hash_count == b->hash_cap &&
      (b->bits_per_key > 0 || b->filter_ops != NULL)) {
    st = sst_reserve_longs(b->context, &b->hashes, &b->hash_cap, 64);
    if (st != DS_OK) {
      return st;
    }
  }
  st = sst_put_varint(b->context, &b->buf, &b->len, &b->cap,
                      (unsigned long)shared);
  if (st == DS_OK) {
    st = sst_put_varint(b->context, &b->buf, &b->len, &b->cap,
                        (unsigned long)(key_len - shared));
  }
  if (st == DS_OK) {
    st = sst_put_varint(b->context, &b->buf, &b->len, &b->cap,
                        ((unsigned long)value_len << 1) |
                            (tombstone ? 1UL : 0UL));
  }
  if (st == DS_OK) {
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, k + shared,
                 key_len - shared);
  }
  if (st == DS_OK) {
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, value, value_len);
  }
  if (st == DS_OK) {
    size_t lc = b->last_cap;
    st = sst_grow(b->context, &b->last_key, &lc, key_len + 1);
    b->last_cap = lc;
  }
  if (st != DS_OK) {
    b->error = st;
    return st;
  }
  if (key_len > 0) {
    memcpy(b->last_key, k, key_len);
  }
  b->last_len = key_len;
  b->has_last = true;
  if (b->bits_per_key > 0 || b->filter_ops != NULL) {
    b->hashes[b->hash_count++] = sst_filter_hash(b, k, key_len);
  }
  b->entries++;
  b->block_entries++;
  if (b->len - b->block_start >= b->block_size) {
    st = sst_flush_block(b);
    if (st != DS_OK) {
      b->error = st;
    }
  }
  return st;
}

ds_status_t ds_sstable_builder_add(ds_sstable_builder_t *b, const void *key,
                                   size_t key_len, const void *value,
                                   size_t value_len) {
  return sst_add(b, key, key_len, value, value_len, false);
}

ds_status_t ds_sstable_builder_add_tombstone(ds_sstable_builder_t *b,
                                             const void *key, size_t key_len) {
  return sst_add(b, key, key_len, NULL, 0, true);
}

static bool sst_bloom_params(size_t bits_per_key, size_t n, size_t *out_k,
                             size_t *out_bits) {
  size_t k = (bits_per_key * 69 + 50) / 100;
  size_t bits;
  if (!ds_size_mul(n, bits_per_key, &bits) || bits > SST_MAXLEN) {
    return false;
  }
  if (k < 1) {
    k = 1;
  }
  if (k > 30) {
    k = 30;
  }
  if (bits < 64) {
    bits = 64;
  }
  *out_k = k;
  *out_bits = (bits + 7) / 8 * 8;
  return true;
}

/* the AMQ filter over the collected hashes, appended as an image; a
 * backend with exact capacity that overflows is rebuilt larger */
static ds_status_t sst_put_amq(ds_sstable_builder_t *b) {
  ds_amq_t f;
  size_t capacity = b->hash_count;
  size_t len = 0;
  int attempt;
  ds_status_t st = DS_ERR_OVERFLOW;
  for (attempt = 0; attempt < 4 && st == DS_ERR_OVERFLOW; attempt++) {
    st = ds_amq_init(&f, b->filter_ops, capacity, b->filter_fp_bits,
                     b->context);
    if (st != DS_OK) {
      return st;
    }
    st = ds_amq_build(&f, b->hashes, b->hash_count);
    if (st == DS_ERR_OVERFLOW) {
      ds_amq_destroy(&f);
      if (!ds_size_mul(capacity < 16 ? 16 : capacity, 2, &capacity)) {
        return DS_ERR_OVERFLOW;
      }
    }
  }
  if (st == DS_ERR_OVERFLOW) {
    return st; /* every attempt overflowed; f is destroyed */
  }
  if (st == DS_OK) {
    st = ds_amq_save(&f, NULL, 0, &len);
  }
  if (st == DS_OK) {
    st = len > SST_MAXLEN || b->len > SST_MAXLEN - len
             ? DS_ERR_OVERFLOW
             : sst_grow(b->context, &b->buf, &b->cap, b->len + len);
  }
  if (st == DS_OK) {
    st = ds_amq_save(&f, b->buf + b->len, len, &len);
  }
  if (st == DS_OK) {
    b->len += len;
  }
  ds_amq_destroy(&f);
  return st;
}

ds_status_t ds_sstable_builder_finish(ds_sstable_builder_t *b,
                                      unsigned char **out, size_t *out_len) {
  unsigned char foot[SST_FOOTER_V3];
  size_t foot_len = SST_FOOTER_V2;
  unsigned char u[4];
  size_t index_off;
  size_t index_len;
  size_t filter_off;
  size_t filter_len = 0;
  size_t pos;
  size_t count = 0;
  ds_status_t st;
  if (b == NULL || out == NULL || out_len == NULL) {
    return DS_ERR_NULL;
  }
  if (b->error != DS_OK) {
    return b->error;
  }
  st = sst_flush_block(b);
  index_off = b->len;
  if (st == DS_OK) {
    st =
        sst_put(b->context, &b->buf, &b->len, &b->cap, b->index, b->index_len);
  }
  /* every index entry is a restart */
  for (pos = 0; pos < b->index_len && st == DS_OK; count++) {
    unsigned long sh;
    unsigned long un;
    unsigned long vf;
    sst_put_u32(u, (unsigned long)pos);
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, u, 4);
    if (!sst_get_varint(b->index, b->index_len, &pos, &sh) ||
        !sst_get_varint(b->index, b->index_len, &pos, &un) ||
        !sst_get_varint(b->index, b->index_len, &pos, &vf)) {
      st = DS_ERR_INTERNAL;
      break;
    }
    pos += un + (vf >> 1);
  }
  if (st == DS_OK) {
    sst_put_u32(u, (unsigned long)count);
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, u, 4);
  }
  index_len = b->len - index_off;
  filter_off = b->len;
  if (st == DS_OK && b->filter_ops != NULL) {
    st = sst_put_amq(b);
    filter_len = b->len - filter_off;
    foot_len = SST_FOOTER_V3;
  } else if (st == DS_OK && b->bits_per_key > 0) {
    size_t k;
    size_t bits;
    size_t i;
    unsigned char kb;
    if (b->keyed) {
      foot_len = SST_FOOTER_V3; /* the kind field carries the flag */
    }
    if (!sst_bloom_params(b->bits_per_key, b->hash_count, &k, &bits)) {
      st = DS_ERR_OVERFLOW;
    }
    if (st == DS_OK) {
      kb = (unsigned char)k;
      st = sst_put(b->context, &b->buf, &b->len, &b->cap, &kb, 1);
    }
    if (st == DS_OK) {
      st = b->len > SST_MAXLEN - bits / 8
               ? DS_ERR_OVERFLOW
               : sst_grow(b->context, &b->buf, &b->cap, b->len + bits / 8);
    }
    if (st == DS_OK) {
      unsigned char *f = b->buf + b->len;
      memset(f, 0, bits / 8);
      for (i = 0; i < b->hash_count; i++) {
        unsigned long h = b->hashes[i];
        unsigned long delta = ((h >> 17) | (h << 15)) & 0xFFFFFFFFUL;
        size_t j;
        for (j = 0; j < k; j++) {
          size_t bit = (size_t)(h % (unsigned long)bits);
          f[bit / 8] = (unsigned char)(f[bit / 8] | (1U << (bit % 8)));
          h = (h + delta) & 0xFFFFFFFFUL;
        }
      }
      b->len += bits / 8;
    }
    filter_len = b->len - filter_off;
  }
  if (st == DS_OK && b->len > SST_MAXLEN - foot_len) {
    st = DS_ERR_OVERFLOW;
  }
  if (st == DS_OK) {
    unsigned long crc;
    size_t fields = foot_len - 8;
    sst_put_u32(foot, (unsigned long)index_off);
    sst_put_u32(foot + 4, (unsigned long)index_len);
    sst_put_u32(foot + 8, (unsigned long)filter_off);
    sst_put_u32(foot + 12, (unsigned long)filter_len);
    sst_put_u32(foot + 16, (unsigned long)b->entries);
    sst_put_u32(foot + 20, b->cmp.id);
    if (foot_len == SST_FOOTER_V3) {
      sst_put_u32(foot + 24,
                  (b->filter_ops != NULL ? DS_SSTABLE_FILTER_AMQ
                                         : DS_SSTABLE_FILTER_BLOOM) |
                      (b->keyed ? DS_SSTABLE_FILTER_KEYED : 0U));
    }
    /* the checksum covers the data and the footer fields before it */
    crc = sst_crc_update(0xFFFFFFFFUL, b->buf, b->len);
    crc = sst_crc_update(crc, foot, fields) ^ 0xFFFFFFFFUL;
    sst_put_u32(foot + fields, crc);
    sst_put_u32(foot + fields + 4,
                foot_len == SST_FOOTER_V3 ? SST_MAGIC_V3 : SST_MAGIC_V2);
    st = sst_put(b->context, &b->buf, &b->len, &b->cap, foot, foot_len);
  }
  if (st != DS_OK) {
    b->error = st;
    return st;
  }
  *out = b->buf;
  *out_len = b->len;
  b->buf = NULL;
  b->len = b->cap = 0;
  b->error = DS_ERR_STATE; /* spent */
  return DS_OK;
}

/* ---- reader
 * -----------------------------------------------------------------
 */

struct ds_sstable_reader {
  ds_sstable_source_t source;
  ds_context_t *context;
  unsigned char *metadata;
  size_t metadata_len;
  size_t data_end;
  ds_sstable_iter_t lookup;
};

static bool sst_is_open(const ds_sstable_t *t) {
  return t != NULL && (t->data != NULL || t->reader != NULL);
}

static void sst_index_view(const ds_sstable_t *t, ds_sstable_t *view) {
  *view = *t;
  if (t->reader != NULL) {
    view->data = t->reader->metadata;
    view->len = t->reader->metadata_len;
    view->index_off = 0;
  }
}

static const unsigned char *sst_filter_bytes(const ds_sstable_t *t) {
  if (t->filter_len == 0) {
    return NULL;
  }
  return t->reader == NULL
             ? t->data + t->filter_off
             : t->reader->metadata + (t->filter_off - t->reader->data_end);
}

static size_t sst_data_end(const ds_sstable_t *t) {
  return t->reader == NULL ? t->index_off : t->reader->data_end;
}

static ds_status_t sst_source_read(const ds_sstable_source_t *source,
                                   size_t offset, void *buf, size_t count) {
  ds_status_t st;
  if (!sst_range(offset, count, source->length)) {
    return DS_ERR_INVALID;
  }
  if (count == 0) {
    return DS_OK;
  }
  st = source->read(source->user, offset, buf, count);
  return st > DS_OK ? DS_ERR_CALLBACK : st;
}

void ds_sstable_source_init(ds_sstable_source_t *source) {
  if (source != NULL) {
    memset(source, 0, sizeof(*source));
  }
}

/* restart array of a block [off, off + size) that must lie below limit */
static bool sst_block_restarts(const ds_sstable_t *t, size_t off, size_t size,
                               size_t limit, size_t *out_count,
                               size_t *out_entries_end) {
  unsigned long n;
  if (size < 4 || !sst_range(off, size, limit)) {
    return false;
  }
  n = sst_get_u32(t->data + off + size - 4);
  if (n > (unsigned long)((size - 4) / 4)) {
    return false;
  }
  *out_count = (size_t)n;
  *out_entries_end = off + size - 4 - 4 * (size_t)n;
  return true;
}

/* Parse a suffix containing the complete footer. No data access here. */
static ds_status_t sst_footer(ds_sstable_t *t, const unsigned char *tail,
                              size_t tail_len, size_t len,
                              const ds_sstable_comparator_t *cmp,
                              size_t *out_foot) {
  unsigned long magic, id, kind = 0;
  bool have_kind = false;
  const unsigned char *f;
  size_t foot, body;
  if (!sst_cmp_valid(cmp) || len < SST_FOOTER_V1 || len > SST_MAXLEN ||
      tail_len < SST_FOOTER_V1) {
    return DS_ERR_INVALID;
  }
  if (cmp == NULL) {
    cmp = &sst_bytewise;
  }
  magic = sst_get_u32(tail + tail_len - 4);
  if (magic == SST_MAGIC_V3) {
    foot = SST_FOOTER_V3;
  } else if (magic == SST_MAGIC_V2) {
    foot = SST_FOOTER_V2;
  } else if (magic == SST_MAGIC_V1) {
    foot = SST_FOOTER_V1;
  } else {
    return DS_ERR_INVALID;
  }
  if (tail_len < foot || len < foot) {
    return DS_ERR_INVALID;
  }
  f = tail + tail_len - foot;
  t->version = magic == SST_MAGIC_V3 ? 3U : magic == SST_MAGIC_V2 ? 2U : 1U;
  id = t->version == 1 ? 0 : sst_get_u32(f + 20);
  if (id != cmp->id) {
    return DS_ERR_INVALID;
  }
  if (t->version == 3) {
    kind = sst_get_u32(f + 24);
    have_kind = true;
  }
  body = len - foot;
  t->len = len;
  t->index_off = (size_t)sst_get_u32(f);
  t->index_len = (size_t)sst_get_u32(f + 4);
  t->filter_off = (size_t)sst_get_u32(f + 8);
  t->filter_len = (size_t)sst_get_u32(f + 12);
  t->entries = (size_t)sst_get_u32(f + 16);
  t->cmp = *cmp;
  if (!sst_range(t->index_off, t->index_len, body) || t->index_len < 4 ||
      (t->filter_len != 0 &&
       (t->filter_off < t->index_off + t->index_len ||
        !sst_range(t->filter_off, t->filter_len, body)))) {
    return DS_ERR_INVALID;
  }
  if (!have_kind) {
    kind =
        t->filter_len >= 2 ? DS_SSTABLE_FILTER_BLOOM : DS_SSTABLE_FILTER_NONE;
  }
  t->keyed = (kind & DS_SSTABLE_FILTER_KEYED) != 0;
  kind &= ~(unsigned long)DS_SSTABLE_FILTER_KEYED;
  if (kind > DS_SSTABLE_FILTER_AMQ ||
      (t->keyed && (cmp->id == 0 || kind == DS_SSTABLE_FILTER_NONE))) {
    return DS_ERR_INVALID;
  }
  t->filter_kind = (unsigned)kind;
  *out_foot = foot;
  return DS_OK;
}

static ds_status_t sst_metadata(ds_sstable_t *t) {
  ds_sstable_t v;
  const unsigned char *f = sst_filter_bytes(t);
  size_t n, end;
  sst_index_view(t, &v);
  if (!sst_block_restarts(&v, v.index_off, v.index_len, v.len, &n, &end)) {
    return DS_ERR_INVALID;
  }
  if (t->filter_kind == DS_SSTABLE_FILTER_NONE) {
    return t->version < 3 || t->filter_len == 0 ? DS_OK : DS_ERR_INVALID;
  }
  if (t->filter_kind == DS_SSTABLE_FILTER_BLOOM) {
    return t->filter_len >= 2 && t->filter_len - 1 <= (size_t)-1 / 8 &&
                   (unsigned long)(t->filter_len - 1) <= ULONG_MAX / 8 &&
                   f[0] >= 1 && f[0] <= 30
               ? DS_OK
               : DS_ERR_INVALID;
  }
  if (t->filter_kind == DS_SSTABLE_FILTER_AMQ) {
    ds_amq_image_t info;
    ds_status_t st;
    memset(&info, 0, sizeof(info));
    st = ds_amq_image_info(f, t->filter_len, &info);
    t->filter_id = info.id;
    return (st == DS_OK && info.length == t->filter_len) ||
                   st == DS_ERR_UNSUPPORTED
               ? DS_OK
               : DS_ERR_INVALID;
  }
  return DS_ERR_INVALID;
}

static ds_status_t sst_load_filter(ds_sstable_t *t, const ds_amq_ops_t *filter,
                                   ds_context_t *context) {
  const unsigned char *img;
  ds_status_t st;
  if (t->filter_kind != DS_SSTABLE_FILTER_AMQ ||
      (t->cmp.id != 0 && !t->keyed)) {
    return DS_OK;
  }
  img = sst_filter_bytes(t);
  if (filter != NULL && filter->id == t->filter_id) {
    st = ds_amq_load_with(&t->amq, filter, img, t->filter_len, context);
  } else {
    st = ds_amq_load(&t->amq, img, t->filter_len, context);
  }
  return st == DS_ERR_UNSUPPORTED ? DS_OK : st;
}

ds_status_t ds_sstable_open_cmp(ds_sstable_t *t, const void *data, size_t len,
                                const ds_sstable_comparator_t *cmp) {
  const unsigned char *p = (const unsigned char *)data;
  size_t foot;
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (data == NULL) {
    return DS_ERR_NULL;
  }
  st = sst_footer(t, p, len, len, cmp, &foot);
  if (st == DS_OK) {
    unsigned long actual =
        t->version == 1
            ? sst_fnv(p, len - foot)
            : (sst_crc_update(0xFFFFFFFFUL, p, len - 8) ^ 0xFFFFFFFFUL);
    if (sst_get_u32(p + len - 8) != actual) {
      st = DS_ERR_INVALID;
    }
  }
  if (st == DS_OK) {
    t->data = p;
    st = sst_metadata(t);
  }
  if (st != DS_OK) {
    memset(t, 0, sizeof(*t));
  }
  return st;
}

ds_status_t ds_sstable_open_ex(ds_sstable_t *t, const void *data, size_t len,
                               const ds_sstable_comparator_t *cmp,
                               const ds_amq_ops_t *filter,
                               ds_context_t *context) {
  ds_status_t st = ds_sstable_open_cmp(t, data, len, cmp);
  if (st == DS_OK) {
    st = sst_load_filter(t, filter, context);
  }
  if (st != DS_OK && t != NULL) {
    ds_sstable_close(t);
  }
  return st;
}

ds_status_t ds_sstable_open_reader(ds_sstable_t *t,
                                   const ds_sstable_source_t *source,
                                   const ds_sstable_comparator_t *cmp,
                                   const ds_amq_ops_t *filter,
                                   ds_context_t *context) {
  unsigned char tail[SST_FOOTER_V3], scratch[4096];
  size_t tail_len, foot, pos, span, meta_len;
  unsigned long checksum, expected;
  struct ds_sstable_reader *r;
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (source == NULL || source->read == NULL) {
    return DS_ERR_NULL;
  }
  if (source->length < SST_FOOTER_V1 || source->length > SST_MAXLEN) {
    return DS_ERR_INVALID;
  }
  tail_len = source->length < sizeof(tail) ? source->length : sizeof(tail);
  st = sst_source_read(source, source->length - tail_len, tail, tail_len);
  if (st != DS_OK) {
    return st;
  }
  st = sst_footer(t, tail, tail_len, source->length, cmp, &foot);
  if (st != DS_OK) {
    goto fail;
  }
  span = t->version == 1 ? t->len - foot : t->len - 8;
  checksum = t->version == 1 ? 2166136261UL : 0xFFFFFFFFUL;
  expected = sst_get_u32(tail + tail_len - 8);
  for (pos = 0; pos < span;) {
    size_t n = span - pos, i;
    if (n > sizeof(scratch)) {
      n = sizeof(scratch);
    }
    st = sst_source_read(source, pos, scratch, n);
    if (st != DS_OK) {
      goto fail;
    }
    if (t->version == 1) {
      for (i = 0; i < n; i++) {
        checksum = ((checksum ^ scratch[i]) * 16777619UL) & 0xFFFFFFFFUL;
      }
    } else {
      checksum = sst_crc_update(checksum, scratch, n);
    }
    pos += n;
  }
  if (t->version != 1) {
    checksum ^= 0xFFFFFFFFUL;
  }
  if (checksum != expected) {
    st = DS_ERR_INVALID;
    goto fail;
  }
  /* Retain the index and optional filter, including any format-permitted gap.
   */
  meta_len = t->index_len;
  if (t->filter_len != 0) {
    meta_len = t->filter_off + t->filter_len - t->index_off;
  }
  if (source->max_metadata_bytes != 0 &&
      meta_len > source->max_metadata_bytes) {
    st = DS_ERR_OVERFLOW;
    goto fail;
  }
  r = (struct ds_sstable_reader *)ds_context_calloc(context, 1, sizeof(*r));
  if (r == NULL) {
    st = DS_ERR_ALLOC;
    goto fail;
  }
  t->reader = r;
  r->context = context != NULL ? context : ds_default_context();
  r->source = *source;
  r->data_end = t->index_off;
  r->metadata_len = meta_len;
  r->lookup.context = context;
  r->metadata = (unsigned char *)ds_context_alloc(context, meta_len);
  if (r->metadata == NULL) {
    st = DS_ERR_ALLOC;
    goto fail;
  }
  st = sst_source_read(source, t->index_off, r->metadata, meta_len);
  if (st == DS_OK) {
    st = sst_metadata(t);
  }
  if (st == DS_OK) {
    st = sst_load_filter(t, filter, context);
  }
  if (st == DS_OK) {
    return DS_OK;
  }
fail:
  ds_sstable_close(t);
  return st;
}

void ds_sstable_close(ds_sstable_t *t) {
  if (t == NULL) {
    return;
  }
  ds_amq_destroy(&t->amq);
  if (t->reader != NULL) {
    struct ds_sstable_reader *r = t->reader;
    ds_sstable_iter_destroy(&r->lookup);
    ds_context_free(r->context, r->metadata);
    ds_context_free(r->context, r);
  }
  memset(t, 0, sizeof(*t));
}

ds_status_t ds_sstable_set_key_hash(ds_sstable_t *t,
                                    const ds_sstable_key_hash_t *hash) {
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (hash == NULL || hash->hash == NULL) {
    t->key_hash.hash = NULL;
    t->key_hash.user = NULL;
  } else {
    t->key_hash = *hash;
  }
  return DS_OK;
}

/* the filter can be trusted: bytewise keys, or a keyed filter with its
 * key hash attached */
static bool sst_filter_usable(const ds_sstable_t *t) {
  return t->cmp.id == 0 || (t->keyed && t->key_hash.hash != NULL);
}

bool ds_sstable_has_filter(const ds_sstable_t *t) {
  if (!sst_is_open(t) || !sst_filter_usable(t)) {
    return false;
  }
  return t->filter_kind == DS_SSTABLE_FILTER_BLOOM ||
         (t->filter_kind == DS_SSTABLE_FILTER_AMQ && t->amq.ops != NULL);
}

ds_status_t ds_sstable_open(ds_sstable_t *t, const void *data, size_t len) {
  return ds_sstable_open_cmp(t, data, len, NULL);
}

size_t ds_sstable_entries(const ds_sstable_t *t) {
  return t == NULL ? 0 : t->entries;
}

size_t ds_sstable_blocks(const ds_sstable_t *t) {
  size_t n;
  size_t e;
  ds_sstable_t v;
  if (!sst_is_open(t)) {
    return 0;
  }
  sst_index_view(t, &v);
  if (!sst_block_restarts(&v, v.index_off, v.index_len, v.len, &n, &e)) {
    return 0;
  }
  return n;
}

bool ds_sstable_may_contain(const ds_sstable_t *t, const void *key,
                            size_t key_len) {
  const unsigned char *f;
  size_t k;
  size_t bits;
  unsigned long h;
  unsigned long delta;
  size_t j;
  if (!sst_is_open(t) || !sst_filter_usable(t)) {
    return true;
  }
  if (t->filter_kind == DS_SSTABLE_FILTER_AMQ) {
    return t->amq.ops == NULL ||
           ds_amq_contains(&t->amq,
                           sst_hash_for(true, t->keyed ? &t->key_hash : NULL,
                                        (const unsigned char *)key, key_len));
  }
  if (t->filter_kind != DS_SSTABLE_FILTER_BLOOM || t->filter_len < 2) {
    return true;
  }
  f = sst_filter_bytes(t);
  k = f[0];
  bits = (t->filter_len - 1) * 8;
  h = sst_hash_for(false, t->keyed ? &t->key_hash : NULL,
                   (const unsigned char *)key, key_len);
  delta = ((h >> 17) | (h << 15)) & 0xFFFFFFFFUL;
  for (j = 0; j < k; j++) {
    size_t bit = (size_t)(h % (unsigned long)bits);
    if ((f[1 + bit / 8] & (1U << (bit % 8))) == 0) {
      return false;
    }
    h = (h + delta) & 0xFFFFFFFFUL;
  }
  return true;
}

/* one entry at *pos (< end): shared, the unshared bytes, the value */
typedef struct sst_entry {
  size_t shared;
  const unsigned char *rest;
  size_t rest_len;
  const unsigned char *value;
  size_t value_len;
  bool tombstone;
} sst_entry_t;

static bool sst_entry(const ds_sstable_t *t, size_t end, size_t *pos,
                      sst_entry_t *e) {
  unsigned long sh;
  unsigned long un;
  unsigned long vf;
  if (!sst_get_varint(t->data, end, pos, &sh) ||
      !sst_get_varint(t->data, end, pos, &un) ||
      !sst_get_varint(t->data, end, pos, &vf)) {
    return false;
  }
  if (un > (unsigned long)(end - *pos) ||
      (vf >> 1) > (unsigned long)(end - *pos - (size_t)un)) {
    return false;
  }
  e->shared = (size_t)sh;
  e->rest = t->data + *pos;
  e->rest_len = (size_t)un;
  e->value = t->data + *pos + (size_t)un;
  e->value_len = (size_t)(vf >> 1);
  e->tombstone = (vf & 1UL) != 0;
  *pos += (size_t)un + (size_t)(vf >> 1);
  return true;
}

/* index of the first restart whose (full) key is >= key, in a block whose
 * restarts start at r; returns count when all keys are smaller */
static size_t sst_restart_lower(const ds_sstable_t *t, size_t r, size_t count,
                                size_t block_off, size_t entries_end,
                                const unsigned char *key, size_t key_len,
                                bool *bad) {
  size_t lo = 0;
  size_t hi = count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    size_t pos;
    sst_entry_t e;
    if (!sst_at(block_off, sst_get_u32(t->data + r + 4 * mid), entries_end,
                &pos) ||
        !sst_entry(t, entries_end, &pos, &e) || e.shared != 0) {
      *bad = true;
      return count;
    }
    if (sst_cmp(&t->cmp, e.rest, e.rest_len, key, key_len) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

/* the data block of index entry number i, given the index's restart array
 * at r and its entries end: lies in [0, index_off), at least 4 bytes */
static bool sst_index_block(const ds_sstable_t *t, size_t r, size_t end,
                            size_t i, size_t *out_off, size_t *out_size,
                            sst_entry_t *out_entry) {
  size_t pos;
  size_t vp;
  size_t vend;
  sst_entry_t e;
  unsigned long off;
  unsigned long size;
  if (!sst_at(t->index_off, sst_get_u32(t->data + r + 4 * i), end, &pos) ||
      !sst_entry(t, end, &pos, &e) || e.shared != 0) {
    return false;
  }
  vp = (size_t)(e.value - t->data);
  vend = vp + e.value_len;
  if (!sst_get_varint(t->data, vend, &vp, &off) ||
      !sst_get_varint(t->data, vend, &vp, &size) ||
      !sst_range((size_t)off, (size_t)size, sst_data_end(t)) || size < 4) {
    return false;
  }
  *out_off = (size_t)off;
  *out_size = (size_t)size;
  if (out_entry != NULL) {
    *out_entry = e;
  }
  return true;
}

/* the data block (offset, size) that may hold key: DS_NOT_FOUND when key is
 * past the last block */
static ds_status_t sst_find_block(const ds_sstable_t *t,
                                  const unsigned char *key, size_t key_len,
                                  size_t *out_index, size_t *out_off,
                                  size_t *out_size) {
  size_t n;
  size_t end;
  size_t i;
  bool bad = false;
  ds_sstable_t v;
  sst_index_view(t, &v);
  t = &v;
  if (!sst_block_restarts(t, t->index_off, t->index_len, t->len, &n, &end)) {
    return DS_ERR_INVALID;
  }
  i = sst_restart_lower(t, end, n, t->index_off, end, key, key_len, &bad);
  if (bad) {
    return DS_ERR_INVALID;
  }
  if (i == n) {
    return DS_NOT_FOUND;
  }
  if (!sst_index_block(t, end, end, i, out_off, out_size, NULL)) {
    return DS_ERR_INVALID;
  }
  *out_index = i;
  return DS_OK;
}

static ds_status_t sst_iter_seek(ds_sstable_iter_t *it, const void *key,
                                 size_t key_len);

ds_status_t ds_sstable_get(const ds_sstable_t *t, const void *key,
                           size_t key_len, const unsigned char **out_value,
                           size_t *out_value_len, bool *out_tombstone,
                           ds_context_t *context) {
  ds_sstable_iter_t local;
  ds_sstable_iter_t *it;
  ds_status_t st;
  ds_stage_t arg;
  if (!sst_is_open(t) || (key == NULL && key_len > 0)) {
    return DS_ERR_NULL;
  }
  if (!ds_sstable_may_contain(t, key, key_len)) {
    return DS_NOT_FOUND;
  }
  if (t->reader != NULL) {
    it = &t->reader->lookup;
    it->table = t; /* an owning handle may have moved since the last get */
  } else {
    it = &local;
    st = ds_sstable_iter_init(it, t, context);
    if (st != DS_OK) {
      return st;
    }
  }
  st = ds_stage_copy(&arg, it->context, true, key, key_len, NULL, 0);
  if (st != DS_OK) {
    if (t->reader == NULL) {
      ds_sstable_iter_destroy(it);
    }
    return st;
  }
  key = arg.key;
  st = sst_iter_seek(it, key, key_len);
  if (st == DS_OK) {
    if (!it->valid || sst_cmp(&t->cmp, it->key, it->key_len,
                              (const unsigned char *)key, key_len) != 0) {
      st = DS_NOT_FOUND;
    } else {
      if (out_value != NULL) {
        *out_value = it->value;
      }
      if (out_value_len != NULL) {
        *out_value_len = it->value_len;
      }
      if (out_tombstone != NULL) {
        *out_tombstone = it->tombstone;
      }
    }
  }
  if (t->reader == NULL) {
    ds_sstable_iter_destroy(it);
  }
  ds_stage_release(&arg);
  return st;
}

/* ---- iterator
 * ---------------------------------------------------------------
 */

ds_status_t ds_sstable_iter_init(ds_sstable_iter_t *it, const ds_sstable_t *t,
                                 ds_context_t *context) {
  if (it == NULL || t == NULL) {
    return DS_ERR_NULL;
  }
  memset(it, 0, sizeof(*it));
  it->table = t;
  it->context = context != NULL ? context : ds_default_context();
  return DS_OK;
}

void ds_sstable_iter_destroy(ds_sstable_iter_t *it) {
  if (it == NULL) {
    return;
  }
  ds_context_free(it->context, it->key);
  it->key = NULL;
  it->key_cap = 0;
  ds_context_free(it->context, it->block_data);
  it->block_data = NULL;
  it->block_cap = 0;
  it->valid = false;
}

bool ds_sstable_iter_valid(const ds_sstable_iter_t *it) {
  return it != NULL && it->valid;
}

static void sst_iter_view(const ds_sstable_iter_t *it, ds_sstable_t *v) {
  *v = *it->table;
  if (v->reader != NULL) {
    v->data = it->block_data;
    v->len = it->block_cap;
    v->index_off = it->block_cap;
    v->reader = NULL;
  }
}

/* Read a block and return a local parser view. On failure no current entry. */
static ds_status_t sst_iter_read(ds_sstable_iter_t *it, size_t off,
                                 size_t size, ds_sstable_t *v,
                                 size_t *local_off) {
  const ds_sstable_t *t = it->table;
  it->valid = false;
  if (t->reader != NULL) {
    ds_status_t st;
    if (t->reader->source.max_block_bytes != 0 &&
        size > t->reader->source.max_block_bytes) {
      return DS_ERR_OVERFLOW;
    }
    if (size > it->block_cap) {
      void *p = ds_context_realloc(it->context, it->block_data, size);
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      it->block_data = (unsigned char *)p;
      it->block_cap = size;
    }
    st = sst_source_read(&t->reader->source, off, it->block_data, size);
    if (st != DS_OK) {
      return st;
    }
    *local_off = 0;
  } else {
    *local_off = off;
  }
  sst_iter_view(it, v);
  return DS_OK;
}

/* decode the entry at it->pos into the iterator */
static ds_status_t sst_iter_load(ds_sstable_iter_t *it) {
  sst_entry_t e;
  size_t pos = it->pos;
  size_t need;
  ds_sstable_t view;
  const ds_sstable_t *t = &view;
  sst_iter_view(it, &view);
  if (!sst_entry(t, it->block_len, &pos, &e) || e.shared > it->key_len ||
      !ds_size_add(e.shared, e.rest_len, &need) ||
      !ds_size_add(need, 1, &need)) {
    it->valid = false;
    return DS_ERR_INVALID;
  }
  if (need > it->key_cap) {
    size_t nc = need <= ((size_t)-1) / 2 ? need * 2 : need;
    void *q = ds_context_realloc(it->context, it->key, nc);
    if (q == NULL) {
      it->valid = false;
      return DS_ERR_ALLOC;
    }
    it->key = (unsigned char *)q;
    it->key_cap = nc;
  }
  if (e.rest_len > 0) {
    memcpy(it->key + e.shared, e.rest, e.rest_len);
  }
  it->key_len = e.shared + e.rest_len;
  it->value = e.value;
  it->value_len = e.value_len;
  it->tombstone = e.tombstone;
  it->pos = pos;
  it->valid = true;
  return DS_OK;
}

/* position at the first entry of data block b or a later one (a loop, so
 * that runs of empty blocks cost no stack) */
static ds_status_t sst_iter_block(ds_sstable_iter_t *it, size_t b) {
  ds_sstable_t index;
  const ds_sstable_t *t = &index;
  size_t n, end;
  sst_index_view(it->table, &index);
  if (!sst_block_restarts(t, t->index_off, t->index_len, t->len, &n, &end)) {
    it->valid = false;
    return DS_ERR_INVALID;
  }
  for (; b < n; b++) {
    size_t off, size, rc, ee, local;
    ds_sstable_t block;
    ds_status_t st;
    if (!sst_index_block(t, end, end, b, &off, &size, NULL)) {
      it->valid = false;
      return DS_ERR_INVALID;
    }
    st = sst_iter_read(it, off, size, &block, &local);
    if (st != DS_OK) {
      return st;
    }
    if (!sst_block_restarts(&block, local, size, block.index_off, &rc, &ee)) {
      return DS_ERR_INVALID;
    }
    it->block = b;
    it->block_off = off;
    it->block_len = ee;
    it->pos = local;
    it->key_len = 0;
    if (it->pos < ee) {
      return sst_iter_load(it);
    }
  }
  it->valid = false;
  return DS_OK;
}

ds_status_t ds_sstable_iter_first(ds_sstable_iter_t *it) {
  if (it == NULL) {
    return DS_ERR_NULL;
  }
  it->valid = false;
  if (!sst_is_open(it->table)) {
    return DS_ERR_STATE;
  }
  return sst_iter_block(it, 0);
}

ds_status_t ds_sstable_iter_next(ds_sstable_iter_t *it) {
  if (it == NULL) {
    return DS_ERR_NULL;
  }
  if (!it->valid) {
    return DS_OK;
  }
  if (it->pos >= it->block_len) {
    return sst_iter_block(it, it->block + 1);
  }
  return sst_iter_load(it);
}

static ds_status_t sst_iter_seek(ds_sstable_iter_t *it, const void *key,
                                 size_t key_len) {
  const ds_sstable_t *t;
  ds_sstable_t view;
  size_t local;
  size_t b;
  size_t off;
  size_t size;
  size_t rc;
  size_t ee;
  size_t i;
  bool bad = false;
  ds_status_t st;
  if (it == NULL || (key == NULL && key_len > 0)) {
    return DS_ERR_NULL;
  }
  t = it->table;
  it->valid = false;
  if (!sst_is_open(t)) {
    return DS_ERR_STATE;
  }
  st = sst_find_block(t, (const unsigned char *)key, key_len, &b, &off, &size);
  if (st == DS_NOT_FOUND) {
    return DS_OK; /* past the end */
  }
  if (st != DS_OK) {
    return st;
  }
  st = sst_iter_read(it, off, size, &view, &local);
  if (st != DS_OK) {
    return st;
  }
  t = &view;
  if (!sst_block_restarts(t, local, size, t->index_off, &rc, &ee)) {
    return DS_ERR_INVALID;
  }
  /* the last restart below key, then scan */
  i = sst_restart_lower(t, ee, rc, local, ee, (const unsigned char *)key,
                        key_len, &bad);
  if (bad) {
    return DS_ERR_INVALID;
  }
  it->block = b;
  it->block_off = off;
  it->block_len = ee;
  it->key_len = 0;
  if (i == 0) {
    it->pos = local;
  } else if (!sst_at(local, sst_get_u32(t->data + ee + 4 * (i - 1)), ee,
                     &it->pos)) {
    return DS_ERR_INVALID;
  }
  if (it->pos >= ee) {
    return sst_iter_block(it, b + 1);
  }
  st = sst_iter_load(it);
  while (st == DS_OK && it->valid &&
         sst_cmp(&t->cmp, it->key, it->key_len, (const unsigned char *)key,
                 key_len) < 0) {
    st = ds_sstable_iter_next(it);
  }
  return st;
}

ds_status_t ds_sstable_iter_seek(ds_sstable_iter_t *it, const void *key,
                                 size_t key_len) {
  ds_stage_t arg;
  ds_status_t st;
  if (it == NULL || (key == NULL && key_len != 0)) {
    return DS_ERR_NULL;
  }
  st = ds_stage_copy(&arg, it->context, true, key, key_len, NULL, 0);
  if (st != DS_OK) {
    it->valid = false;
    return st;
  }
  st = sst_iter_seek(it, arg.key, key_len);
  ds_stage_release(&arg);
  return st;
}

/* ---- verification
 * -----------------------------------------------------------
 */

typedef struct sst_keybuf {
  unsigned char *p;
  size_t len;
  size_t cap;
} sst_keybuf_t;

/* k = prefix[0 .. shared) + rest; a NULL prefix keeps k's own first
 * shared bytes (safe across the reallocation) */
static ds_status_t sst_keybuf_set(ds_context_t *ctx, sst_keybuf_t *k,
                                  const unsigned char *prefix, size_t shared,
                                  const unsigned char *rest, size_t rest_len) {
  size_t need;
  if (!ds_size_add(shared, rest_len, &need) || !ds_size_add(need, 1, &need)) {
    return DS_ERR_INVALID;
  }
  if (need > k->cap) {
    void *q = ds_context_realloc(ctx, k->p, need);
    if (q == NULL) {
      return DS_ERR_ALLOC;
    }
    k->p = (unsigned char *)q;
    k->cap = need;
  }
  if (prefix != NULL && shared > 0) {
    memcpy(k->p, prefix, shared);
  }
  if (rest_len > 0) {
    memcpy(k->p + shared, rest, rest_len);
  }
  k->len = shared + rest_len;
  return DS_OK;
}

/* walk one data block: restart points, prefix sharing, strict key order
 * (continuing from *prev), entry count */
static ds_status_t sst_verify_block(const ds_sstable_t *t, size_t off,
                                    size_t size, bool check_filter,
                                    const ds_sstable_t *filter_table,
                                    sst_keybuf_t *prev, sst_keybuf_t *cur,
                                    bool *have_prev, size_t *count,
                                    ds_context_t *ctx) {
  size_t rc;
  size_t ee;
  size_t pos;
  size_t ridx = 0;
  ds_status_t st;
  if (!sst_block_restarts(t, off, size, t->index_off, &rc, &ee) || ee <= off ||
      rc == 0) {
    return DS_ERR_INVALID; /* the builder writes no empty blocks */
  }
  cur->len = 0;
  for (pos = off; pos < ee;) {
    sst_entry_t e;
    size_t at = pos - off;
    bool restart =
        ridx < rc && sst_get_u32(t->data + ee + 4 * ridx) == (unsigned long)at;
    if (!sst_entry(t, ee, &pos, &e)) {
      return DS_ERR_INVALID;
    }
    if ((at == 0 && !restart) || (restart && e.shared != 0) ||
        e.shared > cur->len) {
      return DS_ERR_INVALID;
    }
    if (restart) {
      ridx++;
    }
    /* cur still holds the previous key of this block: build the new one */
    st = sst_keybuf_set(ctx, cur, NULL, e.shared, e.rest, e.rest_len);
    if (st != DS_OK) {
      return st;
    }
    if (*have_prev &&
        sst_cmp(&t->cmp, prev->p, prev->len, cur->p, cur->len) >= 0) {
      return DS_ERR_INVALID;
    }
    if (check_filter &&
        !ds_sstable_may_contain(filter_table, cur->p, cur->len)) {
      return DS_ERR_INVALID;
    }
    st = sst_keybuf_set(ctx, prev, cur->p, cur->len, NULL, 0);
    if (st != DS_OK) {
      return st;
    }
    *have_prev = true;
    (*count)++;
  }
  /* every restart offset must have named an entry start, in order */
  return ridx == rc ? DS_OK : DS_ERR_INVALID;
}

static size_t sst_footer_len(const ds_sstable_t *t) {
  return t->version == 1   ? SST_FOOTER_V1
         : t->version == 2 ? SST_FOOTER_V2
                           : SST_FOOTER_V3;
}

ds_status_t ds_sstable_verify(const ds_sstable_t *table, bool check_filter,
                              ds_context_t *context) {
  ds_sstable_t tv,
      index; /* the table, with its AMQ filter loaded if need be */
  const ds_sstable_t *t = &tv;
  bool loaded = false;
  ds_sstable_iter_t reader;
  size_t n;
  size_t end;
  size_t i;
  size_t ipos;
  size_t expect = 0;
  size_t count = 0;
  bool have_prev = false;
  sst_keybuf_t prev;
  sst_keybuf_t cur;
  ds_status_t st = DS_OK;
  if (!sst_is_open(table)) {
    return DS_ERR_NULL;
  }
  tv = *table;
  sst_index_view(t, &index);
  if (!sst_block_restarts(&index, index.index_off, index.index_len, index.len,
                          &n, &end) ||
      t->len < sst_footer_len(t)) {
    return DS_ERR_INVALID;
  }
  /* the builder writes the filter right after the index and the footer
   * right after the filter */
  if (t->filter_off != t->index_off + t->index_len ||
      t->filter_off + t->filter_len != t->len - sst_footer_len(t)) {
    return DS_ERR_INVALID;
  }
  /* an AMQ filter is checked by loading it, where this platform can */
  if (t->filter_kind == DS_SSTABLE_FILTER_AMQ && t->amq.ops == NULL) {
    st = ds_amq_load(&tv.amq, sst_filter_bytes(t), t->filter_len, context);
    if (st == DS_OK) {
      loaded = true;
    } else if (st == DS_ERR_UNSUPPORTED) {
      st = DS_OK;
    } else {
      return st;
    }
  }
  memset(&prev, 0, sizeof(prev));
  memset(&cur, 0, sizeof(cur));
  (void)ds_sstable_iter_init(&reader, t, context);
  ipos = index.index_off;
  for (i = 0; i < n && st == DS_OK; i++) {
    size_t off;
    size_t size;
    sst_entry_t e;
    size_t epos = ipos;
    /* index restarts name the index entries in order, and the entries
     * tile the index */
    if (sst_get_u32(index.data + end + 4 * i) !=
            (unsigned long)(ipos - index.index_off) ||
        !sst_index_block(&index, end, end, i, &off, &size, &e) ||
        !sst_entry(&index, end, &epos, &e)) {
      st = DS_ERR_INVALID;
      break;
    }
    {
      size_t vp = (size_t)(e.value - index.data);
      size_t vend = vp + e.value_len;
      unsigned long a;
      unsigned long z;
      if (!sst_get_varint(index.data, vend, &vp, &a) ||
          !sst_get_varint(index.data, vend, &vp, &z) || vp != vend) {
        st = DS_ERR_INVALID;
        break;
      }
    }
    ipos = epos;
    /* data blocks tile [0, index_off) in index order */
    if (off != expect) {
      st = DS_ERR_INVALID;
      break;
    }
    expect = off + size;
    {
      ds_sstable_t block;
      size_t local;
      st = sst_iter_read(&reader, off, size, &block, &local);
      if (st == DS_OK) {
        st = sst_verify_block(&block, local, size, check_filter, t, &prev,
                              &cur, &have_prev, &count, context);
      }
    }
    /* the index key is the block's last key */
    if (st == DS_OK &&
        (e.rest_len != prev.len ||
         (e.rest_len > 0 && memcmp(e.rest, prev.p, e.rest_len) != 0))) {
      st = DS_ERR_INVALID;
    }
  }
  if (st == DS_OK &&
      (ipos != end || expect != t->index_off || count != t->entries)) {
    st = DS_ERR_INVALID;
  }
  ds_sstable_iter_destroy(&reader);
  ds_context_free(context, prev.p);
  ds_context_free(context, cur.p);
  if (loaded) {
    ds_amq_destroy(&tv.amq);
  }
  return st;
}

/* ---- merge
 * ------------------------------------------------------------------
 */

ds_status_t ds_sstable_merge(const ds_sstable_t *tables, size_t n,
                             bool drop_tombstones, size_t block_size,
                             size_t restart_interval, size_t bits_per_key,
                             unsigned char **out, size_t *out_len,
                             ds_context_t *context) {
  ds_sstable_options_t o;
  ds_sstable_options_init(&o);
  o.block_size = block_size;
  o.restart_interval = restart_interval;
  o.bits_per_key = bits_per_key;
  return ds_sstable_merge_opts(tables, n, drop_tombstones, &o, out, out_len,
                               context);
}

static bool sst_same_cmp(const ds_sstable_comparator_t *a,
                         const ds_sstable_comparator_t *b) {
  return a->id == b->id && a->compare == b->compare;
}

ds_status_t ds_sstable_merge_opts(const ds_sstable_t *tables, size_t n,
                                  bool drop_tombstones,
                                  const ds_sstable_options_t *options,
                                  unsigned char **out, size_t *out_len,
                                  ds_context_t *context) {
  ds_sstable_iter_t *its;
  ds_sstable_builder_t b;
  ds_sstable_options_t o;
  const ds_sstable_comparator_t *cmp = &sst_bytewise;
  size_t i;
  ds_status_t st;
  if ((n > 0 && tables == NULL) || out == NULL || out_len == NULL) {
    return DS_ERR_NULL;
  }
  if (options == NULL) {
    ds_sstable_options_init(&o);
  } else {
    o = *options;
  }
  if (n > 0) {
    cmp = &tables[0].cmp;
  } else if (o.comparator != NULL) {
    cmp = o.comparator;
  }
  for (i = 0; i < n; i++) {
    if (!sst_is_open(&tables[i])) {
      return DS_ERR_STATE;
    }
    if (!sst_same_cmp(&tables[i].cmp, cmp)) {
      return DS_ERR_INVALID;
    }
  }
  if (o.comparator != NULL && !sst_same_cmp(o.comparator, cmp)) {
    return DS_ERR_INVALID;
  }
  o.comparator = cmp;
  st = ds_sstable_builder_init_opts(&b, &o, context);
  if (st != DS_OK) {
    return st;
  }
  its = (ds_sstable_iter_t *)ds_context_calloc(context, n + 1,
                                               sizeof(ds_sstable_iter_t));
  if (its == NULL) {
    ds_sstable_builder_destroy(&b);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n && st == DS_OK; i++) {
    st = ds_sstable_iter_init(&its[i], &tables[i], context);
    if (st == DS_OK) {
      st = ds_sstable_iter_first(&its[i]);
    }
  }
  while (st == DS_OK) {
    size_t best = n;
    for (i = 0; i < n; i++) {
      if (its[i].valid &&
          (best == n || sst_cmp(cmp, its[i].key, its[i].key_len, its[best].key,
                                its[best].key_len) < 0)) {
        best = i; /* equal keys keep the newest (lowest i) */
      }
    }
    if (best == n) {
      break;
    }
    if (!(its[best].tombstone && drop_tombstones)) {
      st = its[best].tombstone
               ? ds_sstable_builder_add_tombstone(&b, its[best].key,
                                                  its[best].key_len)
               : ds_sstable_builder_add(&b, its[best].key, its[best].key_len,
                                        its[best].value, its[best].value_len);
    }
    /* advance every table past this key */
    for (i = 0; i < n && st == DS_OK; i++) {
      if (i != best && its[i].valid &&
          sst_cmp(cmp, its[i].key, its[i].key_len, its[best].key,
                  its[best].key_len) == 0) {
        st = ds_sstable_iter_next(&its[i]);
      }
    }
    if (st == DS_OK) {
      st = ds_sstable_iter_next(&its[best]);
    }
  }
  if (st == DS_OK) {
    st = ds_sstable_builder_finish(&b, out, out_len);
  }
  for (i = 0; i < n; i++) {
    ds_sstable_iter_destroy(&its[i]);
  }
  ds_context_free(context, its);
  ds_sstable_builder_destroy(&b);
  return st;
}
