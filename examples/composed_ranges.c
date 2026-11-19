/* Ordered split/join, noncommutative summaries and a movable text timeline. */
#include <dsa.h>
#include <stdio.h>
#include <string.h>

typedef struct text_summary {
  char text[32];
  size_t length;
} text_summary_t;
static int compare_key(const void *a, const void *b, void *user) {
  unsigned long x, y;
  (void)user;
  memcpy(&x, a, sizeof(x));
  memcpy(&y, b, sizeof(y));
  return (x > y) - (x < y);
}
static void summarize(void *out, const void *key, const void *value,
                      void *user) {
  text_summary_t *text = (text_summary_t *)out;
  (void)key;
  (void)user;
  text->text[0] = *(const char *)value;
  text->text[1] = '\0';
  text->length = 1;
}
static void combine(void *out, const void *a, const void *b, void *user) {
  text_summary_t *r = (text_summary_t *)out;
  const text_summary_t *x = (const text_summary_t *)a,
                       *y = (const text_summary_t *)b;
  size_t left = x->length, right = y->length;
  (void)user;
  if (left > 31) {
    left = 31;
  }
  if (right > 31 - left) {
    right = 31 - left;
  }
  memcpy(r->text, x->text, left);
  memcpy(r->text + left, y->text, right);
  r->length = left + right;
  r->text[r->length] = '\0';
}
static bool display(const void *item, size_t width, size_t offset,
                    void *user) {
  (void)user;
  printf("  %c at %lu, width %lu\n", *(const char *)item,
         (unsigned long)offset, (unsigned long)width);
  return true;
}
static ds_status_t display_id(unsigned long key, void *value, void *user) {
  (void)user;
  printf(" %lu:%c", key, *(char *)value);
  return DS_OK;
}
static int display_range(const void *key, void *value, const void *left,
                         const void *right, void *user) {
  unsigned long k;
  const unsigned long *bounds = (const unsigned long *)user;
  (void)left;
  (void)right;
  memcpy(&k, key, sizeof(k));
  if (k >= bounds[0] && k < bounds[1]) {
    printf(" %lu:%c", k, *(char *)value);
  }
  return (k > bounds[0] ? DS_AUG_LEFT : 0) |
         (k < bounds[1] ? DS_AUG_RIGHT : 0);
}
int main(void) {
  ds_ordered_t lower, upper, array, fusion;
  ds_aug_treap_t summary;
  ds_aug_treap_config_t config;
  ds_enfilade_t timeline;
  ds_status_t st;
  text_summary_t range;
  unsigned long i, lo = 2, hi = 8;
  unsigned long bounds[2] = {2, 8};
  void *borrowed = NULL;
  char ch;
  bool lower_ok = false, upper_ok = false, array_ok = false;
  bool summary_ok = false, timeline_ok = false;
  bool fusion_ok = false;
  st = ds_ordered_init(&lower, ds_ordered_treap_ops(), sizeof(char), NULL);
  if (st != DS_OK) {
    goto done;
  }
  lower_ok = true;
  st = ds_ordered_init(&upper, ds_ordered_treap_ops(), sizeof(char), NULL);
  if (st != DS_OK) {
    goto done;
  }
  upper_ok = true;
  st = ds_ordered_init(&array, ds_ordered_array_ops(), sizeof(char), NULL);
  if (st != DS_OK) {
    goto done;
  }
  array_ok = true;
  st = ds_ordered_init(&fusion, ds_ordered_fusion_ops(), sizeof(char), NULL);
  if (st != DS_OK) {
    goto done;
  }
  fusion_ok = true;
  ds_aug_treap_config_init(&config);
  config.key_size = sizeof(unsigned long);
  config.value_size = sizeof(char);
  config.summary_size = sizeof(text_summary_t);
  config.compare = compare_key;
  config.summarize = summarize;
  config.combine = combine;
  st = ds_aug_treap_init(&summary, &config);
  if (st != DS_OK) {
    goto done;
  }
  summary_ok = true;
  st = ds_enfilade_init(&timeline, sizeof(char), NULL);
  if (st != DS_OK) {
    goto done;
  }
  timeline_ok = true;
  for (i = 0; i < 10 && st == DS_OK; i++) {
    unsigned long key = i * 7 % 10;
    ch = (char)('A' + key);
    st = ds_ordered_insert(&lower, key, &ch);
    if (st == DS_OK) {
      st = ds_aug_treap_insert(&summary, &key, &ch);
    }
  }
  if (st == DS_OK) {
    st = ds_ordered_split(&lower, 5, &upper);
  }
  if (st == DS_OK) {
    printf("Split timelines: %lu / %lu entries\n",
           (unsigned long)ds_ordered_size(&lower),
           (unsigned long)ds_ordered_size(&upper));
    st = ds_ordered_join(&lower, &upper);
  }
  if (st == DS_OK) {
    st = ds_aug_treap_range_summary(&summary, &lo, &hi, &range);
  }
  if (st == DS_OK) {
    printf("Ordered summary [2,8): %s\n", range.text);
  }
  i = 5;
  if (st == DS_OK) {
    borrowed = ds_aug_treap_get(&summary, &i);
    if (borrowed == NULL) {
      st = DS_NOT_FOUND;
    } else {
      *(char *)borrowed = 'f';
      st = ds_aug_treap_refresh(&summary, &i);
    }
  }
  i = 4;
  if (st == DS_OK) {
    st = ds_aug_treap_remove(&summary, &i, NULL);
  }
  if (st == DS_OK) {
    st = ds_aug_treap_range_summary(&summary, &lo, &hi, &range);
  }
  if (st == DS_OK) {
    printf("After refresh/removal: %s; pruned traversal:", range.text);
    st = ds_aug_treap_descend(&summary, display_range, bounds);
    puts("");
  }
  for (i = 0; i < 8 && st == DS_OK; i++) {
    ch = (char)('A' + i);
    st = ds_enfilade_insert(&timeline, (size_t)i, &ch, (size_t)i + 1);
    if (st == DS_OK) {
      st = ds_ordered_insert(&array, i + 10, &ch);
    }
    if (st == DS_OK) {
      st = ds_ordered_insert(&fusion, i + 10, &ch);
    }
  }
  /* Both insertions copy a borrowed value across capacity growth. */
  if (st == DS_OK) {
    st = ds_enfilade_at(&timeline, 3, &borrowed, NULL, NULL);
  }
  if (st == DS_OK) {
    st = ds_enfilade_insert(&timeline, 0, borrowed, 4);
  }
  if (st == DS_OK) {
    st = ds_ordered_insert(&array, 0, ds_ordered_find(&array, 13));
  }
  if (st == DS_OK) {
    st = ds_ordered_insert(&fusion, 0, ds_ordered_find(&fusion, 13));
  }
  if (st == DS_OK) {
    st = ds_enfilade_rearrange(&timeline, 1, 3, 9);
  }
  if (st == DS_OK) {
    puts("Rearranged text timeline:");
    ds_enfilade_foreach(&timeline, display, NULL);
    printf("Array index:");
    st = ds_ordered_for_each(&array, display_id, NULL);
    puts("");
  }
  if (st == DS_OK) {
    printf("Fusion index:");
    st = ds_ordered_for_each(&fusion, display_id, NULL);
    puts("");
  }
  if (st == DS_OK) {
    st = ds_enfilade_set_width(&timeline, 0, 2);
  }
  if (st == DS_OK) {
    st = ds_enfilade_remove(&timeline, 2, &ch);
  }
  if (st == DS_OK) {
    printf("Removed %c; revised timeline width: %lu\n", ch,
           (unsigned long)ds_enfilade_width(&timeline));
  }
done:
  if (timeline_ok) {
    ds_enfilade_destroy(&timeline);
  }
  if (summary_ok) {
    ds_aug_treap_destroy(&summary);
  }
  if (array_ok) {
    ds_ordered_destroy(&array);
  }
  if (fusion_ok) {
    ds_ordered_destroy(&fusion);
  }
  if (upper_ok) {
    ds_ordered_destroy(&upper);
  }
  if (lower_ok) {
    ds_ordered_destroy(&lower);
  }
  if (st != DS_OK) {
    fprintf(stderr, "ranges: %s\n", ds_status_name(st));
  }
  return st == DS_OK ? 0 : 1;
}
