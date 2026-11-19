#include <dsa.h>
#include <stdio.h>

static int compare_ints(const void *a, const void *b, void *user) {
  int x = *(const int *)a;
  int y = *(const int *)b;
  (void)user;
  return (x > y) - (x < y);
}

int main(void) {
  int values[] = {3, 1, 2};
  ds_vec_t numbers;
  size_t i;

  if (DS_FAILED(ds_vec_init(&numbers, sizeof(int), NULL))) {
    return 1;
  }
  if (DS_FAILED(ds_vec_append(&numbers, values, 3))) {
    ds_vec_destroy(&numbers);
    return 1;
  }

  ds_vec_sort(&numbers, compare_ints, NULL);
  for (i = 0; i < ds_vec_size(&numbers); ++i) {
    printf("%d\n", *(const int *)ds_vec_at(&numbers, i));
  }

  ds_vec_destroy(&numbers);
  return 0;
}
