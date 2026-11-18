/* Normalize text with all string and Unicode scratch storage in one arena. */
#include <dsad.h>
#include <stdio.h>

int main(void) {
  unsigned char storage[32768];
  ds_arena_t arena;
  ds_context_t context;
  ds_str_t *text = NULL;
  int result = 1;

  ds_arena_init(&arena, storage, sizeof(storage));
  ds_context_init(&context);
  ds_context_use_arena(&context, &arena);
  if (ds_str_create_context(&context, &text) != DS_OK)
    return 1;
  /* Decomposed e + acute accent becomes the composed UTF-8 character. */
  if (ds_str_assign_ex(text, "Cafe\314\201", 6) != DS_OK)
    goto done;
  if (ds_str_u8_normalize(text, text, DS_U8_NFC) != 0)
    goto done;
  printf("%s (%lu bytes); arena used: %lu bytes\n", text->buf,
         (unsigned long)text->len, (unsigned long)ds_arena_used(&arena));
  result = 0;
done:
  ds_str_destroy(text);
  /* Release every arena-backed object before resetting the arena. */
  ds_arena_reset(&arena);
  return result;
}
