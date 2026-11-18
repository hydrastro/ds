/* Require an exact integer matching and inspect the solver's mode. */
#include <dsa/algorithms/weighted_matching.h>
#include <stdio.h>

int main(void) {
  const size_t u[] = {0, 0, 0, 1, 1, 2};
  const size_t v[] = {1, 2, 3, 2, 3, 3};
  const double cost[] = {9, 3, 4, 8, 1, 5};
  size_t mate[4];
  size_t i;
  double total;
  ds_wmatch_options_t options;
  ds_wmatch_mode_t mode;
  ds_status_t st;
  ds_wmatch_options_init(&options);
  options.require_exact = true;
  st = ds_matching_min_weight_perfect_ex(4, u, v, cost, 6, mate, &total,
                                         &options, &mode, NULL);
  if (st != DS_OK) {
    fprintf(stderr, "No exact matching was returned (status %d).\n", (int)st);
    return 1;
  }
  printf("Mode: %s; total cost: %.0f\n",
         mode == DS_WMATCH_MODE_INTEGER ? "integer" : "floating", total);
  for (i = 0; i < 4; i++) {
    if (mate[i] > i) {
      printf("  pair %lu, %lu\n", (unsigned long)i, (unsigned long)mate[i]);
    }
  }
  return 0;
}
