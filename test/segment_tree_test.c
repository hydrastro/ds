#include "../lib/segment_tree.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static long brute_query(const long *arr, size_t l, size_t r,
                        ds_segment_tree_op_t op) {
  long acc;
  size_t i;
  if (op == DS_SEGMENT_TREE_SUM) {
    acc = 0;
  } else if (op == DS_SEGMENT_TREE_MIN) {
    acc = LONG_MAX;
  } else {
    acc = LONG_MIN;
  }
  for (i = l; i <= r; ++i) {
    if (op == DS_SEGMENT_TREE_SUM) {
      acc += arr[i];
    } else if (op == DS_SEGMENT_TREE_MIN) {
      acc = arr[i] < acc ? arr[i] : acc;
    } else {
      acc = arr[i] > acc ? arr[i] : acc;
    }
  }
  return acc;
}

static void run(ds_segment_tree_op_t op, const char *label) {
  size_t n = 300;
  long *arr = (long *)malloc(sizeof(long) * n);
  ds_segment_tree_t *st;
  size_t i;
  unsigned state = 12345U;
  int round;

  for (i = 0; i < n; ++i) {
    arr[i] = (long)((i * 7U) % 50U);
  }
  st = ds_segment_tree_create(arr, n, op);

  for (round = 0; round < 2000; ++round) {
    size_t a, b, tmp;
    state = state * 1103515245U + 12345U;
    a = (state >> 8) % n;
    state = state * 1103515245U + 12345U;
    b = (state >> 8) % n;
    if (a > b) {
      tmp = a;
      a = b;
      b = tmp;
    }
    state = state * 1103515245U + 12345U;

    if ((state >> 16) % 3U == 0U) {
      /* range assign */
      long v = (long)((state >> 4) % 100U);
      size_t j;
      ds_segment_tree_update_assign(st, a, b, v);
      for (j = a; j <= b; ++j) {
        arr[j] = v;
      }
    } else if ((state >> 16) % 3U == 1U) {
      /* range add */
      long d = (long)((state >> 4) % 20U) - 10L;
      size_t j;
      ds_segment_tree_update_add(st, a, b, d);
      for (j = a; j <= b; ++j) {
        arr[j] += d;
      }
    } else {
      /* query */
      long got = ds_segment_tree_query(st, a, b);
      long want = brute_query(arr, a, b, op);
      if (got != want) {
        printf("[%s] MISMATCH [%lu,%lu] got=%ld want=%ld\n", label,
               (unsigned long)a, (unsigned long)b, got, want);
        assert(0);
      }
    }
  }
  printf("[%s] 2000 mixed ops verified against brute force\n", label);
  ds_segment_tree_destroy(st);
  free(arr);
}

int main(void) {
  run(DS_SEGMENT_TREE_SUM, "sum");
  run(DS_SEGMENT_TREE_MIN, "min");
  run(DS_SEGMENT_TREE_MAX, "max");
  printf("ok\n");
  return 0;
}
