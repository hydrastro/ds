/* Lazy sequence editing, randomized ordered catalogs, and visibility rays. */
#include <dsa.h>
#include <dsa/algorithms/visibility.h>
#include <stdio.h>
#include <string.h>

typedef struct phrase {
  char text[64];
  size_t size;
} phrase_t;
static void summarize(void *out, const void *elem, void *user) {
  phrase_t *p = (phrase_t *)out;
  (void)user;
  p->text[0] = *(const char *)elem;
  p->text[1] = '\0';
  p->size = 1;
}
static void combine(void *out, const void *left, const void *right,
                    void *user) {
  phrase_t *p = (phrase_t *)out;
  const phrase_t *a = (const phrase_t *)left, *b = (const phrase_t *)right;
  size_t n = a->size < 63 ? a->size : 63;
  size_t m = b->size < 63 - n ? b->size : 63 - n;
  (void)user;
  memcpy(p->text, a->text, n);
  memcpy(p->text + n, b->text, m);
  p->size = n + m;
  p->text[p->size] = '\0';
}
static void apply_elem(void *elem, const void *tag, void *user) {
  (void)user;
  *(char *)elem = (char)(*(char *)elem + *(const int *)tag);
}
static void apply_sum(void *sum, const void *tag, size_t count, void *user) {
  phrase_t *p = (phrase_t *)sum;
  size_t i;
  (void)count;
  for (i = 0; i < p->size; i++) {
    apply_elem(&p->text[i], tag, user);
  }
}
static void compose(void *tag, const void *newer, void *user) {
  (void)user;
  *(int *)tag += *(const int *)newer;
}
static bool show_char(const void *elem, size_t index, void *user) {
  (void)index;
  (void)user;
  putchar(*(const char *)elem);
  return true;
}
static int compare(const void *a, const void *b, void *user) {
  int x, y;
  (void)user;
  memcpy(&x, a, sizeof(x));
  memcpy(&y, b, sizeof(y));
  return (x > y) - (x < y);
}
static bool show_number(const void *elem, void *user) {
  int x;
  (void)user;
  memcpy(&x, elem, sizeof(x));
  printf(" %d", x);
  return true;
}
int main(void) {
  ds_itreap_config_t config;
  ds_itreap_t text, tail;
  ds_rbst_t catalog, upper;
  ds_status_t st;
  phrase_t phrase;
  size_t i;
  int shift = 'a' - 'A', key = 8;
  bool text_ok = false, tail_ok = false, catalog_ok = false, upper_ok = false;
  const ds_visibility_segment_t segments[3] = {
      {0, 0, 4, 0}, {1, 2, 3, 2}, {0, 4, 4, 4}};
  ds_visibility_ray_t rays[6];
  ds_itreap_config_init(&config);
  config.elem_size = sizeof(char);
  config.summary_size = sizeof(phrase_t);
  config.tag_size = sizeof(int);
  config.summarize = summarize;
  config.combine = combine;
  config.tag_apply_elem = apply_elem;
  config.tag_apply_summary = apply_sum;
  config.tag_compose = compose;
  st = ds_itreap_init(&text, &config);
  if (st != DS_OK) {
    goto done;
  }
  text_ok = true;
  for (i = 0; i < 8 && st == DS_OK; i++) {
    char c = (char)('A' + i);
    st = ds_itreap_push_back(&text, &c);
  }
  if (st == DS_OK) {
    st = ds_itreap_apply(&text, 1, 7, &shift);
  }
  if (st == DS_OK) {
    st = ds_itreap_reverse(&text, 0, 8);
  }
  if (st == DS_OK) {
    st = ds_itreap_split(&text, 3, &tail);
  }
  if (st != DS_OK) {
    goto done;
  }
  tail_ok = true;
  st = ds_itreap_append(&tail, &text);
  if (st == DS_OK) {
    st = ds_itreap_query(&tail, 0, 8, &phrase);
  }
  if (st == DS_OK) {
    printf("Edited summary: %s; traversal: ", phrase.text);
    st = ds_itreap_visit(&tail, show_char, NULL);
    puts("");
  }
  if (st != DS_OK) {
    goto done;
  }
  st = ds_rbst_init(&catalog, sizeof(int), compare, NULL, NULL, NULL, NULL);
  if (st != DS_OK) {
    goto done;
  }
  catalog_ok = true;
  st = ds_rbst_init(&upper, sizeof(int), compare, NULL, NULL, NULL, NULL);
  if (st != DS_OK) {
    goto done;
  }
  upper_ok = true;
  for (i = 0; i < 16 && st == DS_OK; i++) {
    int n = (int)(i * 7 % 16);
    st = ds_rbst_insert(&catalog, &n);
  }
  if (st == DS_OK) {
    st = ds_rbst_split(&catalog, &key, &upper);
  }
  if (st == DS_OK) {
    printf("Catalog split: %lu / %lu\n", (unsigned long)ds_rbst_size(&catalog),
           (unsigned long)ds_rbst_size(&upper));
    st = ds_rbst_join(&catalog, &upper);
  }
  for (key = 0; key < 16 && st == DS_OK; key += 3) {
    st = ds_rbst_remove(&catalog, &key, NULL);
  }
  if (st == DS_OK) {
    printf("Catalog, height %lu:", (unsigned long)ds_rbst_height(&catalog));
    ds_rbst_foreach(&catalog, show_number, NULL);
    puts("");
    st = ds_visibility_map(segments, 3, DS_VISIBILITY_VERTICAL, rays, NULL);
  }
  if (st == DS_OK) {
    printf("Middle platform rays hit segment %lu above and %lu below\n",
           (unsigned long)rays[2].up, (unsigned long)rays[2].down);
  }
done:
  if (upper_ok) {
    ds_rbst_destroy(&upper);
  }
  if (catalog_ok) {
    ds_rbst_destroy(&catalog);
  }
  if (tail_ok) {
    ds_itreap_destroy(&tail);
  }
  if (text_ok) {
    ds_itreap_destroy(&text);
  }
  if (st != DS_OK) {
    fprintf(stderr, "sequence policies: %s\n", ds_status_name(st));
  }
  return st == DS_OK ? 0 : 1;
}
