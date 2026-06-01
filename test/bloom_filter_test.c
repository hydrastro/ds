#include "../lib/bloom_filter.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  ds_bloom_filter_t *filter = bloom_filter_create(1000, 0.01);
  ds_bloom_filter_t *clone;
  char key[32];
  int i;
  int false_positives;
  int absent_checked;

  printf("num_bits: %lu num_hashes: %lu\n", (unsigned long)filter->num_bits,
         (unsigned long)filter->num_hashes);
  assert(filter->num_hashes >= 1);

  /* insert 500 keys "item-0".."item-499" */
  for (i = 0; i < 500; ++i) {
    sprintf(key, "item-%d", i);
    bloom_filter_add(filter, key, strlen(key));
  }
  assert(bloom_filter_count(filter) == 500);

  /* no false negatives: every inserted key must be reported present */
  for (i = 0; i < 500; ++i) {
    sprintf(key, "item-%d", i);
    assert(bloom_filter_maybe_contains(filter, key, strlen(key)));
  }
  printf("no false negatives across 500 keys\n");

  /* measure false positive rate on 5000 absent keys */
  false_positives = 0;
  absent_checked = 5000;
  for (i = 0; i < absent_checked; ++i) {
    sprintf(key, "absent-%d", i);
    if (bloom_filter_maybe_contains(filter, key, strlen(key))) {
      false_positives += 1;
    }
  }
  printf("false positives: %d / %d (%.3f), estimated fpr: %.4f\n",
         false_positives, absent_checked,
         (double)false_positives / (double)absent_checked,
         bloom_filter_estimated_fpr(filter));
  /* should be well under 5% for a filter targeting 1% */
  assert((double)false_positives / (double)absent_checked < 0.05);

  clone = bloom_filter_clone(filter);
  for (i = 0; i < 500; ++i) {
    sprintf(key, "item-%d", i);
    assert(bloom_filter_maybe_contains(clone, key, strlen(key)));
  }

  bloom_filter_clear(filter);
  assert(bloom_filter_count(filter) == 0);
  sprintf(key, "item-0");
  assert(!bloom_filter_maybe_contains(filter, key, strlen(key)));

  bloom_filter_destroy(filter);
  bloom_filter_destroy(clone);
  printf("ok\n");
  return 0;
}
