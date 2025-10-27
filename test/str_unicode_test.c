#include "../lib/str.h"
#include "../lib/str_algo.h"
#include "../lib/str_io.h"
#include "../lib/str_unicode.h"
#include "../lib/unicode_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
#define TASSERT(cond)                                                          \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "ASSERT FAILED at %s:%d: %s\n", __FILE__, __LINE__,      \
              #cond);                                                          \
      g_failures++;                                                            \
    }                                                                          \
  } while (0)

static void dump_s(const char *label, const ds_str_t *s) {
  printf("%s: len=%lu cap=%lu | \"%s\"\n", label ? label : "(null)",
         (unsigned long)(s ? s->len : 0u), (unsigned long)(s ? s->cap : 0u),
         s ? (s->buf ? s->buf : "") : "");
}

static void hexdump(const char *label, const char *p, size_t n) {
  size_t i;
  printf("%s (%lu bytes):", label ? label : "bytes", (unsigned long)n);
  for (i = 0; i < n; ++i)
    printf(" %02X", (unsigned char)p[i]);
  printf("\n");
}

static void test_core(void) {
  ds_str_t *s;
  ds_str_t *t;
  ds_str_t *u;
  char *raw;
  size_t raw_len, raw_cap;
  int ch;
  long at;
  int rc;

  printf("== core ==\n");
  s = str_create();
  TASSERT(s != NULL);
  dump_s("init", s);

  rc = str_append_cstr(s, "Hello");
  TASSERT(rc == 0);
  rc = str_pushc(s, ',');
  TASSERT(rc == 0);
  rc = str_pushc(s, ' ');
  TASSERT(rc == 0);
  rc = str_append_cstr(s, "world");
  TASSERT(rc == 0);
  dump_s("after basic append", s);
  TASSERT(strcmp(s->buf, "Hello, world") == 0);

  rc = str_insert(s, 5u, " <+> ", 5u);
  TASSERT(rc == 0);
  TASSERT(strcmp(s->buf, "Hello <+> , world") == 0);

  rc = str_erase(s, 5u, 5u);
  TASSERT(rc == 0);
  TASSERT(strcmp(s->buf, "Hello, world") == 0);

  at = str_find(s, "world", 5u, 0u);
  TASSERT(at >= 0);

  str_to_upper(s);
  TASSERT(strcmp(s->buf, "HELLO, WORLD") == 0);
  str_to_lower(s);
  TASSERT(strcmp(s->buf, "hello, world") == 0);

  rc = str_reserve(s, 1024u);
  TASSERT(rc == 0);
  TASSERT(s->cap >= 1024u);
  rc = str_shrink_to_fit(s);
  TASSERT(rc == 0);
  TASSERT(s->cap == s->len);

  t = str_clone(s);
  TASSERT(t != NULL);
  TASSERT(str_eq(s, t));
  rc = str_append_cstr(t, "!");
  TASSERT(rc == 0);
  TASSERT(!str_eq(s, t));
  str_destroy(t);

  t = str_slice(s, 7u, 5u);
  TASSERT(t != NULL);
  TASSERT(t->len == 5u);
  TASSERT(memcmp(t->buf, "world", 5u) == 0);
  str_destroy(t);

  rc = str_pop(s, &ch);
  TASSERT(rc == 0);
  TASSERT(ch == (int)'d');
  rc = str_append_cstr(s, "d");
  TASSERT(rc == 0);

  raw = str_detach(s, &raw_len, &raw_cap);
  TASSERT(raw != NULL);
  TASSERT(raw_len > 0);
  str_free_external(s, raw);
  rc = str_adopt(s, NULL, 0u, 0u);
  TASSERT(rc == 0);

  u = str_from("xyz", 3u);
  TASSERT(u != NULL);
  rc = str_adopt(s, u->buf, u->len, u->cap);
  TASSERT(rc == 0);

  u->buf = NULL;
  u->len = 0u;
  u->cap = 0u;
  str_destroy(u);
  TASSERT(strcmp(s->buf, "xyz") == 0);

  str_clear(s);
  TASSERT(s->len == 0u && s->buf[0] == '\0');

  {
    const unsigned char rawb[] = {'A', 0x00u, 'B', 'C'};
    t = str_from(rawb, sizeof(rawb));
    TASSERT(t != NULL);
    hexdump("raw", t->buf, t->len);
    TASSERT(t->len == sizeof(rawb));
    str_destroy(t);
  }

  t = str_from("foo", 3u);
  u = str_from("barbaz", 6u);
  TASSERT(t && u);
  str_swap(t, u);
  TASSERT(t->len == 6u && u->len == 3u);
  str_destroy(t);
  str_destroy(u);

  str_destroy(s);
}

static void token_cb(const char *ptr, size_t len, void *ud) {
  int *count = (int *)ud;
  (void)ptr;
  (void)len;
  (*count)++;
}

static void test_algo_views(void) {
  ds_str_t *s;
  ds_str_t *joined;
  ds_str_t *p0;
  ds_str_t *p1;
  ds_str_t *p2;
  ds_str_t *parts[3];
  long pos;
  int rc;
  int tokc;

  printf("== algo/views ==\n");
  s = str_from(" \t, hello,  world , \n",
               (size_t)strlen(" \t, hello,  world , \n"));
  TASSERT(s != NULL);
  {
    static const unsigned char set[] = {' ', '\t', ',', '\n'};
    rc = str_trim_set(s, set, sizeof(set));
    TASSERT(rc == 0);
    TASSERT(strcmp(s->buf, "hello,  world") == 0);
  }

  tokc = 0;
  str_split_each_c(s, (unsigned char)' ', token_cb, &tokc);
  TASSERT(tokc >= 2);

  pos = str_find_bmh(s, "world", 5u, 0u);
  TASSERT(pos >= 0);
  pos = str_find_twoway(s, "hello", 5u, 0u);
  TASSERT(pos >= 0);

  p0 = str_from("foo", 3u);
  p1 = str_from("bar", 3u);
  p2 = str_from("baz", 3u);
  parts[0] = p0;
  parts[1] = p1;
  parts[2] = p2;
  joined = str_create();
  TASSERT(joined != NULL);
  rc = str_join_c(joined, parts, 3u, " | ", 3u);
  TASSERT(rc == 0);
  TASSERT(strcmp(joined->buf, "foo | bar | baz") == 0);

  str_destroy(p0);
  str_destroy(p1);
  str_destroy(p2);
  str_destroy(joined);
  str_destroy(s);
}

static void test_utf8_primitives(void) {
  ds_str_t *s = str_create();
  ds_str_t *dst = str_create();
  int ok;
  size_t cps, ioff = 0, k = 0;
  int r;
  unsigned long cp, got[8];
  const unsigned long exp[5] = {'A', ' ', 0x2603ul, ' ', 0x1F600ul};
  const char snowman[] = "\xE2\x98\x83";
  const char grinning[] = "\xF0\x9F\x98\x80";
  const char overlong_nul[] = "\xC0\x80";
  const char surrogate_hi[] = "\xED\xA0\x80";
  const char bom[] = "\xEF\xBB\xBF";

  TASSERT(s && dst);

  TASSERT(str_append_cstr(s, "A ") == 0);
  TASSERT(str_append(s, snowman, sizeof(snowman) - 1u) == 0);
  TASSERT(str_append_cstr(s, " ") == 0);
  TASSERT(str_append(s, grinning, sizeof(grinning) - 1u) == 0);

  cps = str_u8_count(s, DS_U8_STRICT, &ok);
  TASSERT(ok && cps == 5u);

  ioff = 0;
  k = 0;
  while ((r = str_u8_next(s->buf, s->len, &ioff, &cp, DS_U8_STRICT)) == 1) {
    got[k++] = cp;
  }
  TASSERT(r == 0);
  TASSERT(k == 5u);
  TASSERT(got[0] == exp[0] && got[1] == exp[1] && got[2] == exp[2] &&
          got[3] == exp[3] && got[4] == exp[4]);

  {
    size_t off = str_u8_cp_to_byte(s, 4u, DS_U8_STRICT);
    TASSERT(off != (size_t)-1);
    TASSERT((unsigned char)s->buf[off] == 0xF0u);
  }

  str_clear(dst);
  TASSERT(str_append(dst, overlong_nul, sizeof(overlong_nul) - 1u) == 0);
  TASSERT(str_append(dst, surrogate_hi, sizeof(surrogate_hi) - 1u) == 0);
  TASSERT(str_u8_sanitize(dst, dst, DS_U8_STRICT) == 0);
  TASSERT(dst->len == 15u);

  str_clear(dst);
  TASSERT(str_append(dst, bom, 3u) == 0);
  TASSERT(str_append_cstr(dst, "plain") == 0);
  TASSERT(str_u8_strip_bom(dst) == 1);
  TASSERT(strcmp(dst->buf, "plain") == 0);

  str_clear(dst);
  TASSERT(str_u8_slice_cp(dst, s, 4u, (size_t)1u, DS_U8_STRICT) == 0);
  TASSERT(dst->len == sizeof(grinning) - 1u);
  TASSERT(memcmp(dst->buf, grinning, sizeof(grinning) - 1u) == 0);

  str_destroy(s);
  str_destroy(dst);
}

static void test_unicode_runtime(void) {
  int rc_init;
  ds_str_t *src;
  ds_str_t *dst;
  long g;

  printf("== unicode runtime (normalize/case/graphemes) ==\n");
  rc_init = ucd_init_once();
  if (rc_init != 0) {
    printf("!! UCD not available (skipping runtime tests). Run `make "
           "unicode-data` first.\n");
    return;
  }

  src = str_create();
  dst = str_create();
  TASSERT(src && dst);

  TASSERT(str_append_cstr(src, "a\xCC\x8A") == 0);
  TASSERT(str_u8_normalize(dst, src, DS_U8_NFC) == 0);
  TASSERT(strcmp(dst->buf, "\xC3\xA5") == 0);

  TASSERT(str_u8_normalize(src, dst, DS_U8_NFD) == 0);
  {
    ds_str_t *tmp = str_create();
    TASSERT(tmp != NULL);
    TASSERT(str_u8_normalize(tmp, src, DS_U8_NFD) == 0);
    TASSERT(str_eq(tmp, src));
    str_destroy(tmp);
  }

  str_clear(src);
  str_clear(dst);
  TASSERT(str_append_cstr(src, "\xE2\x81\xBF") == 0);
  TASSERT(str_u8_normalize(dst, src, DS_U8_NFKC) == 0);
  TASSERT(strcmp(dst->buf, "n") == 0);

  str_clear(src);
  str_clear(dst);
  TASSERT(str_append_cstr(src, "\xC3\x9F") == 0);
  TASSERT(str_u8_casefold(dst, src) == 0);
  TASSERT(strcmp(dst->buf, "ss") == 0);

  str_clear(src);
  str_clear(dst);
  TASSERT(str_append_cstr(src, "\xCE\x91") == 0);
  TASSERT(str_u8_tolower(dst, src) == 0);
  TASSERT(strcmp(dst->buf, "\xCE\xB1") == 0);

  str_clear(src);
  str_clear(dst);
  TASSERT(str_append_cstr(src, "\xCE\xB1") == 0);
  TASSERT(str_u8_toupper(dst, src) == 0);
  TASSERT(strcmp(dst->buf, "\xCE\x91") == 0);

  str_clear(src);
  TASSERT(str_append_cstr(src, "\xF0\x9F\x91\x8B") == 0);
  TASSERT(str_append_cstr(src, "\xF0\x9F\x8F\xBD") == 0);
  TASSERT(str_append_cstr(src, "\xE2\x80\x8D") == 0);
  TASSERT(str_append_cstr(src, "\xF0\x9F\x8F\xA0") == 0);
  g = str_u8_len_graphemes(src);
  TASSERT(g >= 1)
  l

      str_destroy(src);
  str_destroy(dst);
}

int main(void) {
  test_core();
  test_algo_views();
  test_utf8_primitives();
  test_unicode_runtime();

  if (g_failures) {
    fprintf(stderr, "\nFAILED (%d failure%s)\n", g_failures,
            g_failures == 1 ? "" : "s");
    return 1;
  }
  printf("\nOK\n");
  return 0;
}
