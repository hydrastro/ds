/*
 * An inverted index whose parts are all chosen by the caller: a sorted
 * term dictionary (term walks and prefix queries), Elias gamma postings
 * with skip tables, three scorers, and a rank-pairing heap as the top-k
 * queue. Then a document is updated (removed and added under a new id)
 * and the index compacted.
 */
#include "ds.h"
#include <stdio.h>
#include <string.h>

static const char *docs[] = {
    "compact data structures answer queries in compressed space",
    "succinct trees navigate with rank and select on bit vectors",
    "compressed suffix arrays and the fm index count patterns",
    "data compression with gamma and delta codes",
    "rank select dictionaries compress sparse bit vectors",
    "trees of parentheses support navigation by excess search",
    "the compact index stores postings with gamma codes"};
#define NDOCS (sizeof(docs) / sizeof(docs[0]))

static ds_invidx_term_t term(const char *s) {
  ds_invidx_term_t t;
  t.text = s;
  t.length = strlen(s);
  return t;
}

static void show(const char *label, const ds_invidx_result_t *r) {
  size_t i;
  printf("  %-22s", label);
  for (i = 0; i < r->count; i++) {
    printf(" doc %lu (%.3f)", (unsigned long)r->docs[i], r->scores[i]);
  }
  printf("\n");
}

int main(void) {
  ds_inverted_index_t idx;
  ds_invidx_options_t o;
  ds_invidx_scorer_t scorer[3];
  ds_invidx_rank_options_t ro;
  ds_invidx_result_t r;
  ds_invidx_term_t q[2];
  ds_invidx_dict_cursor_t c;
  const char *names[3] = {"BM25", "TF-IDF (pivoted)", "Dirichlet LM"};
  size_t d;
  size_t t;
  size_t matched;
  int s;

  memset(&o, 0, sizeof(o));
  o.mode = DS_INVIDX_POSITIONAL;
  o.dictionary = ds_invidx_dict_sorted(); /* ordered: walks and prefixes */
  o.codec = ds_invidx_codec_gamma();      /* small gaps, few bits */
  o.skip_interval = 4;                    /* tiny lists, so show skips */
  if (ds_invidx_init_opts(&idx, &o, NULL) != DS_OK) {
    return 1;
  }
  for (d = 0; d < NDOCS; d++) {
    (void)ds_invidx_add_text(&idx, d, docs[d], strlen(docs[d]), NULL, NULL,
                             true);
  }
  printf("%lu documents, %lu terms, %lu bytes of gamma postings\n",
         (unsigned long)ds_invidx_documents(&idx),
         (unsigned long)ds_invidx_terms(&idx),
         (unsigned long)ds_invidx_postings_bytes(&idx));

  /* the sorted dictionary walks terms in byte order from any point */
  printf("terms from \"r\":");
  (void)ds_invidx_term_seek(&idx, "r", 1, &c);
  for (d = 0; d < 5 && (t = ds_invidx_term_next(&idx, &c)) != DS_INVIDX_NONE;
       d++) {
    const void *text;
    size_t len;
    (void)ds_invidx_term_text(&idx, t, &text, &len);
    printf(" %.*s", (int)len, (const char *)text);
  }
  printf("\n");
  if (ds_invidx_prefix(&idx, "compress", 8, &r, &matched) == DS_OK) {
    printf("prefix compress*: %lu terms, documents", (unsigned long)matched);
    for (d = 0; d < r.count; d++) {
      printf(" %lu", (unsigned long)r.docs[d]);
    }
    printf("\n");
    ds_invidx_result_free(&idx, &r);
  }

  /* one query, three scorers, a rank-pairing heap keeping the top 3 */
  (void)ds_invidx_scorer_bm25(&scorer[0], 1.2, 0.75);
  (void)ds_invidx_scorer_tfidf(&scorer[1], 0.2);
  (void)ds_invidx_scorer_dirichlet(&scorer[2], 50.0);
  q[0] = term("compact");
  q[1] = term("gamma");
  printf("query \"compact gamma\", top 3:\n");
  for (s = 0; s < 3; s++) {
    memset(&ro, 0, sizeof(ro));
    ro.scorer = &scorer[s];
    ro.top_k = 3;
    ro.queue = ds_pq_rank_pairing_ops();
    if (ds_invidx_rank(&idx, q, 2, &ro, &r) == DS_OK) {
      show(names[s], &r);
      ds_invidx_result_free(&idx, &r);
    }
  }
  ro.scorer = &scorer[0];
  ro.conjunctive = true; /* only documents with both terms */
  if (ds_invidx_rank(&idx, q, 2, &ro, &r) == DS_OK) {
    show("BM25, both terms", &r);
    ds_invidx_result_free(&idx, &r);
  }

  /* an update: document 6 is replaced by document 7 */
  (void)ds_invidx_remove(&idx, 6);
  (void)ds_invidx_add_text(&idx, 7, "a compact gamma coded index", 27, NULL,
                           NULL, true);
  printf("after replacing doc 6 by doc 7: %lu live, %lu deleted\n",
         (unsigned long)ds_invidx_documents(&idx),
         (unsigned long)ds_invidx_deleted(&idx));
  if (ds_invidx_compact(&idx) == DS_OK) {
    printf("compacted: %lu deleted, %lu bytes of postings\n",
           (unsigned long)ds_invidx_deleted(&idx),
           (unsigned long)ds_invidx_postings_bytes(&idx));
  }
  ro.conjunctive = false;
  if (ds_invidx_rank(&idx, q, 2, &ro, &r) == DS_OK) {
    show("BM25 after the update", &r);
    ds_invidx_result_free(&idx, &r);
  }
  ds_invidx_destroy(&idx);
  return 0;
}
