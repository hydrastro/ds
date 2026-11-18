/* Context ownership through string swaps, filters and checked range updates. */
#include "lib/bloom_filter.h"
#include "lib/segment_tree.h"
#include "lib/str.h"
#include "lib/str_algo.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

struct owner {
  size_t allocations, releases;
};
static void *owned_alloc(size_t size, void *user) {
  struct owner *owner = (struct owner *)user;
  void *p = malloc(size);
  if (p != NULL)
    owner->allocations += 1;
  return p;
}
static void owned_free(void *p, void *user) {
  struct owner *owner = (struct owner *)user;
  if (p != NULL)
    owner->releases += 1;
  free(p);
}
static void owner_init(ds_context_t *context, struct owner *owner) {
  ds_context_init(context);
  context->alloc = owned_alloc;
  context->free = owned_free;
  context->calloc = NULL;
  context->realloc = NULL;
  context->user = owner;
}
int main(void) {
  struct owner first = {0, 0}, second = {0, 0};
  ds_context_t a, b;
  ds_str_t *left = NULL, *right = NULL, *copy = NULL;
  ds_bloom_filter_t *filter = NULL, *filter_copy = NULL;
  ds_segment_tree_t *ranges = NULL, *cancellation = NULL;
  long readings[128], cancelling[2], total;
  ds_status_t status;
  bool maybe;
  size_t i;
  char *detached;
  int rc = 1;
  owner_init(&a, &first);
  owner_init(&b, &second);
  if (ds_str_create_context(&a, &left) != DS_OK ||
      ds_str_create_context(&b, &right) != DS_OK)
    goto done;
  if (ds_str_assign_ex(left, "alpha", 5) != DS_OK ||
      ds_str_assign_ex(right, "beta", 4) != DS_OK ||
      ds_str_swap_ex(left, right) != DS_OK)
    goto done;
  printf("Swapped text: %s / %s\n", FUNC_str_cstr(left), FUNC_str_cstr(right));
  if (ds_str_clone_with(right, true, NULL, &copy) != DS_OK ||
      ds_str_replace_all_builder(copy, "a", 1, "A", 1) != 0)
    goto done;
  printf("Cloned replacement: %s\n", FUNC_str_cstr(copy));
  detached = ds_str_detach(copy, NULL, NULL);
  ds_str_free_external(copy, detached);
  if (ds_bloom_filter_create_context(100, 0.01, &a, &filter) != DS_OK ||
      ds_bloom_filter_add_ex(filter, "alpha", 5) != DS_OK ||
      ds_bloom_filter_clone_with(filter, &b, &filter_copy) != DS_OK ||
      ds_bloom_filter_contains_ex(filter_copy, "alpha", 5, &maybe) != DS_OK)
    goto done;
  printf("Cloned filter: alpha %s\n", maybe ? "may be present" : "absent");
  for (i = 0; i < 128; ++i)
    readings[i] = 2;
  if (ds_segment_tree_create_context(readings, 128, DS_SEGMENT_TREE_SUM, &a,
                                     &ranges) != DS_OK ||
      ds_segment_tree_update_add_ex(ranges, 17, 110, 3) != DS_OK ||
      ds_segment_tree_query_ex(ranges, 23, 79, &total) != DS_OK)
    goto done;
  printf("Readings over [23,79]: %ld\n", total);
  status = ds_segment_tree_update_add_ex(ranges, 19, 104, LONG_MAX);
  printf("Unrepresentable adjustment: %s\n", ds_status_name(status));
  if (ds_segment_tree_query_ex(ranges, 23, 79, &total) != DS_OK)
    goto done;
  printf("Readings after rejected adjustment: %ld\n", total);
  cancelling[0] = -LONG_MAX;
  cancelling[1] = 0;
  if (ds_segment_tree_create_context(cancelling, 2, DS_SEGMENT_TREE_SUM, &b,
                                     &cancellation) != DS_OK ||
      ds_segment_tree_update_add_ex(cancellation, 0, 1, LONG_MAX) != DS_OK ||
      ds_segment_tree_query_ex(cancellation, 0, 1, &total) != DS_OK)
    goto done;
  printf("Scaled adjustment with cancellation: %ld\n", total);
  rc = 0;
done:
  ds_segment_tree_destroy(cancellation);
  ds_segment_tree_destroy(ranges);
  ds_bloom_filter_destroy(filter_copy);
  ds_bloom_filter_destroy(filter);
  ds_str_destroy(copy);
  ds_str_destroy(right);
  ds_str_destroy(left);
  printf("First owner: %lu allocations / %lu releases\n",
         (unsigned long)first.allocations, (unsigned long)first.releases);
  printf("Second owner: %lu allocations / %lu releases\n",
         (unsigned long)second.allocations, (unsigned long)second.releases);
  return rc;
}
