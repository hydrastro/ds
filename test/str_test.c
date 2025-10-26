#include "../lib/str.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump(const char *label, ds_str_t *s) {
  printf("%s: len=%lu cap=%lu | \"%s\"\n", label,
         (unsigned long)FUNC_str_len(s), (unsigned long)FUNC_str_cap(s),
         FUNC_str_cstr(s));
}

int main(void) {
  ds_str_t *s = str_create();
  if (!s) {
    fprintf(stderr, "str_create failed\n");
    return 1;
  }

  dump("init", s);

  if (str_append_cstr(s, "Hello") != 0) {
    fprintf(stderr, "append failed\n");
    return 1;
  }
  if (str_pushc(s, ',') != 0)
    return 1;
  if (str_pushc(s, ' ') != 0)
    return 1;
  if (str_append_cstr(s, "world") != 0)
    return 1;
  dump("after basic append", s);

  if (str_insert(s, 5, " <+> ", 5) != 0)
    return 1;
  dump("after insert", s);

  if (str_erase(s, 5, 5) != 0)
    return 1;
  dump("after erase", s);

  {
    const char *needle = "world";
    long at = str_find(s, needle, strlen(needle), 0);
    printf("find(\"%s\") -> %ld\n", needle, at);
  }

  str_to_upper(s);
  dump("upper", s);
  str_to_lower(s);
  dump("lower", s);

  if (str_reserve(s, 1024) != 0)
    return 1;
  dump("after reserve(1024)", s);
  if (str_shrink_to_fit(s) != 0)
    return 1;
  dump("after shrink_to_fit", s);

  {
    const char snowman[] = "\xE2\x98\x83";
    const char grinning[] = "\xF0\x9F\x98\x80";
    str_append_cstr(s, " ");
    str_append(s, snowman, sizeof(snowman) - 1);
    str_append_cstr(s, " ");
    str_append(s, grinning, sizeof(grinning) - 1);
    dump("with UTF-8", s);

    long pos = str_find(s, snowman, sizeof(snowman) - 1, 0);
    if (pos >= 0) {
      str_erase(s, (size_t)pos, sizeof(snowman) - 1);
      dump("after removing snowman", s);
    }
  }

  ds_str_t *t = str_clone(s);
  if (!t) {
    fprintf(stderr, "clone failed\n");
    return 1;
  }
  dump("clone t", t);

  printf("cmp(s,t) = %d, eq=%d\n", str_cmp(s, t), str_eq(s, t));

  str_append_cstr(t, "!");
  dump("mutated t", t);
  printf("cmp(s,t) = %d, eq=%d\n", str_cmp(s, t), str_eq(s, t));

  str_destroy(s);
  str_destroy(t);

  {
    const unsigned char raw[] = {'A', 0x00, 'B', 'C'};
    ds_str_t *u = str_from(raw, sizeof(raw));
    if (!u)
      return 1;
    printf("from raw bytes: len=%lu cap=%lu, bytes:",
           (unsigned long)FUNC_str_len(u), (unsigned long)FUNC_str_cap(u));
    for (size_t i = 0; i < FUNC_str_len(u); ++i) {
      printf(" %02X", (unsigned char)FUNC_str_data(u)[i]);
    }
    printf("\n");
    str_destroy(u);
  }

  return 0;
}
