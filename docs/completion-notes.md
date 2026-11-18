# Completion pass

This pass starts at upstream `b983d99dc7e01d83ac51da518ed7b747d4eb82fd`.
That commit contains the preceding repair tree without changes. The active
scope and remaining work are tracked in `completion-plan.md`.

## Addressable heaps

Min-max and shadow heaps now expose indexed update, repair and removal plus
movement callbacks. The two new `pq_iface` backends reuse those cores and keep
handles and payload addresses stable. Cost metadata includes shadow scans and
folding. Shadow push stages a self-sourced item across growth; merge reserves
before exchanging storage and rejects inconsistent ordering callbacks.

## Callback SSTable reads

The existing three table formats can be opened through an immutable exact-read
callback. Opening streams checksum verification and retains index/filter
metadata. Iterators and point lookups hold separate reusable block buffers.
Metadata/block limits, error propagation and borrowed-view lifetimes are explicit.

LSM `out_of_core` mode uses a separate configurable pager for each persisted
table, including new flush/compaction results. Retiring a table retires its cache
before its pages can be reused. Ordinary source failures abort recovery even if
an underlying read reports `DS_ERR_INVALID`; only malformed images permit
fallback. Memory mode remains the default. Builders and compaction output still
use memory. The existing page-store durability guarantees and file formats remain.

LSM mutation arguments are now staged before replacement, fixing a use-after-free
when an array-memtable value is passed back into `put`.

## Evidence so far

- Strict C89 static/shared builds and all current examples compile.
- `addressable_schedulers`, `sstable_reader` and `paged_catalog` run successfully,
  both ordinarily and with AddressSanitizer/UndefinedBehaviorSanitizer.
- The file catalog example reads one 281-byte block for a point lookup in its
  3,220-byte image. LSM examples compact and reopen, retaining replacement and
  deletion semantics.
- GCC's analyzer reports no diagnostics in the five changed implementation
  units: min-max heap, shadow heap, PQ adapters, SSTable and LSM.
- LeakSanitizer is disabled because this host's tracing environment prevents it
  from running. No tests were added or run; examples are not exhaustive coverage.

Public structs grew. Recompile clients together with the updated library.

## Namespace

All implementation exports now use `ds_`. The 39 former public function
names remain as default compatibility wrappers, with a documented opt-out
using `DS_NO_LEGACY_NAMES`. Private generated Unicode data uses `ds_ucd_`.
The isolated prefixed-only C89 build and all examples compile; `nm` reports
zero unprefixed exports in that archive. Its hash-table, persistent-trie and
text-index examples run successfully. See `namespace.md` for exact mappings.

## Compressed persistent tries

The persistent trie now has selectable byte-edge and radix-label storage, keeping
one version API and the existing ownership policy. The radix core copies branch
paths, splits labels on insertion, and coalesces unary paths on removal. All
prefix operations work when the requested prefix ends inside a label.

Checked creation/update/copy/move/retain entry points report operational failures
and reject a base version owned by another trie. A checked clone callback supports
successful NULL payloads. Prefix-copy sizing and reference increments are checked.

The routing example runs both backends, preserving its snapshot while archiving
and moving prefixes and removing a route; `/binary` and `/binary\0` stay distinct.
It passes normal and ASan/UBSan execution. GCC's analyzer reports no diagnostics
for the changed implementation. These examples do not exhaust operation or
allocation-failure sequences.

## History archives and keyed merges

History storage now accepts a context, and checked branch/transaction/edit APIs
report allocation failure and ID/time exhaustion. Checked portable import stages
all branches and payloads, validates parent/ID/order relationships, enforces
optional resource limits, and uses configurable ordered maps for ID indexes.
Payload readers can distinguish a successful NULL value from an error.

`history_adapters` provides FILE serialization with sticky I/O errors and transfer
budgets. `fflush` is not a durability barrier; atomic file replacement and fsync
remain the caller's storage policy. The memory archive also accepts a context,
checks allocator pairs, exposes sticky status, and safely appends its own data.

The keyed merge adapter projects semantic keys into a configurable map, supports
four conflict policies or a resolver callback, and reports partial progress.
It merges operations; sequence rebasing and domain-specific three-way snapshots
remain explicit application policies. Retained payload clones are released on
skipped/failed operations even when the clone returns the original address.

`history_catalog` uses three different map/ordered backends to merge inventory
edits, keep a newer target change, serialize, and reopen a NULL payload marker.
Normal and ASan/UBSan runs succeeded. GCC's analyzer reports no diagnostics for
both history implementation units. No exhaustive failure-sequence claim is made.

## Catalog formats and locale policies

Atomic `ds_i18n_set_many` preserves options, formatter callbacks and existing
entries while publishing a whole batch. The catalog adapter reads and writes
escaped TSV or flat string-to-string JSON, with I/O callbacks and FILE helpers.
Input limits, UTF-8 checks, surrogate escapes, duplicate handling and NUL rejection
are explicit. A failed load leaves existing translations unchanged. Formatter
callbacks are caller-owned code and are not serialized as catalog data.

Locale helpers take exact decimals, configurable separators/grouping and explicit
Gregorian date fields/patterns. Built-in cardinal rules cover English, Italian,
French, Russian and Arabic for ordinary decimal notation, using CLDR 48; custom
callbacks cover other language policies. Locale negotiation, ordinal/range rules,
currency rounding, time-zone data and calendar conversion are separate policies.

`localized_catalog` imports TSV and JSON, decodes Unicode escapes, saves/reopens
its catalog, renders Italian numeric/date policies, and distinguishes `1` from
`1.0` with large integer counts supported. Normal and ASan/UBSan runs pass. GCC's
analyzer reports no diagnostics for i18n, i18n_catalog and i18n_locale. Strict C89
library and all example builds pass. See `i18n-catalogs.md` for scope and formats.

## Original B-tree and intrusive treap

The original B-tree's void insert path dereferenced failed split-node allocations.
Insertion now plans the exact top-down split path, allocates those nodes first,
and then commits without allocation; a no-split path allocates nothing. Checked
insert/split/find/remove APIs, context allocation and callback-user variants are
available, including clone/walk/destroy. Clone retains the allocation policy.
Degree validation prevents signed sizing overflow. Removal rejects a foreign
live node and clears the removed node's owner link; destroy clears borrowed links.

The intrusive treap now walks, clones and destroys through parent links, using
constant C-stack space even with degenerate priorities. Cloning preserves shape
and priorities in linear time. Its default priority generator uses full 32-bit
xorshift output instead of 15-bit priorities; seeded shapes therefore change.
A config supplies context, comparator user and an optional priority generator.
Checked insertion distinguishes duplicate keys, and checked clone callbacks carry
status plus user data. Live node/item addresses remain stable.

`DS_CONTAINER_OF` and its const counterpart support embedding multiple different
intrusive links in one record. `intrusive_indexes` builds B-tree groups with equal
keys and a unique treap ID index, clones both, removes records and walks totals.
Both active indexes contain 64 records (ID sum 3072); snapshots retain 96 each.
Normal and ASan/UBSan runs pass, and both units have zero GCC analyzer diagnostics.

## Probing-map compaction audit

The older roadmap's tombstone work was already implemented. Open addressing and
Swiss maps both compact in place without allocating, including fixed buffers;
the original generic hash table rebuilds at the same capacity when tombstones
dominate, and Robin Hood deletion closes probe gaps without tombstones. Source
review covered insertion, deletion, reserve and compaction paths. No replacement
algorithm was needed. Header lifetime wording now includes reserve/compact/clear
and documents the actual Swiss compaction threshold instead of implying that
only completely full live tables can grow. See `container-lifetimes.md`.

## Iterative randomized trees and safe borrowed inputs

Parent links now support iterative split/join/repair in ordered-capability,
augmented, Euler-tour, enfilade and implicit treaps. Augmented range summaries
retain key order for noncommutative combines. Guided descent uses an explicit
stack and reports allocation failure after any already-delivered callbacks.
The randomized BST now performs edits, shape-preserving copies, height queries,
walks and validation iteratively; its split/join rejects incompatible ordering
policies. Its size-proportional selection uses all size_t bits and rejection
sampling instead of biased modulo reduction. A custom RNG must supply uniform
bits and make progress. The visibility sweep's internal treap is iterative too.

Sorted-array ordered maps, fusion trees and enfilades now stage borrowed input
before growth or slot movement; NULL values zero-initialize built-in ordered
backends consistently. Count sums are checked before joining/inserting. Public
struct layouts for enfilade and randomized BST change, so rebuild consumers.

`composed_ranges` demonstrates split/join, noncommutative summaries, refresh,
removal, guided descent, and self-source array/fusion/enfilade edits.
`forest_actions` performs lazy component/subtree updates, cuts and relinks with
a Swiss-map edge index. `sequence_policies` combines lazy sequence updates with
reversal/split/append, randomized catalog split/join/removal, and visibility rays.
All three run normally and under ASan/UBSan; the changed tree and visibility
units have no GCC analyzer diagnostics. Existing dynamic-graph examples also run.
The full strict library/example build passes. This is not exhaustive validation.

## Logic graphs and integer boundaries

`algo/logic_graph` evaluates Boolean circuits through the existing graph-view
capability, including arbitrary numbering, shared dependencies and custom gates.
A general Horn-rule engine derives the least fixed point with repeated premises,
empty rules, unsupported cycles and optional provenance. The DADS path-system
wrapper reuses it. Outputs publish only on success; callback side effects do not
roll back. `deployment_rules` demonstrates all three entry points.

The recursive-call review also exposed arithmetic boundary errors. CRT residue
subtraction and modular Fibonacci addition/subtraction now avoid intermediate
word overflow. K-bonacci no longer calculates one unnecessary overflowing term,
and subtracts the outgoing window term before adding. Ackermann's m=3 guard
avoids wrapping n+3 and includes the representable 2^wordbits-3 boundary.
`integer_recurrences` shows the largest representable Fibonacci term, the same
value through k-bonacci, modular arithmetic and the Ackermann boundary. Its
normal and ASan/UBSan runs pass, with no number-theory analyzer diagnostics.

The logic-graph example also passes normally and under ASan/UBSan, with zero
GCC analyzer diagnostics after an explicit custom-policy guard was added.

## Quantum references

The four named quantum algorithms were incorrectly labelled as definitions with
nothing to implement. `quantum_reference` now provides bounded amplitude
simulations of Deutsch-Jozsa, Grover, Simon and Shor's order/factoring branch,
reusing the FFT, RNG and integer arithmetic modules. Public Hadamard/measurement
primitives, explicit state/shot/iteration limits, verified integer results and
failure statuses make their scope reviewable. They are references with
exponential classical storage, not quantum-performance implementations.

`interference_patterns` runs normally and under ASan/UBSan, with no GCC analyzer
diagnostics for the new unit. See `quantum-reference.md` for costs and sources.

## Parallel scans and corrected inventory

Hillis-Steele and Blelloch scans now accept the existing executor capability.
Every round joins before the next, and independent task outputs preserve operand
order for noncommutative monoids. The padded Blelloch variant stages storage
before mutation; the sequential in-place entry point remains available. Element
count multiplication and executor worker-count arithmetic are checked. Reduction
now accumulates privately before publishing an output that aliases its input.

`executor_prefixes` composes seven digit segments using reverse task scheduling,
showing both inclusive/exclusive results and a reduction into an input slot.
Normal and ASan/UBSan execution passes; scan and executor have no GCC analyzer
diagnostics. The example is not a concurrent scheduler stress run.

The full 1,306-entry DADS inventory matches, and 183 index aliases now inherit
their targets' scope. The semantic audit exposed six missing capabilities (all six are now delivered with explicit scope labels),
listed in `coverage-review.md`; the prior zero-missing claim has been removed.
Other misleading unrestricted/definition labels now state actual restrictions,
reference status or variants. Eleven of the ledger's 12 grouped items are complete.

## Hyperedges and composition

`lib/hypergraph` stores sorted, deduplicated vertex sets, including empty and
parallel hyperedges, with copied fixed-size payloads and stable graph-local
handles. The caller selects the handle map backend and allocation context.
Insertion and membership replacement stage input and reserve incidence storage
before publication. Failed reservation preserves logical contents, but may move
incidence arrays. Vertex-universe shrink rejects incident vertices.

`ds_gview_from_hypergraph` exposes the bipartite incidence graph without building
a clique: original vertices and hyperedge vertices alternate along a path.
Rebuild the view after mutation. `shared_projects` demonstrates a Swiss-map
backend, duplicate-member normalization, membership replacement, edge removal,
vertex growth and ordinary graph BFS. Strict compilation, normal and ASan/UBSan
example execution pass; hypergraph and graph-view GCC analysis report no
diagnostics.

Storage is O(V + E + incidences), apart from map capacity. Membership staging
sorts k input IDs in O(k log k); insertion also reserves k incidence vectors.
Replacement/removal scans each old member's incidence sequence, costing the sum
of those degrees. Handle lookup follows the selected map's cost. This design
keeps traversal compact without claiming constant-time incidence deletion.

## Shared-capacity network optimization

`algo/multiflow` builds an owned dense LP for separate source/sink commodities
with shared arc capacities, delivery bounds, optional per-commodity costs and
weighted throughput or minimum-cost objectives. Per-commodity integrality
selects MILP. Integer arc flows can split among paths; unsplittable demand
routing is a different formulation. Construction has checked dimensions and a
caller-selected coefficient limit. Input storage is independent of the model.

The model exposes its LP and integrality mask; solve policies can replace the
solver callback. Candidate solutions are staged and checked for conservation,
capacity, bounds, nonnegativity and integrality before caller arrays change.
Operational errors, infeasibility and budget exhaustion remain distinct. A
budget-limited run can return no incumbent without overwriting caller flows.
This is floating-point optimization with documented tolerances, not an exact
certificate. Dense model memory is documented in the header.

Dependency repairs in `algo/lp` separate integer and double tableau metadata
(avoiding size/alignment assumptions), check allocation arithmetic, reject
nonfinite input and numerical overflow, process the root with a one-node budget,
validate rounded incumbents and recompute their objectives. An unbounded
relaxation now reports `DS_LP_RELAXATION_UNBOUNDED`, not proven integer
unboundedness. Pure-integer Gomory cuts stop before a new fractional row's
slack would invalidate their premise; branch and bound then continues.

`shared_routes` demonstrates fractional throughput 6.5, integer throughput 6,
a one-node limit without a published plan, a selected solver, minimum-cost
required deliveries and infeasible overbooking. Strict builds, normal and
ASan/UBSan execution pass; LP and multi-flow GCC analysis report no diagnostics.
The NIST integer-flow definition and David Kempe's linear-programming lecture
notes informed the formulation:

- https://xlinux.nist.gov/dads/HTML/integermulti.html
- https://www.david-kempe.com/teaching/linear-programming.pdf

## Small Boolean maps

`algo/karnaugh` implements Gray-code layouts with selectable input-axis order,
don’t-care cells, and cube minimization for zero to six variables. Default
covers minimize term count and then literals; a custom cost policy enumerates
all valid cubes so non-prime groups remain available when cheaper. Either true
or false cells can be covered. Search has a caller-selected node budget;
`DS_STOP` publishes a valid owned cover with `optimal=false`, whereas `DS_OK`
means the bounded Boolean search completed. Negative statuses preserve output.
The recursion is explicitly bounded by the 64 possible required cells.

`controller_map` demonstrates axis permutations, unused configurations,
positive/complemented forms, custom gate cost and budget exhaustion. Strict
compilation, ordinary and ASan/UBSan execution pass, with no GCC analyzer
diagnostics in the new module. The coverage map marks the six-variable scope
as restricted rather than claiming an unbounded Boolean minimizer.
Reference: https://xlinux.nist.gov/dads/HTML/karnaughmap.html

## Differential scripts and theoretical shapes

`algo/hsadelta` indexes aligned reference-block hashes with suffix arrays, a
one-hash quick-index filter and a caller-selected lookup map. Colliding block
hashes are disambiguated by bytes both in the reference alphabet and target
comparisons. Binary search selects full-block matches, which are extended by
byte comparison; a block-width alignment window chooses the next copy. Hash,
rolling update, suffix-array builder, lookup map and context are replaceable.
The borrowed reference must remain immutable and alive.

Commands can stream to a caller or populate an owned script. Scripts carry the
reference size/digest and independently owned literals. Apply validates ranges,
total size and the reference digest before publishing reconstructed bytes.
Hash collisions cannot turn unequal bytes into COPY commands. This does not
claim a minimum edit script or the paper's specialized memory/time constants:
block size controls granularity, and repetitions/collisions add comparisons.
Working storage is proportional to reference blocks plus quick-index bits and
the chosen components; literal retention belongs only to the script adapter.
`release_delta` demonstrates reordered/changed records, streaming, owned apply,
a deliberately two-bit hash, Swiss lookup and a selected suffix builder.

`lib/jellyfish` implements the theoretical balanced-body/linked-tentacle shape.
Its default body is a deterministic balanced binary tree; ordered-interface
backends may replace body routing. Existing intrusive list operations link the
tentacles. The body holds floor(sqrt(n)) items; the remaining items are divided
between its external gaps. Insert/delete rebuild atomically and cost O(n) plus
body construction; value replacement is direct. This is a reference, not a
claim of a fast dynamic dictionary. `jellyfish_body` shows the shape, lookups,
borrowed-input insertion, deletion and an (a,b)-tree body policy.

Both new modules pass strict compilation, ordinary and ASan/UBSan example
execution, with no GCC analyzer diagnostics after the final repairs. No new
tests were added or run. Primary definitions:

- https://xlinux.nist.gov/dads/HTML/hsadelta.html
- https://research.ibm.com/publications/an-approximation-to-the-greedy-algorithm-for-differential-compression
- https://xlinux.nist.gov/dads/HTML/jellyfish.html

## Legacy API migration, first stage

Fifteen older containers now expose `create_context`: list, dlist, ring list,
stack, queue, deque, BST, AVL, red-black tree, disjoint set, ART, HAMT,
Aho-Corasick, radix trie and rope. Their internal allocation/free and existing
cloning paths retain the selected owner. Rope concatenation now checks context
compatibility before transferring nodes. The original allocation entry points
remain.

BST, AVL and red-black trees additionally expose checked insertion and lookup,
iterative stoppable traversals in all three orders, and checked cloning and
destruction callbacks with user data. Shape-preserving cloning reuses the
existing engines; explicit destination contexts and callback rollback are
documented. Legacy entry points continue through compatible adapters.

Strict builds and both `legacy_contexts` and `intrusive_policies` examples pass
normally and under ASan/UBSan. GCC analysis reports no diagnostics in changed
container modules after the final ART backlink check. At this first-stage checkpoint the migration remained in progress; the next
section and final validation below record its completed scope.


## Legacy API migration, remaining families

The remaining allocation/callback families have compatible checked entry points;
`legacy-api-migration.md` records exact scope, costs and migration rules. This
includes nullable byte-map lookup, stoppable traversal, explicit callback user
data, source-owner-preserving cloning, checked disjoint-set operations, typed
trie slices, and configurable cloning between different trie child stores.
Configured hash clones now propagate payload-clone status, distinguish NULL data
from failure, require rollback cleanup and preserve custom legacy allocation.
Original intrusive Fibonacci heaps have their own checked/context path in
addition to the existing item heap. Both Fibonacci degree tables are bounded
from the actual size_t bit width. Intrusive meld rejects self-meld and mismatched
comparison policy without moving nodes.

Boundary repairs found during this migration:

- Bitvector rounding no longer overflows before allocation. Checked writes honor
  the documented freeze state; wavelet input validates alphabet membership and
  propagates its context to nested bitvectors.
- Fenwick prefix indexing no longer wraps at SIZE_MAX. Checked signed queries
  allow cancellation; updates/set stage every affected cell before publication.
- Binary heaps accept comparison user data for insertion, extraction and
  addressable repair/removal. Child-index arithmetic stops before leaf nodes.
- Skip-list allocation failure restores the generator seed as well as links.
- String swap no longer exchanges allocator policies for objects that remain
  allocated by their original owners. Cross-owner swaps stage copies atomically.
  Exact self-adoption avoids freeing the buffer about to be reused; safe-adopt
  copying handles empty inputs without writing through NULL. Clones, slices and
  replacement builders retain ownership.
- Segment-tree signed sums, scaled updates, lazy tags and per-element extrema
  are checked. An O(log n) journal restores touched storage on any failure.
  Queries restore lazy caches as well. The representability limits of deferred
  child sums and lazy tags are explicit; this is not arbitrary-precision math.

New ordinary examples and ASan/UBSan executions pass for the completed migration
paths. GCC analyzer diagnostics exposed missing explicit invariants at string
materialization, trie deletion and Fibonacci consolidation; the final guards
make those preconditions explicit. Whole-build/header/integration results are recorded below.


## Final validation and delivery

All 12 items in `completion-plan.md` are complete within their documented scope.
The final integration pass caught one migration defect: the priority-queue
adapter initializes an embedded pairing heap manually and needed the added
policy fields initialized too. This is fixed. A separate instrumented string-map
failure was a stale object after its shared config layout changed; the entire
sanitizer archive was then rebuilt from current sources.

- Strict C89 static/shared library and all 76 examples compile.
- Every installed header compiles independently; every library source compiles
  under the strict warning set without inlining. The final adapter change was
  also compiled separately under that same warning set at O0.
- All 76 ordinary examples exit successfully.
- A fresh ASan/UBSan archive was built from all 273 production translation units;
  all 76 examples compile against it and exit successfully, with no sanitizer
  diagnostics. LeakSanitizer remains disabled for this host's tracing constraint.
- GCC analysis reports zero diagnostics in all 32 implementation units changed
  in the final migration, in addition to the earlier per-feature analysis.
- A separate `DS_NO_LEGACY_NAMES` build compiles static/shared libraries and all
  examples. Its archive has 4,201 defined exported symbols, all prefixed `ds_`.
  Five focused integration examples run successfully against that build.
- `git diff --check` passes. No test source was changed or added, and no test
  target or test executable was run.

The patch is based on `b983d99dc7e01d83ac51da518ed7b747d4eb82fd` and includes
all commits from this completion pass. Recompile consumers because public
layouts changed. Read `legacy-api-migration.md` and `namespace.md` when adopting
the new APIs. Validation covers these builds and example paths; it is not a
claim of exhaustive correctness for the full algorithm catalog.
