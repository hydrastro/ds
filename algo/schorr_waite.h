#ifndef DS_ALGO_SCHORR_WAITE_H
#define DS_ALGO_SCHORR_WAITE_H

/*
 * Schorr-Waite graph marking (Deutsch-Schorr-Waite): marks every node
 * reachable from the roots without a stack or recursion, by reversing the
 * pointer it followed so the way back is stored in the graph itself, and
 * restoring every pointer on the way back. The classic garbage-collector
 * mark phase for memory that cannot afford a mark stack.
 *
 * The graph is n nodes with `fields` pointer slots each:
 * child[v * fields + i] is a node index or DS_SCHORR_WAITE_NONE. Each node
 * needs a small counter (which field is being explored), kept in the
 * caller's state array: 0 means unmarked; marked nodes end at fields + 1.
 * Nodes already marked (non-zero) are not entered again, so several calls
 * mark the union. With fields = 2 this is the original binary algorithm
 * (one tag bit per node suffices there).
 *
 * The child array is modified during the call and restored before it
 * returns; the optional visitor sees each node when it is first marked.
 */

#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_SCHORR_WAITE_NONE (~(size_t)0)

/* Marks from each of the roots; *out_marked (may be NULL) counts the nodes
 * newly marked. */
ds_status_t ds_schorr_waite_mark(size_t *child, size_t n, size_t fields,
                                 const size_t *roots, size_t root_count,
                                 size_t *state,
                                 void (*visit)(size_t node, void *user),
                                 void *user, size_t *out_marked);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_SCHORR_WAITE_H */
