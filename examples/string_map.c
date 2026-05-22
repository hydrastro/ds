#include "ds.h"
#include <stdio.h>

int main(void) {
  ds_string_map_t *map;
  void *value;

  map = ds_string_map_create();
  ds_string_map_put(map, "hello", "world");
  if (ds_string_map_get(map, "hello", &value) == DS_OK) {
    printf("%s\n", (char *)value);
  }
  ds_string_map_destroy(map);
  return 0;
}
