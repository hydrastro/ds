# NIST DADS coverage

This file maps every entry of the NIST *Dictionary of Algorithms and Data
Structures* (DADS, https://xlinux.nist.gov/dads/) to its state in `ds`.
It is the yardstick for completeness: every `todo` names the module that
will own it, and entries move to `done` as modules land.

Status legend:

- **done**: implemented; the module column says where.
- **todo**: implementable and planned; the module column is the target.
- **def**: definition-only: notation, complexity classes, models of
  computation, theorems, general techniques (divide and conquer, greedy,
  ...) and properties (stable, in-place, ...). Nothing to implement,
  though many are exercised by implemented modules.

Several DADS entries are cross-references ("see ..."); they are listed
under their own name and classified like their target.

Entries marked † come from the tail of the index (after "shaker sort"),
which was assembled from the DADS by-area index plus the per-entry pages
rather than read off the A-Z page; they should be rechecked against the
live index.

## Summary

| status | entries |
|---|---|
| done | 216 |
| todo | 508 |
| def | 574 |
| total | 1298 |

## Todo by target module

Families ordered by how many DADS entries they close.

| target | entries | terms |
|---|---|---|
| `algo/graph_alg` | 39 | all simple paths, arborescence, articulation point, best-first search, biconnected component, biconnected graph, Boruvka's algorithm, bridge, centroid, Chinese postman problem, critical path problem, cut vertex, DAG shortest paths, DFS forest, diameter, edge connectivity, Euler cycle, Eulerian graph, Eulerian path, feedback edge set, Johnson's algorithm, k-connected graph, Königsberg bridges problem, Kruskal's algorithm, kth shortest path, lowest common ancestor, minimum vertex cut, reduced digraph, shortest path, shortest spanning tree, single-destination shortest-path problem, single-pair shortest-path problem, single-source shortest-path problem, spanning tree, SST, transitive closure, transitive reduction, triconnected graph, vertex connectivity |
| `algo/string_search` | 36 | Alpha Skip Search algorithm, Apostolico-Crochemore, Apostolico-Giancarlo algorithm, Boyer-Moore, Boyer-Moore-Horspool, brute force string search, brute force string search with mismatches, Colussi, Commentz-Walter, deterministic finite automata string search, exact string matching, Galil-Giancarlo, Galil-Seiferas, Horspool, KmpSkip Search, Maximal Shift, Morris-Pratt algorithm, naive string search, Not So Naive, optimal mismatch, quick search, Raita, Reverse Colussi, Reverse Factor, Shift-Or, skip search, Smith algorithm, string matching, string matching on ordered alphabets, string matching with mismatches, string searching, text searching, Turbo-BM, Turbo Reverse Factor, Two Way algorithm, Zhu-Takaoka |
| `algo/graph_hard` | 33 | bottleneck traveling salesman, capacitated facility location, Christofides algorithm, chromatic index, chromatic number, clique, clique problem, discrete p-center, edge coloring, Euclidean Steiner tree, Euclidean traveling salesman problem, facility location, feedback vertex set, graph coloring, graph isomorphism, graph partition, Hamiltonian cycle, Hamiltonian path, independent set, k-clustering, k-coloring, maximal independent set, path cover, rectilinear Steiner tree, set cover, Steiner tree, subgraph isomorphism, traveling salesman, TSP, vehicle routing problem, vertex coloring, vertex cover, VRP |
| `algo/sort_misc` | 32 | adaptive sort, balanced k-way merge sort, balanced merge sort, balanced quicksort, balanced two-way merge sort, bingo sort, blind sort, bogosort, bozo sort, dual-pivot quicksort, k-way merge sort, linear insertion sort, lucky sort, nonbalanced merge, nonbalanced merge sort, optimal polyphase merge, optimal polyphase merge sort, oscillating merge sort, polyphase merge, polyphase merge sort, p-way merge sort, qm sort, q sort, quicksort, rapid sort, shadow merge, shadow merge insert, simple merge, sinking sort, strand sort, three-way merge sort, two-way merge sort |
| `algo/numeric (extensions)` | 23 | Ackermann's function, BBP algorithm, Chinese remainder theorem, CORDIC, cube root, Doomsday rule, factorial, Ferguson-Forcade algorithm, Fibonacci number, gamma function, Horner's rule, inverse Ackermann function, kth order Fibonacci numbers, midrange, mode, pth order Fibonacci numbers, reduced basis, Rice's method, secant search, square root, Stirling's approximation, Stirling's formula, Zeller's congruence |
| `algo/flow` | 21 | admissible vertex, augmenting path, blocking flow, capacity, capacity constraint, flow, flow conservation, flow function, flow network, Ford-Fulkerson method, layered graph, Malhotra-Kumar-Maheshwari blocking flow, maximum-flow problem, minimum cut, network flow, network flow problem, saturated edge, skew symmetry, s-t cut, supersink, supersource |
| `algo/sort_dist` | 21 | address-calculation sort, American flag sort, bin sort, bucket sort, distribution sort, distributive partitioning sort, flash sort, histogram sort, interpolation sort, JSort, J sort, linear probing sort, multikey Quicksort, permutation sort, pigeonhole sort, postman's sort, range sort, restricted universe sort, shuffle sort, three-way radix quicksort, top-down radix sort |
| `algo/compress` | 18 | adaptive Huffman coding, algorithm BSTW, algorithm FGK, arithmetic coding, coding tree, dynamic Huffman coding, Huffman coding, k-ary Huffman coding, Lempel-Ziv-Welch, LZW compression, move, order-preserving Huffman coding, prefix code, Shannon-Fano coding, space-constrained Lempel-Ziv-Welch, star encoding, static Huffman coding, Vitter's algorithm |
| `algo/geometry` | 16 | bisector, Bresenham's algorithm, Euclidean distance, extreme point, geometric optimization problem, Hausdorff distance, Lm distance, Manhattan distance, Minkowski distance, nearest neighbor, optimal polygon triangulation problem, optimal triangulation problem, orthogonally convex rectilinear polygon, slope selection, star-shaped polygon, Wu's line algorithm |
| `algo/matching` | 15 | alternating path, assignment problem, bipartite matching, blossom, free edge, free vertex, Hungarian algorithm, marriage problem, matched edge, matched vertex, matching, maximum bipartite matching, Munkres' assignment algorithm, perfect matching, stable marriage problem |
| `algo/string_approx` | 13 | approximate string matching, edit operation, edit script, heaviest common subsequence, Jaro-Winkler, K-dominant match, local alignment, longest common substring, Ratcliff/Obershelp pattern recognition, shortest common supersequence, shortest common superstring, Smith-Waterman algorithm, string matching with errors |
| `algo/combinatorics` | 12 | Collatz problem, combination, derangement, 8 queens, Johnson-Trotter, knight's tour, n queens, partition, Steinhaus-Johnson-Trotter, towers of Hanoi, Trotter-Johnson, unranking |
| `algo/knapsack` | 12 | binary knapsack problem, bin packing problem, continuous knapsack problem, cutting stock problem, fractional knapsack problem, knapsack problem, matrix-chain multiplication problem, set packing, strip packing, UKP, unbounded knapsack problem, 0/1 knapsack problem |
| `algo/sequence (extensions)` | 11 | balanced multiway merge, Dutch national flag, ideal merge, inversion list, k-way merge, multiprefix, multiway merge, optimal merge, parallel prefix computation, prefix sums, scan |
| `lib/automaton` | 11 | deterministic finite automaton, DFA, finite state automaton, finite state machine, finite state machine minimization, finite state transducer, Mealy machine, Moore machine, NFA, nondeterministic finite automaton, union of automata |
| `lib/open_map (probe policies)` | 9 | clustering free, double hashing, LCFS hashing, primary clustering, probe sequence, rehashing, secondary clustering, simple uniform hashing, uniform hashing |
| `lib/grid_file` | 8 | BANG file, EXCELL, fixed-grid method, grid file, multilayer grid file, R-file, twin grid file, two-level grid file |
| `lib/hb_tree` | 8 | BD-tree, buddy tree, BV-tree, cell tree, D-tree, GBD-tree, hB-tree, P-tree |
| `lib/quadtree` | 7 | bintree, linear quadtree, PAM, point access method, quadtree, quad trie, regular decomposition |
| `algo/hash (new functions)` | 6 | incremental hashing, linear hash, multiplication method, order-preserving hash, Pearson's hash, universal hashing |
| `algo/lp` | 6 | cutting plane, dual linear program, integer linear program, linear program, mixed integer linear program, simplex |
| `algo/phonetic` | 6 | Caverphone, double metaphone, metaphone, NYSIIS, phonetic coding, soundex |
| `algo/random (extensions)` | 6 | linear congruential generator, Randomized-Select, random sampling, reservoir sampling, spectral test, unbiased coin flipping algorithm |
| `algo/search (extensions)` | 6 | block search, Bond Sequential Search, Fibonaccian search, interpolation-sequential search, jump search, ternary search |
| `algo/sort_heap` | 6 | adaptive heap sort, dominance tree sort, smoothsort, tournament, tournament sort, tree sort |
| `lib/rtree` | 6 | MBB, minimum bounding box, R+-tree, R*-tree, R-tree, spatial access method |
| `lib/self_org_list` | 6 | frequency count heuristic, move-to-front heuristic, move-to-root heuristic, self-organizing list, self-organizing sequential search, transpose sequential search |
| `algo/matrix` | 5 | lower triangular matrix, matrix multiplication, strictly lower triangular matrix, strictly upper triangular matrix, upper triangular matrix |
| `algo/parallel_sim` | 5 | list contraction, pointer jumping, recursive doubling, shortcutting, tree contraction |
| `lib/bdd` | 5 | BDD, OBDD, ordered binary decision diagram, reduced ordered binary decision diagram, ROBDD |
| `lib/linear_hash_map` | 5 | dynamic hashing, expandable hashing, hashbelt, linear hashing, virtual hashing |
| `lib/suffix_tree` | 5 | Burrows-Wheeler transform, BWT, inverse suffix array, multi suffix tree, suffix tree |
| `algo/external_sort` | 4 | external merge, external quicksort, external radix sort, external sort |
| `algo/hmm` | 4 | Baum Welch algorithm, hidden Markov model, HMM, Viterbi algorithm |
| `algo/perfect_hash` | 4 | minimal perfect hashing, optimal hashing, order-preserving minimal perfect hashing, perfect hashing |
| `algo/select` | 4 | Find, MODIFIND, select and partition, select mode |
| `lib/binary_tree_repr` | 4 | binary tree representation of trees, doubly-chained tree, filial-heir chain, first child-next sibling binary tree |
| `lib/elastic_trie` | 4 | blind trie, bucket trie, compact trie, elastic-bucket trie |
| `lib/kdb_tree` | 4 | adaptive k-d tree, extended k-d tree, k-d-B-tree, skd-tree |
| `lib/sorted_list` | 4 | ordered linked list, sorted array, sorted list, unsorted list |
| `lib/sparse_matrix` | 4 | huge sparse array, orthogonal lists, ragged matrix, sparse matrix |
| `lib/suffix_automaton` | 4 | compact DAWG, DAWG, directed acyclic word graph, suffix automaton |
| `lib/threaded_tree` | 4 | Cupif-Giannini tree traversal, right-threaded tree, threaded binary tree, threaded tree |
| `algo/fft` | 3 | fast fourier transform, FFT, finite Fourier transform |
| `lib/merkle_tree` | 3 | hash tree, linked data signature, Merkle tree |
| `lib/wb_tree` | 3 | BB(α) tree, root balance, weight-balanced tree |
| `lib/association_list` | 2 | association list, property list |
| `lib/btree (2-3 / 2-3-4 presets)` | 2 | 2-3-4 tree, 2-3 tree |
| `lib/cactus_stack` | 2 | cactus stack, saguaro stack |
| `lib/diet` | 2 | diet, discrete interval encoding tree |
| `lib/iblt` | 2 | IBLT, invertible Bloom lookup table |
| `lib/multiset` | 2 | bag, multi-set |
| `lib/rbst` | 2 | randomized binary search tree, RBST |
| `lib/tst` | 2 | ternary search tree, TST |
| `lib/two_choice_map` | 2 | 2-choice hashing, 2-left hashing |
| `algo/cartesian_tree` | 1 | cartesian tree |
| `algo/game` | 1 | minimax |
| `algo/lsh` | 1 | locality-sensitive hashing |
| `algo/schorr_waite` | 1 | Schorr-Waite graph marking algorithm |
| `lib/bloom_filter (superimposed codes)` | 1 | superimposed code |
| `lib/bsp_tree` | 1 | BSP-tree |
| `lib/buddy_allocator` | 1 | buddy system |
| `lib/coalesced_map` | 1 | coalesced chaining |
| `lib/cuckoo_map` | 1 | cuckoo hashing |
| `lib/enfilade` | 1 | enfilade |
| `lib/extendible_hash` | 1 | extendible hashing |
| `lib/indexed_heap (hash index)` | 1 | hash heap |
| `lib/jump_list` | 1 | jump list |
| `lib/k2_tree` | 1 | k²-tree |
| `lib/octree` | 1 | octree |
| `lib/pagoda` | 1 | pagoda |
| `lib/scapegoat_tree` | 1 | scapegoat tree |
| `lib/shadow_heap` | 1 | shadow heap |
| `lib/ub_tree` | 1 | UB-tree |

## Index A-Z

### Symbols and digits

| entry | status | module |
|---|---|---|
| α | def |  |
| ω | def |  |
| Ω | def |  |
| ρ-approximation algorithm | def |  |
| ∼ | def |  |
| Θ | def |  |

### A

| entry | status | module |
|---|---|---|
| absolute performance guarantee | def |  |
| abstract data type | def |  |

### Symbols and digits

| entry | status | module |
|---|---|---|
| (a,b)-tree | done | `lib/btree` (minimum degree t gives a (t, 2t)-tree) |

### A

| entry | status | module |
|---|---|---|
| accepting state | def |  |
| Ackermann's function | todo | `algo/numeric` (extensions) |
| active data structure | def |  |
| acyclic directed graph | def |  |
| acyclic graph | def |  |
| adaptive heap sort | todo | `algo/sort_heap` |
| adaptive Huffman coding | todo | `algo/compress` |
| adaptive k-d tree | todo | `lib/kdb_tree` |
| adaptive sort | todo | `algo/sort_misc` |
| address-calculation sort | todo | `algo/sort_dist` |
| adjacency-list representation | done | `lib/graph, algo/graph_view` (CSR) |
| adjacency-matrix representation | def |  |
| adjacent | def |  |
| admissible vertex | todo | `algo/flow` |
| ADT | def |  |
| adversary | def |  |
| Aho-Corasick | done | `lib/aho_corasick` |
| algorithm | def |  |
| algorithm BSTW | todo | `algo/compress` |
| algorithm FGK | todo | `algo/compress` |
| algorithmically solvable | def |  |
| all pairs shortest path | done | `algo/graph_view` (Floyd-Warshall) |
| all simple paths | todo | `algo/graph_alg` |
| alphabet | def |  |
| Alpha Skip Search algorithm | todo | `algo/string_search` |
| alternating path | todo | `algo/matching` |
| alternating Turing machine | def |  |
| alternation | def |  |
| American flag sort | todo | `algo/sort_dist` |
| amortized cost | def |  |
| ancestor | def |  |
| and | def |  |
| ANSI | def |  |
| antichain | def |  |
| antisymmetric | def |  |
| Apostolico-Crochemore | todo | `algo/string_search` |
| Apostolico-Giancarlo algorithm | todo | `algo/string_search` |
| approximate string matching | todo | `algo/string_approx` |
| approximation algorithm | def |  |
| arborescence | todo | `algo/graph_alg` |
| arc | def |  |
| arithmetic coding | todo | `algo/compress` |
| array | done | `lib/vec` |
| array index | def |  |
| array merging | done | `algo/sequence` (ds_merge) |
| array search | done | `algo/search` |
| articulation point | todo | `algo/graph_alg` |
| assignment problem | todo | `algo/matching` |
| association list | todo | `lib/association_list` |
| associative | def |  |
| associative array | done | `lib/hash_table, lib/rh_map, lib/string_map` |
| asymptotically tight bound | def |  |
| asymptotic bound | def |  |
| asymptotic space complexity | def |  |
| asymptotic time complexity | def |  |
| asymptotic upper bound | def |  |
| augmenting path | todo | `algo/flow` |
| automaton | def |  |
| average case | def |  |
| average-case cost | def |  |
| AVL tree | done | `lib/avl` |
| axiomatic semantics | def |  |

### B

| entry | status | module |
|---|---|---|
| backtracking | def |  |
| bag | todo | `lib/multiset` |
| balance | def |  |
| balanced binary search tree | done | `lib/avl, lib/rbt, lib/treap` |
| balanced binary tree | done | `lib/avl, lib/rbt` |
| balanced k-way merge sort | todo | `algo/sort_misc` |
| balanced merge sort | todo | `algo/sort_misc` |
| balanced multiway merge | todo | `algo/sequence` (extensions) |
| balanced multiway tree | done | `lib/btree` |
| balanced quicksort | todo | `algo/sort_misc` |
| balanced tree | done | `lib/avl, lib/rbt, lib/btree` |
| balanced two-way merge sort | todo | `algo/sort_misc` |
| BANG file | todo | `lib/grid_file` |
| Batcher sort | done | `algo/sort_classic` (bitonic) |
| Baum Welch algorithm | todo | `algo/hmm` |
| BB(α) tree | todo | `lib/wb_tree` |
| BBP algorithm | todo | `algo/numeric` (extensions) |
| BDD | todo | `lib/bdd` |
| BD-tree | todo | `lib/hb_tree` |
| Bellman-Ford algorithm | done | `algo/graph_view, lib/graph` |
| Benford's law | def |  |
| best case | def |  |
| best-case cost | def |  |
| best-first search | todo | `algo/graph_alg` |
| biconnected component | todo | `algo/graph_alg` |
| biconnected graph | todo | `algo/graph_alg` |
| bidirectional bubble sort | done | `algo/sort_classic` (cocktail) |
| big-O notation | def |  |
| binary function | def |  |
| binary GCD | done | `algo/numeric` (ds_binary_gcd) |
| binary heap | done | `lib/heap` |
| binary insertion sort | done | `algo/sort_classic` |
| binary knapsack problem | todo | `algo/knapsack` |
| binary priority queue | done | `lib/heap` |
| binary relation | def |  |
| binary search | done | `algo/search` |
| binary search tree | done | `lib/bst` |
| binary tree | done | `lib/bst` |
| binary tree representation of trees | todo | `lib/binary_tree_repr` |
| bingo sort | todo | `algo/sort_misc` |
| binomial heap | done | `lib/binomial_heap` |
| binomial tree | done | `lib/binomial_heap` |
| bin packing problem | todo | `algo/knapsack` |
| bin sort | todo | `algo/sort_dist` |
| bintree | todo | `lib/quadtree` |
| bipartite graph | done | `algo/graph_view` (ds_gview_bipartite) |
| bipartite matching | todo | `algo/matching` |
| bisector | todo | `algo/geometry` |
| bitonic sort | done | `algo/sort_classic` |
| bit vector | done | `lib/bitset, lib/bitvector` |
| Bk tree | done | `lib/binomial_heap` |
| blind sort | todo | `algo/sort_misc` |
| blind trie | todo | `lib/elastic_trie` |
| block | def |  |
| block addressing index | def |  |
| blocking flow | todo | `algo/flow` |
| block search | todo | `algo/search` (extensions) |
| Bloom filter | done | `lib/bloom_filter` |
| blossom | todo | `algo/matching` |
| bogosort | todo | `algo/sort_misc` |
| Bond Sequential Search | todo | `algo/search` (extensions) |
| boolean | def |  |
| boolean expression | def |  |
| boolean function | def |  |
| border | def |  |
| Boruvka's algorithm | todo | `algo/graph_alg` |
| bottleneck traveling salesman | todo | `algo/graph_hard` |
| boundary-based representation | def |  |
| bounded error probability in polynomial time | def |  |
| bounded queue | done | `lib/circular_buffer, lib/array_deque` (fixed buffer) |
| bounded stack | done | `lib/vec` (fixed buffer) |
| Boyer-Moore | todo | `algo/string_search` |
| Boyer-Moore-Horspool | todo | `algo/string_search` |
| bozo sort | todo | `algo/sort_misc` |
| B+-tree | done | `lib/bplus_tree` |
| BPP | def |  |
| Bradford's law | def |  |
| branch and bound | def |  |
| breadth-first search | done | `algo/graph_view, lib/graph` |
| Bresenham's algorithm | todo | `algo/geometry` |
| bridge | todo | `algo/graph_alg` |
| British Museum technique | def |  |
| brute force | def |  |
| brute force string search | todo | `algo/string_search` |
| brute force string search with mismatches | todo | `algo/string_search` |
| BSP-tree | todo | `lib/bsp_tree` |
| B*-tree | done | `lib/bplus_tree` (B* split policy) |
| B-tree | done | `lib/btree` |
| bubble sort | done | `algo/sort_classic` |
| bucket | def |  |
| bucket array | def |  |
| bucketing method | def |  |
| bucket sort | todo | `algo/sort_dist` |
| bucket trie | todo | `lib/elastic_trie` |
| buddy system | todo | `lib/buddy_allocator` |
| buddy tree | todo | `lib/hb_tree` |
| build-heap | done | `lib/heap, algo/sort` (heap sort) |
| Burrows-Wheeler transform | todo | `lib/suffix_tree` |
| busy beaver | def |  |
| BV-tree | todo | `lib/hb_tree` |
| BWT | todo | `lib/suffix_tree` |
| Byzantine generals | def |  |

### C

| entry | status | module |
|---|---|---|
| cactus stack | todo | `lib/cactus_stack` |
| Calculus of Communicating Systems | def |  |
| calendar queue | done | `lib/calendar_queue` |
| candidate consistency testing | def |  |
| candidate verification | def |  |
| canonical complexity class | def |  |
| capacitated facility location | todo | `algo/graph_hard` |
| capacity | todo | `algo/flow` |
| capacity constraint | todo | `algo/flow` |
| cartesian tree | todo | `algo/cartesian_tree` |
| Caverphone | todo | `algo/phonetic` |
| CCS | def |  |
| cell probe model | def |  |
| cell tree | todo | `lib/hb_tree` |
| cellular automaton | def |  |
| centroid | todo | `algo/graph_alg` |
| certificate | def |  |
| chain | def |  |
| chaining | done | `lib/hash_table` (chaining mode) |
| child | def |  |
| Chinese postman problem | todo | `algo/graph_alg` |
| Chinese remainder theorem | todo | `algo/numeric` (extensions) |
| Christofides algorithm | todo | `algo/graph_hard` |
| chromatic index | todo | `algo/graph_hard` |
| chromatic number | todo | `algo/graph_hard` |
| circuit | def |  |
| circuit complexity | def |  |
| circuit value problem | def |  |
| circular list | done | `lib/ring_list` |
| circular queue | done | `lib/circular_buffer` |
| clique | todo | `algo/graph_hard` |
| clique problem | todo | `algo/graph_hard` |
| clustering | def |  |
| clustering free | todo | `lib/open_map` (probe policies) |
| coalesced chaining | todo | `lib/coalesced_map` |
| coarsening | def |  |
| cocktail shaker sort | done | `algo/sort_classic` |
| codeword | def |  |
| coding tree | todo | `algo/compress` |
| Collatz problem | todo | `algo/combinatorics` |
| collective recursion | def |  |
| collision | def |  |
| collision resolution scheme | done | `lib/hash_table, lib/rh_map` |
| Colussi | todo | `algo/string_search` |
| combination | todo | `algo/combinatorics` |
| comb sort | done | `algo/sort_classic` |
| Commentz-Walter | todo | `algo/string_search` |
| Communicating Sequential Processes | def |  |
| commutative | def |  |
| compact data structure | def |  |
| compact DAWG | todo | `lib/suffix_automaton` |
| compact trie | todo | `lib/elastic_trie` |
| comparison sort | done | `algo/sort, algo/sort_classic` |
| competitive analysis | def |  |
| competitive ratio | def |  |
| complement | def |  |
| complete binary tree | def |  |
| complete graph | def |  |
| completely connected graph | def |  |
| complete tree | def |  |
| complexity | def |  |
| complexity class | def |  |
| compound algorithm | def |  |
| computable | def |  |
| concave function | def |  |
| concurrent data structure | def |  |
| concurrent flow | def |  |
| concurrent read, concurrent write | def |  |
| concurrent read, exclusive write | def |  |
| confluently persistent data structure | def |  |
| conjunction | def |  |
| connected components | done | `lib/graph` (weak components) |
| connected graph | def |  |
| constant function | def |  |
| continuous knapsack problem | todo | `algo/knapsack` |
| Cook reduction | def |  |
| Cook's theorem | def |  |
| CORDIC | todo | `algo/numeric` (extensions) |
| counting sort | done | `algo/sort_classic` (ds_counting_sort_keyed) |
| covering | def |  |
| CRC | done | `algo/hash` (CRC-32, CRC-32C) |
| CRCW | def |  |
| CREW | def |  |
| critical path problem | todo | `algo/graph_alg` |
| CSP | def |  |
| CTL | def |  |
| cube root | todo | `algo/numeric` (extensions) |
| cuckoo hashing | todo | `lib/cuckoo_map` |
| Cupif-Giannini tree traversal | todo | `lib/threaded_tree` |
| cut | def |  |
| cutting plane | todo | `algo/lp` |
| cutting stock problem | todo | `algo/knapsack` |
| cutting theorem | def |  |
| cut vertex | todo | `algo/graph_alg` |
| cycle | def |  |
| cyclic redundancy check | done | `algo/hash` |

### D

| entry | status | module |
|---|---|---|
| D-adjacent | def |  |
| DAG | def |  |
| DAG shortest paths | todo | `algo/graph_alg` |
| data structure | def |  |
| DAWG | todo | `lib/suffix_automaton` |
| decidable language | def |  |
| decidable problem | def |  |
| decimation | def |  |
| decision problem | def |  |
| decomposable searching problem | def |  |
| degree | def |  |
| dense graph | def |  |
| depoissonization | def |  |
| depth | def |  |
| depth-first search | done | `lib/graph` |
| deque | done | `lib/deque, lib/array_deque` |
| derangement | todo | `algo/combinatorics` |
| descendant | def |  |
| deterministic | def |  |
| deterministic algorithm | def |  |
| deterministic finite automata string search | todo | `algo/string_search` |
| deterministic finite automaton | todo | `lib/automaton` |
| deterministic finite state machine | def |  |
| deterministic finite tree automaton | def |  |
| deterministic pushdown automaton | def |  |
| deterministic random bit generator | done | `algo/random` (xoshiro, not cryptographic) |
| deterministic tree automaton | def |  |
| Deutsch-Jozsa algorithm | def |  |
| DFA | todo | `lib/automaton` |
| DFS | done | `lib/graph` |
| DFS forest | todo | `algo/graph_alg` |
| DFTA | def |  |
| diagonalization | def |  |
| diameter | todo | `algo/graph_alg` |
| Dianna | def |  |
| dichotomic search | done | `algo/search` |
| dictionary | done | `lib/hash_table, lib/rh_map, lib/avl, ...` |
| diet | todo | `lib/diet` |
| difference | done | `algo/sequence` (ds_set_difference) |
| digital search tree | done | `lib/trie` |
| digital tree | done | `lib/trie` |
| digraph | def |  |
| Dijkstra's algorithm | done | `algo/graph_view, lib/graph` |
| diminishing increment sort | done | `algo/sort` (Shell sort) |
| dining philosophers | def |  |
| direct chaining | done | `lib/hash_table` |
| directed acyclic graph | def |  |
| directed acyclic word graph | todo | `lib/suffix_automaton` |
| directed graph | done | `lib/graph` |
| discrete interval encoding tree | todo | `lib/diet` |
| discrete p-center | todo | `algo/graph_hard` |
| disjoint set | done | `lib/disjoint_set, lib/rollback_union_find` |
| disjunction | def |  |
| distributional complexity | def |  |
| distribution sort | todo | `algo/sort_dist` |
| distributive partitioning sort | todo | `algo/sort_dist` |
| divide and conquer | def |  |
| divide and marriage before conquest | def |  |
| domain | def |  |
| dominance tree sort | todo | `algo/sort_heap` |
| don't care | def |  |
| Doomsday rule | todo | `algo/numeric` (extensions) |
| double-direction bubble sort | done | `algo/sort_classic` (cocktail) |
| double-ended priority queue | done | `lib/min_max_heap` |
| double hashing | todo | `lib/open_map` (probe policies) |
| double left rotation | done | `lib/avl` |
| double metaphone | todo | `algo/phonetic` |
| double right rotation | done | `lib/avl` |
| doubly-chained tree | todo | `lib/binary_tree_repr` |
| doubly-ended queue | done | `lib/deque` |
| doubly linked list | done | `lib/dlist` |
| DPDA | def |  |
| DRBG | done | `algo/random` |
| D-tree | todo | `lib/hb_tree` |
| dual | def |  |
| dual linear program | todo | `algo/lp` |
| dual-pivot quicksort | todo | `algo/sort_misc` |
| Dutch national flag | todo | `algo/sequence` (extensions) |
| dyadic tree | def |  |
| dynamic | def |  |
| dynamic array | done | `lib/vec` |
| dynamic hashing | todo | `lib/linear_hash_map` |
| dynamic Huffman coding | todo | `algo/compress` |
| dynamic programming | def |  |
| dynamization transformation | def |  |

### E

| entry | status | module |
|---|---|---|
| easy split, hard merge | def |  |
| edge | def |  |
| edge coloring | todo | `algo/graph_hard` |
| edge connectivity | todo | `algo/graph_alg` |
| edge crossing | def |  |
| edge-weighted graph | def |  |
| edit distance | done | `algo/stringology` (Levenshtein, OSA) |
| edit operation | todo | `algo/string_approx` |
| edit script | todo | `algo/string_approx` |
| efficiency | def |  |

### Symbols and digits

| entry | status | module |
|---|---|---|
| 8 queens | todo | `algo/combinatorics` |

### E

| entry | status | module |
|---|---|---|
| elastic-bucket trie | todo | `lib/elastic_trie` |
| element uniqueness | done | `algo/sequence` (ds_unique after sort) |
| end-of-string | def |  |
| enfilade | todo | `lib/enfilade` |
| ERCW | def |  |
| EREW | def |  |
| Euclidean algorithm | done | `algo/numeric` (ds_gcd) |
| Euclidean distance | todo | `algo/geometry` |
| Euclidean Steiner tree | todo | `algo/graph_hard` |
| Euclidean traveling salesman problem | todo | `algo/graph_hard` |
| Euclid's algorithm | done | `algo/numeric` |
| Euler cycle | todo | `algo/graph_alg` |
| Eulerian graph | todo | `algo/graph_alg` |
| Eulerian path | todo | `algo/graph_alg` |
| Euler's formula | def |  |
| exact string matching | todo | `algo/string_search` |
| EXCELL | todo | `lib/grid_file` |
| exchange sort | done | `algo/sort_classic` (bubble) |
| exclusive or | def |  |
| exclusive read, concurrent write | def |  |
| exclusive read, exclusive write | def |  |
| exhaustive search | def |  |
| existential state | def |  |
| expandable hashing | todo | `lib/linear_hash_map` |
| expander graph | def |  |
| exponential | def |  |
| extended binary tree | def |  |
| extended Euclid's algorithm | done | `algo/numeric` (ds_ext_gcd) |
| extended k-d tree | todo | `lib/kdb_tree` |
| extendible hashing | todo | `lib/extendible_hash` |
| external chaining | done | `lib/hash_table` |
| external index | def |  |
| external memory algorithm | def |  |
| external memory data structure | def |  |
| external merge | todo | `algo/external_sort` |
| external node | def |  |
| external quicksort | todo | `algo/external_sort` |
| external radix sort | todo | `algo/external_sort` |
| external sort | todo | `algo/external_sort` |
| extrapolation search | done | `algo/search` (interpolation) |
| extremal | def |  |
| extreme point | todo | `algo/geometry` |

### F

| entry | status | module |
|---|---|---|
| facility location | todo | `algo/graph_hard` |
| factor | def |  |
| factorial | todo | `algo/numeric` (extensions) |
| fast fourier transform | todo | `algo/fft` |
| fathoming | def |  |
| feasible region | def |  |
| feasible solution | def |  |
| feedback edge set | todo | `algo/graph_alg` |
| feedback vertex set | todo | `algo/graph_hard` |
| Ferguson-Forcade algorithm | todo | `algo/numeric` (extensions) |
| FFT | todo | `algo/fft` |
| Fibonaccian search | todo | `algo/search` (extensions) |
| Fibonacci heap | done | `lib/fibonacci_heap` |
| Fibonacci number | todo | `algo/numeric` (extensions) |
| Fibonacci tree | def |  |
| FIFO | done | `lib/queue` |
| filial-heir chain | todo | `lib/binary_tree_repr` |
| Find | todo | `algo/select` |
| find kth least element | done | `algo/sort` (nth_element) |
| finitary tree | def |  |
| finite Fourier transform | todo | `algo/fft` |
| finite state automaton | todo | `lib/automaton` |
| finite state machine | todo | `lib/automaton` |
| finite state machine minimization | todo | `lib/automaton` |
| finite state transducer | todo | `lib/automaton` |
| first child-next sibling binary tree | todo | `lib/binary_tree_repr` |
| first come, first served | def |  |
| first-in, first-out | done | `lib/queue` |
| Fisher-Yates shuffle | done | `algo/sequence` (ds_shuffle) |
| fixed-grid method | todo | `lib/grid_file` |
| flash sort | todo | `algo/sort_dist` |
| flow | todo | `algo/flow` |
| flow conservation | todo | `algo/flow` |
| flow function | todo | `algo/flow` |
| flow network | todo | `algo/flow` |
| Floyd-Warshall algorithm | done | `algo/graph_view` |
| Ford-Bellman | done | `algo/graph_view` |
| Ford-Fulkerson method | todo | `algo/flow` |
| forest | def |  |
| forest editing problem | def |  |
| formal language | def |  |
| formal methods | def |  |
| formal verification | def |  |
| forward index | def |  |
| fractional knapsack problem | todo | `algo/knapsack` |
| fractional solution | def |  |
| free edge | todo | `algo/matching` |
| free tree | def |  |
| free vertex | todo | `algo/matching` |
| frequency count heuristic | todo | `lib/self_org_list` |
| full array | def |  |
| full binary tree | def |  |
| full inverted index | def |  |
| fully dynamic graph problem | def |  |
| fully persistent data structure | done | `lib/history` (branching timelines) |
| fully polynomial approximation scheme | def |  |
| function | def |  |
| functional data structure | def |  |

### G

| entry | status | module |
|---|---|---|
| Galil-Giancarlo | todo | `algo/string_search` |
| Galil-Seiferas | todo | `algo/string_search` |
| gamma function | todo | `algo/numeric` (extensions) |
| GBD-tree | todo | `lib/hb_tree` |
| GCD | done | `algo/numeric` |
| geometric optimization problem | todo | `algo/geometry` |
| global optimum | def |  |
| gnome sort | done | `algo/sort_classic` |
| graph | def |  |
| graph coloring | todo | `algo/graph_hard` |
| graph concentration | def |  |
| graph drawing | def |  |
| graph isomorphism | todo | `algo/graph_hard` |
| graph partition | todo | `algo/graph_hard` |
| Gray code | done | `algo/bits` |
| greatest common divisor | done | `algo/numeric` |
| greedy algorithm | def |  |
| greedy heuristic | def |  |
| grid drawing | def |  |
| grid file | todo | `lib/grid_file` |
| Grover's algorithm | def |  |

### H

| entry | status | module |
|---|---|---|
| halting problem | def |  |
| Hamiltonian cycle | todo | `algo/graph_hard` |
| Hamiltonian path | todo | `algo/graph_hard` |
| Hamming distance | done | `algo/stringology` |
| hard split, easy merge | def |  |
| hash | done | `algo/hash` |
| hashbelt | todo | `lib/linear_hash_map` |
| hash function | done | `algo/hash` |
| hash heap | todo | `lib/indexed_heap` (hash index) |
| hash table | done | `lib/hash_table, lib/rh_map` |
| hash table delete | done | `lib/hash_table, lib/rh_map` (backward shift) |
| hash tree | todo | `lib/merkle_tree` |
| Hausdorff distance | todo | `algo/geometry` |
| hB-tree | todo | `lib/hb_tree` |
| head | def |  |
| heap | done | `lib/heap` |
| heapify | done | `lib/heap` |
| heap property | def |  |
| heapsort | done | `algo/sort` |
| heaviest common subsequence | todo | `algo/string_approx` |
| height | def |  |
| height-balanced binary search tree | done | `lib/avl` |
| height-balanced tree | done | `lib/avl` |
| heuristic | def |  |
| hidden Markov model | todo | `algo/hmm` |
| highest common factor | done | `algo/numeric` |
| histogram sort | todo | `algo/sort_dist` |
| HMM | todo | `algo/hmm` |
| homeomorphic | def |  |
| horizontal visibility map | def |  |
| Horner's rule | todo | `algo/numeric` (extensions) |
| Horspool | todo | `algo/string_search` |
| hsadelta | def |  |
| Huffman coding | todo | `algo/compress` |
| huge sparse array | todo | `lib/sparse_matrix` |
| Hungarian algorithm | todo | `algo/matching` |
| hybrid algorithm | def |  |
| hyperedge | def |  |
| hypergraph | def |  |
| HyperLogLog | done | `lib/hyperloglog` |

### I

| entry | status | module |
|---|---|---|
| IBLT | todo | `lib/iblt` |
| ideal merge | todo | `algo/sequence` (extensions) |
| ideal random shuffle | done | `algo/sequence` (ds_shuffle) |
| implication | def |  |
| implies | def |  |
| inclusion-exclusion principle | def |  |
| inclusive or | def |  |
| incompressible string | def |  |
| incremental hashing | todo | `algo/hash` (new functions) |
| in-degree | def |  |
| independent set | todo | `algo/graph_hard` |
| index file | def |  |
| information theoretic bound | def |  |
| in-order traversal | done | `lib/avl, lib/rbt, lib/bst walks` |
| in-place sort | done | `algo/sort` |
| insertion sort | done | `algo/sort` |
| integer linear program | todo | `algo/lp` |
| integer multi-commodity flow | def |  |
| integer polyhedron | def |  |
| interactive proof system | def |  |
| interior-based representation | def |  |
| internal node | def |  |
| internal sort | def |  |
| interpolation search | done | `algo/search` |
| interpolation-sequential search | todo | `algo/search` (extensions) |
| interpolation sort | todo | `algo/sort_dist` |
| intersection | done | `algo/sequence` (ds_set_intersection) |
| interval tree | done | `lib/interval_tree` |
| intractable | def |  |
| introsort | done | `algo/sort` |
| introspective sort | done | `algo/sort` |
| inverse Ackermann function | todo | `algo/numeric` (extensions) |
| inverse suffix array | todo | `lib/suffix_tree` |
| inversion list | todo | `algo/sequence` (extensions) |
| inverted file index | def |  |
| inverted index | def |  |
| invertible Bloom lookup table | todo | `lib/iblt` |
| irreflexive | def |  |
| isomorphic | def |  |
| iteration | def |  |

### J

| entry | status | module |
|---|---|---|
| Jaro-Winkler | todo | `algo/string_approx` |
| jelly-fish | def |  |
| Johnson's algorithm | todo | `algo/graph_alg` |
| Johnson-Trotter | todo | `algo/combinatorics` |
| JSort | todo | `algo/sort_dist` |
| J sort | todo | `algo/sort_dist` |
| jump list | todo | `lib/jump_list` |
| jump search | todo | `algo/search` (extensions) |

### K

| entry | status | module |
|---|---|---|
| Karnaugh map | def |  |
| Karp-Rabin | done | `algo/stringology` |
| Karp reduction | def |  |
| k-ary heap | done | `lib/indexed_heap` (d-ary) |
| k-ary Huffman coding | todo | `algo/compress` |
| k-ary tree | def |  |
| k-clustering | todo | `algo/graph_hard` |
| k-coloring | todo | `algo/graph_hard` |
| k-connected graph | todo | `algo/graph_alg` |
| k-d-B-tree | todo | `lib/kdb_tree` |
| k-dimensional | def |  |
| K-dominant match | todo | `algo/string_approx` |
| k-d tree | done | `lib/kd_tree` |
| key | def |  |
| KMP | done | `algo/stringology` |
| KmpSkip Search | todo | `algo/string_search` |
| knapsack problem | todo | `algo/knapsack` |
| knight's tour | todo | `algo/combinatorics` |
| Knuth-Morris-Pratt algorithm | done | `algo/stringology` |
| Königsberg bridges problem | todo | `algo/graph_alg` |
| Kolmogorov complexity | def |  |
| Kraft's inequality | def |  |
| Kripke structure | def |  |
| Kruskal's algorithm | todo | `algo/graph_alg` |
| kth order Fibonacci numbers | todo | `algo/numeric` (extensions) |
| kth shortest path | todo | `algo/graph_alg` |
| kth smallest element | done | `algo/sort` (nth_element) |
| k²-tree | todo | `lib/k2_tree` |
| KV diagram | def |  |
| k-way merge | todo | `algo/sequence` (extensions) |
| k-way merge sort | todo | `algo/sort_misc` |
| k-way tree | def |  |

### L

| entry | status | module |
|---|---|---|
| labeled graph | def |  |
| language | def |  |
| last-in, first-out | done | `lib/stack` |
| Las Vegas algorithm | def |  |
| lattice | def |  |
| layered graph | todo | `algo/flow` |
| LCFS hashing | todo | `lib/open_map` (probe policies) |
| LCM | done | `algo/numeric` |
| LCS | done | `algo/stringology` |
| LDS | def |  |
| leaf | def |  |
| least common multiple | done | `algo/numeric` |
| leftist tree | done | `lib/meldable_heap` |
| left rotation | done | `lib/avl, lib/rbt` |
| Lempel-Ziv-Welch | todo | `algo/compress` |
| level | def |  |
| level-order traversal | def |  |
| Levenshtein distance | done | `algo/stringology` |
| lexicographical order | done | `algo/sequence` (ds_lexicographic_compare) |
| LIFO | done | `lib/stack` |
| linear | def |  |
| linear congruential generator | todo | `algo/random` (extensions) |
| linear hash | todo | `algo/hash` (new functions) |
| linear hashing | todo | `lib/linear_hash_map` |
| linear insertion sort | todo | `algo/sort_misc` |
| linear order | def |  |
| linear probing | done | `lib/hash_table, lib/rh_map` |
| linear probing sort | todo | `algo/sort_dist` |
| linear product | def |  |
| linear program | todo | `algo/lp` |
| linear quadtree | todo | `lib/quadtree` |
| linear search | done | `algo/search` (ds_find) |
| link | def |  |
| linked data signature | todo | `lib/merkle_tree` |
| linked list | done | `lib/list, lib/dlist` |
| list | done | `lib/list` |
| list contraction | todo | `algo/parallel_sim` |
| little-o notation | def |  |
| Lm distance | todo | `algo/geometry` |
| load factor | done | `lib/hash_table, lib/rh_map` |
| local alignment | todo | `algo/string_approx` |
| locality-sensitive hashing | todo | `algo/lsh` |
| local optimum | def |  |
| logarithmic | def |  |
| longest common subsequence | done | `algo/stringology` |
| longest common substring | todo | `algo/string_approx` |
| Lotka's law | def |  |
| lower bound | done | `algo/search` (ds_lower_bound) |
| lower triangular matrix | todo | `algo/matrix` |
| lowest common ancestor | todo | `algo/graph_alg` |
| l-reduction | def |  |
| lucky sort | todo | `algo/sort_misc` |
| LZW compression | todo | `algo/compress` |

### M

| entry | status | module |
|---|---|---|
| Malhotra-Kumar-Maheshwari blocking flow | todo | `algo/flow` |
| Manhattan distance | todo | `algo/geometry` |
| many-one reduction | def |  |
| map | done | `lib/hash_table, lib/rh_map, lib/avl, ...` |
| Markov chain | def |  |
| Marlena | def |  |
| marriage problem | todo | `algo/matching` |
| Master theorem | def |  |
| matched edge | todo | `algo/matching` |
| matched vertex | todo | `algo/matching` |
| matching | todo | `algo/matching` |
| matrix | def |  |
| matrix-chain multiplication problem | todo | `algo/knapsack` |
| matrix multiplication | todo | `algo/matrix` |
| max-heap property | def |  |
| maximal independent set | todo | `algo/graph_hard` |
| maximally connected component | def |  |
| Maximal Shift | todo | `algo/string_search` |
| maximum bipartite matching | todo | `algo/matching` |
| maximum-flow problem | todo | `algo/flow` |
| MBB | todo | `lib/rtree` |
| Mealy machine | todo | `lib/automaton` |
| mean | done | `algo/numeric` (Welford) |
| median | done | `algo/sort` (nth_element) |
| meld | done | `lib/meldable_heap` |
| memoization | def |  |
| merge | done | `algo/sequence` |
| merge sort | done | `algo/sort` |
| Merkle tree | todo | `lib/merkle_tree` |
| metaheuristic | def |  |
| metaphone | todo | `algo/phonetic` |
| midrange | todo | `algo/numeric` (extensions) |
| Miller-Rabin | done | `algo/numeric` |
| min-heap property | def |  |
| minimal perfect hashing | todo | `algo/perfect_hash` |
| minimax | todo | `algo/game` |
| minimum bounding box | todo | `lib/rtree` |
| minimum cut | todo | `algo/flow` |
| minimum spanning tree | done | `algo/graph_view` (Prim), lib/graph |
| minimum vertex cut | todo | `algo/graph_alg` |
| Minkowski distance | todo | `algo/geometry` |
| mixed integer linear program | todo | `algo/lp` |
| mode | todo | `algo/numeric` (extensions) |
| model checking | def |  |
| model of computation | def |  |
| moderately exponential | def |  |
| MODIFIND | todo | `algo/select` |
| monotone priority queue | done | `lib/radix_heap` |
| monotonically decreasing | def |  |
| monotonically increasing | def |  |
| Monte Carlo algorithm | def |  |
| Moore machine | todo | `lib/automaton` |
| Morris-Pratt algorithm | todo | `algo/string_search` |
| move | todo | `algo/compress` |
| move-to-front heuristic | todo | `lib/self_org_list` |
| move-to-root heuristic | todo | `lib/self_org_list` |
| MST | done | `algo/graph_view, lib/graph` |
| multi-commodity flow | def |  |
| multigraph | def |  |
| multikey Quicksort | todo | `algo/sort_dist` |
| multilayer grid file | todo | `lib/grid_file` |
| multiplication method | todo | `algo/hash` (new functions) |
| multiprefix | todo | `algo/sequence` (extensions) |
| multiprocessor model | def |  |
| multi-set | todo | `lib/multiset` |
| multi suffix tree | todo | `lib/suffix_tree` |
| multiway decision | def |  |
| multiway merge | todo | `algo/sequence` (extensions) |
| multiway search tree | done | `lib/btree` |
| multiway tree | done | `lib/btree` |
| Munkres' assignment algorithm | todo | `algo/matching` |

### N

| entry | status | module |
|---|---|---|
| naive string search | todo | `algo/string_search` |
| nand | def |  |
| n-ary function | def |  |
| NC many-one reducibility | def |  |
| nearest neighbor | todo | `algo/geometry` |
| negation | def |  |
| network flow | todo | `algo/flow` |
| network flow problem | todo | `algo/flow` |
| next state | def |  |
| NFA | todo | `lib/automaton` |
| NFTA | def |  |
| NIST | def |  |
| node | def |  |
| nonbalanced merge | todo | `algo/sort_misc` |
| nonbalanced merge sort | todo | `algo/sort_misc` |
| nondeterministic | def |  |
| nondeterministic algorithm | def |  |
| nondeterministic finite automaton | todo | `lib/automaton` |
| nondeterministic finite state machine | def |  |
| nondeterministic finite tree automaton | def |  |
| nondeterministic polynomial time | def |  |
| nondeterministic tree automaton | def |  |
| nondeterministic Turing machine | def |  |
| nonterminal node | def |  |
| nor | def |  |
| not | def |  |
| Not So Naive | todo | `algo/string_search` |
| NP | def |  |
| NP-complete | def |  |
| NP-complete language | def |  |
| NP-hard | def |  |
| n queens | todo | `algo/combinatorics` |
| nullary function | def |  |
| null tree | def |  |
| NYSIIS | todo | `algo/phonetic` |

### O

| entry | status | module |
|---|---|---|
| O | def |  |
| OBDD | todo | `lib/bdd` |
| objective function | def |  |
| oblivious algorithm | def |  |
| occurrence | def |  |
| octree | todo | `lib/octree` |
| off-line algorithm | def |  |
| offset | def |  |
| omega | def |  |
| omicron | def |  |

### Symbols and digits

| entry | status | module |
|---|---|---|
| 1-based indexing | def |  |

### O

| entry | status | module |
|---|---|---|
| one-dimensional | def |  |
| on-line algorithm | def |  |
| open addressing | done | `lib/hash_table, lib/rh_map` |
| optimal | def |  |
| optimal cost | def |  |
| optimal hashing | todo | `algo/perfect_hash` |
| optimal merge | todo | `algo/sequence` (extensions) |
| optimal mismatch | todo | `algo/string_search` |
| optimal polygon triangulation problem | todo | `algo/geometry` |
| optimal polyphase merge | todo | `algo/sort_misc` |
| optimal polyphase merge sort | todo | `algo/sort_misc` |
| optimal solution | def |  |
| optimal triangulation problem | todo | `algo/geometry` |
| optimal value | def |  |
| optimization problem | def |  |
| or | def |  |
| oracle set | def |  |
| oracle tape | def |  |
| oracle Turing machine | def |  |
| order | def |  |
| ordered array | done | `lib/vec` (sort, binary search) |
| ordered binary decision diagram | todo | `lib/bdd` |
| ordered linked list | todo | `lib/sorted_list` |
| ordered tree | def |  |
| order-preserving hash | todo | `algo/hash` (new functions) |
| order-preserving Huffman coding | todo | `algo/compress` |
| order-preserving minimal perfect hashing | todo | `algo/perfect_hash` |
| oriented acyclic graph | def |  |
| oriented graph | def |  |
| oriented tree | def |  |
| orthogonal drawing | def |  |
| orthogonal lists | todo | `lib/sparse_matrix` |
| orthogonally convex rectilinear polygon | todo | `algo/geometry` |
| oscillating merge sort | todo | `algo/sort_misc` |
| out-degree | def |  |

### P

| entry | status | module |
|---|---|---|
| P | def |  |
| packing | def |  |
| padding argument | def |  |
| pagoda | todo | `lib/pagoda` |
| pairing heap | done | `lib/pairing_heap` |
| PAM | todo | `lib/quadtree` |
| parallel computation thesis | def |  |
| parallel prefix computation | todo | `algo/sequence` (extensions) |
| parallel random-access machine | def |  |
| parametric searching | def |  |
| parent | def |  |
| partial function | def |  |
| partially decidable problem | def |  |
| partially dynamic graph problem | def |  |
| partially ordered set | def |  |
| partially persistent data structure | done | `lib/persistent_rbt, lib/persistent_trie` |
| partial order | def |  |
| partial recursive function | def |  |
| partition | todo | `algo/combinatorics` |
| passive data structure | def |  |
| path | def |  |
| path cover | todo | `algo/graph_hard` |
| path system problem | def |  |
| Patricia tree | done | `lib/radix_trie, lib/critbit_tree` |
| pattern | def |  |
| pattern element | def |  |
| P-complete | def |  |
| PCP | def |  |
| PDA | def |  |
| Pearson's hash | todo | `algo/hash` (new functions) |
| perfect binary tree | def |  |
| perfect hashing | todo | `algo/perfect_hash` |
| perfect k-ary tree | def |  |
| perfect matching | todo | `algo/matching` |
| perfect shuffle | def |  |
| performance guarantee | def |  |
| performance ratio | def |  |
| permutation | done | `algo/sequence` (next/prev permutation) |
| permutation sort | todo | `algo/sort_dist` |
| persistent data structure | done | `lib/persistent_rbt, lib/persistent_trie` |
| phonetic coding | todo | `algo/phonetic` |
| pigeonhole sort | todo | `algo/sort_dist` |
| pile | def |  |
| pipelined divide and conquer | def |  |
| planar graph | def |  |
| planarization | def |  |
| planar straight-line graph | def |  |
| PLOP-hashing | def |  |
| point access method | todo | `lib/quadtree` |
| pointer jumping | todo | `algo/parallel_sim` |
| pointer machine | def |  |
| poissonization | def |  |
| polychotomy | def |  |
| polyhedron | def |  |
| polylogarithmic | def |  |
| polynomial | def |  |
| polynomial approximation scheme | def |  |
| polynomial hierarchy | def |  |
| polynomial time | def |  |
| polynomial-time reduction | def |  |
| polyphase merge | todo | `algo/sort_misc` |
| polyphase merge sort | todo | `algo/sort_misc` |
| polytope | def |  |
| poset | def |  |
| postfix traversal | def |  |
| Post machine | def |  |
| postman's sort | todo | `algo/sort_dist` |
| postorder traversal | done | `lib/avl, lib/bst walks` |
| Post's correspondence problem | def |  |
| potential function | def |  |
| PRAM | def |  |
| predicate | def |  |
| prefix | def |  |
| prefix code | todo | `algo/compress` |
| prefix sums | todo | `algo/sequence` (extensions) |
| prefix traversal | def |  |
| preorder traversal | done | `lib/avl, lib/bst walks` |
| primary clustering | todo | `lib/open_map` (probe policies) |
| primitive algorithm | def |  |
| primitive recursive | def |  |
| Prim-Jarnik algorithm | done | `algo/graph_view` |
| principle of optimality | def |  |
| priority queue | done | `lib/heap, lib/indexed_heap, ...` |
| prisoner's dilemma | def |  |
| PRNG | done | `algo/random` |
| probabilistic algorithm | def |  |
| probabilistically checkable proof | def |  |
| probabilistic Turing machine | def |  |
| probe sequence | todo | `lib/open_map` (probe policies) |
| procedure | def |  |
| process algebra | def |  |
| proper | def |  |
| proper binary tree | def |  |
| proper coloring | def |  |
| proper subset | def |  |
| property list | todo | `lib/association_list` |
| prune and search | def |  |
| pseudo-random number generator | done | `algo/random` |
| PTAS | def |  |
| pth order Fibonacci numbers | todo | `algo/numeric` (extensions) |
| P-tree | todo | `lib/hb_tree` |
| purely functional language | def |  |
| pushdown automaton | def |  |
| pushdown transducer | def |  |
| p-way merge sort | todo | `algo/sort_misc` |

### Q

| entry | status | module |
|---|---|---|
| qm sort | todo | `algo/sort_misc` |
| q sort | todo | `algo/sort_misc` |
| quadratic probing | done | `lib/hash_table` |
| quadtree | todo | `lib/quadtree` |
| quadtree complexity theorem | def |  |
| quad trie | todo | `lib/quadtree` |
| quantum computation | def |  |
| queue | done | `lib/queue` |
| quick search | todo | `algo/string_search` |
| quicksort | todo | `algo/sort_misc` |

### R

| entry | status | module |
|---|---|---|
| Rabin-Karp | done | `algo/stringology` |
| radix sort | done | `algo/sort` (LSD radix) |
| radix tree | done | `lib/radix_trie` |
| ragged matrix | todo | `lib/sparse_matrix` |
| Raita | todo | `algo/string_search` |
| random access machine | def |  |
| randomization | def |  |
| randomized algorithm | def |  |
| randomized binary search tree | todo | `lib/rbst` |
| randomized complexity | def |  |
| randomized polynomial time | def |  |
| randomized rounding | def |  |
| randomized search tree | done | `lib/treap` |
| Randomized-Select | todo | `algo/random` (extensions) |
| random number generator | done | `algo/random` (xoshiro128**) |
| random sampling | todo | `algo/random` (extensions) |
| random search | def |  |
| range | def |  |
| range sort | todo | `algo/sort_dist` |
| rank | done | `lib/order_statistic_tree, lib/bitvector` |
| rapid sort | todo | `algo/sort_misc` |
| Ratcliff/Obershelp pattern recognition | todo | `algo/string_approx` |
| RBST | todo | `lib/rbst` |
| reachability | done | `algo/graph_view` (BFS) |
| reachable | done | `algo/graph_view` (BFS) |
| rebalance | done | `lib/avl, lib/rbt` |
| recognizer | def |  |
| rectangular matrix | def |  |
| rectilinear | def |  |
| rectilinear Steiner tree | todo | `algo/graph_hard` |
| recurrence equations | def |  |
| recurrence relation | def |  |
| recursion | def |  |
| recursion termination | def |  |
| recursion tree | def |  |
| recursive | def |  |
| recursive data structure | def |  |
| recursive doubling | todo | `algo/parallel_sim` |
| recursive language | def |  |
| recursively enumerable language | def |  |
| recursively solvable | def |  |
| red-black tree | done | `lib/rbt, lib/persistent_rbt` |
| reduced basis | todo | `algo/numeric` (extensions) |
| reduced digraph | todo | `algo/graph_alg` |
| reduced ordered binary decision diagram | todo | `lib/bdd` |
| reduction | def |  |
| reflexive | def |  |
| regular decomposition | todo | `lib/quadtree` |
| rehashing | todo | `lib/open_map` (probe policies) |
| relation | def |  |
| relational structure | def |  |
| relative performance guarantee | def |  |
| relaxation | def |  |
| relaxed balance | def |  |
| repeated squaring | done | `algo/numeric` (ds_powmod) |
| rescalable | def |  |
| reservoir sampling | todo | `algo/random` (extensions) |
| restricted universe sort | todo | `algo/sort_dist` |
| Reverse Colussi | todo | `algo/string_search` |
| Reverse Factor | todo | `algo/string_search` |
| R-file | todo | `lib/grid_file` |
| Rice's method | todo | `algo/numeric` (extensions) |
| right rotation | done | `lib/avl, lib/rbt` |
| right-threaded tree | todo | `lib/threaded_tree` |
| RNG | done | `algo/random` |
| ROBDD | todo | `lib/bdd` |
| Robin Hood hashing | done | `lib/rh_map` |
| root | def |  |
| root balance | todo | `lib/wb_tree` |
| rooted tree | def |  |
| rotate left | done | `lib/avl, lib/rbt` |
| rotate right | done | `lib/avl, lib/rbt` |
| rotation | done | `lib/avl, lib/rbt, lib/treap` |
| rough graph | def |  |
| RP | def |  |
| R+-tree | todo | `lib/rtree` |
| R*-tree | todo | `lib/rtree` |
| R-tree | todo | `lib/rtree` |
| run time | def |  |

### S

| entry | status | module |
|---|---|---|
| saguaro stack | todo | `lib/cactus_stack` |
| SAM | def |  |
| saturated edge | todo | `algo/flow` |
| SBB tree | done | `lib/rbt` (symmetric binary B-tree = red-black tree) |
| scan | todo | `algo/sequence` (extensions) |
| scapegoat tree | todo | `lib/scapegoat_tree` |
| scatter storage | def |  |
| Schorr-Waite graph marking algorithm | todo | `algo/schorr_waite` |
| search | def |  |
| search tree | def |  |
| search tree property | def |  |
| secant search | todo | `algo/numeric` (extensions) |
| secondary clustering | todo | `lib/open_map` (probe policies) |
| segment | def |  |
| Select | done | `algo/sort` (nth_element), lib/order_statistic_tree |
| select and partition | todo | `algo/select` |
| selection problem | done | `algo/sort` (nth_element) |
| selection sort | done | `algo/sort_classic` |
| select kth element | done | `algo/sort` (nth_element) |
| select mode | todo | `algo/select` |
| self-loop | def |  |
| self-organizing list | todo | `lib/self_org_list` |
| self-organizing sequential search | todo | `lib/self_org_list` |
| semidefinite programming | def |  |
| separate chaining | done | `lib/hash_table` |
| separation theorem | def |  |
| sequential search | done | `algo/search` |
| set | done | `lib/bitset, lib/sparse_set, lib/string_map` (string set) |
| set cover | todo | `algo/graph_hard` |
| set packing | todo | `algo/knapsack` |
| shadow heap | todo | `lib/shadow_heap` |
| shadow merge | todo | `algo/sort_misc` |
| shadow merge insert | todo | `algo/sort_misc` |
| shaker sort | done | `algo/sort_classic` (cocktail) |
| Shannon-Fano coding | todo | `algo/compress` |
| Shell sort † | done | `algo/sort` |
| Shift-Or | todo | `algo/string_search` |
| Shor's algorithm | def |  |
| shortcutting | todo | `algo/parallel_sim` |
| shortest common supersequence | todo | `algo/string_approx` |
| shortest common superstring | todo | `algo/string_approx` |
| shortest path | todo | `algo/graph_alg` |
| shortest spanning tree | todo | `algo/graph_alg` |
| shuffle | done | `algo/sequence` |
| shuffle sort † | todo | `algo/sort_dist` |
| sibling † | def |  |
| sieve of Eratosthenes | done | `algo/numeric` |
| sift up † | def |  |
| signature | def |  |
| Simon's algorithm | def |  |
| simple merge † | todo | `algo/sort_misc` |
| simple path | def |  |
| simple uniform hashing | todo | `lib/open_map` (probe policies) |
| simplex | todo | `algo/lp` |
| simulated annealing | def |  |
| simulation theorem † | def |  |
| single-destination shortest-path problem | todo | `algo/graph_alg` |
| single-pair shortest-path problem | todo | `algo/graph_alg` |
| single program multiple data | def |  |
| single-source shortest-path problem | todo | `algo/graph_alg` |
| singly linked list | done | `lib/list` |
| singularity analysis † | def |  |
| sink | def |  |
| sinking sort † | todo | `algo/sort_misc` |
| skd-tree † | todo | `lib/kdb_tree` |
| skew heap † | done | `lib/meldable_heap` |
| skew symmetry | todo | `algo/flow` |
| skip list | done | `lib/skip_list` |
| skip search | todo | `algo/string_search` |
| slope selection | todo | `algo/geometry` |
| Smith algorithm † | todo | `algo/string_search` |
| Smith-Waterman algorithm † | todo | `algo/string_approx` |
| smoothsort † | todo | `algo/sort_heap` |
| solvable † | def |  |
| sort † | def |  |
| sorted array † | todo | `lib/sorted_list` |
| sorted list † | todo | `lib/sorted_list` |
| sort in place † | def |  |
| soundex † | todo | `algo/phonetic` |
| source | def |  |
| space-constrained Lempel-Ziv-Welch † | todo | `algo/compress` |
| spanning tree † | todo | `algo/graph_alg` |
| sparse graph | def |  |
| sparse matrix | todo | `lib/sparse_matrix` |
| sparsification | def |  |
| sparsity † | def |  |
| spatial access method † | todo | `lib/rtree` |
| spectral test † | todo | `algo/random` (extensions) |
| splay tree † | done | `lib/splay_tree` |
| SPMD | def |  |
| square matrix | def |  |
| square root | todo | `algo/numeric` (extensions) |
| SST | todo | `algo/graph_alg` |
| stable | def |  |
| stable marriage problem † | todo | `algo/matching` |
| stack | done | `lib/stack` |
| stack tree | def |  |
| star encoding | todo | `algo/compress` |
| star-shaped polygon | todo | `algo/geometry` |
| start state | def |  |
| state | def |  |
| state machine | def |  |
| state transition | def |  |
| static | def |  |
| static Huffman coding | todo | `algo/compress` |
| s-t cut | todo | `algo/flow` |
| st-digraph | def |  |
| Steiner point | def |  |
| Steiner ratio | def |  |
| Steiner tree | todo | `algo/graph_hard` |
| Steiner vertex | def |  |
| Steinhaus-Johnson-Trotter | todo | `algo/combinatorics` |
| Stirling's approximation | todo | `algo/numeric` (extensions) |
| Stirling's formula | todo | `algo/numeric` (extensions) |
| stooge sort † | done | `algo/sort_classic` |
| straight-line drawing | def |  |
| strand sort † | todo | `algo/sort_misc` |
| strictly decreasing | def |  |
| strictly increasing | def |  |
| strictly lower triangular matrix | todo | `algo/matrix` |
| strictly upper triangular matrix | todo | `algo/matrix` |
| string | done | `lib/str` |
| string editing problem | done | `algo/stringology` (edit distances) |
| string matching † | todo | `algo/string_search` |
| string matching on ordered alphabets † | todo | `algo/string_search` |
| string matching with errors † | todo | `algo/string_approx` |
| string matching with mismatches † | todo | `algo/string_search` |
| string searching † | todo | `algo/string_search` |
| strip packing | todo | `algo/knapsack` |
| strongly connected component | done | `algo/graph_view` (Tarjan), lib/graph |
| strongly connected graph | def |  |
| strongly NP-hard † | def |  |
| subadditive ergodic theorem † | def |  |
| subgraph | def |  |
| subgraph isomorphism | todo | `algo/graph_hard` |
| sublinear time algorithm † | def |  |
| subsequence † | def |  |
| subset | def |  |
| substring † | def |  |
| subtree † | def |  |
| succinct data structure † | def |  |
| suffix † | def |  |
| suffix array † | done | `algo/stringology` |
| suffix automaton | todo | `lib/suffix_automaton` |
| suffix tree † | todo | `lib/suffix_tree` |
| superimposed code | todo | `lib/bloom_filter` (superimposed codes) |
| superset | def |  |
| supersink | todo | `algo/flow` |
| supersource | todo | `algo/flow` |
| symmetric | def |  |
| symmetrically linked list | done | `lib/dlist` |
| symmetric binary B-tree † | done | `lib/rbt` |
| symmetric set difference | done | `algo/sequence` (ds_set_symmetric_difference) |
| symmetry breaking | def |  |

### T

| entry | status | module |
|---|---|---|
| taco sort † | def |  |
| tail | def |  |
| tail recursion | def |  |
| target | def |  |
| temporal logic † | def |  |
| terminal | def |  |
| terminal node † | def |  |
| ternary search † | todo | `algo/search` (extensions) |
| ternary search tree † | todo | `lib/tst` |
| text | done | `lib/str, lib/rope, lib/gap_buffer` |
| text searching † | todo | `algo/string_search` |
| theta | def |  |
| threaded binary tree † | todo | `lib/threaded_tree` |
| threaded tree † | todo | `lib/threaded_tree` |
| three-dimensional | def |  |
| three-way merge sort † | todo | `algo/sort_misc` |
| three-way radix quicksort † | todo | `algo/sort_dist` |
| time-constructible function † | def |  |
| time/space complexity † | def |  |
| top-down radix sort † | todo | `algo/sort_dist` |
| top-down tree automaton | def |  |
| topological order | done | `algo/graph_view` |
| topological sort † | done | `algo/graph_view, lib/graph` |
| topology tree † | def |  |
| total function † | def |  |
| totally decidable language † | def |  |
| totally decidable problem † | def |  |
| totally undecidable problem † | def |  |
| total order | def |  |
| tour | def |  |
| tournament | todo | `algo/sort_heap` |
| tournament sort † | todo | `algo/sort_heap` |
| towers of Hanoi | todo | `algo/combinatorics` |
| tractable † | def |  |
| transducer † | def |  |
| transition | def |  |
| transition function | def |  |
| transitive | def |  |
| transitive closure | todo | `algo/graph_alg` |
| transitive reduction | todo | `algo/graph_alg` |
| transpose sequential search † | todo | `lib/self_org_list` |
| traveling salesman | todo | `algo/graph_hard` |
| treap † | done | `lib/treap` |
| tree † | done | `lib/bst` |
| tree automaton | def |  |
| tree contraction | todo | `algo/parallel_sim` |
| tree editing problem | def |  |
| tree sort † | todo | `algo/sort_heap` |
| tree transducer † | def |  |
| tree traversal † | done | `lib/bst walks` |
| triangle inequality | def |  |
| triconnected graph | todo | `algo/graph_alg` |
| trie † | done | `lib/trie` |
| trinary function | def |  |
| tripartition | def |  |
| Trotter-Johnson | todo | `algo/combinatorics` |
| TSP | todo | `algo/graph_hard` |
| TST † | todo | `lib/tst` |
| Turbo-BM † | todo | `algo/string_search` |
| Turbo Reverse Factor † | todo | `algo/string_search` |
| Turing machine | def |  |
| Turing reduction † | def |  |
| Turing transducer † | def |  |
| twin grid file † | todo | `lib/grid_file` |

### Symbols and digits

| entry | status | module |
|---|---|---|
| 2-choice hashing † | todo | `lib/two_choice_map` |

### T

| entry | status | module |
|---|---|---|
| two-dimensional | def |  |

### Symbols and digits

| entry | status | module |
|---|---|---|
| 2-left hashing † | todo | `lib/two_choice_map` |

### T

| entry | status | module |
|---|---|---|
| two-level grid file † | todo | `lib/grid_file` |

### Symbols and digits

| entry | status | module |
|---|---|---|
| 2-3-4 tree † | todo | `lib/btree` (2-3 / 2-3-4 presets) |
| 2-3 tree † | todo | `lib/btree` (2-3 / 2-3-4 presets) |

### T

| entry | status | module |
|---|---|---|
| Two Way algorithm † | todo | `algo/string_search` |
| two-way linked list | done | `lib/dlist` |
| two-way merge sort † | todo | `algo/sort_misc` |

### U

| entry | status | module |
|---|---|---|
| UB-tree † | todo | `lib/ub_tree` |
| UKP | todo | `algo/knapsack` |
| unary function | def |  |
| unbiased coin flipping algorithm | todo | `algo/random` (extensions) |
| unbounded knapsack problem | todo | `algo/knapsack` |
| uncomputable function † | def |  |
| uncomputable problem † | def |  |
| undecidable language † | def |  |
| undecidable problem † | def |  |
| undirected graph | def |  |
| uniform circuit complexity † | def |  |
| uniform circuit family † | def |  |
| uniform hashing † | todo | `lib/open_map` (probe policies) |
| uniform matrix | def |  |
| union | done | `algo/sequence` (ds_set_union) |
| union of automata | todo | `lib/automaton` |
| universal hashing † | todo | `algo/hash` (new functions) |
| universal state | def |  |
| universal Turing machine | def |  |
| universe | def |  |
| unranking | todo | `algo/combinatorics` |
| unsolvable problem † | def |  |
| unsorted list † | todo | `lib/sorted_list` |
| upper triangular matrix | todo | `algo/matrix` |

### V

| entry | status | module |
|---|---|---|
| van Emde Boas priority queue † | done | `lib/veb_tree` |
| vehicle routing problem | todo | `algo/graph_hard` |
| Veitch diagram | def |  |
| Venn diagram † | def |  |
| vertex | def |  |
| vertex coloring | todo | `algo/graph_hard` |
| vertex connectivity | todo | `algo/graph_alg` |
| vertex cover | todo | `algo/graph_hard` |
| vertical visibility map | def |  |
| virtual hashing † | todo | `lib/linear_hash_map` |
| visibility map | def |  |
| visible | def |  |
| Viterbi algorithm | todo | `algo/hmm` |
| Vitter's algorithm | todo | `algo/compress` |
| VRP | todo | `algo/graph_hard` |

### W

| entry | status | module |
|---|---|---|
| walk | def |  |
| weak cluster † | def |  |
| weak-heap † | done | `lib/weak_heap` |
| weak-heap sort † | done | `lib/weak_heap` |
| weight-balanced tree † | todo | `lib/wb_tree` |
| weighted, directed graph | def |  |
| weighted graph | def |  |
| window † | def |  |
| witness | def |  |
| work | def |  |
| work-depth model | def |  |
| work-efficient | def |  |
| work-preserving | def |  |
| worst case † | def |  |
| worst-case cost † | def |  |
| worst-case minimum access † | def |  |
| Wu's line algorithm † | todo | `algo/geometry` |

### X

| entry | status | module |
|---|---|---|
| xor | def |  |

### Y

| entry | status | module |
|---|---|---|
| Yule distribution | def |  |

### Z

| entry | status | module |
|---|---|---|
| Zeller's congruence | todo | `algo/numeric` (extensions) |

### Symbols and digits

| entry | status | module |
|---|---|---|
| 0-ary function † | def |  |
| 0-based indexing † | def |  |
| 0/1 knapsack problem † | todo | `algo/knapsack` |

### Z

| entry | status | module |
|---|---|---|
| Zhu-Takaoka † | todo | `algo/string_search` |
| Zipfian distribution | def |  |
| Zipf's law | def |  |
| ZPP † | def |  |
