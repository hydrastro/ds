/* Write an immutable catalog, then query it through a bounded read callback.
 * Usage: sstable_reader OUTPUT.sst (replaces this example's output file). */
#include <dsa/structures/sstable.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct catalog_file {
  FILE *file;
  size_t bytes;
  size_t reads;
} catalog_file_t;

static ds_status_t read_catalog(void *user, size_t offset, void *out,
                                size_t count) {
  catalog_file_t *source = (catalog_file_t *)user;
  if (offset > (size_t)LONG_MAX) {
    return DS_ERR_OVERFLOW;
  }
  if (fseek(source->file, (long)offset, SEEK_SET) != 0 ||
      fread(out, 1, count, source->file) != count) {
    return DS_ERR_STATE;
  }
  source->bytes += count;
  source->reads++;
  return DS_OK;
}

int main(int argc, char **argv) {
  ds_sstable_builder_t builder;
  ds_sstable_source_t source;
  ds_sstable_t table;
  ds_sstable_iter_t it;
  catalog_file_t file;
  unsigned char *image = NULL;
  size_t length = 0, i, value_len;
  const unsigned char *value;
  bool tombstone;
  ds_status_t st;
  char key[32], item[64];
  if (argc != 2) {
    fprintf(stderr, "usage: %s OUTPUT.sst\n", argv[0]);
    return 1;
  }
  memset(&table, 0, sizeof(table));
  memset(&it, 0, sizeof(it));
  memset(&file, 0, sizeof(file));
  st = ds_sstable_builder_init(&builder, 256, 4, 10, NULL);
  if (st != DS_OK) {
    return 1;
  }
  for (i = 0; i < 120 && st == DS_OK; i++) {
    sprintf(key, "book/%04lu", (unsigned long)i);
    sprintf(item, "Catalog entry %lu", (unsigned long)i);
    st =
        ds_sstable_builder_add(&builder, key, strlen(key), item, strlen(item));
  }
  if (st == DS_OK) {
    st = ds_sstable_builder_finish(&builder, &image, &length);
  }
  ds_sstable_builder_destroy(&builder);
  if (st != DS_OK) {
    goto done;
  }
  file.file = fopen(argv[1], "wb+");
  if (file.file == NULL) {
    st = DS_ERR_STATE;
    goto done;
  }
  if (fwrite(image, 1, length, file.file) != length ||
      fflush(file.file) != 0) {
    st = DS_ERR_STATE;
    goto done;
  }
  ds_context_free(NULL, image);
  image = NULL;
  ds_sstable_source_init(&source);
  source.read = read_catalog;
  source.user = &file;
  source.length = length;
  source.max_metadata_bytes = 4096;
  source.max_block_bytes = 1024;
  st = ds_sstable_open_reader(&table, &source, NULL, NULL, NULL);
  if (st != DS_OK) {
    goto done;
  }
  printf("Opened %lu-byte catalog: %lu blocks, checksum streamed\n",
         (unsigned long)length, (unsigned long)ds_sstable_blocks(&table));
  file.bytes = file.reads = 0;
  st = ds_sstable_get(&table, "book/0042", 9, &value, &value_len, &tombstone,
                      NULL);
  if (st != DS_OK) {
    goto done;
  }
  printf("book/0042 = %.*s (%lu bytes read)\n", (int)value_len, value,
         (unsigned long)file.bytes);
  st = ds_sstable_iter_init(&it, &table, NULL);
  if (st == DS_OK) {
    st = ds_sstable_iter_seek(&it, "book/0117", 9);
  }
  while (st == DS_OK && ds_sstable_iter_valid(&it)) {
    printf("%.*s: %.*s\n", (int)it.key_len, it.key, (int)it.value_len,
           it.value);
    st = ds_sstable_iter_next(&it);
  }
  /* Validate a newly published catalog before distributing it. */
  if (st == DS_OK) {
    st = ds_sstable_verify(&table, true, NULL);
  }
done:
  ds_sstable_iter_destroy(&it);
  ds_sstable_close(&table);
  ds_context_free(NULL, image);
  if (file.file != NULL && fclose(file.file) != 0 && st == DS_OK) {
    st = DS_ERR_STATE;
  }
  if (st != DS_OK) {
    fprintf(stderr, "%s\n", ds_status_name(st));
  }
  return st == DS_OK ? 0 : 1;
}
