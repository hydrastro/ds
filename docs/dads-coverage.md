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
| done | 511 |
| todo | 194 |
| def | 593 |
| total | 1298 |

## Todo by target module

Families ordered by how many DADS entries they close.

| target | entries | terms |
|---|---|---|
| `algo/graph_hard` | 33 | bottleneck traveling salesman, capacitated facility location, Christofides algorithm, chromatic index, chromatic number, clique, clique problem, discrete p-center, edge coloring, Euclidean Steiner tree, Euclidean traveling salesman problem, facility location, feedback vertex set, graph coloring, graph isomorphism, graph partition, Hamiltonian cycle, Hamiltonian path, independent set, k-clustering, k-coloring, maximal independent set, path cover, rectilinear Steiner tree, set cover, Steiner tree, subgraph isomorphism, traveling salesman, TSP, vehicle routing problem, vertex coloring, vertex cover, VRP |
| `algo/string_search` | 11 | Colussi, Commentz-Walter, Galil-Giancarlo, Galil-Seiferas, KmpSkip Search, Maximal Shift, optimal mismatch, Reverse Colussi, Reverse Factor, string matching on ordered alphabets, Turbo Reverse Factor |
| `lib/automaton` | 11 | deterministic finite automaton, DFA, finite state automaton, finite state machine, finite state machine minimization, finite state transducer, Mealy machine, Moore machine, NFA, nondeterministic finite automaton, union of automata |
| `lib/grid_file` | 8 | BANG file, EXCELL, fixed-grid method, grid file, multilayer grid file, R-file, twin grid file, two-level grid file |
| `lib/hb_tree` | 8 | BD-tree, buddy tree, BV-tree, cell tree, D-tree, GBD-tree, hB-tree, P-tree |
| `lib/quadtree` | 7 | bintree, linear quadtree, PAM, point access method, quadtree, quad trie, regular decomposition |
| `algo/lp` | 6 | cutting plane, dual linear program, integer linear program, linear program, mixed integer linear program, simplex |
| `lib/self_org_list` | 6 | frequency count heuristic, move-to-front heuristic, move-to-root heuristic, self-organizing list, self-organizing sequential search, transpose sequential search |
| `algo/matrix` | 5 | lower triangular matrix, matrix multiplication, strictly lower triangular matrix, strictly upper triangular matrix, upper triangular matrix |
| `algo/parallel_sim` | 5 | list contraction, pointer jumping, recursive doubling, shortcutting, tree contraction |
| `lib/bdd` | 5 | BDD, OBDD, ordered binary decision diagram, reduced ordered binary decision diagram, ROBDD |
| `lib/suffix_tree` | 5 | Burrows-Wheeler transform, BWT, inverse suffix array, multi suffix tree, suffix tree |
| `algo/hmm` | 4 | Baum Welch algorithm, hidden Markov model, HMM, Viterbi algorithm |
| `lib/binary_tree_repr` | 4 | binary tree representation of trees, doubly-chained tree, filial-heir chain, first child-next sibling binary tree |
| `lib/elastic_trie` | 4 | blind trie, bucket trie, compact trie, elastic-bucket trie |
| `lib/kdb_tree` | 4 | adaptive k-d tree, extended k-d tree, k-d-B-tree, skd-tree |
| `lib/rtree` | 4 | R+-tree, R*-tree, R-tree, spatial access method |
| `lib/sorted_list` | 4 | ordered linked list, sorted array, sorted list, unsorted list |
| `lib/sparse_matrix` | 4 | huge sparse array, orthogonal lists, ragged matrix, sparse matrix |
| `lib/suffix_automaton` | 4 | compact DAWG, DAWG, directed acyclic word graph, suffix automaton |
| `lib/threaded_tree` | 4 | Cupif-Giannini tree traversal, right-threaded tree, threaded binary tree, threaded tree |
| `algo/fft` | 3 | fast fourier transform, FFT, finite Fourier transform |
| `algo/graph_alg` | 3 | lowest common ancestor, single-destination shortest-path problem, triconnected graph |
| `algo/numeric (extensions)` | 3 | Ferguson-Forcade algorithm, reduced basis, Rice's method |
| `lib/linear_hash_map` | 3 | expandable hashing, hashbelt, virtual hashing |
| `lib/wb_tree` | 3 | BB(α) tree, root balance, weight-balanced tree |
| `lib/association_list` | 2 | association list, property list |
| `lib/btree (2-3 / 2-3-4 presets)` | 2 | 2-3-4 tree, 2-3 tree |
| `lib/cactus_stack` | 2 | cactus stack, saguaro stack |
| `lib/diet` | 2 | diet, discrete interval encoding tree |
| `lib/multiset` | 2 | bag, multi-set |
| `lib/rbst` | 2 | randomized binary search tree, RBST |
| `lib/tst` | 2 | ternary search tree, TST |
| `lib/two_choice_map` | 2 | 2-choice hashing, 2-left hashing |
| `algo/cartesian_tree` | 1 | cartesian tree |
| `algo/game` | 1 | minimax |
| `algo/geometry` | 1 | geometric optimization problem |
| `algo/knapsack` | 1 | set packing |
| `algo/schorr_waite` | 1 | Schorr-Waite graph marking algorithm |
| `lib/bloom_filter (superimposed codes)` | 1 | superimposed code |
| `lib/bsp_tree` | 1 | BSP-tree |
| `lib/buddy_allocator` | 1 | buddy system |
| `lib/enfilade` | 1 | enfilade |
| `lib/indexed_heap (hash index)` | 1 | hash heap |
| `lib/jump_list` | 1 | jump list |
| `lib/k2_tree` | 1 | k²-tree |
| `lib/merkle_tree` | 1 | linked data signature |
| `lib/octree` | 1 | octree |
| `lib/pagoda` | 1 | pagoda |
| `lib/scapegoat_tree` | 1 | scapegoat tree |
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
| Ackermann's function | done | `algo/number_theory` |
| active data structure | def |  |
| acyclic directed graph | def |  |
| acyclic graph | def |  |
| adaptive heap sort | done | `algo/sort_heap` |
| adaptive Huffman coding | done | `algo/adaptive_huffman` |
| adaptive k-d tree | todo | `lib/kdb_tree` |
| adaptive sort | done | `algo/sort_misc` |
| address-calculation sort | done | `algo/sort_dist` |
| adjacency-list representation | done | `lib/graph, algo/graph_view` (CSR) |
| adjacency-matrix representation | def |  |
| adjacent | def |  |
| admissible vertex | def |  |
| ADT | def |  |
| adversary | def |  |
| Aho-Corasick | done | `lib/aho_corasick` |
| algorithm | def |  |
| algorithm BSTW | done | `algo/compress` |
| algorithm FGK | done | `algo/adaptive_huffman` |
| algorithmically solvable | def |  |
| all pairs shortest path | done | `algo/graph_view` (Floyd-Warshall) |
| all simple paths | done | `algo/graph_path` |
| alphabet | def |  |
| Alpha Skip Search algorithm | done | `algo/string_search` |
| alternating path | done | `algo/matching` |
| alternating Turing machine | def |  |
| alternation | def |  |
| American flag sort | done | `algo/sort_dist` |
| amortized cost | def |  |
| ancestor | def |  |
| and | def |  |
| ANSI | def |  |
| antichain | def |  |
| antisymmetric | def |  |
| Apostolico-Crochemore | done | `algo/string_search` |
| Apostolico-Giancarlo algorithm | done | `algo/string_search` |
| approximate string matching | done | `algo/string_approx` |
| approximation algorithm | def |  |
| arborescence | done | `algo/graph_path` |
| arc | def |  |
| arithmetic coding | done | `algo/compress` |
| array | done | `lib/vec` |
| array index | def |  |
| array merging | done | `algo/sequence` (ds_merge) |
| array search | done | `algo/search` |
| articulation point | done | `algo/graph_struct` |
| assignment problem | done | `algo/matching` |
| association list | todo | `lib/association_list` |
| associative | def |  |
| associative array | done | `lib/hash_table, lib/rh_map, lib/string_map` |
| asymptotically tight bound | def |  |
| asymptotic bound | def |  |
| asymptotic space complexity | def |  |
| asymptotic time complexity | def |  |
| asymptotic upper bound | def |  |
| augmenting path | done | `algo/flow` |
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
| balanced k-way merge sort | done | `algo/external_sort` |
| balanced merge sort | done | `algo/external_sort` |
| balanced multiway merge | done | `algo/external_sort` |
| balanced multiway tree | done | `lib/btree` |
| balanced quicksort | done | `algo/sort_misc` |
| balanced tree | done | `lib/avl, lib/rbt, lib/btree` |
| balanced two-way merge sort | done | `algo/external_sort` |
| BANG file | todo | `lib/grid_file` |
| Batcher sort | done | `algo/sort_classic` (bitonic) |
| Baum Welch algorithm | todo | `algo/hmm` |
| BB(α) tree | todo | `lib/wb_tree` |
| BBP algorithm | done | `algo/numerical` |
| BDD | todo | `lib/bdd` |
| BD-tree | todo | `lib/hb_tree` |
| Bellman-Ford algorithm | done | `algo/graph_view, lib/graph` |
| Benford's law | def |  |
| best case | def |  |
| best-case cost | def |  |
| best-first search | done | `algo/graph_path` |
| biconnected component | done | `algo/graph_struct` |
| biconnected graph | done | `algo/graph_struct` |
| bidirectional bubble sort | done | `algo/sort_classic` (cocktail) |
| big-O notation | def |  |
| binary function | def |  |
| binary GCD | done | `algo/numeric` (ds_binary_gcd) |
| binary heap | done | `lib/heap` |
| binary insertion sort | done | `algo/sort_classic` |
| binary knapsack problem | done | `algo/knapsack` |
| binary priority queue | done | `lib/heap` |
| binary relation | def |  |
| binary search | done | `algo/search` |
| binary search tree | done | `lib/bst` |
| binary tree | done | `lib/bst` |
| binary tree representation of trees | todo | `lib/binary_tree_repr` |
| bingo sort | done | `algo/sort_misc` |
| binomial heap | done | `lib/binomial_heap` |
| binomial tree | done | `lib/binomial_heap` |
| bin packing problem | done | `algo/knapsack` |
| bin sort | done | `algo/sort_dist` |
| bintree | todo | `lib/quadtree` |
| bipartite graph | done | `algo/graph_view` (ds_gview_bipartite) |
| bipartite matching | done | `algo/matching` |
| bisector | done | `algo/geometry` |
| bitonic sort | done | `algo/sort_classic` |
| bit vector | done | `lib/bitset, lib/bitvector` |
| Bk tree | done | `lib/binomial_heap` |
| blind sort | done | `algo/sort_dist` |
| blind trie | todo | `lib/elastic_trie` |
| block | def |  |
| block addressing index | def |  |
| blocking flow | done | `algo/flow` |
| block search | done | `algo/search` |
| Bloom filter | done | `lib/bloom_filter` |
| blossom | done | `algo/matching` |
| bogosort | done | `algo/sort_misc` |
| Bond Sequential Search | done | `algo/search` |
| boolean | def |  |
| boolean expression | def |  |
| boolean function | def |  |
| border | def |  |
| Boruvka's algorithm | done | `algo/graph_path` |
| bottleneck traveling salesman | todo | `algo/graph_hard` |
| boundary-based representation | def |  |
| bounded error probability in polynomial time | def |  |
| bounded queue | done | `lib/circular_buffer, lib/array_deque` (fixed buffer) |
| bounded stack | done | `lib/vec` (fixed buffer) |
| Boyer-Moore | done | `algo/string_search` |
| Boyer-Moore-Horspool | done | `algo/string_search` |
| bozo sort | done | `algo/sort_misc` |
| B+-tree | done | `lib/bplus_tree` |
| BPP | def |  |
| Bradford's law | def |  |
| branch and bound | def |  |
| breadth-first search | done | `algo/graph_view, lib/graph` |
| Bresenham's algorithm | done | `algo/geometry` |
| bridge | done | `algo/graph_struct` |
| British Museum technique | def |  |
| brute force | def |  |
| brute force string search | done | `algo/string_search` |
| brute force string search with mismatches | done | `algo/string_search` |
| BSP-tree | todo | `lib/bsp_tree` |
| B*-tree | done | `lib/bplus_tree` (B* split policy) |
| B-tree | done | `lib/btree` |
| bubble sort | done | `algo/sort_classic` |
| bucket | def |  |
| bucket array | def |  |
| bucketing method | def |  |
| bucket sort | done | `algo/sort_dist` |
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
| capacity | def |  |
| capacity constraint | def |  |
| cartesian tree | todo | `algo/cartesian_tree` |
| Caverphone | done | `algo/phonetic` |
| CCS | def |  |
| cell probe model | def |  |
| cell tree | todo | `lib/hb_tree` |
| cellular automaton | def |  |
| centroid | done | `algo/graph_struct` |
| certificate | def |  |
| chain | def |  |
| chaining | done | `lib/hash_table` (chaining mode) |
| child | def |  |
| Chinese postman problem | done | `algo/graph_path` |
| Chinese remainder theorem | done | `algo/number_theory` |
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
| clustering free | def |  |
| coalesced chaining | done | `lib/coalesced_map` |
| coarsening | def |  |
| cocktail shaker sort | done | `algo/sort_classic` |
| codeword | def |  |
| coding tree | done | `algo/prefix_code` |
| Collatz problem | done | `algo/combinatorics` |
| collective recursion | def |  |
| collision | def |  |
| collision resolution scheme | done | `lib/hash_table, lib/rh_map` |
| Colussi | todo | `algo/string_search` |
| combination | done | `algo/combinatorics` |
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
| continuous knapsack problem | done | `algo/knapsack` |
| Cook reduction | def |  |
| Cook's theorem | def |  |
| CORDIC | done | `algo/numerical` |
| counting sort | done | `algo/sort_classic` (ds_counting_sort_keyed) |
| covering | def |  |
| CRC | done | `algo/hash` (CRC-32, CRC-32C) |
| CRCW | def |  |
| CREW | def |  |
| critical path problem | done | `algo/graph_path` |
| CSP | def |  |
| CTL | def |  |
| cube root | done | `algo/numerical` |
| cuckoo hashing | done | `lib/cuckoo_map` |
| Cupif-Giannini tree traversal | todo | `lib/threaded_tree` |
| cut | def |  |
| cutting plane | todo | `algo/lp` |
| cutting stock problem | done | `algo/knapsack` |
| cutting theorem | def |  |
| cut vertex | done | `algo/graph_struct` |
| cycle | def |  |
| cyclic redundancy check | done | `algo/hash` |

### D

| entry | status | module |
|---|---|---|
| D-adjacent | def |  |
| DAG | def |  |
| DAG shortest paths | done | `algo/graph_path` |
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
| derangement | done | `algo/combinatorics` |
| descendant | def |  |
| deterministic | def |  |
| deterministic algorithm | def |  |
| deterministic finite automata string search | done | `algo/string_search` |
| deterministic finite automaton | todo | `lib/automaton` |
| deterministic finite state machine | def |  |
| deterministic finite tree automaton | def |  |
| deterministic pushdown automaton | def |  |
| deterministic random bit generator | done | `algo/random` (xoshiro, not cryptographic) |
| deterministic tree automaton | def |  |
| Deutsch-Jozsa algorithm | def |  |
| DFA | todo | `lib/automaton` |
| DFS | done | `lib/graph` |
| DFS forest | done | `algo/graph_struct` |
| DFTA | def |  |
| diagonalization | def |  |
| diameter | done | `algo/graph_struct` |
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
| distribution sort | done | `algo/sort_dist` |
| distributive partitioning sort | done | `algo/sort_dist` |
| divide and conquer | def |  |
| divide and marriage before conquest | def |  |
| domain | def |  |
| dominance tree sort | done | `algo/sort_heap` |
| don't care | def |  |
| Doomsday rule | done | `algo/number_theory` |
| double-direction bubble sort | done | `algo/sort_classic` (cocktail) |
| double-ended priority queue | done | `lib/min_max_heap` |
| double hashing | done | `lib/open_map` |
| double left rotation | done | `lib/avl` |
| double metaphone | done | `algo/phonetic` |
| double right rotation | done | `lib/avl` |
| doubly-chained tree | todo | `lib/binary_tree_repr` |
| doubly-ended queue | done | `lib/deque` |
| doubly linked list | done | `lib/dlist` |
| DPDA | def |  |
| DRBG | done | `algo/random` |
| D-tree | todo | `lib/hb_tree` |
| dual | def |  |
| dual linear program | todo | `algo/lp` |
| dual-pivot quicksort | done | `algo/sort_misc` |
| Dutch national flag | done | `algo/sort_misc` |
| dyadic tree | def |  |
| dynamic | def |  |
| dynamic array | done | `lib/vec` |
| dynamic hashing | done | `lib/linear_hash_map` |
| dynamic Huffman coding | done | `algo/adaptive_huffman` |
| dynamic programming | def |  |
| dynamization transformation | def |  |

### E

| entry | status | module |
|---|---|---|
| easy split, hard merge | def |  |
| edge | def |  |
| edge coloring | todo | `algo/graph_hard` |
| edge connectivity | done | `algo/flow` |
| edge crossing | def |  |
| edge-weighted graph | def |  |
| edit distance | done | `algo/stringology` (Levenshtein, OSA) |
| edit operation | done | `algo/string_approx` |
| edit script | done | `algo/string_approx` |
| efficiency | def |  |

### Symbols and digits

| entry | status | module |
|---|---|---|
| 8 queens | done | `algo/combinatorics` |

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
| Euclidean distance | done | `algo/geometry` |
| Euclidean Steiner tree | todo | `algo/graph_hard` |
| Euclidean traveling salesman problem | todo | `algo/graph_hard` |
| Euclid's algorithm | done | `algo/numeric` |
| Euler cycle | done | `algo/graph_struct` |
| Eulerian graph | done | `algo/graph_struct` |
| Eulerian path | done | `algo/graph_struct` |
| Euler's formula | def |  |
| exact string matching | done | `algo/string_search` |
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
| extendible hashing | done | `lib/extendible_hash` |
| external chaining | done | `lib/hash_table` |
| external index | def |  |
| external memory algorithm | def |  |
| external memory data structure | def |  |
| external merge | done | `algo/external_sort` |
| external node | def |  |
| external quicksort | done | `algo/external_sort` |
| external radix sort | done | `algo/external_sort` |
| external sort | done | `algo/external_sort` |
| extrapolation search | done | `algo/search` (interpolation) |
| extremal | def |  |
| extreme point | done | `algo/geometry` |

### F

| entry | status | module |
|---|---|---|
| facility location | todo | `algo/graph_hard` |
| factor | def |  |
| factorial | done | `algo/combinatorics` |
| fast fourier transform | todo | `algo/fft` |
| fathoming | def |  |
| feasible region | def |  |
| feasible solution | def |  |
| feedback edge set | done | `algo/graph_struct` |
| feedback vertex set | todo | `algo/graph_hard` |
| Ferguson-Forcade algorithm | todo | `algo/numeric` (extensions) |
| FFT | todo | `algo/fft` |
| Fibonaccian search | done | `algo/search` |
| Fibonacci heap | done | `lib/fibonacci_heap` |
| Fibonacci number | done | `algo/number_theory` |
| Fibonacci tree | def |  |
| FIFO | done | `lib/queue` |
| filial-heir chain | todo | `lib/binary_tree_repr` |
| Find | done | `algo/select` |
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
| flash sort | done | `algo/sort_dist` |
| flow | def |  |
| flow conservation | def |  |
| flow function | def |  |
| flow network | done | `algo/flow` |
| Floyd-Warshall algorithm | done | `algo/graph_view` |
| Ford-Bellman | done | `algo/graph_view` |
| Ford-Fulkerson method | done | `algo/flow` |
| forest | def |  |
| forest editing problem | def |  |
| formal language | def |  |
| formal methods | def |  |
| formal verification | def |  |
| forward index | def |  |
| fractional knapsack problem | done | `algo/knapsack` |
| fractional solution | def |  |
| free edge | def |  |
| free tree | def |  |
| free vertex | def |  |
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
| gamma function | done | `algo/numerical` |
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
| hash tree | done | `lib/merkle_tree` |
| Hausdorff distance | done | `algo/geometry` |
| hB-tree | todo | `lib/hb_tree` |
| head | def |  |
| heap | done | `lib/heap` |
| heapify | done | `lib/heap` |
| heap property | def |  |
| heapsort | done | `algo/sort` |
| heaviest common subsequence | done | `algo/string_approx` |
| height | def |  |
| height-balanced binary search tree | done | `lib/avl` |
| height-balanced tree | done | `lib/avl` |
| heuristic | def |  |
| hidden Markov model | todo | `algo/hmm` |
| highest common factor | done | `algo/numeric` |
| histogram sort | done | `algo/sort_dist` |
| HMM | todo | `algo/hmm` |
| homeomorphic | def |  |
| horizontal visibility map | def |  |
| Horner's rule | done | `algo/numerical` |
| Horspool | done | `algo/string_search` |
| hsadelta | def |  |
| Huffman coding | done | `algo/prefix_code` |
| huge sparse array | todo | `lib/sparse_matrix` |
| Hungarian algorithm | done | `algo/matching` |
| hybrid algorithm | def |  |
| hyperedge | def |  |
| hypergraph | def |  |
| HyperLogLog | done | `lib/hyperloglog` |

### I

| entry | status | module |
|---|---|---|
| IBLT | done | `lib/iblt` |
| ideal merge | done | `algo/kway_merge` |
| ideal random shuffle | done | `algo/sequence` (ds_shuffle) |
| implication | def |  |
| implies | def |  |
| inclusion-exclusion principle | def |  |
| inclusive or | def |  |
| incompressible string | def |  |
| incremental hashing | done | `algo/hash_family` |
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
| interpolation-sequential search | done | `algo/search` |
| interpolation sort | done | `algo/sort_dist` |
| intersection | done | `algo/sequence` (ds_set_intersection) |
| interval tree | done | `lib/interval_tree` |
| intractable | def |  |
| introsort | done | `algo/sort` |
| introspective sort | done | `algo/sort` |
| inverse Ackermann function | done | `algo/number_theory` |
| inverse suffix array | todo | `lib/suffix_tree` |
| inversion list | done | `lib/inversion_list` |
| inverted file index | def |  |
| inverted index | def |  |
| invertible Bloom lookup table | done | `lib/iblt` |
| irreflexive | def |  |
| isomorphic | def |  |
| iteration | def |  |

### J

| entry | status | module |
|---|---|---|
| Jaro-Winkler | done | `algo/string_approx` |
| jelly-fish | def |  |
| Johnson's algorithm | done | `algo/graph_path` |
| Johnson-Trotter | done | `algo/combinatorics` |
| JSort | done | `algo/sort_heap` |
| J sort | done | `algo/sort_dist` |
| jump list | todo | `lib/jump_list` |
| jump search | done | `algo/search` |

### K

| entry | status | module |
|---|---|---|
| Karnaugh map | def |  |
| Karp-Rabin | done | `algo/stringology` |
| Karp reduction | def |  |
| k-ary heap | done | `lib/indexed_heap` (d-ary) |
| k-ary Huffman coding | done | `algo/prefix_code` |
| k-ary tree | def |  |
| k-clustering | todo | `algo/graph_hard` |
| k-coloring | todo | `algo/graph_hard` |
| k-connected graph | done | `algo/flow` |
| k-d-B-tree | todo | `lib/kdb_tree` |
| k-dimensional | def |  |
| K-dominant match | done | `algo/string_approx` |
| k-d tree | done | `lib/kd_tree` |
| key | def |  |
| KMP | done | `algo/stringology` |
| KmpSkip Search | todo | `algo/string_search` |
| knapsack problem | done | `algo/knapsack` |
| knight's tour | done | `algo/combinatorics` |
| Knuth-Morris-Pratt algorithm | done | `algo/stringology` |
| Königsberg bridges problem | done | `algo/graph_struct` |
| Kolmogorov complexity | def |  |
| Kraft's inequality | def |  |
| Kripke structure | def |  |
| Kruskal's algorithm | done | `algo/graph_path` |
| kth order Fibonacci numbers | done | `algo/number_theory` |
| kth shortest path | done | `algo/graph_path` |
| kth smallest element | done | `algo/sort` (nth_element) |
| k²-tree | todo | `lib/k2_tree` |
| KV diagram | def |  |
| k-way merge | done | `algo/kway_merge` |
| k-way merge sort | done | `algo/sort_misc` |
| k-way tree | def |  |

### L

| entry | status | module |
|---|---|---|
| labeled graph | def |  |
| language | def |  |
| last-in, first-out | done | `lib/stack` |
| Las Vegas algorithm | def |  |
| lattice | def |  |
| layered graph | done | `algo/flow` |
| LCFS hashing | done | `lib/open_map` |
| LCM | done | `algo/numeric` |
| LCS | done | `algo/stringology` |
| LDS | def |  |
| leaf | def |  |
| least common multiple | done | `algo/numeric` |
| leftist tree | done | `lib/meldable_heap` |
| left rotation | done | `lib/avl, lib/rbt` |
| Lempel-Ziv-Welch | done | `algo/compress` |
| level | def |  |
| level-order traversal | def |  |
| Levenshtein distance | done | `algo/stringology` |
| lexicographical order | done | `algo/sequence` (ds_lexicographic_compare) |
| LIFO | done | `lib/stack` |
| linear | def |  |
| linear congruential generator | done | `algo/random` |
| linear hash | done | `algo/hash_family` |
| linear hashing | done | `lib/linear_hash_map` |
| linear insertion sort | done | `algo/sort` |
| linear order | def |  |
| linear probing | done | `lib/hash_table, lib/rh_map` |
| linear probing sort | done | `algo/sort_dist` |
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
| Lm distance | done | `algo/geometry` |
| load factor | done | `lib/hash_table, lib/rh_map` |
| local alignment | done | `algo/string_approx` |
| locality-sensitive hashing | done | `algo/lsh` |
| local optimum | def |  |
| logarithmic | def |  |
| longest common subsequence | done | `algo/stringology` |
| longest common substring | done | `algo/string_approx` |
| Lotka's law | def |  |
| lower bound | done | `algo/search` (ds_lower_bound) |
| lower triangular matrix | todo | `algo/matrix` |
| lowest common ancestor | todo | `algo/graph_alg` |
| l-reduction | def |  |
| lucky sort | done | `algo/sort_misc` |
| LZW compression | done | `algo/compress` |

### M

| entry | status | module |
|---|---|---|
| Malhotra-Kumar-Maheshwari blocking flow | done | `algo/flow` |
| Manhattan distance | done | `algo/geometry` |
| many-one reduction | def |  |
| map | done | `lib/hash_table, lib/rh_map, lib/avl, ...` |
| Markov chain | def |  |
| Marlena | def |  |
| marriage problem | done | `algo/matching` |
| Master theorem | def |  |
| matched edge | def |  |
| matched vertex | def |  |
| matching | done | `algo/matching` |
| matrix | def |  |
| matrix-chain multiplication problem | done | `algo/knapsack` |
| matrix multiplication | todo | `algo/matrix` |
| max-heap property | def |  |
| maximal independent set | todo | `algo/graph_hard` |
| maximally connected component | def |  |
| Maximal Shift | todo | `algo/string_search` |
| maximum bipartite matching | done | `algo/matching` |
| maximum-flow problem | done | `algo/flow` |
| MBB | done | `algo/geometry` |
| Mealy machine | todo | `lib/automaton` |
| mean | done | `algo/numeric` (Welford) |
| median | done | `algo/sort` (nth_element) |
| meld | done | `lib/meldable_heap` |
| memoization | def |  |
| merge | done | `algo/sequence` |
| merge sort | done | `algo/sort` |
| Merkle tree | done | `lib/merkle_tree` |
| metaheuristic | def |  |
| metaphone | done | `algo/phonetic` |
| midrange | done | `algo/numerical` |
| Miller-Rabin | done | `algo/numeric` |
| min-heap property | def |  |
| minimal perfect hashing | done | `algo/perfect_hash` |
| minimax | todo | `algo/game` |
| minimum bounding box | done | `algo/geometry` |
| minimum cut | done | `algo/flow` |
| minimum spanning tree | done | `algo/graph_view` (Prim), lib/graph |
| minimum vertex cut | done | `algo/flow` |
| Minkowski distance | done | `algo/geometry` |
| mixed integer linear program | todo | `algo/lp` |
| mode | done | `algo/numerical` |
| model checking | def |  |
| model of computation | def |  |
| moderately exponential | def |  |
| MODIFIND | done | `algo/select` |
| monotone priority queue | done | `lib/radix_heap` |
| monotonically decreasing | def |  |
| monotonically increasing | def |  |
| Monte Carlo algorithm | def |  |
| Moore machine | todo | `lib/automaton` |
| Morris-Pratt algorithm | done | `algo/string_search` |
| move | done | `algo/compress` |
| move-to-front heuristic | todo | `lib/self_org_list` |
| move-to-root heuristic | todo | `lib/self_org_list` |
| MST | done | `algo/graph_view, lib/graph` |
| multi-commodity flow | def |  |
| multigraph | def |  |
| multikey Quicksort | done | `algo/sort_dist` |
| multilayer grid file | todo | `lib/grid_file` |
| multiplication method | done | `algo/hash_family` |
| multiprefix | done | `algo/scan` |
| multiprocessor model | def |  |
| multi-set | todo | `lib/multiset` |
| multi suffix tree | todo | `lib/suffix_tree` |
| multiway decision | def |  |
| multiway merge | done | `algo/kway_merge` |
| multiway search tree | done | `lib/btree` |
| multiway tree | done | `lib/btree` |
| Munkres' assignment algorithm | done | `algo/matching` |

### N

| entry | status | module |
|---|---|---|
| naive string search | done | `algo/string_search` |
| nand | def |  |
| n-ary function | def |  |
| NC many-one reducibility | def |  |
| nearest neighbor | done | `algo/geometry` |
| negation | def |  |
| network flow | done | `algo/flow` |
| network flow problem | done | `algo/flow` |
| next state | def |  |
| NFA | todo | `lib/automaton` |
| NFTA | def |  |
| NIST | def |  |
| node | def |  |
| nonbalanced merge | done | `algo/external_sort` |
| nonbalanced merge sort | done | `algo/external_sort` |
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
| Not So Naive | done | `algo/string_search` |
| NP | def |  |
| NP-complete | def |  |
| NP-complete language | def |  |
| NP-hard | def |  |
| n queens | done | `algo/combinatorics` |
| nullary function | def |  |
| null tree | def |  |
| NYSIIS | done | `algo/phonetic` |

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
| optimal hashing | done | `algo/perfect_hash` |
| optimal merge | done | `algo/kway_merge` |
| optimal mismatch | todo | `algo/string_search` |
| optimal polygon triangulation problem | done | `algo/geometry` |
| optimal polyphase merge | done | `algo/external_sort` |
| optimal polyphase merge sort | done | `algo/external_sort` |
| optimal solution | def |  |
| optimal triangulation problem | done | `algo/geometry` |
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
| order-preserving hash | done | `algo/hash_family` |
| order-preserving Huffman coding | done | `algo/prefix_code` |
| order-preserving minimal perfect hashing | done | `algo/perfect_hash` |
| oriented acyclic graph | def |  |
| oriented graph | def |  |
| oriented tree | def |  |
| orthogonal drawing | def |  |
| orthogonal lists | todo | `lib/sparse_matrix` |
| orthogonally convex rectilinear polygon | done | `algo/geometry` |
| oscillating merge sort | done | `algo/external_sort` |
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
| parallel prefix computation | done | `algo/scan` |
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
| partition | done | `algo/combinatorics` |
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
| Pearson's hash | done | `algo/hash_family` |
| perfect binary tree | def |  |
| perfect hashing | done | `algo/perfect_hash` |
| perfect k-ary tree | def |  |
| perfect matching | done | `algo/matching` |
| perfect shuffle | def |  |
| performance guarantee | def |  |
| performance ratio | def |  |
| permutation | done | `algo/sequence` (next/prev permutation) |
| permutation sort | done | `algo/sort_misc` |
| persistent data structure | done | `lib/persistent_rbt, lib/persistent_trie` |
| phonetic coding | done | `algo/phonetic` |
| pigeonhole sort | done | `algo/sort_dist` |
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
| polyphase merge | done | `algo/external_sort` |
| polyphase merge sort | done | `algo/external_sort` |
| polytope | def |  |
| poset | def |  |
| postfix traversal | def |  |
| Post machine | def |  |
| postman's sort | done | `algo/sort_dist` |
| postorder traversal | done | `lib/avl, lib/bst walks` |
| Post's correspondence problem | def |  |
| potential function | def |  |
| PRAM | def |  |
| predicate | def |  |
| prefix | def |  |
| prefix code | done | `algo/prefix_code` |
| prefix sums | done | `algo/scan` |
| prefix traversal | def |  |
| preorder traversal | done | `lib/avl, lib/bst walks` |
| primary clustering | def |  |
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
| probe sequence | done | `lib/open_map` |
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
| pth order Fibonacci numbers | done | `algo/number_theory` |
| P-tree | todo | `lib/hb_tree` |
| purely functional language | def |  |
| pushdown automaton | def |  |
| pushdown transducer | def |  |
| p-way merge sort | done | `algo/sort_misc` |

### Q

| entry | status | module |
|---|---|---|
| qm sort | done | `algo/sort_misc` |
| q sort | done | `algo/sort_misc` |
| quadratic probing | done | `lib/hash_table` |
| quadtree | todo | `lib/quadtree` |
| quadtree complexity theorem | def |  |
| quad trie | todo | `lib/quadtree` |
| quantum computation | def |  |
| queue | done | `lib/queue` |
| quick search | done | `algo/string_search` |
| quicksort | done | `algo/sort_misc` |

### R

| entry | status | module |
|---|---|---|
| Rabin-Karp | done | `algo/stringology` |
| radix sort | done | `algo/sort` (LSD radix) |
| radix tree | done | `lib/radix_trie` |
| ragged matrix | todo | `lib/sparse_matrix` |
| Raita | done | `algo/string_search` |
| random access machine | def |  |
| randomization | def |  |
| randomized algorithm | def |  |
| randomized binary search tree | todo | `lib/rbst` |
| randomized complexity | def |  |
| randomized polynomial time | def |  |
| randomized rounding | def |  |
| randomized search tree | done | `lib/treap` |
| Randomized-Select | done | `algo/select` |
| random number generator | done | `algo/random` (xoshiro128**) |
| random sampling | done | `algo/random` |
| random search | def |  |
| range | def |  |
| range sort | done | `algo/sort_dist` |
| rank | done | `lib/order_statistic_tree, lib/bitvector` |
| rapid sort | done | `algo/sort_dist` |
| Ratcliff/Obershelp pattern recognition | done | `algo/string_approx` |
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
| reduced digraph | done | `algo/graph_struct` |
| reduced ordered binary decision diagram | todo | `lib/bdd` |
| reduction | def |  |
| reflexive | def |  |
| regular decomposition | todo | `lib/quadtree` |
| rehashing | done | `lib/open_map` |
| relation | def |  |
| relational structure | def |  |
| relative performance guarantee | def |  |
| relaxation | def |  |
| relaxed balance | def |  |
| repeated squaring | done | `algo/numeric` (ds_powmod) |
| rescalable | def |  |
| reservoir sampling | done | `algo/random` |
| restricted universe sort | done | `algo/sort_dist` |
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
| saturated edge | def |  |
| SBB tree | done | `lib/rbt` (symmetric binary B-tree = red-black tree) |
| scan | done | `algo/scan` |
| scapegoat tree | todo | `lib/scapegoat_tree` |
| scatter storage | def |  |
| Schorr-Waite graph marking algorithm | todo | `algo/schorr_waite` |
| search | def |  |
| search tree | def |  |
| search tree property | def |  |
| secant search | done | `algo/numerical` |
| secondary clustering | def |  |
| segment | def |  |
| Select | done | `algo/sort` (nth_element), lib/order_statistic_tree |
| select and partition | done | `algo/select` |
| selection problem | done | `algo/sort` (nth_element) |
| selection sort | done | `algo/sort_classic` |
| select kth element | done | `algo/sort` (nth_element) |
| select mode | done | `algo/select` |
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
| shadow heap | done | `lib/shadow_heap` |
| shadow merge | done | `lib/shadow_heap` |
| shadow merge insert | done | `lib/shadow_heap` |
| shaker sort | done | `algo/sort_classic` (cocktail) |
| Shannon-Fano coding | done | `algo/prefix_code` |
| Shell sort † | done | `algo/sort` |
| Shift-Or | done | `algo/string_search` |
| Shor's algorithm | def |  |
| shortcutting | todo | `algo/parallel_sim` |
| shortest common supersequence | done | `algo/string_approx` |
| shortest common superstring | done | `algo/string_approx` |
| shortest path | done | `algo/graph_path` |
| shortest spanning tree | done | `algo/graph_path` |
| shuffle | done | `algo/sequence` |
| shuffle sort † | done | `algo/sort_dist` |
| sibling † | def |  |
| sieve of Eratosthenes | done | `algo/numeric` |
| sift up † | def |  |
| signature | def |  |
| Simon's algorithm | def |  |
| simple merge † | done | `algo/sequence` |
| simple path | def |  |
| simple uniform hashing | def |  |
| simplex | todo | `algo/lp` |
| simulated annealing | def |  |
| simulation theorem † | def |  |
| single-destination shortest-path problem | todo | `algo/graph_alg` |
| single-pair shortest-path problem | done | `algo/graph_path` |
| single program multiple data | def |  |
| single-source shortest-path problem | done | `algo/graph_path` |
| singly linked list | done | `lib/list` |
| singularity analysis † | def |  |
| sink | def |  |
| sinking sort † | done | `algo/sort_classic` |
| skd-tree † | todo | `lib/kdb_tree` |
| skew heap † | done | `lib/meldable_heap` |
| skew symmetry | def |  |
| skip list | done | `lib/skip_list` |
| skip search | done | `algo/string_search` |
| slope selection | done | `algo/geometry` |
| Smith algorithm † | done | `algo/string_search` |
| Smith-Waterman algorithm † | done | `algo/string_approx` |
| smoothsort † | done | `algo/sort_heap` |
| solvable † | def |  |
| sort † | def |  |
| sorted array † | todo | `lib/sorted_list` |
| sorted list † | todo | `lib/sorted_list` |
| sort in place † | def |  |
| soundex † | done | `algo/phonetic` |
| source | def |  |
| space-constrained Lempel-Ziv-Welch † | done | `algo/compress` |
| spanning tree † | done | `algo/graph_path` |
| sparse graph | def |  |
| sparse matrix | todo | `lib/sparse_matrix` |
| sparsification | def |  |
| sparsity † | def |  |
| spatial access method † | todo | `lib/rtree` |
| spectral test † | done | `algo/random` |
| splay tree † | done | `lib/splay_tree` |
| SPMD | def |  |
| square matrix | def |  |
| square root | done | `algo/numerical` |
| SST | done | `algo/graph_path` |
| stable | def |  |
| stable marriage problem † | done | `algo/matching` |
| stack | done | `lib/stack` |
| stack tree | def |  |
| star encoding | done | `algo/compress` |
| star-shaped polygon | done | `algo/geometry` |
| start state | def |  |
| state | def |  |
| state machine | def |  |
| state transition | def |  |
| static | def |  |
| static Huffman coding | done | `algo/prefix_code` |
| s-t cut | done | `algo/flow` |
| st-digraph | def |  |
| Steiner point | def |  |
| Steiner ratio | def |  |
| Steiner tree | todo | `algo/graph_hard` |
| Steiner vertex | def |  |
| Steinhaus-Johnson-Trotter | done | `algo/combinatorics` |
| Stirling's approximation | done | `algo/numerical` |
| Stirling's formula | done | `algo/numerical` |
| stooge sort † | done | `algo/sort_classic` |
| straight-line drawing | def |  |
| strand sort † | done | `algo/sort_misc` |
| strictly decreasing | def |  |
| strictly increasing | def |  |
| strictly lower triangular matrix | todo | `algo/matrix` |
| strictly upper triangular matrix | todo | `algo/matrix` |
| string | done | `lib/str` |
| string editing problem | done | `algo/stringology` (edit distances) |
| string matching † | done | `algo/string_search` |
| string matching on ordered alphabets † | todo | `algo/string_search` |
| string matching with errors † | done | `algo/string_approx` |
| string matching with mismatches † | done | `algo/string_search` |
| string searching † | done | `algo/string_search` |
| strip packing | done | `algo/knapsack` |
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
| supersink | def |  |
| supersource | def |  |
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
| ternary search † | done | `algo/search` |
| ternary search tree † | todo | `lib/tst` |
| text | done | `lib/str, lib/rope, lib/gap_buffer` |
| text searching † | done | `algo/string_search` |
| theta | def |  |
| threaded binary tree † | todo | `lib/threaded_tree` |
| threaded tree † | todo | `lib/threaded_tree` |
| three-dimensional | def |  |
| three-way merge sort † | done | `algo/sort_misc` |
| three-way radix quicksort † | done | `algo/sort_dist` |
| time-constructible function † | def |  |
| time/space complexity † | def |  |
| top-down radix sort † | done | `algo/sort_dist` |
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
| tournament | done | `algo/kway_merge` |
| tournament sort † | done | `algo/sort_heap` |
| towers of Hanoi | done | `algo/combinatorics` |
| tractable † | def |  |
| transducer † | def |  |
| transition | def |  |
| transition function | def |  |
| transitive | def |  |
| transitive closure | done | `algo/graph_struct` |
| transitive reduction | done | `algo/graph_struct` |
| transpose sequential search † | todo | `lib/self_org_list` |
| traveling salesman | todo | `algo/graph_hard` |
| treap † | done | `lib/treap` |
| tree † | done | `lib/bst` |
| tree automaton | def |  |
| tree contraction | todo | `algo/parallel_sim` |
| tree editing problem | def |  |
| tree sort † | done | `algo/sort_heap` |
| tree transducer † | def |  |
| tree traversal † | done | `lib/bst walks` |
| triangle inequality | def |  |
| triconnected graph | todo | `algo/graph_alg` |
| trie † | done | `lib/trie` |
| trinary function | def |  |
| tripartition | def |  |
| Trotter-Johnson | done | `algo/combinatorics` |
| TSP | todo | `algo/graph_hard` |
| TST † | todo | `lib/tst` |
| Turbo-BM † | done | `algo/string_search` |
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
| Two Way algorithm † | done | `algo/string_search` |
| two-way linked list | done | `lib/dlist` |
| two-way merge sort † | done | `algo/sort_misc` |

### U

| entry | status | module |
|---|---|---|
| UB-tree † | todo | `lib/ub_tree` |
| UKP | done | `algo/knapsack` |
| unary function | def |  |
| unbiased coin flipping algorithm | done | `algo/random` |
| unbounded knapsack problem | done | `algo/knapsack` |
| uncomputable function † | def |  |
| uncomputable problem † | def |  |
| undecidable language † | def |  |
| undecidable problem † | def |  |
| undirected graph | def |  |
| uniform circuit complexity † | def |  |
| uniform circuit family † | def |  |
| uniform hashing † | def |  |
| uniform matrix | def |  |
| union | done | `algo/sequence` (ds_set_union) |
| union of automata | todo | `lib/automaton` |
| universal hashing † | done | `algo/hash_family` |
| universal state | def |  |
| universal Turing machine | def |  |
| universe | def |  |
| unranking | done | `algo/combinatorics` |
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
| vertex connectivity | done | `algo/flow` |
| vertex cover | todo | `algo/graph_hard` |
| vertical visibility map | def |  |
| virtual hashing † | todo | `lib/linear_hash_map` |
| visibility map | def |  |
| visible | def |  |
| Viterbi algorithm | todo | `algo/hmm` |
| Vitter's algorithm | done | `algo/adaptive_huffman` |
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
| Wu's line algorithm † | done | `algo/geometry` |

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
| Zeller's congruence | done | `algo/number_theory` |

### Symbols and digits

| entry | status | module |
|---|---|---|
| 0-ary function † | def |  |
| 0-based indexing † | def |  |
| 0/1 knapsack problem † | done | `algo/knapsack` |

### Z

| entry | status | module |
|---|---|---|
| Zhu-Takaoka † | done | `algo/string_search` |
| Zipfian distribution | def |  |
| Zipf's law | def |  |
| ZPP † | def |  |
