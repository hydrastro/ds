# ds: roadmap ideas

Ideas only, no code. Written after reading `hydrastro/ds` at commit `9e3224b`
(91 commits, ~23k lines, 45 modules). The companion file `ds-algorithms.md`
lists algorithms.

Legend used in the catalogue below:

- **✅** already in ds
- **⭐** recommended next (high value, fits ds's style)
- **(E)** suits embedded / stack-only use (fixed size, no malloc needed)
- **(C)** concurrent / lock-free
- **(P)** persistent / functional
- **◇** niche: worth it for "the most data structures ever", low everyday use

---

## 1. Where ds stands

Strong points already:

- **Intrusive and allocator-aware.** The links live in the user's struct and
  allocation goes through the user's allocator. Few C libraries do both, and
  it is already most of what embedded users want.
- **Status codes, diagnostics and contexts** (`ds_status_t`, `ds_context_t`):
  more structured error handling than most C libraries have.
- **Unusual structures:** persistent RB tree and trie, retroactive history
  with branches and merge, wavelet tree, ART, HAMT, rope, succinct bitvector.
  Most libraries stop at lists, trees and hash tables.
- **Testing:** ASan/UBSan, Valgrind, randomized oracle tests. That is the
  right base to grow from.

Gaps that stand out:

- **Nothing contiguous.** Deque, queue, stack and list are all linked
  nodes. A dynamic array (vector) and an array-backed deque are missing, and
  they are the most used containers in practice: cache-friendly, with no
  per-element allocation. Most other structures can be built on them
  (binary heap, open-addressing tables, sparse sets...).
- **Two API generations:** the legacy sentinel-returning APIs and the
  `ds_status_t` ones. Finish that migration before multiplying modules,
  because every new module copies whichever style is nearest.
- **Callbacks without `void *user`** in the old APIs (your roadmap says so).
  Without it, compare and destroy callbacks need globals, which also breaks
  thread safety and reentrancy.

---

## 2. Is `ds_safe` worth it?

Short answer: **as it is, no.** I would remove it, or shrink it to something
much smaller and more honest. Here is why.

**What it does.** With `DS_THREAD_SAFE`, every structure gets a mutex and every
public function locks it (recursive mutexes, plus a runtime `is_thread_safe`
flag). The library is built twice (`libds` and `libds_safe`), and `FUNC()`
renames every function `name_safe`.

**Why that's weaker than it looks:**

1. **Locking each call does not make a sequence of calls safe.** "If the key
   is missing, insert it", "pop if not empty", "search, then remove the node
   found" are all check-then-act races. Each call is atomic, but the
   combination isn't. Your own `string_map` needed a special
   `insert_take_key` for exactly this reason. Every user will hit the same
   problem, and only for the operations you happened to anticipate.
2. **Returned pointers outlive the lock.** `ds_stack_peek` returns a node
   after unlocking, so another thread can pop and free it while the caller
   still uses it. Intrusive design makes this worse: the caller owns the
   memory, so the container cannot keep it alive.
3. **Iteration and walks:** a callback runs under the lock (deadlock-prone
   if it calls other containers), or iteration runs outside it (unsafe). The
   recursive mutex hides the cases where a callback reenters the same
   container; that usually means a design bug.
4. **ABI and ODR hazard.** `ds_stack_t` has extra fields only when
   `DS_THREAD_SAFE` is defined. Two translation units compiled differently
   see two different layouts under the same type name. The `_safe` names
   keep the functions apart, but structs passed between those units, or
   headers included in the wrong mode, corrupt memory silently. This is the
   strongest technical argument against the current form.
5. **Cost for everyone:** two builds, two test runs, name-mangling macros in
   every header, a branch in every operation, and pthreads on platforms that
   have none (bare metal, many RTOSes).
6. **It is not what concurrent code needs.** Code that really shares
   containers wants either one coarse lock around several operations, or
   structures designed for concurrency (lock-free queues, striped hash
   maps). A mutex per call satisfies neither.

**Prior art agrees.** Java kept `Vector` and `Hashtable` (synchronized on
every call) but steers people to unsynchronized collections plus
`java.util.concurrent`. The C++ standard library has no internal locking,
and neither do glib, klib or the BSD `queue.h` macros. "Externally
synchronized" is the standard contract.

**What to do instead:**

- **Default contract:** "a ds object may be used by one thread at a time;
  different objects are independent". Then keep all hidden global state out
  of the library (the default `ds_context_t` and `last_error`), or make it
  thread-local.
- **Optional helper:** one generic lock wrapper, a small `ds_lock_t` holder
  with explicit `lock`/`unlock`, so users lock around whole sequences of
  operations. One wrapper replaces the safe variant of every module.
- **A real concurrent module** for the cases that matter: SPSC ring buffer,
  MPMC bounded queue, Treiber stack, Chase-Lev work-stealing deque, striped
  hash map, with hazard pointers or epoch reclamation (section 6, M). Those
  are the structures people pick a library for.
- **If you keep `_safe`**, at least make the struct layout identical in both
  modes (always include the lock field) so mixing modes can't corrupt
  memory, and document its limits plainly.

---

## 3. Embedded / stack-only track

This is a good direction, and intrusive design already does half of it.
Design rules for an "embedded tier", each one a checkable property:

- **No hidden allocation.** The caller passes the storage, as a static
  array, a struct on the stack, or memory from an arena. Size is fixed when
  the container is set up (e.g. `ds_ring_init(&r, buf, 64, sizeof(item))`),
  or at compile time through a declaration macro.
- **Bounded worst-case time and stack:** no recursion (or a proven depth
  bound), no amortized resizing. Document the worst case per operation.
- **Index links instead of pointers:** `uint8_t`/`uint16_t` indices into the
  caller's array halve or quarter the link size, and the structure survives
  `memcpy`, serialization and placement in flash or shared memory.
- **ISR-safe primitives:** an SPSC ring buffer that needs only atomic loads
  and stores, the bread and butter of interrupt-to-main-loop
  communication.
- **No libc dependencies** beyond `stddef.h`, `stdint.h`, `stdbool.h`
  (`string.h` optional). Compiles freestanding (`-ffreestanding`), with no
  floating point unless asked.
- **MISRA-C / CERT-C awareness:** a mode, or at least documented
  deviations, is what makes a library usable in automotive and medical
  work.
- **Deterministic iteration order** and no randomness unless seeded by the
  caller (treaps and skip lists need an injectable RNG).

Embedded candidates (all **(E)**):

| Structure | Why embedded people want it |
|---|---|
| Fixed-capacity vector (static array + count) | the basic container, with no malloc |
| Ring buffer, SPSC lock-free ⭐ | UART/DMA/ISR to main loop |
| Ring buffer of variable-length records (bip buffer) | log and packet queues without fragmentation |
| Fixed-size object pool with free list ⭐ | replaces malloc for one type; O(1) |
| Bitmap allocator | slots in flash/RAM pages |
| Static binary heap in an array | timers, schedulers |
| Timer wheel (hierarchical) ⭐ | thousands of timeouts, O(1) each |
| Open-addressing hash table in a caller buffer | fixed-size lookup, no chains |
| Sorted array map with binary search | small maps; flash-friendly (read-only tables) |
| Index-linked list / tree (`uint16_t` links) | half the RAM of pointer links |
| Bitset of compile-time size | flags, pin states, free maps |
| Fixed-point / integer-only sketches (count-min, Bloom) | telemetry on microcontrollers |
| Static B-tree in flash (read-only, vEB/Eytzinger layout) | lookup tables, fonts, dictionaries |
| Event queue with priorities (bucketed) | cooperative schedulers |
| Small string with inline storage (SSO) | names and labels without malloc |
| Slot map with generational handles | safe references to pooled objects |
| Circular log in flash (wear-aware append) | persistent logs on NOR flash |

---

## 4. Scaling to "the most data structures ever"

Hundreds of modules only stay correct if each one comes with the same
scaffolding. Suggested per-module contract:

1. **Header doc block:** the complexity table (every operation, average and
   worst case, amortized or not, space), the ownership rules, and the
   concurrency contract.
2. **An invariant checker** (`ds_x_validate`). You have this for the RB
   tree; make it mandatory. Tests, fuzzers and the formal tools in section 5
   all reuse it.
3. **An oracle test:** random operations applied to the structure and to a
   dumb reference model (a sorted array, a linear-scan list), compared after
   every step.
4. **A fuzz target** (libFuzzer/AFL++): the operation sequence decoded from
   the fuzzer's bytes, the invariant checked after each operation.
5. **A benchmark** against the obvious alternative (e.g. treap vs RB tree vs
   skip list): it tells you and users when each one is worth it.
6. **Tiers in the README:** *core* (stable, everything above),
   *extended*, *experimental*. Then you can add the 200th structure without
   promising it's production-grade.
7. **One file pair per structure, no hidden cross-dependencies**, so people
   can copy single files into firmware (a big selling point for C).
8. **A generated catalogue page:** a table built from the header doc blocks,
   so documentation never drifts from code.

Possible extra: code generation from a spec for families with many variants
(heaps, probing schemes), so you don't write 12 heaps by hand.

---

## 5. Formal verification

Yes, it can be done, and data-structure libraries are among the best-studied
targets. A realistic ladder, cheapest first:

1. **Contracts as runtime checks.** Assertions plus the invariant checkers,
   switched on in test builds. Cheap and already half done.
2. **Fuzzing plus sanitizers** (section 4.4). Not proof, but it finds most
   real bugs.
3. **Bounded model checking with CBMC (or ESBMC).** You write small
   harnesses: "for every sequence of up to N operations on up to K elements,
   no out-of-bounds, no use-after-free, no leak, the invariant holds".
   Works on unmodified C. **This is the best effort-to-value step for ds.**
   AWS uses CBMC this way on its C libraries (aws-c-common).
4. **Deductive proof with Frama-C/WP and ACSL annotations.** You annotate
   pre- and postconditions and loop invariants, and the tool proves them for
   all sizes. Start with array-based modules (bitset, Fenwick tree, binary
   heap index math, ring buffer, fixed vector): pointer-heavy code is much
   harder. The Contiki OS linked-list module was verified this way, a good
   model to copy. E-ACSL turns the same annotations into runtime checks.
5. **Separation logic for pointer structures:** VeriFast, or the Verified
   Software Toolchain (Coq), which has verified C binary search trees and
   hash tables. Research-grade effort: pick one showcase (e.g. the RB tree
   insert), not the whole library.
6. **TLA+ / PlusCal for designs, not code:** the lock-free queue (if you add
   one) and the history branch-merge semantics. Model checking finds
   interleaving bugs no test will.

A credible goal: "every module passes CBMC harnesses for bounded sizes; the
embedded tier is proved with Frama-C/WP". Very few C libraries can claim
that.

---

## 6. The catalogue

Grouped by family. Each line gives the structure, then what it is for.

### A. Arrays and sequences
- Dynamic array / vector ⭐ (E with fixed capacity): contiguous, amortized O(1) push
- Small vector with inline buffer (E): no allocation until it grows
- Array-backed deque ⭐ (E): ring buffer that grows; O(1) at both ends
- Circular buffer ✅ (E)
- Bip buffer (E): ring buffer giving contiguous blocks, for DMA/IO
- Gap buffer: text editors, O(1) edits at the cursor
- Piece table: undo-friendly text buffer (VS Code, Word)
- Rope ✅
- Tiered vector: O(√n) insert in the middle, O(1) access
- Hashed array tree / Brodnik resizable array ◇: O(√n) wasted space
- Unrolled linked list: nodes holding small arrays; cache-friendly list
- VList ◇: O(1) average indexing on a list-like structure
- Skip list ✅
- Indexable skip list: skip list with rank/select
- XOR linked list ◇: a doubly linked list with one pointer field
- Self-organizing list (move-to-front, transpose, count) ◇
- Sparse array (E): mostly-empty array with compact storage
- Sparse set ⭐ (E): O(1) insert/remove/clear over small integers (ECS, compilers)
- Slot map / generational arena ⭐ (E): stable handles that detect dangling use
- Colony / hive: unordered container with stable pointers and O(1) erase
- Packed integer array / bit-packed vector (E): values of k bits each
- Zipper (P): cursor into an immutable sequence
- Finger tree (P): deque plus concatenation and split; basis of many sequences
- RRB-vector (P): persistent vector with fast concatenation
- Bitmapped vector trie (P): Clojure's persistent vector
- Implicit treap: sequence with O(log n) split, merge, reverse range

### B. Stacks, queues, deques
- Stack ✅, queue ✅, deque ✅ (linked; add array versions)
- Min-stack / max-stack: O(1) current minimum
- Monotonic queue / deque: sliding-window min and max
- Queue from two stacks; real-time queue (Okasaki) (P)
- Banker's queue and deque (P)
- Randomized queue ◇: dequeue returns a random element
- Bag / multiset: unordered, with counts
- Steque ◇: stack-ended queue

### C. Priority queues and heaps
- Binary heap ✅ (E in an array)
- d-ary heap ⭐: faster decrease-key-heavy loads (Dijkstra)
- Indexed priority queue ⭐: decrease-key by handle, the API graph code needs
- Min-max heap: both ends in O(1)/O(log n)
- Interval heap / double-ended priority queue
- Binomial heap: mergeable
- Fibonacci heap ✅, pairing heap ✅
- Leftist heap, skew heap: simple mergeable heaps
- Rank-pairing heap ◇, hollow heap ◇, strict Fibonacci heap ◇, Brodal queue ◇: optimal worst-case bounds
- Soft heap ◇: error-tolerant, used in the fastest MST algorithms
- Weak heap ◇, 2-3 heap ◇, thin heap ◇, quake heap ◇, beap ◇
- Radix heap ⭐: monotone integer keys (Dijkstra with integer weights)
- Bucket queue / calendar queue (E): small integer priorities, event simulation
- Tournament tree / loser tree: k-way merge
- Leonardo heap: the heap inside smoothsort
- Van Emde Boas tree as a priority queue (see F)

### D. Hashing
- Separate chaining ✅, linear ✅ and quadratic ✅ probing
- Double hashing
- Robin Hood hashing with backward-shift deletion ⭐: low variance, no tombstones
- Hopscotch hashing
- Cuckoo hashing (2 tables, bucketed variants): worst-case O(1) lookup
- Swiss table (group probing with control bytes; portable SWAR version) ⭐: today's fastest general design
- Compact ordered dict (Python layout) ⭐: insertion order and density
- Extendible hashing; linear hashing (Litwin): grow one bucket at a time; databases
- Coalesced hashing ◇
- Perfect hashing (FKS); minimal perfect hashing (CHD, BDZ, RecSplit, PTHash) ⭐: static key sets in O(1) with tiny space
- Consistent hashing: hash ring, jump hash, rendezvous (HRW), Maglev
- Bidirectional map, multimap, multiset map
- Hash trie / HAMT ✅, CHAMP (P): better-compacted HAMT
- Concurrent hash map (C): lock striping, or split-ordered lists
- Hash table with stable references (node-based) vs flat: offer both

### E. Probabilistic and streaming (sketches)
- Bloom filter ✅
- Counting, scalable, blocked and partitioned Bloom filters
- Cuckoo filter ⭐: deletions; better than Bloom below about 3% false positives
- Quotient filter, counting quotient filter: mergeable and resizable
- Xor filter, binary fuse filter ⭐ (static sets, smaller than Bloom), ribbon filter
- Bloomier filter ◇, invertible Bloom lookup table (IBLT) ◇: set reconciliation
- Count-Min sketch ⭐, Count sketch: frequency estimates
- HyperLogLog / HLL++ ⭐: distinct counts in kilobytes
- LogLog ◇, KMV (k minimum values)
- MinHash, b-bit MinHash, SimHash: similarity and near-duplicate detection
- Misra-Gries, Space-Saving: heavy hitters / top-k
- Quantile sketches: GK, KLL ⭐, t-digest, q-digest, DDSketch
- Reservoir sampler (as a structure); weighted variants (A-Res)
- Morris counter ◇: approximate counter in O(log log n) bits

### F. Ordered trees and search structures
- Binary search tree ✅, AVL ✅, red-black ✅, splay ✅, treap ✅
- Left-leaning red-black (✅ in persistent form), AA tree ◇
- WAVL / rank-balanced tree ⭐: fewer rotations than AVL or RB
- Weight-balanced tree (BB[α]): good for persistence and set operations
- Scapegoat tree: no per-node balance data
- Zip tree ◇: treap variant with simple insert and delete
- 2-3 tree, 2-3-4 tree (teaching links to RB)
- B-tree ✅, B+ tree ✅ (B* split ✅)
- B-link tree (C): concurrent B-tree (Lehman-Yao)
- Bε-tree / buffered B-tree ◇: write-optimized (fractal trees)
- Cache-oblivious B-tree / vEB layout ◇; static Eytzinger and B-layout arrays ⭐ (E)
- T-tree ◇: in-memory databases
- Order-statistic tree ✅
- Finger search tree ◇; tango tree ◇ (log log n competitive)
- Cartesian tree: RMQ to LCA, treap foundation
- Van Emde Boas tree ⭐, x-fast and y-fast tries ◇: O(log log U) integer sets
- Fusion tree ◇ (theory showcase)
- Exponential search tree ◇
- Top tree ◇, link-cut tree ⭐, Euler tour tree: dynamic trees for dynamic graph algorithms

### G. Range query structures
- Segment tree ✅ (lazy ✅); persistent segment tree (P); dynamic / implicit segment tree; 2D segment tree
- Merge sort tree, wavelet matrix: k-th smallest in a range
- Li Chao tree: dynamic convex hull trick (min of lines)
- Fenwick tree ✅; range-update/range-query and 2D variants
- Sparse table ⭐ (E): O(1) static RMQ after O(n log n) build
- Disjoint sparse table ◇: any associative operation in O(1)
- Sqrt decomposition, Mo's-algorithm-friendly buckets
- Interval tree ⭐: overlap queries
- Priority search tree ◇
- Range tree (with fractional cascading) ◇
- Kinetic segment tree ◇

### H. Tries and string structures
- Trie ✅, radix trie ✅, ART ✅
- Crit-bit (PATRICIA) tree ⭐: compact and simple
- qp-trie ◇: very compact crit-bit variant
- Ternary search tree
- Double-array trie (Aoe) ⭐: compact and static, used in tokenizers
- Burst trie, HAT-trie ◇: cache-conscious string sets
- Minimal acyclic automaton (DAWG / MA-FSA), finite state transducer ⭐: huge static dictionaries (Lucene, ripgrep's fst)
- Suffix array ⭐ with LCP array
- Suffix tree (Ukkonen); suffix automaton ⭐
- Compressed suffix array, FM-index ⭐ (BWT-based full-text index)
- Palindromic tree (eertree) ◇
- Aho-Corasick ✅
- String B-tree ◇
- Rope ✅; persistent rope (P)

### I. Succinct and compressed structures
- Bitvector with rank/select ✅ (add RRR compression ◇)
- Elias-Fano encoding ⭐: monotone integer sequences near their entropy
- Wavelet tree ✅, wavelet matrix ⭐ (simpler and faster for large alphabets)
- Balanced-parentheses tree, DFUDS, LOUDS ⭐: trees in 2n+o(n) bits
- Range min-max tree ◇: navigation for balanced parentheses
- Succinct (LOUDS) trie ◇
- k²-tree ◇: compressed graph adjacency
- Compressed integer arrays: varint, PForDelta, Simple-8b, StreamVByte
- Compressed bitmaps: Roaring ⭐, EWAH, WAH, Concise

### J. Persistent and functional (P)
- Persistent red-black tree ✅, persistent trie ✅, HAMT ✅
- Persistent vector (RRB / bitmapped trie) ⭐
- Persistent deque and queue (Okasaki, real-time)
- Persistent array (fat nodes; Baker's version trick)
- Persistent union-find ◇
- Finger tree ⭐
- Persistent segment tree (see G)
- Generic persistence techniques: path copying ✅, fat node, node copying (Driscoll et al.) ◇
- Confluent persistence ◇: merging versions (relates to your history module)
- Retroactive structures ✅ (history); partially vs fully retroactive (Demaine et al.) ◇

### K. Union-find and partitions
- Disjoint set ✅
- Union-find with rollback (undo) ⭐: offline dynamic connectivity, backtracking
- Weighted union-find (potentials / offsets)
- Partition refinement ◇: DFA minimization, graph algorithms

### L. Graph representations
- Adjacency list ✅ (graph module), adjacency matrix, bit-matrix (E)
- CSR / CSC (compressed sparse row) ⭐: static graphs, fastest traversal
- Edge list; incidence matrix
- Dynamic graph with O(1) edge delete (intrusive edges)
- Multigraph, hypergraph ◇
- Implicit graph interface (neighbors from a callback): huge state spaces
- Dynamic connectivity (Holm-de Lichtenberg-Thorup) ◇
- Dominator tree (as output structure)

### M. Concurrent (C)
- SPSC ring buffer ⭐ (E), Lamport style with acquire/release atomics
- MPMC bounded queue (Vyukov) ⭐
- Treiber stack ⭐ (with ABA protection)
- Michael-Scott queue
- Chase-Lev work-stealing deque ⭐
- LCRQ ◇, Disruptor-style ring ◇
- Concurrent skip list; lock-free ordered list (Harris)
- Split-ordered list hash table
- RCU-protected list and hash ◇
- Memory reclamation: hazard pointers ⭐, epoch-based reclamation ⭐, QSBR ◇
- Flat combining ◇: one thread applies a batch for everyone
- Seqlock-protected snapshot (E)

### N. Spatial and geometric
- k-d tree ⭐, k-d-B tree ◇
- Quadtree, octree (point and region variants), loose octree
- R-tree, R*-tree ⭐, Hilbert R-tree, R+ tree ◇
- BSP tree; bounding volume hierarchy (BVH) ⭐ (ray tracing, collision)
- Uniform grid / spatial hash (E)
- Z-order (Morton) and Hilbert curve keys: turn 2D/3D into 1D sorted keys
- Geohash ◇
- Ball tree, cover tree, VP-tree ⭐, M-tree ◇: metric-space nearest neighbours
- BK-tree ⭐: fuzzy string search by edit distance
- Half-edge / doubly connected edge list (DCEL), winged edge, quad edge ◇: polygon meshes and planar subdivisions
- Interval and segment trees for geometry (see G)
- HNSW graph ◇: approximate nearest neighbours (vector search)

### O. Memory and allocation (as data structures)
- Arena ✅, pool ✅, debug allocator ✅
- Slab allocator; buddy allocator ⭐; TLSF ⭐ (E: O(1), real-time)
- Bitmap allocator (E); stack/LIFO allocator (E); ring allocator (E)
- Free list with size classes
- Generational handles (see slot map)

### P. Caches and eviction
- LRU cache ⭐ (hash plus list), LFU with O(1) operations
- ARC, 2Q, CLOCK, CLOCK-Pro ◇
- S3-FIFO ⭐, SIEVE ⭐ (simple, recent, often better than LRU)
- W-TinyLFU (count-min admission plus LRU)
- TTL cache with timer wheel

### Q. Time and scheduling
- Timer wheel, hierarchical timing wheels ⭐ (E)
- Calendar queue (event simulation)
- Interval map / range set (non-overlapping ranges with merge)
- Token bucket / leaky bucket rate limiter (E)
- Sliding-window counters (time-bucketed)

### R. Storage and database
- LSM tree (memtable, SSTables, compaction) ⭐ as a capstone project
- Log-structured hash (Bitcask) ◇
- Bw-tree ◇ (lock-free B-tree)
- Cache-oblivious lookahead array (COLA) ◇
- Write-ahead log, buffer pool / page cache
- Extendible and linear hashing (see D)

### S. Decision diagrams and automata
- Binary decision diagram (ROBDD) with a unique table ⭐: SAT, model checking, configuration
- Zero-suppressed decision diagram (ZDD) ◇: Knuth's favourite for combinatorics
- DFA / NFA structures, minimized DFA
- Dancing links (DLX) ⭐: exact cover (Sudoku, pentominoes); a classic
  showcase for intrusive lists, so it fits ds well

### T. Hash-linked and authenticated
- Merkle tree ⭐, sparse Merkle tree, Merkle mountain range ◇
- Merkle-Patricia trie ◇ (Ethereum)
- Verkle tree ◇
- Hash chains / skip lists with hashes ◇

### U. Matrices and numeric containers
- Dense matrix (row/column major); sparse matrix: COO, CSR, CSC, ELL ◇
- Sparse vector
- Bit matrix (E) (graph transitive closure, GF(2) linear algebra)
- Tensor with strides (views, slicing) ◇
- Big integer ◇, rational ◇ (arguably a separate library)

### V. Text and Unicode (you already have a strong base)
- Grapheme-cluster indexed string
- Line index for large texts (offset to line/column in O(log n))
- Piece table / gap buffer (see A)
- Normalization buffers (NFC/NFD streaming)

---

## 7. Suggested order

The ⭐ items, ordered by value for effort and by how well they fit ds:

1. Vector, array deque, small vector (fills the contiguous-storage gap)
2. Fixed-capacity embedded versions of vector, ring, pool, heap, hash table; SPSC ring
3. Slot map, sparse set
4. Indexed priority queue, d-ary heap, radix heap (makes the graph module fast)
5. Robin Hood or Swiss-table hash; compact ordered dict
6. LRU, S3-FIFO and SIEVE caches; timer wheel
7. Interval tree, sparse table, union-find with rollback
8. Crit-bit / double-array trie; suffix array with LCP; suffix automaton
9. Cuckoo and binary fuse filters; Count-Min; HyperLogLog; KLL
10. Elias-Fano, wavelet matrix, LOUDS, Roaring bitmaps
11. Persistent vector (RRB), finger tree
12. Concurrent module: MPMC queue, Treiber stack, Chase-Lev deque, hazard pointers / epoch reclamation
13. k-d tree, R*-tree, BVH, VP-tree, BK-tree
14. BDD, dancing links, Merkle tree
15. Capstones: LSM tree, minimal perfect hashing, FST dictionaries

Before step 1: finish the `ds_status_t` migration, add `void *user` to
callbacks, and decide on `ds_safe` (section 2). Every module after that is
cheaper to write and never needs migrating.

## Reference catalogues

- NIST Dictionary of Algorithms and Data Structures: https://xlinux.nist.gov/dads/
- MIT 6.851 Advanced Data Structures (Demaine), lecture videos and notes
- Okasaki, *Purely Functional Data Structures*
- Navarro, *Compact Data Structures*
- Mehta and Sahni, *Handbook of Data Structures and Applications*
- Brass, *Advanced Data Structures*
- Knuth, *The Art of Computer Programming*, volumes 1, 3 and 4
