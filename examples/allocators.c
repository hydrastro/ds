#include "ds.h"
#include <stdio.h>

int main(void) {
  ds_context_t context;
  ds_arena_t arena;
  unsigned char buffer[256];
  char *message;

  ds_context_init(&context);
  ds_arena_init(&arena, buffer, sizeof(buffer));
  ds_context_use_arena(&context, &arena);

  message = (char *)ds_context_alloc(&context, 16U);
  if (message == NULL) {
    return 1;
  }
  message[0] = 'd';
  message[1] = 's';
  message[2] = '\0';
  printf("%s arena-used=%lu\n", message, (unsigned long)ds_arena_used(&arena));
  return 0;
}
