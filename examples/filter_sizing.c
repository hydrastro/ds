/* Measure candidate rates for one documented workload. Rates are statistical
 * estimates, not guarantees for arbitrary or adversarial keys. Candidates
 * from either structure still need verification against the source records. */
#include "ds.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool count_candidate(size_t record, void *user) {
  size_t *count = (size_t *)user;
  (void)record;
  (*count)++;
  return true;
}

int main(void) {
  double targets[3] = {0.05, 0.01, 0.001};
  size_t inserted = 2000, probes = 50000, i, j;
  char key[64];
  ds_signature_file_t signatures;
  ds_status_t st;
  size_t records = 200, queries = 1000, candidates = 0;
  double predicted = 0.0, density = 0.0;

  for (j = 0; j < 3; j++) {
    ds_bloom_filter_t *filter = ds_bloom_filter_create(inserted, targets[j]);
    size_t positives = 0;
    if (filter == NULL)
      return 1;
    for (i = 0; i < inserted; i++) {
      sprintf(key, "present:%lu", (unsigned long)i);
      ds_bloom_filter_add(filter, key, strlen(key));
    }
    for (i = 0; i < probes; i++) {
      sprintf(key, "absent:%lu", (unsigned long)i);
      if (ds_bloom_filter_maybe_contains(filter, key, strlen(key)))
        positives++;
    }
    printf("Bloom: %lu bits, %lu probes/key, target %.4f, estimate %.4f, "
           "observed %.4f (%lu/%lu absent keys)\n",
           (unsigned long)filter->num_bits, (unsigned long)filter->num_hashes,
           targets[j], ds_bloom_filter_estimated_fpr(filter),
           (double)positives / (double)probes, (unsigned long)positives,
           (unsigned long)probes);
    ds_bloom_filter_destroy(filter);
  }

  st = ds_signature_file_init(&signatures, 128, 4, NULL, NULL, NULL);
  if (st != DS_OK)
    return 1;
  for (i = 0; i < records; i++) {
    char storage[8][64];
    const unsigned char *words[8];
    size_t lengths[8];
    for (j = 0; j < 8; j++) {
      sprintf(storage[j], "record:%lu:word:%lu", (unsigned long)i,
              (unsigned long)j);
      words[j] = (const unsigned char *)storage[j];
      lengths[j] = strlen(storage[j]);
    }
    st = ds_signature_file_add(&signatures, words, lengths, 8, NULL);
    if (st != DS_OK)
      goto cleanup;
    density += ds_signature_file_density(&signatures, i);
    predicted += pow(ds_signature_file_density(&signatures, i), 4.0);
  }
  for (i = 0; i < queries; i++) {
    const unsigned char *word;
    size_t length;
    sprintf(key, "missing:%lu", (unsigned long)i);
    word = (const unsigned char *)key;
    length = strlen(key);
    st = ds_signature_file_query(&signatures, &word, &length, 1,
                                 count_candidate, &candidates);
    if (st != DS_OK)
      goto cleanup;
  }
  printf("Signatures: 128 bits/record, 8 words/record, 4 probes/word\n"
         "  mean density %.4f, independent-probe estimate %.5f, "
         "observed %.5f (%lu/%lu absent record/query pairs)\n",
         density / (double)records, predicted / (double)records,
         (double)candidates / ((double)records * (double)queries),
         (unsigned long)candidates, (unsigned long)(records * queries));
cleanup:
  ds_signature_file_destroy(&signatures);
  if (st != DS_OK)
    fprintf(stderr, "signature operation failed: %d\n", (int)st);
  return st == DS_OK ? 0 : 1;
}
