# Repair pass — 15 November 2026

Base: `5b28ae168235717973801cf1715ada9173ebeea6`.
Branch: `codex/repair-reviewed-contracts`.

This pass implements repairs from the third review and addresses additional
problems in the newly added persistent LSM. The result/monad representation and
API are unchanged. Existing tests are unchanged; no tests were added or run.

## Changes from the review

| Finding | Repair | Scope or tradeoff |
|---|---|---|
| R3-01: custom comparators and filters disagree about equality | SSTable builders omit raw-byte filters for custom comparators; readers bypass them, including in older images. This also fixes LSM lookups and tombstone lookup through those tables. | Custom-comparator lookups lose filter acceleration. A persisted, comparator-compatible hash policy is still a future extension. |
| R3-02: buffered writes undermine pager retries | The stdio store selects unbuffered I/O before any I/O. Short writes return errors while the pager still owns its dirty frame. Retry clears the stream error indicator. Opening an existing file no longer falls back to truncating creation on errors other than a missing file. | Successful writes are accepted by the OS, not necessarily durable against machine failure. The backend write contract now states what a pager can rely on. |
| R3-03: unsafe schema transfer | Validate both schemas before selecting a transfer service; reject an owned source with no destination copy callback. | Explicit conversion remains necessary for incompatible ownership policies. |
| R3-04: negative target heuristic in A* | Normalize the target heuristic to zero, including when source equals target. | Dijkstra, Prim and A* also propagate queue backend errors instead of interpreting every failed pop as exhaustion. |
| R3-05: rounded hull tie decisions | Graham and Jarvis compare coordinates along a collinear ray. Quickhull compares signed heights with an error filter and exact expansion fallback, with an extreme-point tie break. | Exactness retains the coordinate and floating-point environment requirements of `robust_predicates.h`. Hull workspace arithmetic is now checked too. |
| R3-06: cyclic B-tree iteration and failed-init cleanup | Keep a leaf budget across calls, reject non-increasing keys, and free the value buffer if settling during initialization fails. | Tree mutation invalidates iterators. |
| R3-07: trie traversal changes under allocation failure | Robin Hood snapshots store slice keys, and its allocation-free fallback scans successive keys. Vector stores maintain live cursors adjusted by insert/remove and need no snapshot. | The Robin Hood fallback is quadratic in child count. A visitor must detach nodes before freeing them and must not destroy a pending ancestor or the store being traversed. |
| R3-08: RRB overflow and self-copy | Reject append at maximum size before allocation; self-copy does not increment the reference count. | This does not add a general reference-count overflow policy. |
| R3-09: matching workspace overflow | Check all workspace byte counts before reading arrays, guard queue growth, and safely clean up partially allocated blossom arrays. | Bounds cover 32-bit arithmetic by construction; no 32-bit execution was performed. |
| R3-10: interrupted initial B-tree format | Write an explicit initialization marker, and let a subsequent open finish the empty root and publish normal metadata. Also tighten value-size arithmetic. | In-process retry, not crash recovery or repair of arbitrary damaged files. |
| R3-11: heap cost claims | Binomial insertion only follows its carry chain. Strict Fibonacci heaps cache the ceiling logarithm and update it in constant work on size changes and meld. | Removes the specific extra scans identified in the review; no benchmark or runtime complexity measurement was performed. |
| R3-12: infinite MSF weights poison totals | Reject nonfinite weights in dynamic and decremental MSF. Replacement removes the old weight before adding the new one, avoiding an unnecessarily overflowing `new-old` intermediate. | Finite running sums can still overflow or accumulate rounding error; this is documented. Forest membership does not use the running sum. |
| R3-13: list constructor allocation failures | Check both allocations and release the list if allocating its sentinel fails. Legacy cloning checks list creation. | New `ds_list_clone_ex` accepts a cleanup callback and rolls back fallible node cloning. The legacy callback still has a nonfailure precondition. |

## Additional persistent LSM repairs

- Return errors from automatic persistent flushes. A failed put/delete may already
  be visible in memory; the header now makes that explicit.
- Reject sync and flush on a tree whose manifest publication is ambiguous.
- Retry publication when a previous flush emptied the memtable but did not
  publish its tables.
- After a failed log sync, finish the backend sync before writing another log
  version that could overwrite the last successfully synced image.
- Abort recovery on page-store read errors. Checksum/format failures can still
  use the older manifest or identify an incomplete log tail.
- Check extent capacity arithmetic and reject overlapping runs immediately,
  including cyclic log chains. Check run bounds against the store before
  allocating read buffers.
- Check replay buffer growth. Avoid narrowing the extent count to `int`.
- Seal a log page before its 32-bit version wraps; reject exhausted log indexes
  and manifest sequences. Keep the unsynced-update counter from wrapping.
- Enforce the 32-bit page-size field and the 64-table level-zero fan-in. Permit
  the valid maximum of 64 level-zero tables plus all 24 levels in a manifest.

## Numerical policy and composition

Weighted matching now has `_ex` entry points, `ds_wmatch_options_t`, and a
reported `ds_wmatch_mode_t`. `require_exact` rejects nonintegral/out-of-range
inputs or a dual-range overflow instead of silently retrying in floating mode.
The original entry points retain automatic selection.

Minimum-weight perfect matching maximizes `-w` among maximum-cardinality
matchings. Negation preserves finite binary64 weights exactly and avoids the
overflow/cancellation in the previous positive-offset transformation. The
optional final weight calculation no longer allocates when no weight is
requested. Floating-mode weights and duals are constrained to a safe arithmetic
range; an unsafe range returns `DS_ERR_OVERFLOW`.

Matching's floating comparison tolerance is no longer described as a certified
objective-error bound. Christofides documentation now makes its exact-matching
assumption explicit instead of promising a 3/2 bound for arbitrary integer or
floating inputs to this implementation.

Other small changes:

- `ds_elias_fano_storage_bytes` accounts for allocated words, spare words and
  select directories; the rank/select adapter uses it.
- RRR word-boundary shifts explicitly exclude a zero bit offset, making their
  existing bounded-width precondition visible to static analysis.
- Ordered-map context documentation now says compatible contexts.
- The backward list walk documents its temporary link reversal and callback
  restrictions.
- Unicode generation uses grouped Make targets, so deleting both generated
  files while retaining their stamp cannot start competing generators.

## Examples

- `examples/owned_list_clone.c`: caller-owned intrusive layout, independent
  cloned strings, cleanup on a failed clone, and normal destruction.
- `examples/matching_policy.c`: require exact matching and report the numeric
  mode and chosen pairs.
- The existing intrusive-list example now handles constructor allocation
  failure.

## Validation performed

- `make -j4 all examples check-headers`: passed with the project's strict C89
  warnings, including `-Werror`; all 43 examples compiled and linked.
- All 259 installed headers compiled independently as C89. All 259 workspace
  headers also compiled independently as C++17 with pedantic errors and
  warnings treated as errors.
- Removed both generated Unicode outputs in each build directory while leaving
  both stamps in place, then ran `make -j8 all examples`: passed, exactly one
  generator invocation per output pair.
- GCC 14.2 `-fanalyzer` covered the 17 changed library/algorithm C files and
  three changed/new examples. No compiler failures or timeouts; no diagnostics
  in the library/algorithm files. The final LSM, trie-store and rank/select
  changes were analyzed again.
- One analyzer diagnostic remains in `owned_list_clone.c`: it reports the
  allocation as leaked when the callback returns an embedded node pointer.
  The pointer escapes to the list; `release_item` recovers the allocation with
  `offsetof` and frees it on rollback and normal destruction. This is a
  source-reviewed ownership-escape false positive, not a runtime result.
- `git diff --check`: passed.

No test binaries or example binaries were executed. Compilation and static
analysis do not establish runtime correctness or prove crash recovery. This is
a focused repair pass, not a claim that every module has been exhaustively
audited.

## Compatibility and remaining work

Rebuild clients: the exposed B-tree iterator, strict Fibonacci heap and LSM
struct layouts changed. Existing function entry points remain available;
checked cloning, matching policy and storage accounting APIs are additive.
GNU Make 4.3 or newer is now required for grouped generated outputs.

The stdio backend still does not provide durable machine-crash sync or persistent
truncation. The paged B-tree still lacks a WAL or other crash-atomic publication
mechanism. An LSM's first-ever initialization can still leave a partial store
after an I/O failure before its first valid manifest; automatic recovery of that
initial-format case needs a separate format protocol.

Legacy trie insert/clone callbacks still cannot fully report and roll back
allocation failures through their existing void/pointer-only contracts. The
traversal repairs here do not solve that broader API problem. General numeric
certification, arbitrary-range MSF sums, and comparator-compatible persisted
filter hashing also remain separate work.
