/*
 * Crash-safe B+ tree: the paged B+ tree runs on a pager whose page store
 * is write-ahead logged (dsa/structures/wal_store.h), and the two stores under
 * the log act like disks with volatile write caches
 * (dsa/structures/fault_store.h over memory stores;
 * dsa/structures/posix_store.h would put real files there). Committing is
 * ds_pbtree_sync then ds_pager_flush. A power cut in the middle of a
 * transaction, even one that tears a page, loses that transaction and
 * nothing else: the tree reopens as of the last commit, intact.
 */
#include <dsa.h>
#include <stdio.h>

#define PAGE 256

typedef struct db {
  ds_page_store_t data_disk, log_disk; /* what survives a power cut */
  ds_page_store_t data, log;           /* the disks behind their caches */
  ds_page_store_t wal;
  ds_pager_t pager;
  ds_pbtree_t tree;
} db_t;

static ds_status_t db_open(db_t *db) {
  /* recovery replays the commits the log holds; by default a sync
   * checkpoints once the log reaches 1024 pages */
  ds_status_t st = ds_page_store_wal(&db->wal, &db->data, &db->log, NULL, NULL);
  if (st != DS_OK) {
    return st;
  }
  st = ds_pager_init(&db->pager, &db->wal, 16, NULL, NULL);
  if (st == DS_OK) {
    st = ds_pbtree_open(&db->tree, &db->pager, sizeof(unsigned long));
    if (st != DS_OK) {
      ds_pager_discard(&db->pager);
    }
  }
  if (st != DS_OK) {
    ds_page_store_close(&db->wal);
  }
  return st;
}

static ds_status_t db_commit(db_t *db) {
  ds_status_t st = ds_pbtree_sync(&db->tree);
  return st == DS_OK ? ds_pager_flush(&db->pager) : st;
}

/* the power fails: the process's memory is gone, and of the writes the
 * disks had not synced, some landed and one was torn */
static void db_power_cut(db_t *db, unsigned long seed) {
  ds_pager_discard(&db->pager);
  ds_page_store_close(&db->wal);
  (void)ds_fault_store_crash(&db->data, DS_FAULT_CRASH_TORN, seed);
  (void)ds_fault_store_crash(&db->log, DS_FAULT_CRASH_TORN, seed + 1);
}

static unsigned long count_range(db_t *db, unsigned long lo, unsigned long hi) {
  unsigned long n = 0, k, v;
  for (k = lo; k < hi; k++) {
    if (ds_pbtree_get(&db->tree, k, &v) == DS_OK && v == k * 10) {
      n++;
    }
  }
  return n;
}

int main(void) {
  db_t db;
  ds_wal_store_stats_t ws;
  unsigned long k, round;
  int ok = 1;
  if (ds_page_store_memory(&db.data_disk, PAGE, NULL) != DS_OK ||
      ds_page_store_memory(&db.log_disk, PAGE, NULL) != DS_OK ||
      ds_page_store_fault(&db.data, &db.data_disk, NULL, NULL, NULL) != DS_OK ||
      ds_page_store_fault(&db.log, &db.log_disk, NULL, NULL, NULL) != DS_OK ||
      db_open(&db) != DS_OK) {
    return 1;
  }
  for (round = 0; round < 5; round++) {
    unsigned long base = round * 1000;
    /* a committed batch */
    for (k = base; k < base + 500; k++) {
      unsigned long v = k * 10;
      if (ds_pbtree_put(&db.tree, k, &v) != DS_OK) {
        return 1;
      }
    }
    if (db_commit(&db) != DS_OK) {
      return 1;
    }
    /* an uncommitted batch: the pager evicts some of its pages into the
     * log, but no commit record follows them */
    for (k = base + 500; k < base + 1000; k++) {
      unsigned long v = k * 10;
      if (ds_pbtree_put(&db.tree, k, &v) != DS_OK) {
        return 1;
      }
    }
    for (k = 0; k < base; k += 3) {
      (void)ds_pbtree_remove(&db.tree, k); /* also lost */
    }
    db_power_cut(&db, round + 1);
    if (db_open(&db) != DS_OK) {
      printf("round %lu: the tree did not reopen\n", round);
      return 1;
    }
    if (ds_pbtree_verify(&db.tree, NULL) != DS_OK ||
        ds_pbtree_size(&db.tree) != 500 * (round + 1) ||
        count_range(&db, 0, base + 1000) != 500 * (round + 1)) {
      ok = 0;
    }
    (void)ds_wal_store_stats(&db.wal, &ws);
    printf("power cut %lu: %lu entries, every committed one intact "
           "(commits replayed from the log: %lu)\n",
           round + 1, ds_pbtree_size(&db.tree), ws.recovered);
  }
  if (db_commit(&db) != DS_OK || ds_wal_store_checkpoint(&db.wal) != DS_OK) {
    return 1;
  }
  (void)ds_wal_store_stats(&db.wal, &ws);
  printf("checkpointed: epoch %lu, the log is back to %lu pages, the data "
         "store holds %lu\n",
         ws.epoch, ws.log_pages, ds_page_store_count(&db.data_disk));
  printf("%s\n", ok ? "all checks passed" : "CHECK FAILED");
  if (ds_pager_close(&db.pager) != DS_OK) {
    return 1;
  }
  ds_page_store_close(&db.wal);
  ds_page_store_close(&db.data);
  ds_page_store_close(&db.log);
  ds_page_store_close(&db.data_disk);
  ds_page_store_close(&db.log_disk);
  return ok ? 0 : 1;
}
