/*
 * The same RDF data written twice, with other blank node labels and the
 * statements in another order: RDF Dataset Canonicalization (RDFC-1.0)
 * gives both one canonical form, so a signature over the first verifies
 * over the second. The signature covers the hash data of the W3C Data
 * Integrity cryptosuites (the proof options' canonical hash, then the
 * document's) and is made with Ed25519, as the eddsa-rdfc-2022 suite
 * does: the key is the test key of the W3C EdDSA cryptosuites
 * specification, read from its Multikey texts, and the specification's
 * own hash data gives its published proofValue. Changing one value breaks
 * the signature, and a dataset built to make canonicalization explode is
 * refused instead of hanging. (Without callbacks the suite is HMAC-SHA256
 * with a shared key.)
 */
#include <dsa.h>
#include <stdio.h>
#include <string.h>

static const char doc_a[] =
    "_:alice <http://xmlns.com/foaf/0.1/name> \"Alice\" .\n"
    "_:alice <http://xmlns.com/foaf/0.1/knows> _:bob .\n"
    "_:bob <http://xmlns.com/foaf/0.1/name> \"Bob\" .\n"
    "_:bob <http://xmlns.com/foaf/0.1/knows> _:alice .\n"
    "_:alice <http://xmlns.com/foaf/0.1/mbox> <mailto:alice@example.org> "
    "<http://example.org/contacts> .\n";

/* the same data: relabelled, reordered, one statement twice */
static const char doc_b[] =
    "_:p2 <http://xmlns.com/foaf/0.1/knows> _:p1 .\n"
    "_:p1 <http://xmlns.com/foaf/0.1/mbox> <mailto:alice@example.org> "
    "<http://example.org/contacts> .\n"
    "_:p2 <http://xmlns.com/foaf/0.1/name> \"Bob\" .\n"
    "_:p1 <http://xmlns.com/foaf/0.1/knows> _:p2 .\n"
    "_:p1 <http://xmlns.com/foaf/0.1/name> \"Alice\" .\n"
    "_:p2 <http://xmlns.com/foaf/0.1/name> \"Bob\" .\n";

static const char proof[] =
    "_:proof <http://www.w3.org/1999/02/22-rdf-syntax-ns#type> "
    "<https://w3id.org/security#DataIntegrityProof> .\n"
    "_:proof <http://purl.org/dc/terms/created> "
    "\"2026-11-14T12:00:00Z\"^^<http://www.w3.org/2001/XMLSchema#dateTime> "
    ".\n";

static int load(ds_rdf_dataset_t *d, const char *text) {
  size_t line = 0;
  ds_rdf_dataset_init(d, NULL);
  if (ds_rdf_parse_nquads(d, text, strlen(text), &line) != DS_OK) {
    printf("syntax error on line %lu\n", (unsigned long)line);
    return 0;
  }
  return 1;
}

static void show_hex(const char *what, const unsigned char *p, size_t n) {
  size_t i;
  printf("%s ", what);
  for (i = 0; i < n; i++) {
    printf("%02x", p[i]);
  }
  printf("\n");
}

/* the key pair of the W3C test vectors (Data Integrity EdDSA
 * Cryptosuites v1.0, eddsa-rdfc-2022) */
static const char w3c_public[] =
    "z6MkrJVnaZkeFzdQyMZu1cgjg7k1pZZ6pvBQ7XJPt4swbTQ2";
static const char w3c_secret[] =
    "z3u2en7t5LR2WtQH5PfFqMqwVHBeXouLzo6haApm8XHqvjxq";
/* its hash data (proof options, then credential) and proofValue */
static const char w3c_hash_data[] =
    "bea7b7acfbad0126b135104024a5f1733e705108f42d59668b05c0c50004c6b0"
    "517744132ae165a5349155bef0bb0cf2258fff99dfe1dbd914b938d775a36017";
static const char w3c_proof_value[] =
    "z2YwC8z3ap7yx1nZYCg4L3j3ApHsF8kgPdSb5xoS1VR7vPG3F561B52hYnQF9iseabecm3i"
    "jx4K1FBTQsCZahKZme";

int main(void) {
  ds_rdf_dataset_t a, b, p;
  ds_rdfc_result_t ca, cb;
  ds_lds_suite_t suite;
  ds_lds_ed25519_key_t key;
  unsigned char secret[64], public_key[DS_ED25519_PUBLIC_SIZE];
  unsigned char data[2 * DS_DIGEST_MAX_SIZE], sig[DS_ED25519_SIGNATURE_SIZE];
  char multikey[DS_LDS_ED25519_MULTIKEY_LEN + 1], proof_value[100];
  size_t len, sig_len = sizeof(sig), mb_len = sizeof(secret), i;
  int ok = 1;
  if (!load(&a, doc_a) || !load(&b, doc_b) || !load(&p, proof)) {
    return 1;
  }
  /* one canonical form */
  if (ds_rdfc_canonicalize(&a, NULL, &ca) != DS_OK ||
      ds_rdfc_canonicalize(&b, NULL, &cb) != DS_OK) {
    return 1;
  }
  printf("canonical form (%lu distinct quads):\n%s", (unsigned long)ca.quads,
         ca.nquads);
  printf("same for the relabelled copy: %s\n",
         strcmp(ca.nquads, cb.nquads) == 0 ? "yes" : "NO");
  ok &= strcmp(ca.nquads, cb.nquads) == 0;
  for (i = 0; i < cb.label_count; i++) {
    printf("  _:%s is _:%s\n", cb.labels[i].input, cb.labels[i].canonical);
  }
  /* the key: the secret Multikey is the multicodec 0x80 0x26 and the seed;
   * the public key derived from the seed gives the public Multikey */
  if (ds_lds_multibase_decode(w3c_secret, strlen(w3c_secret), secret,
                              &mb_len) != DS_OK ||
      mb_len != 34 || secret[0] != 0x80 || secret[1] != 0x26 ||
      ds_ed25519_public_key(secret + 2, public_key) != DS_OK ||
      ds_lds_ed25519_multikey(public_key, multikey) != DS_OK) {
    return 1;
  }
  printf("verification method: did:key:%s#%s\n", multikey, multikey);
  printf("the specification's public key: %s\n",
         strcmp(multikey, w3c_public) == 0 ? "yes" : "NO");
  ok &= strcmp(multikey, w3c_public) == 0;
  key.seed = secret + 2;
  key.public_key = public_key;
  ds_lds_ed25519_suite(&suite, &key);
  /* the specification's hash data signs to its proofValue */
  for (i = 0; i < 64; i++) {
    unsigned v;
    sscanf(w3c_hash_data + 2 * i, "%2x", &v);
    data[i] = (unsigned char)v;
  }
  mb_len = sizeof(proof_value) - 1;
  if (ds_lds_sign_data(data, 64, &suite, sig, &sig_len) != DS_OK ||
      ds_lds_multibase_encode(sig, sig_len, proof_value, &mb_len) != DS_OK) {
    return 1;
  }
  printf("the specification's proofValue: %s\n",
         strcmp(proof_value, w3c_proof_value) == 0 ? "yes" : "NO");
  ok &= strcmp(proof_value, w3c_proof_value) == 0;
  /* sign the hash data of the first copy, verify with the second */
  sig_len = sizeof(sig);
  mb_len = sizeof(proof_value) - 1;
  if (ds_lds_hash_data(&a, &p, NULL, NULL, data, &len) != DS_OK ||
      ds_lds_sign_data(data, len, &suite, sig, &sig_len) != DS_OK ||
      ds_lds_multibase_encode(sig, sig_len, proof_value, &mb_len) != DS_OK) {
    return 1;
  }
  show_hex("hash data", data, len);
  printf("proofValue %s\n", proof_value);
  if (ds_lds_hash_data(&b, &p, NULL, NULL, data, &len) != DS_OK) {
    return 1;
  }
  printf("verifies over the relabelled copy: %s\n",
         ds_lds_verify_data(data, len, &suite, sig, sig_len) == DS_OK ? "yes"
                                                                      : "NO");
  ok &= ds_lds_verify_data(data, len, &suite, sig, sig_len) == DS_OK;
  /* a changed value does not */
  {
    ds_rdf_dataset_t c;
    char tampered[sizeof(doc_b)];
    memcpy(tampered, doc_b, sizeof(doc_b));
    memcpy(strstr(tampered, "\"Bob\""), "\"Rob\"", 5);
    if (!load(&c, tampered) ||
        ds_lds_hash_data(&c, &p, NULL, NULL, data, &len) != DS_OK) {
      return 1;
    }
    printf("verifies after Bob became Rob: %s\n",
           ds_lds_verify_data(data, len, &suite, sig, sig_len) == DS_OK
               ? "YES"
               : "no");
    ok &= ds_lds_verify_data(data, len, &suite, sig, sig_len) != DS_OK;
    ds_rdf_dataset_destroy(&c);
  }
  /* a clique of mutually linked blank nodes: symmetric enough to make
   * Hash N-Degree Quads explode, so canonicalization stops at its limit */
  {
    char clique[4096];
    size_t n = 0, x, y;
    ds_rdfc_result_t cc;
    ds_status_t st;
    for (x = 0; x < 8; x++) {
      for (y = 0; y < 8; y++) {
        if (x != y) {
          n += (size_t)sprintf(clique + n,
                               "_:n%lu <http://example.org/p> "
                               "_:n%lu .\n",
                               (unsigned long)x, (unsigned long)y);
        }
      }
    }
    st = ds_rdfc_canonicalize_nquads(clique, n, NULL, &cc, NULL, NULL);
    printf("8-node clique: %s after %lu runs of Hash N-Degree Quads\n",
           st == DS_ERR_RANGE ? "refused as a poison dataset"
                              : "canonicalized",
           (unsigned long)cc.calls);
    ok &= st == DS_ERR_RANGE;
    ds_rdfc_result_destroy(&cc);
  }
  ds_rdfc_result_destroy(&ca);
  ds_rdfc_result_destroy(&cb);
  ds_rdf_dataset_destroy(&a);
  ds_rdf_dataset_destroy(&b);
  ds_rdf_dataset_destroy(&p);
  return ok ? 0 : 1;
}
