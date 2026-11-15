/*
 * A key-value store that survives power cuts: the LSM tree keeps its
 * tables, manifest and write-ahead log in a page store. The store here is
 * lib/fault_store over the in-memory one, which acts like a disk with a
 * volatile write cache: writes wait in the cache until a sync, and a power
 * cut throws away the cache (or, worse, keeps a random part of it). After
 * every cut the tree is reopened from what reached "the disk", and every
 * update that a completed ds_lsm_sync or flush covered is still there.
 */
#include "ds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE 512

static unsigned long seed = 7;
static unsigned long next(void) {
  seed = (seed * 1103515245ul + 12345ul) & 0xFFFFFFFFul;
  return seed >> 8;
}

#define KEYS 200
#define HIST 64

/* every value a key has held since the last sync (-1: absent); after a
 * power cut it must hold one of them */
static long hist[KEYS][HIST];
static int nh[KEYS];

static void record(int k, long v) {
  if (nh[k] < HIST) {
    hist[k][nh[k]++] = v;
  }
}

static void synced_now(void) {
  int k;
  for (k = 0; k < KEYS; k++) {
    hist[k][0] = hist[k][nh[k] - 1];
    nh[k] = 1;
  }
}

int main(void) {
  ds_page_store_t platter; /* what survives */
  ds_page_store_t store;   /* the platter behind a volatile cache */
  ds_lsm_config_t cfg;
  ds_lsm_t kv;
  int cut, k, lost = 0, kept = 0, ok = 1;
  if (ds_page_store_memory(&platter, PAGE, NULL) != DS_OK) {
    return 1;
  }
  if (ds_page_store_fault(&store, &platter, NULL, NULL, NULL) != DS_OK) {
    ds_page_store_close(&platter);
    return 1;
  }
  ds_lsm_config_init(&cfg);
  cfg.store = &store;
  cfg.memtable_bytes = 2048; /* small, so tables are flushed and merged */
  for (k = 0; k < KEYS; k++) {
    hist[k][0] = -1;
    nh[k] = 1;
  }
  for (cut = 0; cut < 25; cut++) {
    int op;
    if (ds_lsm_init(&kv, &cfg) != DS_OK) {
      printf("the store could not be opened\n");
      return 1;
    }
    /* every key holds its last synced value, or a later one */
    for (k = 0; k < KEYS; k++) {
      char key[16];
      const unsigned char *v;
      size_t vl;
      long got = -1;
      int i, found = -1;
      sprintf(key, "key%03d", k);
      if (ds_lsm_get(&kv, key, strlen(key), &v, &vl) == DS_OK) {
        char buf[32];
        memcpy(buf, v, vl < 31 ? vl : 31);
        buf[vl < 31 ? vl : 31] = '\0';
        got = atol(buf);
      }
      for (i = nh[k] - 1; i >= 0 && found < 0; i--) {
        if (hist[k][i] == got) {
          found = i;
        }
      }
      if (found < 0 && nh[k] < HIST) {
        ok = 0;
      }
      if (found == 0 && nh[k] > 1) {
        lost++;
      } else if (found > 0) {
        kept++;
      }
      hist[k][0] = got;
      nh[k] = 1;
    }
    /* a burst of updates, synced now and then, then the power fails */
    for (op = 0; op < 300; op++) {
      char key[16], val[32];
      k = (int)(next() % KEYS);
      sprintf(key, "key%03d", k);
      if (next() % 5 == 0) {
        if (ds_lsm_delete(&kv, key, strlen(key)) != DS_OK) {
          return 1;
        }
        record(k, -1);
      } else {
        long v = (long)(next() % 100000);
        sprintf(val, "%ld", v);
        if (ds_lsm_put(&kv, key, strlen(key), val, strlen(val)) != DS_OK) {
          return 1;
        }
        record(k, v);
      }
      if (next() % 40 == 0 && ds_lsm_sync(&kv) == DS_OK) {
        synced_now();
      }
    }
    ds_lsm_destroy(&kv);
    /* the power fails: some cached writes made it, in order, the rest not */
    (void)ds_fault_store_crash(&store, DS_FAULT_CRASH_SUBSET, next());
  }
  printf("25 power cuts: every key came back with its last synced value or "
         "a later one (%s); %d keys lost unsynced updates, %d kept some "
         "(flushes made them durable)\n",
         ok ? "checked" : "VIOLATED", lost, kept);
  printf("the disk holds %lu pages of %d bytes\n",
         ds_page_store_count(&platter), PAGE);
  ds_page_store_close(&store);
  ds_page_store_close(&platter);
  return ok ? 0 : 1;
}
