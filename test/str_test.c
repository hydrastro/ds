#include "../lib/str.h"
#include "../lib/str_algo.h"
#include "../lib/str_io.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void dump(const char *label, ds_str_t *s) {
  printf("%s: len=%lu cap=%lu | \"%s\"\n", label,
         (unsigned long)FUNC_str_len(s), (unsigned long)FUNC_str_cap(s),
         FUNC_str_cstr(s));
}

long file_write(void *ud, const void *buf, size_t n) {
  FILE *fp = (FILE *)ud;
  size_t w = fwrite(buf, 1, n, fp);
  if (w != n)
    return -1;
  return (long)w;
}

long file_writev(void *ud, const void *const *bufs, const size_t *lens,
                 size_t count) {
  FILE *fp = (FILE *)ud;
  size_t i, total = 0;
  for (i = 0; i < count; ++i) {
    if (lens[i] == 0)
      continue;
    if (fwrite(bufs[i], 1, lens[i], fp) != lens[i])
      return -1;
    total += lens[i];
  }
  return (long)total;
}

void print_tok(const char *ptr, size_t len, void *ud) {
  (void)ud;
  fwrite(ptr, 1, len, stdout);
  fputc('\n', stdout);
}

int main(void) {
  ds_str_t *s = str_create();
  if (!s) {
    fprintf(stderr, "str_create failed\n");
    return 1;
  }
  dump("init", s);

  if (str_append_cstr(s, "Hello") != 0)
    return 1;
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
    long at = str_find(s, needle, (size_t)strlen(needle), 0);
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
    long at = str_find_bmh(s, "world", 5, 0);
    printf("BMH find(\"world\") -> %ld\n", at);
    {
      const unsigned char set[] = {' ', '\t', '\r', '\n', ','};
      (void)str_insert(s, 0, " \t, ", 4);
      (void)str_append_cstr(s, " , \n");
      dump("pre-trim", s);
      (void)str_trim_set(s, set, sizeof(set));
      dump("post-trim", s);
    }

    printf("split by ' ':\n");
    str_split_each_c(s, ' ', print_tok, NULL);

    {
      ds_str_t *dst = str_create();
      ds_str_t *a = str_from("foo", 3);
      ds_str_t *b = str_from("bar", 3);
      ds_str_t *c = str_from("baz", 3);
      ds_str_t *parts[3];
      parts[0] = a;
      parts[1] = b;
      parts[2] = c;
      (void)str_join_c(dst, parts, 3, " | ", 3);
      dump("join result", dst);
      str_destroy(a);
      str_destroy(b);
      str_destroy(c);
      str_destroy(dst);
    }
  }

  {
    ds_str_t *t = str_clone(s);
    if (!t) {
      fprintf(stderr, "clone failed\n");
      return 1;
    }
    dump("clone t", t);
    printf("cmp(s,t) = %d, eq=%d\n", str_cmp(s, t), str_eq(s, t));
    (void)str_append_cstr(t, "!");
    dump("mutated t", t);
    printf("cmp(s,t) = %d, eq=%d\n", str_cmp(s, t), str_eq(s, t));
    str_destroy(t);
  }

  {
    ds_str_t *a = str_from("A", 1);
    ds_str_t *b = str_from("B", 1);
    ds_str_t *c = str_from("C\n", 2);
    ds_str_t *arr[3];
    arr[0] = a;
    arr[1] = b;
    arr[2] = c;

    if (str_writev_all(arr, 3, file_writev, stdout) != 0)
      return 1;

    if (str_writev_all_cb(arr, 3, file_write, stdout) != 0)
      return 1;

    str_destroy(a);
    str_destroy(b);
    str_destroy(c);
  }

  {
    const unsigned char raw[] = {'A', 0x00, 'B', 'C'};
    size_t i, n;
    ds_str_t *u = str_from(raw, sizeof(raw));
    if (!u)
      return 1;
    printf("from raw bytes: len=%lu cap=%lu, bytes:",
           (unsigned long)FUNC_str_len(u), (unsigned long)FUNC_str_cap(u));
    n = FUNC_str_len(u);
    for (i = 0; i < n; ++i) {
      printf(" %02X", (unsigned char)FUNC_str_data(u)[i]);
    }
    printf("\n");
    str_destroy(u);
  }

  str_destroy(s);
  return 0;
}
