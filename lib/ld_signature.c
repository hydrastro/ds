#include "ld_signature.h"
#include "ed25519.h"
#include <stdlib.h>
#include <string.h>

/* ---- canonicalization: RDFC-1.0 through lib/rdf_canon */

/* the statements as a dataset, one quad each */
static ds_status_t ld_dataset(const char *const *quads, size_t n,
                              ds_rdf_dataset_t *d) {
  size_t i;
  ds_status_t st = DS_OK;
  if (n > 0 && quads == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < n && st == DS_OK; i++) {
    size_t before = d->count;
    if (quads[i] == NULL) {
      return DS_ERR_NULL;
    }
    st = ds_rdf_parse_nquads(d, quads[i], strlen(quads[i]), NULL);
    if (st == DS_OK && d->count != before + 1) {
      st = DS_ERR_INVALID; /* not exactly one statement */
    }
  }
  return st;
}

ds_status_t ds_lds_canonicalize(const char *const *quads, size_t n, char **out,
                                size_t *out_len, ds_context_t *context) {
  ds_rdf_dataset_t d;
  ds_rdfc_result_t r;
  ds_status_t st;
  if (out == NULL || out_len == NULL) {
    return DS_ERR_NULL;
  }
  *out = NULL;
  *out_len = 0;
  ds_rdf_dataset_init(&d, context);
  st = ld_dataset(quads, n, &d);
  if (st == DS_OK) {
    st = ds_rdfc_canonicalize(&d, NULL, &r);
  }
  ds_rdf_dataset_destroy(&d);
  if (st != DS_OK) {
    return st;
  }
  /* hand over the text; the rest of the result is not needed */
  *out = r.nquads;
  *out_len = r.length;
  r.nquads = NULL;
  ds_rdfc_result_destroy(&r);
  return DS_OK;
}

/* ---- hash data and suites */

static ds_status_t ld_hash_dataset(const ds_rdf_dataset_t *d,
                                   const ds_digest_ops_t *digest,
                                   const ds_rdfc_options_t *c14n,
                                   unsigned char *out) {
  ds_rdfc_result_t r;
  void *state;
  ds_status_t st = ds_rdfc_canonicalize(d, c14n, &r);
  if (st != DS_OK) {
    return st;
  }
  state = ds_context_alloc(d->context, digest->state_size + 1);
  if (state == NULL) {
    ds_rdfc_result_destroy(&r);
    return DS_ERR_ALLOC;
  }
  digest->init(state);
  digest->update(state, r.nquads, r.length);
  digest->final(state, out);
  ds_context_free(d->context, state);
  ds_rdfc_result_destroy(&r);
  return DS_OK;
}

ds_status_t ds_lds_hash_data(const ds_rdf_dataset_t *document,
                             const ds_rdf_dataset_t *proof,
                             const ds_digest_ops_t *digest,
                             const ds_rdfc_options_t *c14n, unsigned char *out,
                             size_t *out_len) {
  ds_status_t st;
  if (document == NULL || proof == NULL || out == NULL || out_len == NULL) {
    return DS_ERR_NULL;
  }
  *out_len = 0;
  if (digest == NULL) {
    digest = ds_digest_sha256();
  }
  if (digest->size == 0 || digest->size > DS_DIGEST_MAX_SIZE) {
    return DS_ERR_INVALID;
  }
  st = ld_hash_dataset(proof, digest, c14n, out);
  if (st == DS_OK) {
    st = ld_hash_dataset(document, digest, c14n, out + digest->size);
  }
  if (st == DS_OK) {
    *out_len = 2 * digest->size;
  }
  return st;
}

ds_status_t ds_lds_sign_data(const unsigned char *data, size_t len,
                             const ds_lds_suite_t *suite, unsigned char *sig,
                             size_t *sig_len) {
  if (suite == NULL || sig == NULL || sig_len == NULL ||
      (data == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  if (suite->sign != NULL) {
    return suite->sign(data, len, sig, sig_len, suite->user);
  }
  if (*sig_len < DS_SHA256_SIZE) {
    return DS_ERR_RANGE;
  }
  ds_hmac_sha256(suite->key, suite->key_len, data, len, sig);
  *sig_len = DS_SHA256_SIZE;
  return DS_OK;
}

ds_status_t ds_lds_verify_data(const unsigned char *data, size_t len,
                               const ds_lds_suite_t *suite,
                               const unsigned char *sig, size_t sig_len) {
  unsigned char mac[DS_SHA256_SIZE];
  unsigned diff = 0;
  size_t i;
  if (suite == NULL || sig == NULL || (data == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  if (suite->verify != NULL) {
    return suite->verify(data, len, sig, sig_len, suite->user) ? DS_OK
                                                               : DS_NOT_FOUND;
  }
  if (sig_len != DS_SHA256_SIZE) {
    return DS_NOT_FOUND;
  }
  ds_hmac_sha256(suite->key, suite->key_len, data, len, mac);
  for (i = 0; i < DS_SHA256_SIZE; i++) {
    diff |= (unsigned)(mac[i] ^ sig[i]);
  }
  return diff == 0 ? DS_OK : DS_NOT_FOUND;
}

/* ---- N-Quads statements with name/value options */

static int ld_cmp_str(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static ds_status_t ld_options_hash(const ds_lds_option_t *options, size_t n,
                                   unsigned char *out, ds_context_t *ctx) {
  char **l;
  size_t i;
  ds_sha256_t h;
  ds_status_t st = DS_OK;
  if (n > 0 && options == NULL) {
    return DS_ERR_NULL;
  }
  l = (char **)ds_context_calloc(ctx, n + 1, sizeof(char *));
  if (l == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n && st == DS_OK; i++) {
    size_t nl;
    size_t vl;
    if (options[i].name == NULL || options[i].value == NULL) {
      st = DS_ERR_NULL;
      break;
    }
    nl = strlen(options[i].name);
    vl = strlen(options[i].value);
    l[i] = (char *)ds_context_alloc(ctx, nl + vl + 2);
    if (l[i] == NULL) {
      st = DS_ERR_ALLOC;
      break;
    }
    memcpy(l[i], options[i].name, nl);
    l[i][nl] = ' ';
    memcpy(l[i] + nl + 1, options[i].value, vl + 1);
  }
  if (st == DS_OK) {
    qsort(l, n, sizeof(char *), ld_cmp_str);
    ds_sha256_init(&h);
    for (i = 0; i < n; i++) {
      ds_sha256_update(&h, l[i], strlen(l[i]));
      ds_sha256_update(&h, "\n", 1);
    }
    ds_sha256_final(&h, out);
  }
  for (i = 0; i < n; i++) {
    ds_context_free(ctx, l[i]);
  }
  ds_context_free(ctx, l);
  return st;
}

ds_status_t ds_lds_tbs(const char *const *quads, size_t n,
                       const ds_lds_option_t *options, size_t n_options,
                       unsigned char *out, ds_context_t *context) {
  char *c14n;
  size_t len;
  ds_status_t st;
  if (out == NULL) {
    return DS_ERR_NULL;
  }
  st = ld_options_hash(options, n_options, out, context);
  if (st != DS_OK) {
    return st;
  }
  st = ds_lds_canonicalize(quads, n, &c14n, &len, context);
  if (st != DS_OK) {
    return st;
  }
  ds_sha256(c14n, len, out + DS_SHA256_SIZE);
  ds_context_free(context, c14n);
  return DS_OK;
}

ds_status_t ds_lds_sign(const char *const *quads, size_t n,
                        const ds_lds_option_t *options, size_t n_options,
                        const ds_lds_suite_t *suite, unsigned char *sig,
                        size_t *sig_len, ds_context_t *context) {
  unsigned char tbs[2 * DS_SHA256_SIZE];
  ds_status_t st;
  if (suite == NULL || sig == NULL || sig_len == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_lds_tbs(quads, n, options, n_options, tbs, context);
  if (st != DS_OK) {
    return st;
  }
  return ds_lds_sign_data(tbs, sizeof(tbs), suite, sig, sig_len);
}

ds_status_t ds_lds_verify(const char *const *quads, size_t n,
                          const ds_lds_option_t *options, size_t n_options,
                          const ds_lds_suite_t *suite, const unsigned char *sig,
                          size_t sig_len, ds_context_t *context) {
  unsigned char tbs[2 * DS_SHA256_SIZE];
  ds_status_t st;
  if (suite == NULL || sig == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_lds_tbs(quads, n, options, n_options, tbs, context);
  if (st != DS_OK) {
    return st;
  }
  return ds_lds_verify_data(tbs, sizeof(tbs), suite, sig, sig_len);
}

/* ---- Ed25519 suite, multibase, Multikey */

static ds_status_t ld_ed25519_sign(const unsigned char *tbs, size_t len,
                                   unsigned char *sig, size_t *sig_len,
                                   void *user) {
  const ds_lds_ed25519_key_t *key = (const ds_lds_ed25519_key_t *)user;
  ds_status_t st;
  if (key == NULL || key->public_key == NULL) {
    return DS_ERR_NULL;
  }
  if (key->seed == NULL) {
    return DS_ERR_UNSUPPORTED;
  }
  if (*sig_len < DS_ED25519_SIGNATURE_SIZE) {
    *sig_len = DS_ED25519_SIGNATURE_SIZE;
    return DS_ERR_RANGE;
  }
  st = ds_ed25519_sign(key->seed, key->public_key, tbs, len, sig);
  if (st == DS_OK) {
    *sig_len = DS_ED25519_SIGNATURE_SIZE;
  }
  return st;
}

static bool ld_ed25519_verify(const unsigned char *tbs, size_t len,
                              const unsigned char *sig, size_t sig_len,
                              void *user) {
  const ds_lds_ed25519_key_t *key = (const ds_lds_ed25519_key_t *)user;
  return key != NULL && key->public_key != NULL &&
         sig_len == DS_ED25519_SIGNATURE_SIZE &&
         ds_ed25519_verify(key->public_key, tbs, len, sig) == DS_OK;
}

void ds_lds_ed25519_suite(ds_lds_suite_t *suite, ds_lds_ed25519_key_t *key) {
  if (suite == NULL) {
    return;
  }
  memset(suite, 0, sizeof(*suite));
  suite->sign = ld_ed25519_sign;
  suite->verify = ld_ed25519_verify;
  suite->user = key;
}

static const char ld_b58[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

ds_status_t ds_lds_multibase_encode(const unsigned char *data, size_t len,
                                    char *out, size_t *out_len) {
  size_t zeros = 0;
  size_t room;
  size_t digits = 0; /* base58 digits so far, least significant first */
  size_t i;
  size_t j;
  if (out == NULL || out_len == NULL || (data == NULL && len > 0)) {
    return DS_ERR_NULL;
  }
  if (len > ((size_t)-1 - 3) / 138) {
    return DS_ERR_OVERFLOW;
  }
  while (zeros < len && data[zeros] == 0) {
    zeros++;
  }
  /* digits of the rest: at most len * log(256) / log(58) + 1 */
  room = 1 + zeros + (len - zeros) * 138 / 100 + 1;
  if (*out_len < room) {
    *out_len = room;
    return DS_ERR_RANGE;
  }
  /* the digits accumulate at the end of out, least significant last */
  for (i = zeros; i < len; i++) {
    unsigned long carry = data[i];
    for (j = 0; j < digits; j++) {
      unsigned char *d = (unsigned char *)out + *out_len - 1 - j;
      carry += (unsigned long)*d << 8;
      *d = (unsigned char)(carry % 58);
      carry /= 58;
    }
    while (carry > 0) {
      out[*out_len - 1 - digits] = (char)(unsigned char)(carry % 58);
      digits++;
      carry /= 58;
    }
  }
  out[0] = 'z';
  for (i = 0; i < zeros; i++) {
    out[1 + i] = '1';
  }
  for (i = 0; i < digits; i++) {
    unsigned char d = (unsigned char)out[*out_len - digits + i];
    out[1 + zeros + i] = ld_b58[d];
  }
  *out_len = 1 + zeros + digits;
  out[*out_len] = '\0';
  return DS_OK;
}

ds_status_t ds_lds_multibase_decode(const char *text, size_t len,
                                    unsigned char *out, size_t *out_len) {
  size_t zeros = 0;
  size_t bytes = 0; /* bytes so far, least significant at the end */
  size_t room;
  size_t i;
  size_t j;
  if (text == NULL || out_len == NULL || (out == NULL && *out_len > 0)) {
    return DS_ERR_NULL;
  }
  if (len == 0 || text[0] != 'z') {
    return DS_ERR_INVALID;
  }
  while (1 + zeros < len && text[1 + zeros] == '1') {
    zeros++;
  }
  /* bytes of the rest: at most digits * log(58) / log(256) + 1 */
  room = zeros + (len - 1 - zeros) * 733 / 1000 + 1;
  for (i = 1 + zeros; i < len; i++) {
    const char *p = text[i] != '\0' ? strchr(ld_b58, text[i]) : NULL;
    unsigned long carry;
    if (p == NULL) {
      return DS_ERR_INVALID;
    }
    carry = (unsigned long)(p - ld_b58);
    if (*out_len < room) {
      continue; /* still validate, then report the room */
    }
    for (j = 0; j < bytes; j++) {
      unsigned char *b = out + *out_len - 1 - j;
      carry += (unsigned long)*b * 58UL;
      *b = (unsigned char)(carry & 0xFFUL);
      carry >>= 8;
    }
    while (carry > 0) {
      out[*out_len - 1 - bytes] = (unsigned char)(carry & 0xFFUL);
      bytes++;
      carry >>= 8;
    }
  }
  if (*out_len < room) {
    *out_len = room;
    return DS_ERR_RANGE;
  }
  memmove(out + zeros, out + *out_len - bytes, bytes);
  memset(out, 0, zeros);
  *out_len = zeros + bytes;
  return DS_OK;
}

ds_status_t ds_lds_ed25519_multikey(const unsigned char *public_key,
                                    char *out) {
  unsigned char buf[2 + DS_ED25519_PUBLIC_SIZE];
  char text[2 + (2 + DS_ED25519_PUBLIC_SIZE) * 138 / 100 + 2];
  size_t n = sizeof(text) - 1;
  ds_status_t st;
  if (public_key == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  buf[0] = 0xed; /* the multicodec for Ed25519 public keys, as a varint */
  buf[1] = 0x01;
  memcpy(buf + 2, public_key, DS_ED25519_PUBLIC_SIZE);
  st = ds_lds_multibase_encode(buf, sizeof(buf), text, &n);
  if (st != DS_OK) {
    return st;
  }
  if (n != DS_LDS_ED25519_MULTIKEY_LEN) {
    return DS_ERR_STATE; /* cannot happen: the prefix fixes the length */
  }
  memcpy(out, text, n + 1);
  return DS_OK;
}

ds_status_t ds_lds_ed25519_from_multikey(const char *text, size_t len,
                                         unsigned char *public_key) {
  unsigned char buf[64];
  size_t n = sizeof(buf);
  ds_status_t st;
  if (text == NULL || public_key == NULL) {
    return DS_ERR_NULL;
  }
  if (len != DS_LDS_ED25519_MULTIKEY_LEN) {
    return DS_ERR_INVALID;
  }
  st = ds_lds_multibase_decode(text, len, buf, &n);
  if (st != DS_OK) {
    return st == DS_ERR_RANGE ? DS_ERR_INVALID : st;
  }
  if (n != 2 + DS_ED25519_PUBLIC_SIZE || buf[0] != 0xed || buf[1] != 0x01) {
    return DS_ERR_INVALID;
  }
  memcpy(public_key, buf + 2, DS_ED25519_PUBLIC_SIZE);
  return DS_OK;
}
