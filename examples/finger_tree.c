#include "ds.h"
#include <stdio.h>

/* One finger tree, two views: the measure (count, max) makes it both an
 * indexed sequence (split by count) and a priority queue (look up the
 * first element reaching the maximum). */

typedef struct {
  long count;
  long max;
} measure_t;

static void measure(void *out, const void *elem, void *user) {
  measure_t *m = (measure_t *)out;
  (void)user;
  m->count = 1;
  m->max = *(const long *)elem;
}

static void combine(void *out, const void *a, const void *b, void *user) {
  const measure_t *x = (const measure_t *)a;
  const measure_t *y = (const measure_t *)b;
  measure_t *m = (measure_t *)out;
  (void)user;
  m->count = x->count + y->count;
  m->max = x->max > y->max ? x->max : y->max;
}

static bool count_above(const void *m, void *user) {
  return ((const measure_t *)m)->count > *(const long *)user;
}

static bool reaches(const void *m, void *user) {
  return ((const measure_t *)m)->max >= *(const long *)user;
}

static bool print(const void *elem, void *user) {
  (void)user;
  printf(" %ld", *(const long *)elem);
  return true;
}

int main(void) {
  static const measure_t identity = {0, -1};
  ds_ftree_config_t config = {0};
  ds_ftree_t seq, left, right;
  measure_t total;
  long i, k = 3;

  config.elem_size = sizeof(long);
  config.measure_size = sizeof(measure_t);
  config.measure = measure;
  config.combine = combine;
  config.identity = &identity;

  ds_ftree_init(&seq, &config);
  for (i = 0; i < 8; i++) {
    long v = (i * 5) % 8;
    ds_ftree_push_back(&seq, &v, &seq);
  }
  printf("sequence:");
  ds_ftree_visit(&seq, print, NULL);

  /* indexed access and splitting by position */
  printf("\nelement 3: %ld\n",
         *(const long *)ds_ftree_lookup(&seq, count_above, &k));
  ds_ftree_split(&seq, count_above, &k, &left, &right);
  printf("first 3:");
  ds_ftree_visit(&left, print, NULL);
  printf("\nrest:");
  ds_ftree_visit(&right, print, NULL);

  /* the maximum, found through the same measure */
  ds_ftree_measure(&seq, &total);
  printf("\nmax %ld (first such element %ld)\n", total.max,
         *(const long *)ds_ftree_lookup(&seq, reaches, &total.max));

  ds_ftree_release(&left);
  ds_ftree_release(&right);
  ds_ftree_release(&seq);
  return 0;
}
