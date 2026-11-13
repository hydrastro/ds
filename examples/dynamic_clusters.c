/*
 * Top trees: a road network that is a tree of towns, rewired over time.
 * A path cluster keeps the route's length in towns, its total and largest
 * population, and the population of everything around the route; point
 * clusters keep hanging populations. Routes, regions (subtrees) and
 * searches along a route are answered as roads are cut and built.
 */
#include "ds.h"
#include <stdio.h>
#include <string.h>

typedef struct {
  long towns, route_pop, route_max, all_pop;
} route_t;
typedef struct {
  long pop;
} region_t;

static void on_vertex(void *out, const void *value, const void *point,
                      void *user) {
  route_t *r = (route_t *)out;
  long pop = *(const long *)value;
  (void)user;
  r->towns = 1;
  r->route_pop = pop;
  r->route_max = pop;
  r->all_pop = pop + (point != NULL ? ((const region_t *)point)->pop : 0);
}

static void on_compress(void *out, const void *near, const void *far,
                        void *user) {
  const route_t *a = (const route_t *)near;
  const route_t *b = (const route_t *)far;
  route_t r;
  (void)user;
  r.towns = a->towns + b->towns;
  r.route_pop = a->route_pop + b->route_pop;
  r.route_max = a->route_max > b->route_max ? a->route_max : b->route_max;
  r.all_pop = a->all_pop + b->all_pop;
  *(route_t *)out = r;
}

static void on_to_point(void *out, const void *path, void *user) {
  (void)user;
  ((region_t *)out)->pop = ((const route_t *)path)->all_pop;
}

static void on_rake(void *out, const void *a, const void *b, void *user) {
  (void)user;
  ((region_t *)out)->pop =
      ((const region_t *)a)->pop + ((const region_t *)b)->pop;
}

/* along a route: the first town of at least *user inhabitants */
static int first_big(const void *before, const void *here, void *user) {
  long need = *(const long *)user;
  if (before != NULL && ((const route_t *)before)->route_max >= need) {
    return -1;
  }
  return ((const route_t *)here)->route_pop >= need ? 0 : 1;
}

static const char *names[8] = {"Ash", "Birch", "Cedar", "Dale",
                               "Elm", "Fir",   "Gorse", "Hazel"};

static void route(ds_top_tree_t *t, size_t a, size_t b) {
  route_t r;
  if (ds_top_expose(t, a, b, &r) != DS_OK) {
    printf("  %s -> %s: no road\n", names[a], names[b]);
    return;
  }
  printf("  %s -> %s: %ld towns, %ld people on the route (largest %ld), "
         "%ld in the whole network\n",
         names[a], names[b], r.towns, r.route_pop, r.route_max, r.all_pop);
}

int main(void) {
  static const long pop[8] = {120, 3400, 560, 89, 15000, 230, 4100, 700};
  ds_top_tree_ops_t ops;
  ds_top_tree_t t;
  route_t r;
  long need = 1000;
  size_t town = 0;
  memset(&ops, 0, sizeof(ops));
  ops.value_size = sizeof(long);
  ops.path_size = sizeof(route_t);
  ops.point_size = sizeof(region_t);
  ops.vertex = on_vertex;
  ops.compress = on_compress;
  ops.to_point = on_to_point;
  ops.rake = on_rake; /* sums and maxima need no reverse */
  if (ds_top_tree_init(&t, 8, &ops, pop, NULL) != DS_OK) {
    return 1;
  }
  /* Ash - Birch - Cedar - Dale, Birch - Elm - Fir, Elm - Gorse */
  (void)ds_top_link(&t, 0, 1);
  (void)ds_top_link(&t, 1, 2);
  (void)ds_top_link(&t, 2, 3);
  (void)ds_top_link(&t, 1, 4);
  (void)ds_top_link(&t, 4, 5);
  (void)ds_top_link(&t, 4, 6);
  printf("initial roads:\n");
  route(&t, 0, 5);
  route(&t, 3, 6);
  route(&t, 0, 7);
  if (ds_top_subtree(&t, 4, 0, &r) == DS_OK) {
    printf("  beyond Elm, seen from Ash: %ld people\n", r.all_pop);
  }
  if (ds_top_path_search(&t, 3, 6, first_big, &need, &town) == DS_OK) {
    printf("  first town of 1000+ from Dale to Gorse: %s\n", names[town]);
  }
  printf("the Birch-Elm road closes, Hazel connects Dale and Elm:\n");
  (void)ds_top_cut(&t, 1, 4);
  (void)ds_top_link(&t, 7, 3);
  (void)ds_top_link(&t, 7, 4);
  route(&t, 0, 5);
  route(&t, 3, 6);
  need = 4000;
  if (ds_top_path_search(&t, 0, 6, first_big, &need, &town) == DS_OK) {
    printf("  first town of 4000+ from Ash to Gorse: %s\n", names[town]);
  }
  ds_top_tree_destroy(&t);
  return 0;
}
