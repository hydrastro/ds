/*
 * Families of sets with zero-suppressed decision diagrams.
 *
 * The independent sets of a path 0 - 1 - ... - (n-1) are the subsets that
 * contain no edge {i, i + 1}: all subsets, minus (nonsup) every superset
 * of an edge. There are Fibonacci(n + 2) of them, yet the diagram stays
 * linear in n. maximal() keeps those no vertex can be added to.
 */
#include "ds.h"
#include <stdio.h>

/* the independent sets of an n-vertex path, and the maximal ones */
static ds_status_t path_families(ds_zdd_t *m, size_t n, size_t *independent,
                                 size_t *maximal) {
  size_t all = DS_ZDD_BASE;
  size_t edges = DS_ZDD_EMPTY;
  size_t v;
  ds_status_t st = DS_OK;
  for (v = 0; v < n && st == DS_OK; v++) {
    size_t single;
    size_t choice;
    /* all subsets: join with {{}, {v}} for every vertex */
    st = ds_zdd_single(m, v, &single);
    if (st == DS_OK) {
      st = ds_zdd_union(m, DS_ZDD_BASE, single, &choice);
    }
    if (st == DS_OK) {
      st = ds_zdd_join(m, all, choice, &all);
    }
    if (st == DS_OK && v + 1 < n) {
      size_t edge[2];
      size_t e;
      edge[0] = v;
      edge[1] = v + 1;
      st = ds_zdd_set(m, edge, 2, &e);
      if (st == DS_OK) {
        st = ds_zdd_union(m, edges, e, &edges);
      }
    }
  }
  if (st == DS_OK) {
    st = ds_zdd_nonsup(m, all, edges, independent);
  }
  if (st == DS_OK) {
    st = ds_zdd_maximal(m, *independent, maximal);
  }
  return st;
}

static bool print_set(const size_t *set, size_t k, void *user) {
  size_t i;
  (void)user;
  printf("  {");
  for (i = 0; i < k; i++) {
    printf(i == 0 ? "%lu" : " %lu", (unsigned long)set[i]);
  }
  printf("}\n");
  return true;
}

int main(void) {
  ds_zdd_t m;
  size_t independent;
  size_t maximal;
  double count = 0.0;

  if (ds_zdd_init(&m, 60, NULL, NULL) != DS_OK ||
      path_families(&m, 60, &independent, &maximal) != DS_OK) {
    return 1;
  }
  (void)ds_zdd_count(&m, independent, &count);
  printf("independent sets of a 60-vertex path: %.0f in %lu nodes\n", count,
         (unsigned long)ds_zdd_node_count(&m, independent));
  (void)ds_zdd_count(&m, maximal, &count);
  printf("maximal ones: %.0f\n", count);
  ds_zdd_destroy(&m);

  if (ds_zdd_init(&m, 6, NULL, NULL) != DS_OK ||
      path_families(&m, 6, &independent, &maximal) != DS_OK) {
    return 1;
  }
  printf("maximal independent sets of a 6-vertex path:\n");
  (void)ds_zdd_foreach(&m, maximal, print_set, NULL);
  ds_zdd_destroy(&m);
  return 0;
}
