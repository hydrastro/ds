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

The tail of the index (after "shaker sort"), which the A-Z page cuts
off, was checked against DADS's second-level index pages (t42 to t53).

## Summary

| status | entries |
|---|---|
| done | 712 |
| todo | 43 |
| def | 552 |
| total | 1307 |

## Todo by target module

Families ordered by how many DADS entries they close.

| target | entries | terms |
|---|---|---|
| `lib/tree_automaton` | 8 | deterministic finite tree automaton, deterministic tree automaton, DFTA, NFTA, nondeterministic finite tree automaton, nondeterministic tree automaton, top-down tree automaton, tree automaton |
| `lib/inverted_index` | 5 | block addressing index, forward index, full inverted index, inverted file index, inverted index |
| `lib/pushdown` | 5 | deterministic pushdown automaton, DPDA, PDA, pushdown automaton, pushdown transducer |
| `lib/turing_machine` | 4 | busy beaver, nondeterministic Turing machine, Turing machine, Turing transducer |
| `algo/model_check` | 3 | CTL, Kripke structure, model checking |
| `algo/visibility` | 3 | horizontal visibility map, vertical visibility map, visibility map |
| `algo/tree_edit` | 2 | forest editing problem, tree editing problem |
| `lib/sstable` | 2 | sorted-string table, SSTable |
| `algo/anneal` | 1 | simulated annealing |
| `algo/dynamization` | 1 | dynamization transformation |
| `algo/markov` | 1 | Markov chain |
| `algo/space_order` | 1 | space ordering method |
| `lib/binary_tree_repr (extensions)` | 1 | stack tree |
| `lib/cellular_automaton` | 1 | cellular automaton |
| `lib/confluent_list` | 1 | confluently persistent data structure |
| `lib/plop_hash` | 1 | PLOP-hashing |
| `lib/spiral_storage` | 1 | spiral storage |
| `lib/topology_tree` | 1 | topology tree |
| `lib/zipper` | 1 | zipper |

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
| adaptive k-d tree | done | `lib/kdb_tree` |
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
| association list | done | `lib/association_list` |
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
| bag | done | `lib/multiset` |
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
| BANG file | done | `lib/bang_file` |
| Batcher sort | done | `algo/sort_classic` (bitonic) |
| Baum Welch algorithm | done | `algo/hmm` |
| BB(α) tree | done | `lib/wb_tree` |
| BBP algorithm | done | `algo/numerical` |
| BDD | done | `lib/bdd` |
| BD-tree | done | `lib/hb_tree` |
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
| binary tree representation of trees | done | `lib/binary_tree_repr` |
| bingo sort | done | `algo/sort_misc` |
| binomial heap | done | `lib/binomial_heap` |
| binomial tree | done | `lib/binomial_heap` |
| bin packing problem | done | `algo/knapsack` |
| bin sort | done | `algo/sort_dist` |
| bintree | done | `lib/quadtree` |
| bipartite graph | done | `algo/graph_view` (ds_gview_bipartite) |
| bipartite matching | done | `algo/matching` |
| bisector | done | `algo/geometry` |
| bitonic sort | done | `algo/sort_classic` |
| bit vector | done | `lib/bitset, lib/bitvector` |
| Bk tree | done | `lib/binomial_heap` |
| blind sort | done | `algo/sort_dist` |
| blind trie | done | `lib/elastic_trie` |
| block | def |  |
| block addressing index | todo | `lib/inverted_index` |
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
| bottleneck traveling salesman | done | `algo/tsp` |
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
| BSP-tree | done | `lib/bsp_tree` |
| B*-tree | done | `lib/bplus_tree` (B* split policy) |
| B-tree | done | `lib/btree` |
| bubble sort | done | `algo/sort_classic` |
| bucket | def |  |
| bucket array | def |  |
| bucketing method | def |  |
| bucket sort | done | `algo/sort_dist` |
| bucket trie | done | `lib/elastic_trie` |
| buddy system | done | `lib/buddy_allocator` |
| buddy tree | done | `lib/hb_tree` |
| build-heap | done | `lib/heap, algo/sort` (heap sort) |
| Burrows-Wheeler transform | done | `lib/suffix_tree` |
| busy beaver | todo | `lib/turing_machine` |
| BV-tree | done | `lib/hb_tree` |
| BWT | done | `lib/suffix_tree` |
| Byzantine generals | def |  |

### C

| entry | status | module |
|---|---|---|
| cactus stack | done | `lib/cactus_stack` |
| Calculus of Communicating Systems | def |  |
| calendar queue | done | `lib/calendar_queue` |
| candidate consistency testing | def |  |
| candidate verification | def |  |
| canonical complexity class | def |  |
| capacitated facility location | done | `algo/facility` |
| capacity | def |  |
| capacity constraint | def |  |
| cartesian tree | done | `algo/cartesian_tree` |
| Caverphone | done | `algo/phonetic` |
| CCS | def |  |
| cell probe model | def |  |
| cell tree | done | `lib/bsp_tree` |
| cellular automaton | todo | `lib/cellular_automaton` |
| centroid | done | `algo/graph_struct` |
| certificate | def |  |
| chain | def |  |
| chaining | done | `lib/hash_table` (chaining mode) |
| child | def |  |
| Chinese postman problem | done | `algo/graph_path` |
| Chinese remainder theorem | done | `algo/number_theory` |
| Christofides algorithm | done | `algo/tsp` |
| chromatic index | done | `algo/graph_np` |
| chromatic number | done | `algo/graph_np` |
| circuit | def |  |
| circuit complexity | def |  |
| circuit value problem | def |  |
| circular list | done | `lib/ring_list` |
| circular queue | done | `lib/circular_buffer` |
| clique | done | `algo/graph_np` |
| clique problem | done | `algo/graph_np` |
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
| Colussi | done | `algo/string_search` |
| combination | done | `algo/combinatorics` |
| comb sort | done | `algo/sort_classic` |
| Commentz-Walter | done | `algo/string_search` |
| Communicating Sequential Processes | def |  |
| commutative | def |  |
| compact data structure | def |  |
| compact DAWG | done | `lib/suffix_automaton` |
| compact trie | done | `lib/elastic_trie` |
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
| confluently persistent data structure | todo | `lib/confluent_list` |
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
| CTL | todo | `algo/model_check` |
| cube root | done | `algo/numerical` |
| cuckoo hashing | done | `lib/cuckoo_map` |
| Cupif-Giannini tree traversal | done | `lib/threaded_tree` |
| cut | def |  |
| cutting plane | done | `algo/lp` |
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
| DAWG | done | `lib/suffix_automaton` |
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
| deterministic finite automaton | done | `lib/automaton` |
| deterministic finite state machine | def |  |
| deterministic finite tree automaton | todo | `lib/tree_automaton` |
| deterministic pushdown automaton | todo | `lib/pushdown` |
| deterministic random bit generator | done | `algo/random` (xoshiro, not cryptographic) |
| deterministic tree automaton | todo | `lib/tree_automaton` |
| Deutsch-Jozsa algorithm | def |  |
| DFA | done | `lib/automaton` |
| DFS | done | `lib/graph` |
| DFS forest | done | `algo/graph_struct` |
| DFTA | todo | `lib/tree_automaton` |
| diagonalization | def |  |
| diameter | done | `algo/graph_struct` |
| Dianna | def |  |
| dichotomic search | done | `algo/search` |
| dictionary | done | `lib/hash_table, lib/rh_map, lib/avl, ...` |
| diet | done | `lib/diet` |
| difference | done | `algo/sequence` (ds_set_difference) |
| digital search tree | done | `lib/trie` |
| digital tree | done | `lib/trie` |
| digraph | def |  |
| Dijkstra's algorithm | done | `algo/graph_view, lib/graph` |
| diminishing increment sort | done | `algo/sort` (Shell sort) |
| dining philosophers | def |  |
| direct chaining | done | `lib/hash_table` |
| directed acyclic graph | def |  |
| directed acyclic word graph | done | `lib/suffix_automaton` |
| directed graph | done | `lib/graph` |
| discrete interval encoding tree | done | `lib/diet` |
| discrete p-center | done | `algo/facility` |
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
| doubly-chained tree | done | `lib/binary_tree_repr` |
| doubly-ended queue | done | `lib/deque` |
| doubly linked list | done | `lib/dlist` |
| DPDA | todo | `lib/pushdown` |
| DRBG | done | `algo/random` |
| D-tree | done | `lib/d_tree` |
| dual | def |  |
| dual linear program | done | `algo/lp` |
| dual-pivot quicksort | done | `algo/sort_misc` |
| Dutch national flag | done | `algo/sort_misc` |
| dyadic tree | def |  |
| dynamic | def |  |
| dynamic array | done | `lib/vec` |
| dynamic hashing | done | `lib/linear_hash_map` |
| dynamic Huffman coding | done | `algo/adaptive_huffman` |
| dynamic programming | def |  |
| dynamization transformation | todo | `algo/dynamization` |

### E

| entry | status | module |
|---|---|---|
| easy split, hard merge | def |  |
| edge | def |  |
| edge coloring | done | `algo/graph_np` |
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
| elastic-bucket trie | done | `lib/elastic_trie` |
| element uniqueness | done | `algo/sequence` (ds_unique after sort) |
| end-of-string | def |  |
| enfilade | done | `lib/enfilade` |
| ERCW | def |  |
| EREW | def |  |
| Euclidean algorithm | done | `algo/numeric` (ds_gcd) |
| Euclidean distance | done | `algo/geometry` |
| Euclidean Steiner tree | done | `algo/steiner` |
| Euclidean traveling salesman problem | done | `algo/tsp` |
| Euclid's algorithm | done | `algo/numeric` |
| Euler cycle | done | `algo/graph_struct` |
| Eulerian graph | done | `algo/graph_struct` |
| Eulerian path | done | `algo/graph_struct` |
| Euler's formula | def |  |
| exact string matching | done | `algo/string_search` |
| EXCELL | done | `lib/bang_file` |
| exchange sort | done | `algo/sort_classic` (bubble) |
| exclusive or | def |  |
| exclusive read, concurrent write | def |  |
| exclusive read, exclusive write | def |  |
| exhaustive search | def |  |
| existential state | def |  |
| expandable hashing | done | `lib/dynamic_hash` |
| expander graph | def |  |
| exponential | def |  |
| extended binary tree | def |  |
| extended Euclid's algorithm | done | `algo/numeric` (ds_ext_gcd) |
| extended k-d tree | done | `lib/kdb_tree` |
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
| facility location | done | `algo/facility` |
| factor | def |  |
| factorial | done | `algo/combinatorics` |
| fast fourier transform | done | `algo/fft` |
| fathoming | def |  |
| feasible region | def |  |
| feasible solution | def |  |
| feedback edge set | done | `algo/graph_struct` |
| feedback vertex set | done | `algo/graph_np` |
| Ferguson-Forcade algorithm | done | `algo/lattice` |
| FFT | done | `algo/fft` |
| Fibonaccian search | done | `algo/search` |
| Fibonacci heap | done | `lib/fibonacci_heap` |
| Fibonacci number | done | `algo/number_theory` |
| Fibonacci tree | def |  |
| FIFO | done | `lib/queue` |
| filial-heir chain | done | `lib/binary_tree_repr` |
| Find | done | `algo/select` |
| find kth least element | done | `algo/sort` (nth_element) |
| finitary tree | def |  |
| finite Fourier transform | done | `algo/fft` |
| finite state automaton | done | `lib/automaton` |
| finite state machine | done | `lib/automaton` |
| finite state machine minimization | done | `lib/automaton` |
| finite state transducer | done | `lib/automaton` |
| first child-next sibling binary tree | done | `lib/binary_tree_repr` |
| first come, first served | def |  |
| first-in, first-out | done | `lib/queue` |
| Fisher-Yates shuffle | done | `algo/sequence` (ds_shuffle) |
| fixed-grid method | done | `lib/grid_file` |
| flash sort | done | `algo/sort_dist` |
| flow | def |  |
| flow conservation | def |  |
| flow function | def |  |
| flow network | done | `algo/flow` |
| Floyd-Warshall algorithm | done | `algo/graph_view` |
| Ford-Bellman | done | `algo/graph_view` |
| Ford-Fulkerson method | done | `algo/flow` |
| forest | def |  |
| forest editing problem | todo | `algo/tree_edit` |
| formal language | def |  |
| formal methods | def |  |
| formal verification | def |  |
| forward index | todo | `lib/inverted_index` |
| fractional knapsack problem | done | `algo/knapsack` |
| fractional solution | def |  |
| free edge | def |  |
| free tree | def |  |
| free vertex | def |  |
| frequency count heuristic | done | `lib/self_org_list` |
| full array | def |  |
| full binary tree | def |  |
| full inverted index | todo | `lib/inverted_index` |
| fully dynamic graph problem | def |  |
| fully persistent data structure | done | `lib/history` (branching timelines) |
| fully polynomial approximation scheme | def |  |
| function | def |  |
| functional data structure | def |  |

### G

| entry | status | module |
|---|---|---|
| Galil-Giancarlo | done | `algo/string_search` |
| Galil-Seiferas | done | `algo/string_search` |
| gamma function | done | `algo/numerical` |
| GBD-tree | done | `lib/hb_tree` |
| GCD | done | `algo/numeric` |
| geometric optimization problem | done | `algo/geo_opt` |
| global optimum | def |  |
| gnome sort | done | `algo/sort_classic` |
| graph | def |  |
| graph coloring | done | `algo/graph_np` |
| graph concentration | def |  |
| graph drawing | def |  |
| graph isomorphism | done | `algo/graph_np` |
| graph partition | done | `algo/graph_np` |
| Gray code | done | `algo/bits` |
| greatest common divisor | done | `algo/numeric` |
| greedy algorithm | def |  |
| greedy heuristic | def |  |
| grid drawing | def |  |
| grid file | done | `lib/grid_file` |
| Grover's algorithm | def |  |

### H

| entry | status | module |
|---|---|---|
| halting problem | def |  |
| Hamiltonian cycle | done | `algo/graph_np` |
| Hamiltonian path | done | `algo/graph_np` |
| Hamming distance | done | `algo/stringology` |
| hard split, easy merge | def |  |
| hash | done | `algo/hash` |
| hashbelt | done | `lib/dynamic_hash` |
| hash function | done | `algo/hash` |
| hash heap | done | `lib/hash_heap` |
| hash table | done | `lib/hash_table, lib/rh_map` |
| hash table delete | done | `lib/hash_table, lib/rh_map` (backward shift) |
| hash tree | done | `lib/merkle_tree` |
| Hausdorff distance | done | `algo/geometry` |
| hB-tree | done | `lib/hb_tree` |
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
| hidden Markov model | done | `algo/hmm` |
| highest common factor | done | `algo/numeric` |
| histogram sort | done | `algo/sort_dist` |
| HMM | done | `algo/hmm` |
| homeomorphic | def |  |
| horizontal visibility map | todo | `algo/visibility` |
| Horner's rule | done | `algo/numerical` |
| Horspool | done | `algo/string_search` |
| hsadelta | def |  |
| Huffman coding | done | `algo/prefix_code` |
| huge sparse array | done | `lib/sparse_matrix` |
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
| independent set | done | `algo/graph_np` |
| index file | def |  |
| information theoretic bound | def |  |
| in-order traversal | done | `lib/avl, lib/rbt, lib/bst walks` |
| in-place sort | done | `algo/sort` |
| insertion sort | done | `algo/sort` |
| integer linear program | done | `algo/lp` |
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
| inverse suffix array | done | `lib/suffix_tree` |
| inversion list | done | `lib/inversion_list` |
| inverted file index | todo | `lib/inverted_index` |
| inverted index | todo | `lib/inverted_index` |
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
| jump list | done | `lib/jump_list` |
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
| k-clustering | done | `algo/facility` |
| k-coloring | done | `algo/graph_np` |
| k-connected graph | done | `algo/flow` |
| k-d-B-tree | done | `lib/kdb_tree` |
| k-dimensional | def |  |
| K-dominant match | done | `algo/string_approx` |
| k-d tree | done | `lib/kd_tree` |
| key | def |  |
| KMP | done | `algo/stringology` |
| KmpSkip Search | done | `algo/string_search` |
| knapsack problem | done | `algo/knapsack` |
| knight's tour | done | `algo/combinatorics` |
| Knuth-Morris-Pratt algorithm | done | `algo/stringology` |
| Königsberg bridges problem | done | `algo/graph_struct` |
| Kolmogorov complexity | def |  |
| Kraft's inequality | def |  |
| Kripke structure | todo | `algo/model_check` |
| Kruskal's algorithm | done | `algo/graph_path` |
| kth order Fibonacci numbers | done | `algo/number_theory` |
| kth shortest path | done | `algo/graph_path` |
| kth smallest element | done | `algo/sort` (nth_element) |
| k²-tree | done | `lib/k2_tree` |
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
| linear program | done | `algo/lp` |
| linear quadtree | done | `lib/quadtree` |
| linear search | done | `algo/search` (ds_find) |
| link | def |  |
| linked data signature | done | `lib/ld_signature` |
| linked list | done | `lib/list, lib/dlist` |
| list | done | `lib/list` |
| list contraction | done | `algo/parallel_sim` |
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
| lower triangular matrix | done | `algo/matrix` |
| lowest common ancestor | done | `algo/lca` |
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
| Markov chain | todo | `algo/markov` |
| Marlena | def |  |
| marriage problem | done | `algo/matching` |
| Master theorem | def |  |
| matched edge | def |  |
| matched vertex | def |  |
| matching | done | `algo/matching` |
| matrix | def |  |
| matrix-chain multiplication problem | done | `algo/knapsack` |
| matrix multiplication | done | `algo/matrix` |
| max-heap property | def |  |
| maximal independent set | done | `algo/graph_np` |
| maximally connected component | def |  |
| Maximal Shift | done | `algo/string_search` |
| maximum bipartite matching | done | `algo/matching` |
| maximum-flow problem | done | `algo/flow` |
| MBB | done | `algo/geometry` |
| Mealy machine | done | `lib/automaton` |
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
| minimax | done | `algo/game` |
| minimum bounding box | done | `algo/geometry` |
| minimum cut | done | `algo/flow` |
| minimum spanning tree | done | `algo/graph_view` (Prim), lib/graph |
| minimum vertex cut | done | `algo/flow` |
| Minkowski distance | done | `algo/geometry` |
| mixed integer linear program | done | `algo/lp` |
| mode | done | `algo/numerical` |
| model checking | todo | `algo/model_check` |
| model of computation | def |  |
| moderately exponential | def |  |
| MODIFIND | done | `algo/select` |
| monotone priority queue | done | `lib/radix_heap` |
| monotonically decreasing | def |  |
| monotonically increasing | def |  |
| Monte Carlo algorithm | def |  |
| Moore machine | done | `lib/automaton` |
| Morris-Pratt algorithm | done | `algo/string_search` |
| move | done | `algo/compress` |
| move-to-front heuristic | done | `lib/self_org_list` |
| move-to-root heuristic | done | `lib/self_org_list` |
| MST | done | `algo/graph_view, lib/graph` |
| multi-commodity flow | def |  |
| multigraph | def |  |
| multikey Quicksort | done | `algo/sort_dist` |
| multilayer grid file | done | `lib/grid_file` |
| multiplication method | done | `algo/hash_family` |
| multiprefix | done | `algo/scan` |
| multiprocessor model | def |  |
| multi-set | done | `lib/multiset` |
| multi suffix tree | done | `lib/suffix_tree` |
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
| NFA | done | `lib/automaton` |
| NFTA | todo | `lib/tree_automaton` |
| NIST | def |  |
| node | def |  |
| nonbalanced merge | done | `algo/external_sort` |
| nonbalanced merge sort | done | `algo/external_sort` |
| nondeterministic | def |  |
| nondeterministic algorithm | def |  |
| nondeterministic finite automaton | done | `lib/automaton` |
| nondeterministic finite state machine | def |  |
| nondeterministic finite tree automaton | todo | `lib/tree_automaton` |
| nondeterministic polynomial time | def |  |
| nondeterministic tree automaton | todo | `lib/tree_automaton` |
| nondeterministic Turing machine | todo | `lib/turing_machine` |
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
| OBDD | done | `lib/bdd` |
| objective function | def |  |
| oblivious algorithm | def |  |
| occurrence | def |  |
| octree | done | `lib/quadtree` |
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
| optimal mismatch | done | `algo/string_search` |
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
| ordered binary decision diagram | done | `lib/bdd` |
| ordered linked list | done | `lib/sorted_list` |
| ordered tree | def |  |
| order-preserving hash | done | `algo/hash_family` |
| order-preserving Huffman coding | done | `algo/prefix_code` |
| order-preserving minimal perfect hashing | done | `algo/perfect_hash` |
| oriented acyclic graph | def |  |
| oriented graph | def |  |
| oriented tree | def |  |
| orthogonal drawing | def |  |
| orthogonal lists | done | `lib/sparse_matrix` |
| orthogonally convex rectilinear polygon | done | `algo/geometry` |
| oscillating merge sort | done | `algo/external_sort` |
| out-degree | def |  |

### P

| entry | status | module |
|---|---|---|
| P | def |  |
| packing | def |  |
| padding argument | def |  |
| pagoda | done | `lib/pagoda` |
| pairing heap | done | `lib/pairing_heap` |
| PAM | done | `lib/quadtree` |
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
| path cover | done | `algo/graph_np` |
| path system problem | def |  |
| Patricia tree | done | `lib/radix_trie, lib/critbit_tree` |
| pattern | def |  |
| pattern element | def |  |
| P-complete | def |  |
| PCP | def |  |
| PDA | todo | `lib/pushdown` |
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
| PLOP-hashing | todo | `lib/plop_hash` |
| point access method | done | `lib/quadtree` |
| pointer jumping | done | `algo/parallel_sim` |
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
| property list | done | `lib/association_list` |
| prune and search | def |  |
| pseudo-random number generator | done | `algo/random` |
| PTAS | def |  |
| pth order Fibonacci numbers | done | `algo/number_theory` |
| P-tree | done | `lib/p_tree` |
| purely functional language | def |  |
| pushdown automaton | todo | `lib/pushdown` |
| pushdown transducer | todo | `lib/pushdown` |
| p-way merge sort | done | `algo/sort_misc` |

### Q

| entry | status | module |
|---|---|---|
| qm sort | done | `algo/sort_misc` |
| q sort | done | `algo/sort_misc` |
| quadratic probing | done | `lib/hash_table` |
| quadtree | done | `lib/quadtree` |
| quadtree complexity theorem | def |  |
| quad trie | done | `lib/quadtree` |
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
| ragged matrix | done | `lib/sparse_matrix` |
| Raita | done | `algo/string_search` |
| random access machine | def |  |
| randomization | def |  |
| randomized algorithm | def |  |
| randomized binary search tree | done | `lib/rbst` |
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
| RBST | done | `lib/rbst` |
| reachability | done | `algo/graph_view` (BFS) |
| reachable | done | `algo/graph_view` (BFS) |
| rebalance | done | `lib/avl, lib/rbt` |
| recognizer | def |  |
| rectangular matrix | def |  |
| rectilinear | def |  |
| rectilinear Steiner tree | done | `algo/steiner` |
| recurrence equations | def |  |
| recurrence relation | def |  |
| recursion | def |  |
| recursion termination | def |  |
| recursion tree | def |  |
| recursive | def |  |
| recursive data structure | def |  |
| recursive doubling | done | `algo/parallel_sim` |
| recursive language | def |  |
| recursively enumerable language | def |  |
| recursively solvable | def |  |
| red-black tree | done | `lib/rbt, lib/persistent_rbt` |
| reduced basis | done | `algo/lattice` |
| reduced digraph | done | `algo/graph_struct` |
| reduced ordered binary decision diagram | done | `lib/bdd` |
| reduction | def |  |
| reflexive | def |  |
| regular decomposition | done | `lib/quadtree` |
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
| Reverse Colussi | done | `algo/string_search` |
| Reverse Factor | done | `algo/string_search` |
| R-file | done | `lib/bang_file` |
| Rice's method | done | `algo/rice_integral` |
| right rotation | done | `lib/avl, lib/rbt` |
| right-threaded tree | done | `lib/threaded_tree` |
| RNG | done | `algo/random` |
| ROBDD | done | `lib/bdd` |
| Robin Hood hashing | done | `lib/rh_map` |
| root | def |  |
| root balance | done | `lib/wb_tree` |
| rooted tree | def |  |
| rotate left | done | `lib/avl, lib/rbt` |
| rotate right | done | `lib/avl, lib/rbt` |
| rotation | done | `lib/avl, lib/rbt, lib/treap` |
| rough graph | def |  |
| RP | def |  |
| R+-tree | done | `lib/rtree` |
| R*-tree | done | `lib/rtree` |
| R-tree | done | `lib/rtree` |
| run time | def |  |

### S

| entry | status | module |
|---|---|---|
| saguaro stack | done | `lib/cactus_stack` |
| SAM | def |  |
| saturated edge | def |  |
| SBB tree | done | `lib/rbt` (symmetric binary B-tree = red-black tree) |
| scan | done | `algo/scan` |
| scapegoat tree | done | `lib/scapegoat_tree` |
| scatter storage | def |  |
| Schorr-Waite graph marking algorithm | done | `algo/schorr_waite` |
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
| self-organizing list | done | `lib/self_org_list` |
| self-organizing sequential search | done | `lib/self_org_list` |
| semidefinite programming | def |  |
| separate chaining | done | `lib/hash_table` |
| separation theorem | def |  |
| sequential search | done | `algo/search` |
| set | done | `lib/bitset, lib/sparse_set, lib/string_map` (string set) |
| set cover | done | `algo/facility` |
| set packing | done | `algo/facility` |
| shadow heap | done | `lib/shadow_heap` |
| shadow merge | done | `lib/shadow_heap` |
| shadow merge insert | done | `lib/shadow_heap` |
| shaker sort | done | `algo/sort_classic` (cocktail) |
| shaker sort | done | `algo/sort_classic` (cocktail) |
| Shannon-Fano coding | done | `algo/prefix_code` |
| shared memory | def |  |
| Shell sort | done | `algo/sort` |
| Shift-Or | done | `algo/string_search` |
| Shor's algorithm | def |  |
| shortcutting | done | `algo/parallel_sim` |
| shortest common supersequence | done | `algo/string_approx` |
| shortest common superstring | done | `algo/string_approx` |
| shortest path | done | `algo/graph_path` |
| shortest spanning tree | done | `algo/graph_path` |
| shuffle | done | `algo/sequence` |
| shuffle sort | done | `algo/sort_dist` |
| sibling | def |  |
| sieve of Eratosthenes | done | `algo/numeric` |
| sift up | done | `lib/heap` |
| signature | def |  |
| Simon's algorithm | def |  |
| simple merge | done | `algo/sequence` |
| simple path | def |  |
| simple uniform hashing | def |  |
| simplex | done | `algo/lp` |
| simulated annealing | todo | `algo/anneal` |
| simulation theorem | def |  |
| single-destination shortest-path problem | done | `algo/graph_conn` |
| single-pair shortest-path problem | done | `algo/graph_path` |
| single program multiple data | def |  |
| single-source shortest-path problem | done | `algo/graph_path` |
| singly linked list | done | `lib/list` |
| singularity analysis | def |  |
| sink | def |  |
| sinking sort | done | `algo/sort_classic` |
| skd-tree | done | `lib/kdb_tree` |
| skew symmetry | def |  |
| skip list | done | `lib/skip_list` |
| skip search | done | `algo/string_search` |
| slope selection | done | `algo/geometry` |
| Smith algorithm | done | `algo/string_search` |
| Smith-Waterman algorithm | done | `algo/string_approx` |
| smoothsort | done | `algo/sort_heap` |
| solvable | def |  |
| sort | def |  |
| sorted array | done | `lib/sorted_list` |
| sorted list | done | `lib/sorted_list` |
| sorted-string table | todo | `lib/sstable` |
| sort in place | def |  |
| soundex | done | `algo/phonetic` |
| source | def |  |
| space-constructible function | def |  |
| space ordering method | todo | `algo/space_order` |
| spanning tree | done | `algo/graph_path` |
| sparse graph | def |  |
| sparse matrix | done | `lib/sparse_matrix` |
| sparsification | def |  |
| sparsity | def |  |
| spatial access method | done | `lib/rtree` |
| spiral storage | todo | `lib/spiral_storage` |
| splay tree | done | `lib/splay_tree` |
| SPMD | def |  |
| square matrix | def |  |
| square root | done | `algo/numerical` |
| SST | done | `algo/graph_path` |
| SSTable | todo | `lib/sstable` |
| stable | def |  |
| stack | done | `lib/stack` |
| stack tree | todo | `lib/binary_tree_repr` (extensions) |
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
| Steiner tree | done | `algo/steiner` |
| Steiner vertex | def |  |
| Steinhaus-Johnson-Trotter | done | `algo/combinatorics` |
| Stirling's approximation | done | `algo/numerical` |
| Stirling's formula | done | `algo/numerical` |
| stooge sort | done | `algo/sort_classic` |
| straight-line drawing | def |  |
| strand sort | done | `algo/sort_misc` |
| strictly decreasing | def |  |
| strictly increasing | def |  |
| strictly lower triangular matrix | done | `algo/matrix` |
| strictly upper triangular matrix | done | `algo/matrix` |
| string | done | `lib/str` |
| string editing problem | done | `algo/stringology` (edit distances) |
| string matching | done | `algo/string_search` |
| string matching on ordered alphabets | done | `algo/string_search` |
| string matching with errors | done | `algo/string_approx` |
| string matching with mismatches | done | `algo/string_search` |
| string searching | done | `algo/string_search` |
| strip packing | done | `algo/knapsack` |
| strongly connected component | done | `algo/graph_view` (Tarjan), lib/graph |
| strongly connected graph | def |  |
| strongly NP-hard | def |  |
| strsrch | done | `algo/string_search` (brute force) |
| stupid sort | done | `algo/sort_classic` (gnome sort), algo/sort_misc (bogosort) |
| subadditive ergodic theorem | def |  |
| subgraph | def |  |
| subgraph isomorphism | done | `algo/graph_np` |
| sublinear time algorithm | def |  |
| subsequence | def |  |
| subset | def |  |
| substring | def |  |
| subtree | def |  |
| suffix | def |  |
| suffix array | done | `algo/stringology` |
| suffix automaton | done | `lib/suffix_automaton` |
| suffix tree | done | `lib/suffix_tree` |
| superimposed code | done | `lib/signature_file` |
| superset | def |  |
| supersink | def |  |
| supersource | def |  |
| symmetric | def |  |
| symmetrically linked list | done | `lib/dlist` |
| symmetric binary B-tree | done | `lib/rbt` |
| symmetric set difference | done | `algo/sequence` (ds_set_symmetric_difference) |
| symmetric traversal | done | `lib/bst` (in-order traversal) |
| symmetry breaking | def |  |

### T

| entry | status | module |
|---|---|---|
| tabulation hashing | done | `algo/hash_family` |
| taco sort | done | `algo/sort_misc` |
| tail | def |  |
| tail recursion | def |  |
| target | def |  |
| temporal logic | def |  |
| terminal | def |  |
| terminal node | def |  |
| ternary search tree | done | `lib/tst` |
| text | done | `lib/str, lib/rope, lib/gap_buffer` |
| text searching | done | `algo/string_search` |
| theta | def |  |
| threaded binary tree | done | `lib/threaded_tree` |
| threaded tree | done | `lib/threaded_tree` |
| three-dimensional | def |  |
| three-way merge sort | done | `algo/sort_misc` |
| three-way radix quicksort | done | `algo/sort_dist` |
| time-constructible function | def |  |
| time/space complexity | def |  |
| top-down radix sort | done | `algo/sort_dist` |
| top-down tree automaton | todo | `lib/tree_automaton` |
| topological order | done | `algo/graph_view` |
| topological sort | done | `algo/graph_view, lib/graph` |
| topology tree | todo | `lib/topology_tree` |
| total function | def |  |
| totally decidable language | def |  |
| totally decidable problem | def |  |
| totally undecidable problem | def |  |
| total order | def |  |
| tour | def |  |
| tournament | done | `algo/kway_merge` |
| tournament sort | done | `algo/sort_heap` |
| towers of Hanoi | done | `algo/combinatorics` |
| tractable | def |  |
| transition | def |  |
| transition function | def |  |
| transitive | def |  |
| transitive closure | done | `algo/graph_struct` |
| transitive reduction | done | `algo/graph_struct` |
| transpose sequential search | done | `lib/self_org_list` |
| traveling salesman | done | `algo/tsp` |
| treap | done | `lib/treap` |
| tree | done | `lib/bst` |
| tree automaton | todo | `lib/tree_automaton` |
| tree contraction | done | `algo/parallel_sim` |
| tree editing problem | todo | `algo/tree_edit` |
| treesort (1) | done | `algo/sort_heap` (tree sort) |
| treesort (2) | done | `algo/sort_heap` (heapsort variants, tournament sort) |
| tree traversal | done | `lib/bst walks` |
| triangle inequality | def |  |
| triconnected graph | done | `algo/graph_conn` |
| trie | done | `lib/trie` |
| trinary function | def |  |
| tripartition | done | `algo/sort_misc` (Dutch national flag) |
| Trotter-Johnson | done | `algo/combinatorics` |
| TSP | done | `algo/tsp` |
| TST | done | `lib/tst` |
| Turbo-BM | done | `algo/string_search` |
| Turbo Reverse Factor | done | `algo/string_search` |
| Turing machine | todo | `lib/turing_machine` |
| Turing reduction | def |  |
| Turing transducer | todo | `lib/turing_machine` |
| twin grid file | done | `lib/grid_file` |
| twisted tabulation hashing | done | `algo/hash_family` |

### Symbols and digits

| entry | status | module |
|---|---|---|
| 2-choice hashing | done | `lib/two_choice_map` |

### T

| entry | status | module |
|---|---|---|
| two-dimensional | def |  |

### Symbols and digits

| entry | status | module |
|---|---|---|
| 2-left hashing | done | `lib/two_choice_map` |

### T

| entry | status | module |
|---|---|---|
| two-level grid file | done | `lib/grid_file` |

### Symbols and digits

| entry | status | module |
|---|---|---|
| 2-3-4 tree | done | `lib/mway_btree` |
| 2-3 tree | done | `lib/mway_btree` |

### T

| entry | status | module |
|---|---|---|
| Two Way algorithm | done | `algo/string_search` |
| two-way linked list | done | `lib/dlist` |
| two-way merge sort | done | `algo/sort_misc` |

### U

| entry | status | module |
|---|---|---|
| UB-tree | done | `lib/ub_tree` |
| UKP | done | `algo/knapsack` |
| unary function | def |  |
| unbiased coin flipping algorithm | done | `algo/random` |
| unbounded knapsack problem | done | `algo/knapsack` |
| uncomputable function | def |  |
| uncomputable problem | def |  |
| undecidable language | def |  |
| undecidable problem | def |  |
| undirected graph | def |  |
| uniform circuit complexity | def |  |
| uniform circuit family | def |  |
| uniform hashing | def |  |
| uniform matrix | def |  |
| union | done | `algo/sequence` (ds_set_union) |
| union of automata | done | `lib/automaton` |
| universal B-tree | done | `lib/ub_tree` |
| universal hashing | done | `algo/hash_family` |
| universal state | def |  |
| universal Turing machine | def |  |
| universe | def |  |
| unlimited branching tree | done | `lib/ub_tree` |
| unranking | done | `algo/combinatorics` |
| UnShuffle sort | done | `algo/sort_dist` |
| unsolvable problem | def |  |
| unsorted list | done | `lib/sorted_list` |
| upper triangular matrix | done | `algo/matrix` |

### V

| entry | status | module |
|---|---|---|
| van Emde-Boas priority queue | done | `lib/veb_tree` |
| vehicle routing problem | done | `algo/tsp` |
| Veitch diagram | def |  |
| Venn diagram | def |  |
| vertex | def |  |
| vertex coloring | done | `algo/graph_np` |
| vertex connectivity | done | `algo/flow` |
| vertex cover | done | `algo/graph_np` |
| vertical visibility map | todo | `algo/visibility` |
| virtual hashing | done | `lib/dynamic_hash` |
| visibility map | todo | `algo/visibility` |
| visible | def |  |
| Viterbi algorithm | done | `algo/hmm` |
| Vitter's algorithm | done | `algo/adaptive_huffman` |
| VRP | done | `algo/tsp` |

### W

| entry | status | module |
|---|---|---|
| walk | def |  |
| WCET | def |  |
| weak-heap | done | `lib/weak_heap` |
| weak-heap sort | done | `lib/weak_heap` |
| weight-balanced tree | done | `lib/wb_tree` |
| weighted, directed graph | def |  |
| weighted graph | def |  |
| window | def |  |
| witness | def |  |
| work | def |  |
| work-depth model | def |  |
| work-efficient | def |  |
| work-preserving | def |  |
| worst case | def |  |
| worst-case cost | def |  |
| worst-case execution time | def |  |
| worst-case minimum access | def |  |

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
| 0-ary function | def |  |
| 0-based indexing | def |  |
| 0-1 knapsack problem | done | `algo/knapsack` |

### Z

| entry | status | module |
|---|---|---|
| Zhu-Takaoka | done | `algo/string_search` |
| Zipfian distribution | def |  |
| Zipf's law | def |  |
| zipper | todo | `lib/zipper` |
| ZPP | def |  |
