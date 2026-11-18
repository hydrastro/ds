> Historical notes for the preceding repair pass. For the current work and
> remaining items, read `completion-notes.md` and `completion-plan.md`.

# Finalization pass after batch 23

Base: `f5d42b3a84882d374472415b4bb9c33326065e24` on `hydrastro/ds`.
Remote `master` still matched that commit when checked during this pass.

This pass repairs the specific remaining defects and implements the supplied
Result/i18n concepts in C. It does not certify every DADS mapping or establish
that no further defects remain. No tests were added or run; validation used
compilation, static analysis, source review and runnable examples.
Examples now link the normal strict-C89 `libds.a`, rather than the separately
compiled test-library archive.

## Repairs

| Area | Resulting behavior |
|---|---|
| HAMT | Full 32-bit collisions preserve every exact key in a leaf chain; routing uses the top two hash bits correctly; failed insertion preserves existing entries |
| ART | Full compressed prefixes, a terminal slot separate from byte zero, empty/binary keys, checked staged insertion, iterative walk/destruction using parent links |
| Bloom filter | Two byte hashes followed by separately mixed probes replace the correlated FNV scheme; item counters saturate rather than wrap |
| Signature files | Separately mixed default probes and one seed per custom-hash probe replace arithmetic-progression codewords; malformed word/length pairs are rejected |
| dlist | Insertion beside the sentinel updates both cached endpoints; clearing reconnects the sentinel to itself |
| B-tree | Empty extrema return the tree's nil sentinel without indexing an empty key array |
| B+ tree / B* | Checked insertion reserves all split nodes and scratch space before mutation; checked cloning rolls back when insertion cannot allocate |
| Radix trie | Checked insertion stages split nodes, edges and child storage; parent links remove the old bounded removal path; failed optional compression leaves a valid unary node |
| Aho-Corasick | Checked add stages missing paths privately; compile reserves its queue before replacing links; checked query APIs expose compilation failure |
| Rope | Reference-counted immutable AVL subtrees let edits prepare a replacement root before publication; concat requires matching allocator pairs; index/count arithmetic is checked |
| WAL | Recovery accepts a zero-entry commit that closes preceding full groups; first-format recovery requires a checksummed initialization marker or an empty log |
| LSM | A zero-filled foreign page is no longer mistaken for a torn initialization marker; incomplete unidentifiable magic is refused |

The ART trades parent pointers and full-prefix storage for iterative traversal
and reliable path compression. Optional shrinking uses hysteresis and can be
skipped on allocation failure. Rope edits trade temporary path allocations and
reference counts for recoverable failure; explicit rebalance additionally
repackages leaf fragments. These are implementation tradeoffs, not claims that
every workload became faster or smaller.

WAL and LSM preserve recognizable interrupted initialization without guessing
that arbitrary short files are unformatted. A first marker whose identifying
bytes never reached storage cannot safely be distinguished from a foreign file
and is rejected. Existing valid WAL superblocks and committed transactions
remain readable. Durable power-loss guarantees still depend on the underlying
store's sync contract.

## Result and i18n

The new implementation includes explicit success/error tags, NULL payloads on
either branch, borrowed and schema-owned payloads, shared owners and subviews,
immutable diagnostic snapshots, map/bind/recovery, and non-consuming emission.
Diagnostics use the existing RRB vector with configurable branching and context.
Collector snapshots own temporary strings and typed arguments; pointer pointees
remain borrowed. Void-sink collection failures are recorded rather than hidden.

Localization uses locale/domain/key catalogs on the map capability, an optional
external provider, formatter callbacks, single-pass substitution, quiet
diagnostic fallbacks and viewer filtering/severity overrides. [result-i18n.md](result-i18n.md) describes the APIs, ownership,
composition and presentation contracts.

## Compatibility

- Rebuild the library and every consumer. Public `ds_diagnostics_t`,
  `ds_radix_trie_node_t` and `ds_rope_node_t` layouts changed. Do not mix new
  headers with old binaries or persist raw C struct images.
- Existing function names, legacy wrappers and `ds_result_ptr_t` /
  `ds_result_size_t` remain. Use checked `_ex` operations to distinguish
  allocation failures from an update, duplicate or no-match condition.
- ART/HAMT/radix insertion returns `DS_EXISTS` after replacing a value;
  B+ insertion returns `DS_EXISTS` without replacing a duplicate record.
- Regenerate raw bit arrays saved by applications using `lib/bloom_filter` or
  `lib/signature_file`: the probe mapping changed. Signature custom hashes now
  receive seeds `0..k-1`, each producing one probe. The separate AMQ image and
  SSTable Bloom implementations/formats were not changed by this repair.
- Owning Results and payload handles must use copy/move/release, not raw struct
  assignment. Configs, contexts and callback state must outlive retained owners.
  Diagnostic `items` and tree internals are read-only views to callers.
- A rope concat between different allocator/deallocator pairs is rejected.
  Its legacy void wrapper leaves both inputs unchanged in that case.
- `LICENSE` and `CODE_OF_CONDUCT` already match `hydrastro/dw` at
  `7a64bb9b8043bc68186b330771c4b9478147086f`; both were compared byte for byte
  and left unchanged.

## Validation

On the available Linux C/C++ toolchain:

| Check | Outcome |
|---|---|
| `make -j4 all examples check-headers check-noinline` | Passed: static/shared libraries, all 49 examples, installed C89 headers and strict warnings without optimization |
| Standalone C++17 compilation of installed headers | All 265 passed |
| Four new examples built as C89 and executed | All completed successfully |
| Library build with AddressSanitizer and UBSan, then the four examples | All completed with both checks active; no reported address/UB errors |
| LeakSanitizer | Unavailable: the host's process tracing causes its fatal ptrace diagnostic; those runs were repeated with only leak detection disabled |
| GCC `-fanalyzer` on 16 changed/new library translation units | One ART diagnostic, reviewed below; the other 15 units reported none |
| clang-format 18.1.8 on changed C sources/headers; `git diff --check` | Passed |

The ART analyzer warning is a possible NULL reference returned by `art_ref`
during deletion. Its reported path assumes a root leaf has a non-NULL parent
and that a linked node is absent from its parent's edge slot. The opaque ART
maintains the opposite invariant on construction, split, resize and replacement:
the root has no parent, and every other node is named by its parent. Source
review found the reported path infeasible under that invariant. The warning was
not suppressed; this assessment is not a whole-library proof of correctness.

The four new examples are:

- `examples/result_i18n.c`: owned payload composition, ordered diagnostics,
  repeated English/Italian rendering, formatter callbacks and a NULL error.
- `examples/binary_key_maps.c`: empty keys, long shared prefixes, NUL suffixes,
  prefix removal and two distinct keys with the same FNV-1a hash.
- `examples/text_editing.c`: rope edits and concat, Aho-Corasick matching and
  B* indexing of 120 matches with a range query.
- `examples/filter_sizing.c`: observable false-positive/candidate measurements
  for documented deterministic inputs, without pass/fail statistical thresholds.

Measured in the filter example (2,000 inserted Bloom keys, 50,000 absent probes):

| Target | Observed false positives |
|---|---|
| 5% | 2,531 / 50,000 = 5.062% |
| 1% | 474 / 50,000 = 0.948% |
| 0.1% | 60 / 50,000 = 0.12% |

For 200 signature records, each with eight words, 128 bits and four probes per
word, 1,000 absent single-word queries produced 486 candidates among 200,000
record/query pairs: 0.243%, versus the density-based estimate of about 0.248%.
These results describe one workload. They do not guarantee a false-positive
rate for all keys, especially adversarial inputs. Neither filter is cryptographic.

Compilation and examples do not cover every failure point, operation sequence,
word width, storage device or callback implementation. Crash behavior and
allocation rollback in this pass also rely on source-level reasoning; no new
random model, fault-injection or regression tests were run.

## Work still open

- Out-of-core SSTable reads: opening an LSM tree still loads tables into memory.
- Addressable min-max and shadow-heap backends for `pq_iface`.
- A namespace migration policy for the original unprefixed exported symbols.
- Legacy callback/user-context and status API migrations beyond the checked
  operations listed above; not every old mutation now tolerates exhaustion.
- Detailed DADS mapping audits and further correctness review of other modules.
- Optional catalog-file adapters and locale-data packages. Pluralization,
  number/date rules and catalog editing are separate extension policies.

The coverage map remains an inventory of claimed implementations, not evidence
that every mapped term has identical semantics or complexity to its reference.
