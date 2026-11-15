# ds: algorithm catalogue

The algorithms a "most data structures ever" library could ship, grouped by
area. Companion to `ds-roadmap.md` (same markers: ✅ already in ds,
⭐ recommended, (E) embedded-friendly).

A note on scope first. A data-structure library gets the most from
algorithms that **operate on its own structures** (sorting a linked list,
merging heaps, set operations on trees, graph algorithms on the graph
module). Big standalone families (numerical linear algebra, cryptography,
compression codecs) are better as separate companion libraries, say
`ds-algo`, `ds-compress`, `ds-num`: they can use ds without making the core
huge. Cryptography in particular: implement it only for learning, and say
so in big letters. Constant-time correct crypto is a speciality of its own.

---

## 1. Sorting

**Comparison sorts**
- Insertion sort (E), binary insertion sort: small inputs, base case of hybrids
- Selection sort, bubble sort, cocktail sort (teaching)
- Shellsort with good gap sequences (Ciura, Tokuda) (E): no recursion, no memory
- Merge sort: top-down, bottom-up, natural ⭐
- **Linked-list merge sort** ⭐: O(n log n), O(1) extra space; a perfect fit for ds's lists
- In-place stable merge: block merge sort (WikiSort, GrailSort) ◇
- Quicksort: Lomuto, Hoare, 3-way (Dutch flag), dual-pivot
- Introsort ⭐ (E): quicksort plus a heapsort fallback; bounded worst case
- Pattern-defeating quicksort (pdqsort) ⭐: today's best unstable sort
- Timsort / Powersort ⭐: best stable sort on real data (runs)
- Heapsort (E); smoothsort ◇ (adaptive, O(1) space)
- Tree sort (via ds's own trees), tournament sort
- Patience sort (also gives the LIS)
- Library sort ◇, cycle sort ◇ (minimum writes; flash-friendly)

**Non-comparison sorts**
- Counting sort (E), radix sort LSD and MSD ⭐, American flag sort (in-place MSD)
- Bucket sort, flashsort ◇, spreadsort ◇
- Ska sort ◇ (fast in-place radix)

**Parallel and external**
- Bitonic sort, odd-even merge sort (sorting networks; SIMD-friendly) ◇
- Sorting networks for small fixed n (E): branch-free sort of 4/8/16 items
- Sample sort, parallel merge sort ◇
- External merge sort with a loser tree ⭐; polyphase merge ◇

**Related**
- Merging k sorted sequences (heap or loser tree)
- Checking sortedness, counting inversions (merge-sort based)
- Stable partition, nth_element-style partial sorting
- Topological sort ✅ (see graphs)

## 2. Selection and order statistics
- Quickselect, introselect ⭐
- Median of medians (deterministic linear time)
- Floyd-Rivest selection ◇
- Top-k with a bounded heap ⭐ (E)
- Weighted median; streaming median (two heaps)
- Rank/select on order-statistic trees ✅, wavelet trees ✅

## 3. Searching
- Binary search with branchless and lower/upper-bound variants ⭐ (E)
- Exponential / galloping search ⭐ (unbounded; merging sorted runs)
- Interpolation search; Fibonacci search ◇; jump search ◇
- Ternary search (unimodal functions)
- Eytzinger-layout and B-layout search (cache-friendly static arrays) ⭐ (E)
- Fractional cascading ◇ (searching many sorted lists)
- Saddleback search in sorted matrices ◇

## 4. Set and sequence operations
- Union, intersection, difference of sorted sequences (merge plus galloping) ⭐
- Set operations on balanced trees via split/join (join-based algorithms) ⭐: one join gives parallelizable union/intersection for AVL, RB, treap, WB trees
- Multiset operations; symmetric difference
- Deduplication (sorted and hash-based)
- Run-length grouping; partition by predicate
- Rotations: reversal, juggling (GCD), block-swap
- Permutations: next/prev permutation, Heap's algorithm, Steinhaus-Johnson-Trotter, ranking and unranking (Lehmer code)
- Combinations and subsets: Gray codes, revolving-door, k-subset enumeration
- Shuffles: Fisher-Yates ⭐ (E); random sampling without replacement (Floyd); reservoir sampling ⭐; weighted sampling (Vose's alias method ⭐, A-Res)
- Longest increasing subsequence (patience, O(n log n))
- Maximum subarray (Kadane)
- Majority element (Boyer-Moore voting), Misra-Gries generalization
- Two-pointer and sliding-window templates (monotonic deque ✅-ready)

## 5. Strings

**Exact matching**
- Naive, Knuth-Morris-Pratt ⭐, Z-algorithm ⭐
- Boyer-Moore, Boyer-Moore-Horspool ✅, Sunday / quick search
- Two-way (Crochemore-Perrin) ✅
- Rabin-Karp (rolling hash) ⭐
- Shift-Or / Bitap (bit-parallel; also approximate)
- Aho-Corasick ✅ (multi-pattern); Commentz-Walter ◇; Wu-Manber ◇
- SIMD substring search (e.g. "Generic SIMD" first/last-byte filter) ◇

**Index construction**
- Suffix array: prefix doubling, SA-IS ⭐ (linear), DC3/skew ◇
- LCP array (Kasai) ⭐; LCP-based queries (distinct substrings, longest repeated)
- Suffix tree: Ukkonen ◇, from SA plus LCP
- Suffix automaton construction ⭐
- Burrows-Wheeler transform and inverse; FM-index backward search ⭐
- Palindromic tree construction ◇

**Combinatorics on words**
- Manacher (all palindromes, linear) ⭐
- Lyndon factorization (Duval) ◇; minimal rotation (Booth) ◇
- Period and border computation; prefix function
- Run enumeration (Main-Lorentz / Kolpakov-Kucherov) ◇

**Approximate matching and distance**
- Levenshtein (DP), Damerau-Levenshtein, banded / Ukkonen cut-off
- Myers' bit-parallel edit distance ⭐
- Hamming, Jaro-Winkler, n-gram/Jaccard similarity
- Longest common subsequence: DP, Hirschberg (linear space), Hunt-Szymanski
- Diff: Myers O(ND) ⭐, patience diff, histogram diff (git's)
- Sequence alignment: Needleman-Wunsch, Smith-Waterman ◇

**Text processing**
- Unicode: normalization (NFC/NFD/NFKC), case folding, grapheme/word/sentence segmentation (your `data/` tables already cover parts), collation (UCA) ◇, bidi ◇, width (East Asian)
- UTF-8/16/32 validation and transcoding ✅-adjacent (SIMD versions ◇)
- Tokenizing, splitting, trimming ✅
- Regular expressions: Thompson NFA, Pike VM (submatches), lazy DFA, DFA minimization (Hopcroft) ⭐ ◇
- Wildcard / glob matching (linear-time) ⭐ (E)
- Natural ("human") sort comparison

## 6. Hashing and checksums
- Non-cryptographic hashes: FNV-1a (E), MurmurHash3, xxHash (XXH3) ⭐, wyhash, SipHash ⭐ (keyed, DoS-resistant; good default for hash tables)
- Integer mixers: splitmix64, Murmur finalizer; Fibonacci hashing
- Universal and tabulation hashing (provable guarantees) ◇
- Rolling hashes: Rabin fingerprint, buzhash; content-defined chunking (FastCDC) ◇
- Checksums: CRC-32/CRC-32C (table and slicing-by-8) ⭐ (E), Adler-32, Fletcher (E)
- Zobrist hashing (game states)
- Locality-sensitive hashing (MinHash LSH, random hyperplanes) ◇

## 7. Graphs
**Traversal**
- BFS ✅, DFS ✅ (iterative ⭐ to avoid stack overflow), iterative deepening
- Bidirectional BFS; 0-1 BFS
- Topological sort ✅ (Kahn and DFS); cycle detection

**Shortest paths**
- Dijkstra ✅ (with indexed / radix heaps ⭐)
- Bellman-Ford ✅, SPFA ◇
- DAG shortest/longest paths
- Floyd-Warshall ⭐; Johnson's algorithm
- A* ⭐, bidirectional Dijkstra, IDA*
- D* Lite / LPA* ◇ (replanning)
- k shortest paths: Yen ⭐, Eppstein ◇
- Contraction hierarchies ◇ (road networks)

**Spanning trees and cuts**
- Kruskal and Prim (✅ MST helper), Borůvka ⭐ (parallel-friendly)
- Minimum spanning arborescence (Chu-Liu/Edmonds) ◇
- Global min cut: Stoer-Wagner, Karger ◇
- Gomory-Hu tree ◇

**Connectivity**
- Connected components ✅; SCC ✅ (Tarjan, Kosaraju)
- Bridges, articulation points, biconnected components ⭐
- 2-SAT via SCC ⭐
- Transitive closure (bitset Warshall) and reduction
- Dominator tree (Lengauer-Tarjan) ⭐ (compilers)
- Dynamic connectivity, offline with union-find rollback ◇

**Flows and matching**
- Ford-Fulkerson, Edmonds-Karp, Dinic ⭐, push-relabel (FIFO, highest-label) ◇
- Min-cost max-flow (successive shortest paths with potentials) ⭐
- Bipartite matching: Hopcroft-Karp ⭐; Hungarian (assignment) ⭐
- General matching: Edmonds' blossom ◇
- Stable marriage (Gale-Shapley); hospitals/residents ◇

**Tours and paths**
- Eulerian path/circuit (Hierholzer) ⭐
- Hamiltonian path / TSP: Held-Karp DP, nearest neighbour, 2-opt, Christofides ◇
- Chinese postman ◇

**Trees**
- Lowest common ancestor: binary lifting ⭐, Euler tour plus RMQ, Tarjan offline
- Heavy-light decomposition ⭐, centroid decomposition
- Tree diameter, center; rerooting DP
- Tree isomorphism (AHU)
- Prüfer codes ◇

**Other**
- Graph coloring: greedy, DSatur, Welsh-Powell; bipartiteness check
- Maximum clique / independent set: Bron-Kerbosch ◇
- PageRank ⭐, HITS ◇; betweenness centrality (Brandes) ◇
- Community detection: label propagation, Louvain ◇
- Planarity testing (left-right) ◇
- Graph isomorphism heuristics (Weisfeiler-Lehman) ◇
- Maximum flow-based scheduling and bipartite applications

## 8. Computational geometry
- Orientation and intersection predicates (robust, integer-exact ⭐ (E))
- Convex hull: Andrew's monotone chain ⭐, Graham scan, Jarvis march, QuickHull, Chan ◇; 3D hull ◇
- Segment intersection: Bentley-Ottmann sweep ◇
- Closest pair of points (divide and conquer)
- Point in polygon (winding number, ray casting) ⭐ (E)
- Polygon area (shoelace), centroid; polygon clipping (Sutherland-Hodgman, Weiler-Atherton, Greiner-Hormann ◇)
- Triangulation: ear clipping, monotone polygon; Delaunay (Bowyer-Watson ⭐, incremental with walking); Voronoi (Fortune ◇)
- Rotating calipers (diameter, width, minimum bounding box)
- Half-plane intersection; Minkowski sum ◇
- Smallest enclosing circle (Welzl) ⭐
- Line simplification (Ramer-Douglas-Peucker ⭐, Visvalingam) (maps!)
- Nearest neighbour queries over k-d trees, R-trees, VP-trees (see roadmap N)
- Space-filling curve encoding (Morton, Hilbert) ⭐ (E)

## 9. Number theory and arithmetic
- GCD (Euclid, binary/Stein (E)), extended Euclid, modular inverse
- Fast exponentiation (binary, Montgomery multiplication ◇)
- Sieve of Eratosthenes, linear sieve, segmented sieve
- Primality: Miller-Rabin (deterministic bases for 64-bit) ⭐, Baillie-PSW ◇
- Factorization: trial division, Pollard rho ⭐, ECM ◇
- Chinese remainder theorem; discrete log (baby-step giant-step); primitive roots
- Integer square root, perfect power tests (E)
- Bit tricks: popcount, clz/ctz, next power of two, bit reversal, Gray code, Morton interleave (E) ⭐
- Big integer arithmetic: schoolbook, Karatsuba, Toom-Cook ◇, FFT/NTT multiplication ◇, division (Knuth D)
- Rational arithmetic, continued fractions, Stern-Brocot ◇

## 10. Numerics (better as a companion library)
- FFT (Cooley-Tukey radix-2, Bluestein ◇), NTT
- Matrix multiplication (blocked; Strassen ◇)
- Gaussian elimination, LU, Cholesky, QR (Householder), SVD ◇
- Sparse matrix-vector multiply (CSR) ⭐ (pairs with roadmap U)
- Iterative solvers: Jacobi, Gauss-Seidel, conjugate gradient ◇
- Root finding: bisection, Newton, secant, Brent
- Integration: trapezoid, Simpson, Romberg, Gaussian quadrature
- Interpolation: Lagrange, Newton, cubic splines
- Linear programming: simplex ◇
- Fixed-point arithmetic helpers (E) ⭐
- Kahan / Neumaier compensated summation ⭐

## 11. Random numbers and randomized algorithms
- PRNGs: splitmix64 (seeding), xoshiro256**/xoroshiro ⭐, PCG ⭐, Lehmer / LCG (E)
- Uniform integers without modulo bias (Lemire's method) ⭐
- Distributions: normal (Box-Muller, ziggurat), exponential, Poisson ◇
- Random sampling and shuffling (see section 4)
- Treap and skip-list coin flips through an injectable RNG (testability!)
- Monte Carlo and Las Vegas templates; randomized rounding ◇

## 12. Dynamic programming and optimization
- Knapsack (0/1, unbounded, bounded with monotone queue)
- Edit distance and LCS (section 5); LIS
- Matrix chain multiplication; optimal BST (Knuth's optimization)
- Interval DP, bitmask DP, digit DP, DP over trees (rerooting)
- Sum over subsets (SOS) / fast zeta and Möbius transforms ◇
- Optimizations: convex hull trick (Li Chao ✅-adjacent), divide-and-conquer, Knuth-Yao, monotone queue ◇
- Backtracking and branch-and-bound templates
- Exact cover: Algorithm X with dancing links ⭐ (fits ds's intrusive lists)
- SAT: DPLL, CDCL basics ◇ (and BDD-based, see roadmap S)
- Local search: hill climbing, simulated annealing, tabu search ◇
- Scheduling: interval scheduling, weighted interval scheduling, EDF, rate-monotonic analysis (E) ◇

## 13. Streaming and approximate algorithms (pair with roadmap E)
- Reservoir sampling ⭐, weighted reservoir
- Heavy hitters: Misra-Gries ⭐, Space-Saving
- Frequency: Count-Min ⭐, Count sketch
- Cardinality: HyperLogLog ⭐, linear counting, KMV
- Quantiles: GK, KLL ⭐, t-digest
- Similarity: MinHash, SimHash; LSH banding
- Sliding-window counts: exponential histograms ◇
- Moving averages: EMA (E), Welford's online mean/variance ⭐ (E)

## 14. Compression and encoding (companion library)
- Run-length encoding (E), delta and delta-of-delta encoding (E) (time series)
- Variable-length integers: LEB128 / varint ⭐ (E), zigzag, Elias gamma/delta, Golomb-Rice
- Bit-packing, frame of reference, PForDelta, Simple-8b, StreamVByte
- Huffman (canonical) ⭐, arithmetic / range coding ◇, ANS (rANS, tANS) ◇
- LZ77 / LZSS ⭐ (E), LZ78 / LZW, LZ4-style fast LZ ◇
- DEFLATE decoder (inflate) ◇: needed to read zip/png/gzip
- Burrows-Wheeler plus move-to-front plus RLE (bzip2 pipeline) ◇
- Base64 / base32 / hex (E) ⭐
- Bitmap compression: Roaring, EWAH (roadmap I)

## 15. Caching and memory management
- Eviction policies: LRU, LFU, ARC, 2Q, CLOCK, CLOCK-Pro, S3-FIFO, SIEVE, W-TinyLFU (roadmap P)
- Garbage collection algorithms ◇: reference counting with cycle detection (trial deletion), mark-sweep, copying (Cheney), generational, for persistent structures and the history module
- Allocator algorithms: buddy, TLSF, slab, segregated fits (roadmap O)
- Memory reclamation for lock-free structures: hazard pointers, epoch-based, QSBR (roadmap M)

## 16. Concurrency techniques (pair with roadmap M)
- Lock-free: CAS loops, ABA solutions (tagged pointers, hazard pointers)
- Wait-free queue constructions ◇
- Flat combining, elimination backoff (stacks) ◇
- RCU (read-copy-update) ◇
- Sharding and lock striping
- Seqlocks (E), ticket locks and MCS locks (as structures) ◇
- Work stealing (Chase-Lev deque) ⭐

## 17. Database and external-memory algorithms
- External sort (section 1); k-way merge
- Joins: nested loop, sort-merge, hash join (grace hash) ◇
- B-tree bulk loading (bottom-up build from sorted data) ⭐
- LSM compaction strategies: leveled, tiered ◇
- Buffer-tree / batched updates ◇
- Cache-oblivious algorithms: funnelsort ◇, vEB layout search

## 18. Cryptography (learning only, or a separate audited library)
- Hashes: SHA-256, SHA-3/Keccak, BLAKE2/BLAKE3
- MAC / KDF: HMAC, HKDF, PBKDF2, Argon2
- Ciphers: ChaCha20-Poly1305, AES (constant-time is hard: lookup tables leak)
- Public key: X25519, Ed25519, RSA basics
- Merkle trees, hash chains, commitments (fit ds: roadmap T)

---

## Where to look things up

- NIST DADS: https://xlinux.nist.gov/dads/ (definitions, as you know)
- cp-algorithms: https://cp-algorithms.com/ (clear write-ups of most of 5, 7, 8, 9, 12)
- CLRS (*Introduction to Algorithms*), Sedgewick & Wayne (*Algorithms*), Skiena (*The Algorithm Design Manual*: also a catalogue)
- Knuth, *TAOCP* (volume 3 for sorting and searching, 4A/4B for combinatorics and dancing links)
- Gusfield, *Algorithms on Strings, Trees, and Sequences*; Navarro & Raffinot, *Flexible Pattern Matching in Strings*
- de Berg et al., *Computational Geometry: Algorithms and Applications*
- Cormode & Yi, *Small Summaries for Big Data* (sketches)
- Herlihy & Shavit, *The Art of Multiprocessor Programming* (concurrent structures)
