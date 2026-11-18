/* Component and edge-side updates through lazy Euler-tour aggregates. */
#include <dsa.h>
#include <stdio.h>

static void summarize(void *out, const void *elem, void *user) {
  (void)user;
  *(long *)out = *(const long *)elem;
}
static void combine(void *out, const void *left, const void *right,
                    void *user) {
  (void)user;
  *(long *)out = *(const long *)left + *(const long *)right;
}
static void apply_elem(void *elem, const void *tag, void *user) {
  (void)user;
  *(long *)elem += *(const long *)tag;
}
static void apply_sum(void *sum, const void *tag, size_t count, void *user) {
  (void)user;
  *(long *)sum += *(const long *)tag * (long)count;
}
static void compose(void *tag, const void *newer, void *user) {
  (void)user;
  *(long *)tag += *(const long *)newer;
}
static bool display(size_t vertex, const void *elem, void *user) {
  (void)user;
  printf(" %lu:%ld", (unsigned long)vertex, *(const long *)elem);
  return true;
}
int main(void) {
  ds_ett_config_t config;
  ds_ett_t forest;
  long values[6] = {10, 20, 30, 40, 50, 60};
  const size_t edges[5][2] = {{0, 1}, {1, 2}, {1, 3}, {3, 4}, {3, 5}};
  long sum, delta = 5;
  ds_status_t st;
  size_t i;
  ds_ett_config_init(&config);
  config.elem_size = sizeof(long);
  config.summary_size = sizeof(long);
  config.tag_size = sizeof(long);
  config.summarize = summarize;
  config.combine = combine;
  config.tag_apply_elem = apply_elem;
  config.tag_apply_summary = apply_sum;
  config.tag_compose = compose;
  config.map_ops = ds_map_swiss_ops();
  st = ds_ett_init(&forest, 6, values, &config);
  if (st != DS_OK)
    goto done;
  for (i = 0; i < 5 && st == DS_OK; i++)
    st = ds_ett_link(&forest, edges[i][0], edges[i][1]);
  if (st == DS_OK)
    st = ds_ett_component_apply(&forest, 0, &delta);
  delta = 10;
  if (st == DS_OK)
    st = ds_ett_subtree_apply(&forest, 3, 1, &delta);
  if (st == DS_OK)
    st = ds_ett_subtree_query(&forest, 3, 1, &sum);
  if (st == DS_OK)
    printf("Side rooted at 3: %ld\n", sum);
  if (st == DS_OK)
    st = ds_ett_cut(&forest, 1, 3);
  if (st == DS_OK)
    st = ds_ett_component_query(&forest, 0, &sum);
  if (st == DS_OK)
    printf("Detached first component: %ld\n", sum);
  delta = -2;
  if (st == DS_OK)
    st = ds_ett_component_apply(&forest, 3, &delta);
  if (st == DS_OK)
    st = ds_ett_link(&forest, 2, 5);
  if (st == DS_OK)
    st = ds_ett_component_query(&forest, 0, &sum);
  if (st == DS_OK) {
    printf("Rejoined sum: %ld; vertices:", sum);
    st = ds_ett_visit_component(&forest, 0, display, NULL);
    puts("");
  }
  ds_ett_destroy(&forest);
done:
  if (st != DS_OK)
    fprintf(stderr, "forest actions: %s\n", ds_status_name(st));
  return st == DS_OK ? 0 : 1;
}
