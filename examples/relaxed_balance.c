/*
 * Chromatic trees: red-black trees whose rebalancing can wait. A batch of
 * sorted keys goes in with rebalancing deferred (no rotations at all, so
 * the tree degenerates into a path), then the pending work runs in slices,
 * as it could between requests, and the height falls back to a red-black
 * tree's. Searches, rank and select are exact throughout.
 */
#include "ds.h"
#include <stdio.h>

static int cmp_long(const void *a, const void *b, void *user) {
  long x = *(const long *)a;
  long y = *(const long *)b;
  (void)user;
  return x < y ? -1 : (x > y ? 1 : 0);
}

static void report(ds_chromatic_tree_t *t, const char *when) {
  size_t h = 0;
  size_t violations = 0;
  bool valid = false;
  (void)ds_chromatic_height(t, &h);
  (void)ds_chromatic_check(t, &valid, &violations);
  printf("%-28s height %5lu, %5lu violations, %5lu pending%s\n", when,
         (unsigned long)h, (unsigned long)violations,
         (unsigned long)ds_chromatic_pending(t), valid ? "" : " (INVALID)");
}

int main(void) {
  ds_chromatic_tree_t t;
  long k;
  long probe = 4321;
  size_t steps = 0;
  size_t slice = 0;
  int i;
  if (ds_chromatic_init(&t, sizeof(long), cmp_long, NULL, NULL) != DS_OK) {
    return 1;
  }
  (void)ds_chromatic_set_deferred(&t, true);
  for (k = 0; k < 10000; k++) {
    (void)ds_chromatic_insert(&t, &k);
  }
  report(&t, "10000 sorted inserts:");
  printf("  meanwhile: rank(4321) = %lu, select(9999) = %ld\n",
         (unsigned long)ds_chromatic_rank(&t, &probe),
         *(long *)ds_chromatic_select(&t, 9999));
  while (ds_chromatic_pending(&t) > 0) {
    char label[40];
    (void)ds_chromatic_rebalance(&t, 4000, &steps);
    sprintf(label, "after slice %lu (%lu steps):", (unsigned long)++slice,
            (unsigned long)steps);
    report(&t, label);
  }
  (void)ds_chromatic_set_deferred(&t, false); /* eager from here on */
  for (k = 0; k < 10000; k += 3) {
    (void)ds_chromatic_remove(&t, &k, NULL);
  }
  report(&t, "eager: a third removed:");
  printf("transformations:");
  for (i = 0; i < DS_CHROMATIC_RULES; i++) {
    static const char *names[DS_CHROMATIC_RULES] = {
        "blacking", "rotate1", "rotate2", "push",
        "red-sib",  "absorb1", "absorb2"};
    printf(" %s %lu", names[i], t.transformations[i]);
  }
  printf("\n");
  ds_chromatic_destroy(&t);
  return 0;
}
