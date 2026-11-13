/*
 * Approximate-membership filters as values: saved as images, loaded back,
 * and carried inside SSTables and LSM trees.
 *
 * Any lib/amq_iface backend that supports images can be written to bytes
 * and restored after a full check. An SSTable stores such an image as its
 * filter block (format version 3) and an LSM tree builds every table's
 * filter with the backend in its configuration.
 */
#include "ds.h"
#include <stdio.h>
#include <string.h>

static unsigned long key_hash(unsigned long i) { return i * 2654435761UL + 1; }

static void filter_images(void) {
  ds_amq_t f;
  ds_amq_t g;
  unsigned char image[4096];
  size_t len = 0;
  unsigned long i;
  size_t hits = 0;
  if (ds_amq_init(&f, ds_amq_quotient(), 200, 10, NULL) != DS_OK) {
    return;
  }
  for (i = 0; i < 200; i++) {
    (void)ds_amq_insert(&f, key_hash(i));
  }
  if (ds_amq_save(&f, image, sizeof(image), &len) == DS_OK &&
      ds_amq_load(&g, image, len, NULL) == DS_OK) {
    for (i = 0; i < 200; i++) {
      hits += ds_amq_contains(&g, key_hash(i)) ? 1u : 0u;
    }
    printf("quotient filter image: %lu bytes, %lu of 200 keys found again\n",
           (unsigned long)len, (unsigned long)hits);
    ds_amq_destroy(&g);
  }
  image[len / 2] ^= 1; /* a flipped bit fails the checksum */
  printf("corrupted image loads: %s\n",
         ds_amq_load(&g, image, len, NULL) == DS_OK ? "yes" : "no");
  ds_amq_destroy(&f);
}

static void filtered_table(void) {
  ds_sstable_options_t o;
  ds_sstable_builder_t b;
  ds_sstable_t t;
  unsigned char *data = NULL;
  size_t len = 0;
  char key[16];
  int i;
  int false_hits = 0;
  ds_sstable_options_init(&o);
  o.filter = ds_amq_xor(); /* a static filter suits an immutable table */
  o.filter_fp_bits = 12;
  if (ds_sstable_builder_init_opts(&b, &o, NULL) != DS_OK) {
    return;
  }
  for (i = 0; i < 1000; i++) {
    sprintf(key, "key%05d", 2 * i);
    (void)ds_sstable_builder_add(&b, key, strlen(key), "v", 1);
  }
  if (ds_sstable_builder_finish(&b, &data, &len) == DS_OK &&
      ds_sstable_open_ex(&t, data, len, NULL, NULL, NULL) == DS_OK) {
    for (i = 0; i < 1000; i++) {
      sprintf(key, "key%05d", 2 * i + 1); /* never added */
      false_hits += ds_sstable_may_contain(&t, key, strlen(key)) ? 1 : 0;
    }
    printf("SSTable with a xor filter: %lu bytes, version %u, %d of 1000 "
           "absent keys pass the filter\n",
           (unsigned long)len, t.version, false_hits);
    ds_sstable_close(&t);
  }
  ds_sstable_builder_destroy(&b);
  ds_context_free(NULL, data);
}

static void filtered_lsm(void) {
  ds_lsm_config_t c;
  ds_lsm_t l;
  char key[16];
  const unsigned char *v;
  size_t vl;
  int i;
  ds_lsm_config_init(&c);
  c.filter = ds_amq_cuckoo(); /* deletable fingerprints, 16 bits each */
  c.memtable_bytes = 4096;
  if (ds_lsm_init(&l, &c) != DS_OK) {
    return;
  }
  for (i = 0; i < 5000; i++) {
    sprintf(key, "user%04d", i);
    (void)ds_lsm_put(&l, key, strlen(key), &i, sizeof(i));
  }
  printf("LSM tree with cuckoo filters: %lu tables after %lu flushes",
         (unsigned long)ds_lsm_table_count(&l), l.flushes);
  if (ds_lsm_get(&l, "user1234", 8, &v, &vl) == DS_OK && vl == sizeof(int)) {
    int x;
    memcpy(&x, v, sizeof(x));
    printf(", user1234 -> %d", x);
  }
  printf("\n");
  ds_lsm_destroy(&l);
}

int main(void) {
  filter_images();
  filtered_table();
  filtered_lsm();
  return 0;
}
