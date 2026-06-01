#include "../lib/rope.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* reference model: a growable plain char array */
static char *ref;
static size_t ref_len;
static size_t ref_cap;

static void ref_ensure(size_t extra) {
  if (ref_len + extra + 1 > ref_cap) {
    size_t nc = (ref_cap == 0 ? 64 : ref_cap);
    while (ref_len + extra + 1 > nc) {
      nc *= 2;
    }
    ref = (char *)realloc(ref, nc);
    ref_cap = nc;
  }
}

static void ref_insert(size_t index, const char *s, size_t n) {
  size_t i;
  ref_ensure(n);
  memmove(ref + index + n, ref + index, ref_len - index);
  for (i = 0; i < n; ++i) {
    ref[index + i] = s[i];
  }
  ref_len += n;
  ref[ref_len] = '\0';
}

static void ref_delete(size_t index, size_t count) {
  if (index >= ref_len) {
    return;
  }
  if (index + count > ref_len) {
    count = ref_len - index;
  }
  memmove(ref + index, ref + index + count, ref_len - index - count);
  ref_len -= count;
  ref[ref_len] = '\0';
}

static void check_equal(ds_rope_t *rope, const char *label) {
  char *s;
  size_t i;
  assert(ds_rope_length(rope) == ref_len);
  s = ds_rope_to_string(rope);
  if (strcmp(s, ref) != 0) {
    printf("[%s] MISMATCH\n rope=%s\n ref =%s\n", label, s, ref);
    assert(0);
  }
  for (i = 0; i < ref_len; i += (ref_len / 50 + 1)) {
    assert(ds_rope_char_at(rope, i) == ref[i]);
  }
  free(s);
}

int main(void) {
  ds_rope_t *rope = ds_rope_from_string("Hello, world!", 13);
  unsigned state = 7777U;
  int round;
  char buf[128];

  ref_ensure(13);
  memcpy(ref, "Hello, world!", 13);
  ref_len = 13;
  ref[13] = '\0';
  check_equal(rope, "init");
  printf("init ok, len=%lu\n", (unsigned long)ds_rope_length(rope));

  for (round = 0; round < 4000; ++round) {
    size_t op, idx, n, i;
    state = state * 1103515245U + 12345U;
    op = (state >> 16) % 3U;
    state = state * 1103515245U + 12345U;
    idx = ref_len > 0 ? (state >> 8) % (ref_len + 1) : 0;
    state = state * 1103515245U + 12345U;
    n = 1 + (state >> 8) % 20U;

    if (op == 0) {
      /* insert */
      for (i = 0; i < n; ++i) {
        buf[i] = (char)('a' + (int)((state >> i) % 26U));
      }
      ds_rope_insert(rope, idx, buf, n);
      ref_insert(idx, buf, n);
    } else if (op == 1) {
      /* append */
      for (i = 0; i < n; ++i) {
        buf[i] = (char)('A' + (int)((state >> i) % 26U));
      }
      ds_rope_append(rope, buf, n);
      ref_insert(ref_len, buf, n);
    } else {
      /* delete */
      ds_rope_delete(rope, idx, n);
      ref_delete(idx, n);
    }

    if (round % 200 == 0) {
      check_equal(rope, "mid");
    }
  }
  check_equal(rope, "after-ops");
  printf("4000 mixed ops verified, len=%lu\n",
         (unsigned long)ds_rope_length(rope));

  /* explicit rebalance preserves content */
  ds_rope_rebalance(rope);
  check_equal(rope, "after-rebalance");
  printf("rebalance preserved content\n");

  /* substring matches reference */
  {
    size_t start = ref_len / 4;
    size_t count = ref_len / 4;
    char *out = (char *)malloc(count + 1);
    size_t got = ds_rope_substring(rope, start, count, out);
    out[got] = '\0';
    assert(got == count);
    assert(strncmp(out, ref + start, count) == 0);
    free(out);
    printf("substring verified\n");
  }

  /* concat */
  {
    ds_rope_t *other = ds_rope_from_string("XYZ-tail", 8);
    ds_rope_concat(rope, other);
    ref_insert(ref_len, "XYZ-tail", 8);
    check_equal(rope, "after-concat");
    assert(ds_rope_length(other) == 0);
    ds_rope_destroy(other);
    printf("concat verified\n");
  }

  ds_rope_destroy(rope);
  free(ref);
  printf("ok\n");
  return 0;
}
