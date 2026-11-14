/*
 * The same RDF data written twice, with other blank node labels and the
 * statements in another order: RDF Dataset Canonicalization (RDFC-1.0)
 * gives both one canonical form, so a signature over the first verifies
 * over the second. The signature covers the hash data of the W3C Data
 * Integrity cryptosuites (the proof options' canonical hash, then the
 * document's); this example signs with the default shared-key suite, and
 * a public-key suite plugs into the same callbacks. Changing one value
 * breaks the signature, and a dataset built to make canonicalization
 * explode is refused instead of hanging.
 */
#include "ds.h"
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
    "\"2026-11-14T12:00:00Z\"^^<http://www.w3.org/2001/XMLSchema#dateTime> .\n";

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

int main(void) {
  ds_rdf_dataset_t a, b, p;
  ds_rdfc_result_t ca, cb;
  ds_lds_suite_t suite;
  unsigned char data[2 * DS_DIGEST_MAX_SIZE], sig[DS_SHA256_SIZE];
  size_t len, sig_len = sizeof(sig), i;
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
  /* sign the hash data of the first copy, verify with the second */
  memset(&suite, 0, sizeof(suite));
  suite.key = (const unsigned char *)"shared secret";
  suite.key_len = 13;
  if (ds_lds_hash_data(&a, &p, NULL, NULL, data, &len) != DS_OK ||
      ds_lds_sign_data(data, len, &suite, sig, &sig_len) != DS_OK) {
    return 1;
  }
  show_hex("hash data", data, len);
  show_hex("signature", sig, sig_len);
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
           ds_lds_verify_data(data, len, &suite, sig, sig_len) == DS_OK ? "YES"
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
           st == DS_ERR_RANGE ? "refused as a poison dataset" : "canonicalized",
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
