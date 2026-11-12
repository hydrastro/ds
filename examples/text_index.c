/*
 * One text indexed three ways: an FM-index whose rank structure over the
 * BWT is chosen at build time. The answers are the same; the space and
 * the cost of each LF step differ:
 *
 *   ds_fm_occ_table()        sampled occurrence counts per symbol
 *   ds_fm_occ_wavelet()      a wavelet matrix of plain rank bit vectors
 *   ds_fm_occ_wavelet_rrr()  the same matrix over RRR-compressed levels
 *
 * The sampling rate trades locate speed (LF steps per occurrence) for the
 * size of the sampled suffix array. The index does not keep the text:
 * extract recovers any part of it.
 */
#include "ds.h"
#include <stdio.h>
#include <string.h>

static const char *text =
    "it was the best of times it was the worst of times it was the age of "
    "wisdom it was the age of foolishness it was the epoch of belief it was "
    "the epoch of incredulity it was the season of light it was the season "
    "of darkness";

int main(void) {
  const ds_fm_occ_ops_t *occ[3];
  const char *name[3] = {"occurrence table", "wavelet matrix",
                         "wavelet matrix (RRR)"};
  int k;
  occ[0] = ds_fm_occ_table();
  occ[1] = ds_fm_occ_wavelet();
  occ[2] = ds_fm_occ_wavelet_rrr();
  for (k = 0; k < 3; k++) {
    ds_fm_config_t cfg;
    ds_fm_index_t fm;
    size_t pos[16];
    size_t total = 0;
    size_t i;
    unsigned char back[24];
    ds_fm_config_init(&cfg);
    cfg.occ = occ[k];
    cfg.sa_sample = 8; /* a sampled suffix array entry every 8 rows */
    if (ds_fm_index_build(&fm, (const unsigned char *)text, strlen(text),
                          &cfg) != DS_OK) {
      return 1;
    }
    printf("%-22s %6lu bytes for %lu text bytes\n", name[k],
           (unsigned long)ds_fm_index_bytes(&fm), (unsigned long)fm.n);
    printf("  count(\"it was\") = %lu\n",
           (unsigned long)ds_fm_index_count(
               &fm, (const unsigned char *)"it was", 6, NULL, NULL));
    if (ds_fm_index_locate(&fm, (const unsigned char *)"epoch", 5, pos, 16,
                           &total) == DS_OK) {
      printf("  locate(\"epoch\"):");
      for (i = 0; i < total && i < 16; i++) {
        printf(" %lu", (unsigned long)pos[i]);
      }
      printf("\n");
    }
    if (ds_fm_index_extract(&fm, 100, 23, back) == DS_OK) {
      back[23] = '\0';
      printf("  extract(100, 23) = \"%s\"\n", (const char *)back);
    }
    ds_fm_index_destroy(&fm);
  }
  return 0;
}
