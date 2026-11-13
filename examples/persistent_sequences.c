/*
 * RRB vectors as an editable, versioned text buffer: every edit (insert,
 * remove, cut and paste by slice and concat) is O(log n) and makes a new
 * version, while the old versions stay intact and share most of their
 * nodes with the new ones.
 */
#include "ds.h"
#include <stdio.h>
#include <string.h>

static void print(const char *label, const ds_rrb_t *v) {
  ds_rrb_iter_t it;
  const void *p;
  printf("%-10s \"", label);
  ds_rrb_iter_init(&it, v, 0);
  while ((p = ds_rrb_iter_next(&it)) != NULL) {
    putchar(*(const char *)p);
  }
  printf("\"\n");
}

int main(void) {
  ds_rrb_config_t c;
  ds_rrb_t v1, v2, v3, head, tail, v4;
  const char *text = "the quick fox jumps";
  size_t i;
  ds_rrb_config_init(&c, sizeof(char));
  c.bits = 2; /* branching 4: a deep tree even for a short text */
  if (ds_rrb_init(&v1, &c) != DS_OK) {
    return 1;
  }
  for (i = 0; text[i] != '\0'; i++) {
    (void)ds_rrb_push_back(&v1, &text[i], &v1);
  }
  /* version 2: insert "brown " before "fox" */
  ds_rrb_copy(&v1, &v2);
  for (i = 0; i < 6; i++) {
    (void)ds_rrb_insert(&v2, 10 + i, &"brown "[i], &v2);
  }
  /* version 3: remove "quick " */
  ds_rrb_copy(&v2, &v3);
  for (i = 0; i < 6; i++) {
    (void)ds_rrb_remove(&v3, 4, NULL, &v3);
  }
  /* version 4: move the first word to the end (cut and paste) */
  (void)ds_rrb_split(&v3, 4, &head, &tail);
  (void)ds_rrb_push_back(&tail, " ", &tail);
  (void)ds_rrb_concat(&tail, &head, &v4);
  print("version 1", &v1);
  print("version 2", &v2);
  print("version 3", &v3);
  print("version 4", &v4);
  printf("heights %u %u %u %u; all four versions share nodes\n", v1.height,
         v2.height, v3.height, v4.height);
  ds_rrb_release(&v1);
  ds_rrb_release(&v2);
  ds_rrb_release(&v3);
  ds_rrb_release(&head);
  ds_rrb_release(&tail);
  ds_rrb_release(&v4);
  return 0;
}
