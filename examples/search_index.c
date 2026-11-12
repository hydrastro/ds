/*
 * One collection indexed at three granularities.
 *
 *   document    postings name documents with term frequencies: boolean
 *               queries and BM25 ranking
 *   positional  postings also hold every position: phrase queries
 *   block       postings name blocks of consecutive documents only: the
 *               smallest index; a query returns candidate blocks whose
 *               documents are then scanned
 *
 * Posting lists are delta + variable-byte coded in every mode; the
 * printed sizes show what each granularity costs.
 */
#include "ds.h"
#include <stdio.h>
#include <string.h>

static const char *docs[] = {
    "the quick brown fox jumps over the lazy dog",
    "a quick movement of the enemy will jeopardize six gunboats",
    "the five boxing wizards jump quickly",
    "the lazy dog sleeps while the quick fox runs",
    "pack my box with five dozen liquor jugs",
    "jumps over the lazy dog again and again"};
#define NDOCS (sizeof(docs) / sizeof(docs[0]))

static ds_invidx_term_t term(const char *s) {
  ds_invidx_term_t t;
  t.text = s;
  t.length = strlen(s);
  return t;
}

static int build(ds_inverted_index_t *idx, ds_invidx_mode_t mode) {
  size_t d;
  if (ds_invidx_init(idx, mode, 2, false, NULL) != DS_OK) {
    return 1;
  }
  for (d = 0; d < NDOCS; d++) {
    if (ds_invidx_add_text(idx, d, docs[d], strlen(docs[d]), NULL, NULL,
                           true) != DS_OK) {
      return 1;
    }
  }
  return 0;
}

static void show(const char *label, const ds_invidx_result_t *r) {
  size_t i;
  printf("  %s:", label);
  for (i = 0; i < r->count; i++) {
    if (r->scores != NULL) {
      printf(" %lu (%.2f)", (unsigned long)r->docs[i], r->scores[i]);
    } else {
      printf(" %lu", (unsigned long)r->docs[i]);
    }
  }
  printf("\n");
}

int main(void) {
  ds_inverted_index_t idx;
  ds_invidx_result_t r;
  ds_invidx_term_t q[2];

  q[0] = term("lazy");
  q[1] = term("dog");

  if (build(&idx, DS_INVIDX_DOCUMENT) != 0) {
    return 1;
  }
  printf("document index: %lu terms, %lu posting bytes\n",
         (unsigned long)ds_invidx_terms(&idx),
         (unsigned long)ds_invidx_postings_bytes(&idx));
  if (ds_invidx_boolean(&idx, q, 2, NULL, 0, NULL, 0, &r) == DS_OK) {
    show("lazy AND dog", &r);
    ds_invidx_result_free(&idx, &r);
  }
  if (ds_invidx_bm25(&idx, q, 2, 1.2, 0.75, 3, &r) == DS_OK) {
    show("BM25 top 3 for lazy dog", &r);
    ds_invidx_result_free(&idx, &r);
  }
  ds_invidx_destroy(&idx);

  if (build(&idx, DS_INVIDX_POSITIONAL) != 0) {
    return 1;
  }
  printf("positional index: %lu posting bytes\n",
         (unsigned long)ds_invidx_postings_bytes(&idx));
  q[0] = term("quick");
  q[1] = term("fox");
  if (ds_invidx_phrase(&idx, q, 2, &r) == DS_OK) {
    show("phrase \"quick fox\"", &r);
    ds_invidx_result_free(&idx, &r);
  }
  ds_invidx_destroy(&idx);

  if (build(&idx, DS_INVIDX_BLOCK) != 0) {
    return 1;
  }
  printf("block index (2 documents per block): %lu posting bytes\n",
         (unsigned long)ds_invidx_postings_bytes(&idx));
  q[0] = term("lazy");
  if (ds_invidx_boolean(&idx, q, 1, NULL, 0, NULL, 0, &r) == DS_OK) {
    show("candidate blocks for lazy", &r);
    ds_invidx_result_free(&idx, &r);
  }
  ds_invidx_destroy(&idx);
  return 0;
}
