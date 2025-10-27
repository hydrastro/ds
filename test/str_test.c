#include "../lib/str.h"
#include "../lib/str_algo.h"
#include "../lib/str_io.h"
#include "../lib/str_io_posix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump(const char *label, ds_str_t *s) {
  printf("%s: len=%lu cap=%lu | \"%s\"\n", label,
         (unsigned long)FUNC_str_len(s), (unsigned long)FUNC_str_cap(s),
         FUNC_str_cstr(s));
}

static void print_token(const char *ptr, size_t len, void *ud) {
  (void)ud;
  if (len > 0)
    fwrite(ptr, 1, len, stdout);
  fputc('\n', stdout);
}

int main(void) {
  ds_str_t *s;
  ds_str_t *parts0;
  ds_str_t *parts1;
  ds_str_t *parts2;
  ds_str_t *parts[3];
  ds_str_t *joined;
  long at;
  int rc;

  s = str_create();
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

  at = str_find(s, "world", 5u, 0u);
  printf("find(\"world\") -> %ld\n", at);

  str_to_upper(s);
  dump("upper", s);
  str_to_lower(s);
  dump("lower", s);

  if (str_reserve(s, 1024u) != 0)
    return 1;
  dump("after reserve(1024)", s);
  if (str_shrink_to_fit(s) != 0)
    return 1;
  dump("after shrink_to_fit", s);

  {
    static const unsigned char set[] = {' ', '\t', ',', '\n'};
    ds_str_t *t;
    t = str_create();
    if (!t)
      return 1;
    rc = str_append_cstr(t, " \t, hello, world , \n");
    if (rc != 0)
      return 1;
    dump("pre-trim", t);
    rc = str_trim_set(t, set, sizeof(set));
    if (rc != 0)
      return 1;
    dump("post-trim", t);

    printf("split by ' ':\n");
    str_split_each_c(t, (unsigned char)' ', print_token, NULL);

    str_destroy(t);
  }

  parts0 = str_from("foo", 3u);
  parts1 = str_from("bar", 3u);
  parts2 = str_from("baz", 3u);
  if (!parts0 || !parts1 || !parts2)
    return 1;
  parts[0] = parts0;
  parts[1] = parts1;
  parts[2] = parts2;

  joined = str_create();
  if (!joined)
    return 1;
  rc = str_join_c(joined, parts, 3u, " | ", 3u);
  if (rc != 0)
    return 1;
  dump("join result", joined);

  {
    static const char snowman[] = "\xE2\x98\x83";
    static const char grinning[] = "\xF0\x9F\x98\x80";

    rc = str_append_cstr(s, " ");
    if (rc != 0)
      return 1;
    rc = str_append(s, snowman, sizeof(snowman) - 1u);
    if (rc != 0)
      return 1;
    rc = str_append_cstr(s, " ");
    if (rc != 0)
      return 1;
    rc = str_append(s, grinning, sizeof(grinning) - 1u);
    if (rc != 0)
      return 1;
    dump("with UTF-8", s);

    at = str_find(s, snowman, sizeof(snowman) - 1u, 0u);
    if (at >= 0) {
      rc = str_erase(s, (size_t)at, sizeof(snowman) - 1u);
      if (rc != 0)
        return 1;
      dump("after removing snowman", s);
    }
  }

  {
    ds_str_t *t2;
    t2 = str_clone(s);
    if (!t2)
      return 1;
    dump("clone t", t2);
    printf("cmp(s,t) = %d, eq=%d\n", str_cmp(s, t2), str_eq(s, t2));
    rc = str_append_cstr(t2, "!");
    if (rc != 0)
      return 1;
    dump("mutated t", t2);
    printf("cmp(s,t) = %d, eq=%d\n", str_cmp(s, t2), str_eq(s, t2));
    str_destroy(t2);
  }

  {
    const unsigned char raw[] = {'A', 0x00, 'B', 'C'};
    ds_str_t *u;
    size_t i;
    u = str_from(raw, sizeof(raw));
    if (!u)
      return 1;
    printf("from raw bytes: len=%lu cap=%lu, bytes:",
           (unsigned long)FUNC_str_len(u), (unsigned long)FUNC_str_cap(u));
    for (i = 0; i < FUNC_str_len(u); ++i) {
      printf(" %02X", (unsigned char)FUNC_str_data(u)[i]);
    }
    printf("\n");
    str_destroy(u);
  }

  {
    long wrc1;
    printf("\n-- write_all to stdout --\n");
    wrc1 = ds_posix_write_cb((void *)(long)1, FUNC_str_data(joined),
                             FUNC_str_len(joined));
    if (wrc1 < 0) {
      fprintf(stderr, "write_all failed\n");
      return 1;
    }
    printf("\n");
  }

  {
    const void *bufs[4];
    size_t lens[4];
    ds_str_t *a;
    ds_str_t *b;
    ds_str_t *c;
    ds_str_t *sp;

    a = str_from("A", 1u);
    b = str_from("B", 1u);
    c = str_from("C", 1u);
    sp = str_from("\n", 1u);
    if (!a || !b || !c || !sp)
      return 1;

    bufs[0] = a->buf;
    lens[0] = a->len;
    bufs[1] = b->buf;
    lens[1] = b->len;
    bufs[2] = c->buf;
    lens[2] = c->len;
    bufs[3] = sp->buf;
    lens[3] = sp->len;

    printf("-- writev to stdout --\n");
    if (ds_posix_writev_cb((void *)(long)1, bufs, lens, 4u) < 0) {
      fprintf(stderr, "writev failed\n");
      return 1;
    }

    str_destroy(a);
    str_destroy(b);
    str_destroy(c);
    str_destroy(sp);
  }

  {
    ds_str_view_t views[3];
    ds_str_t *p0;
    ds_str_t *p1;
    ds_str_t *nl;
    const void *bufs2[3];
    size_t lens2[3];

    p0 = str_from("view-0", 6u);
    p1 = str_from(" view-1", 7u);
    nl = str_from("\n", 1u);
    if (!p0 || !p1 || !nl)
      return 1;

    if (str_view(p0, 0u, FUNC_str_len(p0), &views[0]) != 0)
      return 1;
    if (str_view(p1, 0u, FUNC_str_len(p1), &views[1]) != 0)
      return 1;
    if (str_view(nl, 0u, FUNC_str_len(nl), &views[2]) != 0)
      return 1;

    bufs2[0] = views[0].data;
    lens2[0] = views[0].len;
    bufs2[1] = views[1].data;
    lens2[1] = views[1].len;
    bufs2[2] = views[2].data;
    lens2[2] = views[2].len;

    printf("-- views writev to stdout --\n");
    if (ds_posix_writev_cb((void *)(long)1, bufs2, lens2, 3u) < 0) {
      fprintf(stderr, "views writev failed\n");
      return 1;
    }

    str_destroy(p0);
    str_destroy(p1);
    str_destroy(nl);
  }

  str_destroy(s);
  str_destroy(parts0);
  str_destroy(parts1);
  str_destroy(parts2);
  str_destroy(joined);

  return 0;
}
