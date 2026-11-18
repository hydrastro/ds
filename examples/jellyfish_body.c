#include <dsa.h>
#include <stdio.h>
#include <string.h>

static ds_status_t show_item(unsigned long key, void *value, void *user) {
  const char *separator = (const char *)user;
  int number;
  memcpy(&number, value, sizeof(number));
  printf("%s%lu:%d", separator, key, number);
  return DS_OK;
}
static ds_status_t show_shape(const ds_jellyfish_t *tree) {
  ds_jellyfish_shape_t shape;
  size_t i;
  ds_status_t st = ds_jellyfish_shape(tree, &shape);
  if (st != DS_OK)
    return st;
  printf("%lu items, %lu body items, %lu tentacles, longest %lu\n",
         (unsigned long)shape.items, (unsigned long)shape.body_items,
         (unsigned long)shape.tentacles, (unsigned long)shape.longest_tentacle);
  for (i = 0; i < shape.tentacles; i++) {
    printf("Tentacle %lu:", (unsigned long)i);
    st = ds_jellyfish_tentacle(tree, i, show_item, (void *)" ");
    if (st != DS_OK)
      return st;
    putchar('\n');
  }
  return DS_OK;
}
static ds_status_t run(const ds_ordered_ops_t *body) {
  ds_jellyfish_config_t config;
  ds_jellyfish_t tree;
  unsigned long keys[36];
  int values[36], found;
  size_t i;
  void *borrowed;
  ds_status_t st;
  for (i = 0; i < 36; i++) {
    keys[i] = (unsigned long)((i * 11) % 36);
    values[i] = (int)(keys[i] * keys[i]);
  }
  ds_jellyfish_config_init(&config);
  config.value_size = sizeof(int);
  config.body_ops = body;
  st = ds_jellyfish_init(&tree, &config);
  if (st != DS_OK)
    return st;
  st = ds_jellyfish_assign(&tree, keys, values, 36);
  if (st != DS_OK)
    goto done;
  st = show_shape(&tree);
  if (st != DS_OK)
    goto done;
  st = ds_jellyfish_get(&tree, 17, &borrowed);
  if (st != DS_OK)
    goto done;
  memcpy(&found, borrowed, sizeof(found));
  printf("Lookup 17: %d\n", found);
  /* Input is borrowed from the old shape; insertion stages it before rebuild.
   */
  st = ds_jellyfish_put(&tree, 40, borrowed);
  if (st != DS_OK)
    goto done;
  st = ds_jellyfish_remove(&tree, 0);
  if (st != DS_OK)
    goto done;
  printf("After insertion/removal:");
  st = ds_jellyfish_for_each(&tree, show_item, (void *)" ");
  putchar('\n');
done:
  ds_jellyfish_destroy(&tree);
  return st;
}
int main(void) {
  ds_status_t st;
  puts("Balanced binary body:");
  st = run(NULL);
  if (st != DS_OK)
    goto fail;
  puts("Caller-selected (a,b)-tree body routing:");
  st = run(ds_ordered_ab_ops());
  if (st != DS_OK)
    goto fail;
  return 0;
fail:
  fprintf(stderr, "Jelly-fish failed: %s\n", ds_status_name(st));
  return 1;
}
