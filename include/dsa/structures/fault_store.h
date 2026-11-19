#ifndef DS_FAULT_STORE_H
#define DS_FAULT_STORE_H

/*
 * A page store that simulates a disk under faults: it stacks on another
 * store (the "disk"), keeps every write and truncation since the last sync
 * in memory so that a crash can be simulated at any moment, and fails
 * operations on demand. Storage stacks (the pager and the paged B+ tree,
 * the LSM tree, the write-ahead-logged store of dsa/structures/wal_store.h)
 * can then be tested for what they promise after a crash.
 *
 * Volatile state. write and truncate are recorded in order, reads see
 * them, and the page count follows them. sync applies them to the base
 * store in order and syncs it; if the base store fails on the way, the
 * operations not yet applied stay for the next sync.
 *
 * Crashes. ds_fault_store_crash applies part of the volatile state to the
 * base store and drops the rest, as a power loss would:
 *
 *   DS_FAULT_CRASH_LOSE    nothing since the last sync reaches the disk
 *   DS_FAULT_CRASH_ALL     everything does (the crash came after the
 *                          writes, before the caller heard of them)
 *   DS_FAULT_CRASH_SUBSET  each operation independently with probability
 *                          1/2 (a generator seeded by the caller), in order
 *   DS_FAULT_CRASH_TORN    as SUBSET, and one write that reaches the disk
 *                          is torn: only a prefix of the page (a random
 *                          number of bytes, possibly none) replaces the
 *                          old contents
 *
 * Afterwards the store holds no volatile state and reads the base store,
 * as a reopened file would: discard the structures stacked on it and open
 * them again.
 *
 * Injected failures. A callback sees each operation before it happens:
 * its kind, its page (the new count for a truncation, 0 for a sync) and
 * how many operations of that kind there have been, this one included. It
 * returns DS_OK to let the operation through or the status the operation
 * fails with; a failed operation changes nothing.
 *
 * Reads look for the latest volatile write of a page by scanning the
 * pending operations backwards, so they cost O(pending) between syncs.
 * Memory: a copy of every pending write. The fault store does not own the
 * base store: close it after the fault store.
 */

#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <dsa/structures/pager.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_fault_op {
  DS_FAULT_READ = 0,
  DS_FAULT_WRITE = 1,
  DS_FAULT_SYNC = 2,
  DS_FAULT_TRUNCATE = 3
} ds_fault_op_t;

/* DS_OK lets the operation through; anything else is returned instead */
typedef ds_status_t (*ds_fault_fn)(ds_fault_op_t op, unsigned long page,
                                   unsigned long nth, void *user);

typedef enum ds_fault_crash {
  DS_FAULT_CRASH_LOSE = 0,
  DS_FAULT_CRASH_ALL = 1,
  DS_FAULT_CRASH_SUBSET = 2,
  DS_FAULT_CRASH_TORN = 3
} ds_fault_crash_t;

typedef struct ds_fault_stats {
  unsigned long ops[4];   /* operations seen, by ds_fault_op_t */
  unsigned long failures; /* operations the callback failed */
  unsigned long crashes;
  size_t pending; /* volatile operations */
} ds_fault_stats_t;

/* fault NULL: no injected failures */
ds_status_t ds_page_store_fault(ds_page_store_t *store, ds_page_store_t *base,
                                ds_fault_fn fault, void *user,
                                ds_context_t *context);
/* replaces the callback (NULL: none); DS_ERR_INVALID for another store */
ds_status_t ds_fault_store_set(ds_page_store_t *store, ds_fault_fn fault,
                               void *user);
/* simulates a crash; the first error of the base store, if any, is
 * returned after the volatile state has been dropped all the same */
ds_status_t ds_fault_store_crash(ds_page_store_t *store, ds_fault_crash_t mode,
                                 unsigned long seed);
ds_status_t ds_fault_store_stats(const ds_page_store_t *store,
                                 ds_fault_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_FAULT_STORE_H */
