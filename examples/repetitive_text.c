/*
 * Indexing a repetitive collection: forty versions of a document, each a
 * copy of the previous one with a few edits. The r-index stores data per
 * run of its Burrows-Wheeler transform, and runs barely grow with each
 * version, so it stays far smaller than an FM-index sampled for locating.
 * The suffix array comes from SA-IS, which also yields the LCP array: its
 * maximum is the longest repeated substring.
 */
#include "ds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION 4000
#define VERSIONS 40

int main(void) {
  size_t n = VERSION * VERSIONS;
  unsigned char *text = (unsigned char *)malloc(n);
  size_t *sa = (size_t *)malloc(n * sizeof(size_t));
  size_t *lcp = (size_t *)malloc(n * sizeof(size_t));
  size_t pos[8];
  size_t total = 0;
  size_t i;
  size_t best = 0;
  size_t where = 0;
  size_t runs;
  unsigned long seed = 12345;
  const unsigned char *pattern = (const unsigned char *)"GATTACA";
  ds_rindex_t ri;
  ds_fm_index_t fm;
  ds_fm_config_t config;
  if (text == NULL || sa == NULL || lcp == NULL) {
    return 1;
  }
  for (i = 0; i < VERSION; i++) { /* version 0 at random */
    seed = seed * 1103515245ul + 12345ul;
    text[i] = (unsigned char)"ACGT"[(seed >> 16) & 3];
  }
  memcpy(text + 1234, pattern, 7);
  for (i = VERSION; i < n; i++) { /* each version copies the previous one */
    seed = seed * 1103515245ul + 12345ul;
    text[i] = (seed >> 8) % 1000 == 0 ? (unsigned char)"ACGT"[(seed >> 20) & 3]
                                      : text[i - VERSION];
  }
  if (ds_rindex_build(&ri, text, n, NULL, NULL) != DS_OK) {
    return 1;
  }
  ds_fm_config_init(&config);
  if (ds_fm_index_build(&fm, text, n, &config) != DS_OK) {
    return 1;
  }
  runs = ds_rindex_runs(&ri);
  printf("%d versions, %lu bytes: %lu BWT runs (n/r = %.0f)\n", VERSIONS,
         (unsigned long)n, (unsigned long)runs, (double)n / (double)runs);
  printf("r-index %lu bytes, FM-index %lu bytes\n",
         (unsigned long)ds_rindex_bytes(&ri),
         (unsigned long)ds_fm_index_bytes(&fm));
  (void)ds_rindex_locate(&ri, pattern, 7, pos, 8, &total);
  printf("GATTACA occurs %lu times (FM-index agrees: %lu); a few:",
         (unsigned long)total,
         (unsigned long)ds_fm_index_count(&fm, pattern, 7, NULL, NULL));
  for (i = 0; i < 8 && i < total; i++) {
    printf(" %lu", (unsigned long)pos[i]);
  }
  printf("\n");
  if (ds_sa_build(text, n, sa, NULL) == DS_OK &&
      ds_sa_lcp(text, n, sa, lcp, NULL) == DS_OK) {
    for (i = 1; i < n; i++) {
      if (lcp[i] > best) {
        best = lcp[i];
        where = sa[i];
      }
    }
    printf("longest repeated substring: %lu bytes, at %lu\n",
           (unsigned long)best, (unsigned long)where);
  }
  ds_rindex_destroy(&ri);
  ds_fm_index_destroy(&fm);
  free(text);
  free(sa);
  free(lcp);
  return 0;
}
