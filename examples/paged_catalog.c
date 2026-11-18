/* Persistent catalog with on-demand SSTable blocks and configurable paging.
 * Usage: paged_catalog STORE.pages. Creates or reopens this example's store. */
#include "lib/lsm_tree.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  ds_page_store_t store;
  ds_lsm_config_t config;
  ds_lsm_t catalog;
  ds_lsm_iter_t it;
  ds_status_t st;
  const unsigned char *value;
  size_t value_len, i;
  char key[32], item[80];
  if (argc != 2) {
    fprintf(stderr, "usage: %s STORE.pages\n", argv[0]);
    return 1;
  }
  st = ds_page_store_file(&store, argv[1], 512, false, NULL);
  if (st != DS_OK)
    return 1;
  ds_lsm_config_init(&config);
  config.store = &store;
  config.out_of_core = true;
  config.read_cache_frames = 2;
  config.block_size = 256;
  config.memtable_bytes = 1024;
  config.l0_tables = 3;
  config.max_table_metadata_bytes = 65536;
  config.max_table_block_bytes = 4096;
  st = ds_lsm_init(&catalog, &config);
  if (st != DS_OK)
    goto close_store;
  for (i = 0; i < 200 && st == DS_OK; i++) {
    sprintf(key, "book/%04lu", (unsigned long)i);
    sprintf(item, "Book %lu: available", (unsigned long)i);
    st = ds_lsm_put(&catalog, key, strlen(key), item, strlen(item));
  }
  if (st == DS_OK)
    st = ds_lsm_put(&catalog, "book/0042", 9, "reserved", 8);
  if (st == DS_OK)
    st = ds_lsm_delete(&catalog, "book/0198", 9);
  if (st == DS_OK)
    st = ds_lsm_compact_all(&catalog);
  ds_lsm_destroy(&catalog);
  if (st != DS_OK)
    goto close_store;
  /* Reopen from the manifest: table data remains in the page store. */
  st = ds_lsm_init(&catalog, &config);
  if (st != DS_OK)
    goto close_store;
  st = ds_lsm_get(&catalog, "book/0042", 9, &value, &value_len);
  if (st == DS_OK)
    printf("book/0042 after reopen: %.*s\n", (int)value_len, value);
  memset(&it, 0, sizeof(it));
  if (st == DS_OK)
    st = ds_lsm_iter_init(&it, &catalog);
  if (st == DS_OK)
    st = ds_lsm_iter_seek(&it, "book/0196", 9);
  while (st == DS_OK && ds_lsm_iter_valid(&it)) {
    printf("%.*s: %.*s\n", (int)it.key_len, it.key, (int)it.value_len,
           it.value);
    st = ds_lsm_iter_next(&it);
  }
  ds_lsm_iter_destroy(&it);
  ds_lsm_destroy(&catalog);
close_store:
  ds_page_store_close(&store);
  if (st != DS_OK)
    fprintf(stderr, "%s\n", ds_status_name(st));
  return st == DS_OK ? 0 : 1;
}
