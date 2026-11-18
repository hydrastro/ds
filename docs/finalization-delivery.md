# Finalization delivery

Completed 18 November 2026. Base commit:
`b983d99dc7e01d83ac51da518ed7b747d4eb82fd` in `hydrastro/ds`.

All twelve tracked items in `docs/completion-plan.md` are complete within their
specified scope. Changes are committed locally on `codex/complete-open-work`;
they have not been pushed to the remote repository. The bundle identifies the
exact delivery commit and retains its complete history.

## What this completes

- Standalone Result/i18n contracts; checked history archives and keyed merging;
  atomic catalog adapters, exact-decimal plural selection and locale policies.
- Addressable min-max/shadow priority-queue backends, plus checked legacy heap
  policies and addressable binary-heap repair/removal.
- Callback-backed SSTables and optional out-of-core LSM reads with bounded
  metadata/block caches, read-failure propagation and preserved formats.
- Canonical `ds_` exports, optional compatibility names and a migration guide.
- Context ownership and checked callback/status paths throughout the legacy
  families, while retaining their source entry points. Intrusive records remain
  caller-controlled. Clone callbacks support explicit rollback and user data.
- Iterative replacements for previously unbounded internal recursion; explicit
  iterator, borrowed-buffer, persistence and compaction contracts.
- Persistent compressed radix storage with selectable backends.
- Reconciled DADS inventory and corrected claims about restricted/reference
  implementations. Newly delivered capabilities include logic/path systems,
  bounded quantum references, executor scans, hypergraphs, multi-commodity flow,
  Karnaugh maps, hash-suffix differential scripts and a jelly-fish reference.

The final API work additionally fixes bitvector allocation rounding, Fenwick
index wraparound, failed skip-list insertion changing its seed, allocator
ownership during string swap, unsafe string self-adoption, and signed overflow
in segment-tree arithmetic. Segment-tree operations journal touched nodes and
roll back on failure. New hash/trie clone callbacks distinguish a successful
NULL payload from cloning failure. A checked generic-trie clone can change the
child-store backend. Pairing/Fibonacci meld rejects self-meld and mismatched
ordering policies before moving records.

The full implementation record is `docs/completion-notes.md`; per-family entry
points, costs and ownership rules are in `docs/legacy-api-migration.md`.

## Validation actually performed

| Check | Observed result |
|---|---|
| Strict C89 static/shared library build | Passed |
| All installed headers compiled independently | Passed |
| Every production source compiled with strict warnings and no inlining | Passed |
| Ordinary examples | All 76 completed successfully |
| Fresh ASan/UBSan build | All 273 production translation units rebuilt |
| Examples against the fresh sanitizer archive | All 76 completed successfully, no diagnostics |
| GCC analyzer on the final migration | Zero diagnostics in all 32 changed implementation units |
| Separate build with `DS_NO_LEGACY_NAMES` | Libraries and all examples compiled |
| Export inspection in that build | 4,201 exports; none without `ds_` |
| Focused examples against that build | Five completed successfully |
| Whitespace/error checks | `git diff --check` passed |

The sanitizer sweep caught a missing initialization in the embedded pairing-heap
adapter, which was repaired. It also exposed a stale instrumented consumer after
a config layout change; the entire sanitizer archive was rebuilt before the
successful final sweep. LeakSanitizer was disabled because of this host's tracing
constraint. No tests were added, changed or run.

The delivery patch is separately applied to a pristine checkout of the base and
its resulting Git tree is compared with the delivered commit. The result is
recorded in the accompanying `validation-summary.json`.

## Deliberate limits

DADS has 1,306 reconciled indexed terms here: 771 implemented, 522 definition-only
or not applicable, five reference implementations, seven restricted and one
variant. No currently identified indexed capability remains missing. Those
labels describe implemented scope; they are not a proof of every algorithm's
correctness or a claim to contain every possible data structure and algorithm.

Public structures have grown: **recompile all consumers**. Legacy convenience
APIs retain documented preconditions; use checked alternatives for explicit
failure reporting. Contexts and callback state must outlive their containers.
Custom trie stores without a `next` operation keep their legacy recursive path;
the new checked walk/clone/destruction APIs require `next`.

Segment-tree node sums/lazy tags must fit `long`; deferred child sums can fail
when later materialized. LP/MILP remains floating point, without exact numerical
certificates. Bounded/reference algorithms retain their stated limits. LSM
builders and compaction output still use memory even when reads are out of core;
actual fsync/truncation guarantees require the POSIX/WAL stores described in the
storage contracts. Builds and examples do not exhaust allocation failures,
operation sequences, custom policies or platform portability.

## Apply and commit the patch

Use a clean checkout at the base above, or first establish that the patch applies
to your current branch. These commands do not publish anything until the final
`git push` command:

```sh
git switch -c ds-finalization
git apply --check /path/to/ds-finalization.patch
git apply /path/to/ds-finalization.patch
git add -A
git commit -m "Complete data-structure library finalization"
git push -u origin ds-finalization
```

**`git add` stages files; `git commit` is required before `git push` can publish
those changes.** If `git apply --check` reports conflicts, reconcile the branch
with the documented base before applying the patch.

To rebuild without running tests:

```sh
make -j3 all examples
make check-headers check-noinline
```

Alternatively, recover the full committed tree from the bundle:

```sh
git clone -b codex/complete-open-work ds-finalization.bundle ds-finalized
```

The ZIP includes the source tree, the patch, this note, the bundle and validation
metadata. The source tree includes cached Unicode data for an offline build.
The `.tar.gz` recovery package contains the same materials.
