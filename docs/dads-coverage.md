# NIST DADS coverage

This file maps every entry of the NIST *Dictionary of Algorithms and Data
Structures* (DADS, https://xlinux.nist.gov/dads/) to `ds`. DADS is a
dictionary of algorithms, structures, problems, techniques and definitions,
with aliases as separate rows, so its entry count is a provenance list,
not a count of independent capabilities: read the two dimensions below
rather than a single "done" number.

**Kind** (what the entry is): `structure`, `algorithm`, `problem` (solved
by the listed module), `alias` (another name for an entry), `concept`
(definitions, models, properties, techniques, families).

**State** (what `ds` provides):

- `implemented`: a dedicated implementation; the module column says where.
- `restricted`: implemented with stated limits (see the note).
- `variant`: covered by a preset, a mode or a closely related implementation
  (the note says which), not a separate algorithm.
- `reference`: a reference implementation, e.g. a sequential simulation
  of a parallel algorithm, useful for study rather than production.
- `missing`: implementable but not provided.
- `n/a`: nothing to implement (definitions and other concepts).

The tail of the index (after "shaker sort"), which the A-Z page cuts
off, was checked against DADS's second-level index pages (t42 to t53).
Topics that are not DADS index entries but belong next to them are listed
under "Supplemental entries" with the same columns.

## Summary

| state | entries |
|---|---|
| implemented | 748 |
| restricted | 3 |
| variant | 1 |
| reference | 5 |
| missing | 2 |
| n/a | 547 |
| total | 1306 |

Every indexed term has a classification; implementation scope and known
limitations are recorded per row. The counts are table classifications,
not independently verified capability totals: an `implemented` row means
a dedicated implementation exists, not that every variant or operation
the term covers is provided.

| kind | entries |
|---|---|
| structure | 276 |
| algorithm | 442 |
| problem | 30 |
| alias | 10 |
| concept | 548 |

## Missing

| entry | note |
|---|---|
| alternating Turing machine | a simulator is implementable; out of the current scope |
| semidefinite programming | a solver is implementable; out of the current scope |

## Index A-Z

### Symbols and digits

| entry | kind | state | module | note |
|---|---|---|---|---|
| α | concept | n/a |  |  |
| ω | concept | n/a |  |  |
| Ω | concept | n/a |  |  |
| ρ-approximation algorithm | concept | n/a |  |  |
| ∼ | concept | n/a |  |  |
| Θ | concept | n/a |  |  |
| (a,b)-tree | structure | restricted | lib/btree | the (t, 2t) family only, not every allowed (a, b) pair; minimum degree t gives a (t, 2t)-tree |
| 8 queens | algorithm | implemented | algo/combinatorics |  |
| 1-based indexing | concept | n/a |  |  |
| 2-choice hashing | structure | implemented | lib/two_choice_map |  |
| 2-left hashing | structure | implemented | lib/two_choice_map |  |
| 2-3-4 tree | structure | implemented | lib/mway_btree |  |
| 2-3 tree | structure | implemented | lib/mway_btree |  |
| 0-ary function | concept | n/a |  |  |
| 0-based indexing | concept | n/a |  |  |
| 0-1 knapsack problem | problem | implemented | algo/knapsack |  |

### A

| entry | kind | state | module | note |
|---|---|---|---|---|
| absolute performance guarantee | concept | n/a |  |  |
| abstract data type | concept | n/a |  |  |
| accepting state | concept | n/a |  |  |
| Ackermann's function | algorithm | implemented | algo/number_theory |  |
| active data structure | concept | n/a |  |  |
| acyclic directed graph | concept | n/a |  |  |
| acyclic graph | concept | n/a |  |  |
| adaptive heap sort | algorithm | implemented | algo/sort_heap |  |
| adaptive Huffman coding | algorithm | implemented | algo/adaptive_huffman |  |
| adaptive k-d tree | structure | implemented | lib/kdb_tree |  |
| adaptive sort | algorithm | implemented | algo/sort_misc |  |
| address-calculation sort | algorithm | implemented | algo/sort_dist |  |
| adjacency-list representation | structure | implemented | lib/graph, algo/graph_view | CSR |
| adjacency-matrix representation | concept | n/a |  |  |
| adjacent | concept | n/a |  |  |
| admissible vertex | concept | n/a |  |  |
| ADT | concept | n/a |  |  |
| adversary | concept | n/a |  |  |
| Aho-Corasick | structure | implemented | lib/aho_corasick |  |
| algorithm | concept | n/a |  |  |
| algorithm BSTW | algorithm | implemented | algo/compress |  |
| algorithm FGK | algorithm | implemented | algo/adaptive_huffman |  |
| algorithmically solvable | concept | n/a |  |  |
| all pairs shortest path | algorithm | implemented | algo/graph_view | Floyd-Warshall |
| all simple paths | algorithm | implemented | algo/graph_path |  |
| alphabet | concept | n/a |  |  |
| Alpha Skip Search algorithm | algorithm | implemented | algo/string_search |  |
| alternating path | algorithm | implemented | algo/matching |  |
| alternating Turing machine | concept | missing |  | a simulator is implementable; out of the current scope |
| alternation | concept | n/a |  |  |
| American flag sort | algorithm | implemented | algo/sort_dist |  |
| amortized cost | concept | n/a |  |  |
| ancestor | concept | n/a |  |  |
| and | concept | n/a |  |  |
| ANSI | concept | n/a |  |  |
| antichain | concept | n/a |  |  |
| antisymmetric | concept | n/a |  |  |
| Apostolico-Crochemore | algorithm | implemented | algo/string_search |  |
| Apostolico-Giancarlo algorithm | algorithm | implemented | algo/string_search |  |
| approximate string matching | algorithm | implemented | algo/string_approx |  |
| approximation algorithm | concept | n/a |  |  |
| arborescence | algorithm | implemented | algo/graph_path |  |
| arc | concept | n/a |  |  |
| arithmetic coding | algorithm | implemented | algo/compress |  |
| array | structure | implemented | lib/vec |  |
| array index | concept | n/a |  |  |
| array merging | algorithm | implemented | algo/sequence | ds_merge |
| array search | algorithm | implemented | algo/search |  |
| articulation point | algorithm | implemented | algo/graph_struct |  |
| assignment problem | problem | implemented | algo/matching |  |
| association list | structure | implemented | lib/association_list |  |
| associative | concept | n/a |  |  |
| associative array | structure | implemented | lib/hash_table, lib/rh_map, lib/string_map |  |
| asymptotically tight bound | concept | n/a |  |  |
| asymptotic bound | concept | n/a |  |  |
| asymptotic space complexity | concept | n/a |  |  |
| asymptotic time complexity | concept | n/a |  |  |
| asymptotic upper bound | concept | n/a |  |  |
| augmenting path | algorithm | implemented | algo/flow |  |
| automaton | concept | n/a |  |  |
| average case | concept | n/a |  |  |
| average-case cost | concept | n/a |  |  |
| AVL tree | structure | implemented | lib/avl |  |
| axiomatic semantics | concept | n/a |  |  |

### B

| entry | kind | state | module | note |
|---|---|---|---|---|
| backtracking | concept | n/a |  |  |
| bag | structure | implemented | lib/multiset |  |
| balance | concept | n/a |  |  |
| balanced binary search tree | structure | implemented | lib/avl, lib/rbt, lib/treap |  |
| balanced binary tree | structure | implemented | lib/avl, lib/rbt |  |
| balanced k-way merge sort | algorithm | implemented | algo/external_sort |  |
| balanced merge sort | algorithm | implemented | algo/external_sort |  |
| balanced multiway merge | algorithm | implemented | algo/external_sort |  |
| balanced multiway tree | structure | implemented | lib/btree |  |
| balanced quicksort | algorithm | implemented | algo/sort_misc |  |
| balanced tree | structure | implemented | lib/avl, lib/rbt, lib/btree |  |
| balanced two-way merge sort | algorithm | implemented | algo/external_sort |  |
| BANG file | structure | implemented | lib/bang_file |  |
| Batcher sort | algorithm | implemented | algo/sort_classic | bitonic |
| Baum Welch algorithm | algorithm | implemented | algo/hmm |  |
| BB(α) tree | structure | implemented | lib/wb_tree |  |
| BBP algorithm | algorithm | implemented | algo/numerical |  |
| BDD | structure | implemented | lib/bdd |  |
| BD-tree | structure | implemented | lib/hb_tree |  |
| Bellman-Ford algorithm | algorithm | implemented | algo/graph_view, lib/graph |  |
| Benford's law | concept | n/a |  |  |
| best case | concept | n/a |  |  |
| best-case cost | concept | n/a |  |  |
| best-first search | algorithm | implemented | algo/graph_path |  |
| biconnected component | algorithm | implemented | algo/graph_struct |  |
| biconnected graph | algorithm | implemented | algo/graph_struct |  |
| bidirectional bubble sort | algorithm | implemented | algo/sort_classic | cocktail |
| big-O notation | concept | n/a |  |  |
| binary function | concept | n/a |  |  |
| binary GCD | algorithm | implemented | algo/numeric | ds_binary_gcd |
| binary heap | structure | implemented | lib/heap |  |
| binary insertion sort | algorithm | implemented | algo/sort_classic |  |
| binary knapsack problem | problem | implemented | algo/knapsack |  |
| binary priority queue | structure | implemented | lib/heap |  |
| binary relation | concept | n/a |  |  |
| binary search | algorithm | implemented | algo/search |  |
| binary search tree | structure | implemented | lib/bst |  |
| binary tree | structure | implemented | lib/bst |  |
| binary tree representation of trees | structure | implemented | lib/binary_tree_repr |  |
| bingo sort | algorithm | implemented | algo/sort_misc |  |
| binomial heap | structure | implemented | lib/binomial_heap |  |
| binomial tree | structure | implemented | lib/binomial_heap |  |
| bin packing problem | problem | implemented | algo/knapsack |  |
| bin sort | algorithm | implemented | algo/sort_dist |  |
| bintree | structure | implemented | lib/quadtree |  |
| bipartite graph | algorithm | implemented | algo/graph_view | ds_gview_bipartite |
| bipartite matching | algorithm | implemented | algo/matching |  |
| bisector | algorithm | implemented | algo/geometry |  |
| bitonic sort | algorithm | implemented | algo/sort_classic |  |
| bit vector | structure | implemented | lib/bitset, lib/bitvector |  |
| Bk tree | structure | implemented | lib/binomial_heap |  |
| blind sort | algorithm | implemented | algo/sort_dist |  |
| blind trie | structure | implemented | lib/elastic_trie |  |
| block | concept | n/a |  |  |
| block addressing index | structure | implemented | lib/inverted_index |  |
| blocking flow | algorithm | implemented | algo/flow |  |
| block search | algorithm | implemented | algo/search |  |
| Bloom filter | structure | implemented | lib/bloom_filter |  |
| blossom | algorithm | implemented | algo/matching |  |
| bogosort | algorithm | implemented | algo/sort_misc |  |
| Bond Sequential Search | algorithm | implemented | algo/search |  |
| boolean | concept | n/a |  |  |
| boolean expression | concept | n/a |  |  |
| boolean function | concept | n/a |  |  |
| border | concept | n/a |  |  |
| Boruvka's algorithm | algorithm | implemented | algo/graph_path |  |
| bottleneck traveling salesman | algorithm | implemented | algo/tsp |  |
| boundary-based representation | concept | n/a |  |  |
| bounded error probability in polynomial time | concept | n/a |  |  |
| bounded queue | structure | implemented | lib/circular_buffer, lib/array_deque | fixed buffer |
| bounded stack | structure | implemented | lib/vec | fixed buffer |
| Boyer-Moore | algorithm | implemented | algo/string_search |  |
| Boyer-Moore-Horspool | algorithm | implemented | algo/string_search |  |
| bozo sort | algorithm | implemented | algo/sort_misc |  |
| B+-tree | structure | implemented | lib/bplus_tree |  |
| BPP | concept | n/a |  |  |
| Bradford's law | concept | n/a |  |  |
| branch and bound | concept | n/a |  |  |
| breadth-first search | algorithm | implemented | algo/graph_view, lib/graph |  |
| Bresenham's algorithm | algorithm | implemented | algo/geometry |  |
| bridge | algorithm | implemented | algo/graph_struct |  |
| British Museum technique | concept | n/a |  |  |
| brute force | concept | n/a |  |  |
| brute force string search | algorithm | implemented | algo/string_search |  |
| brute force string search with mismatches | algorithm | implemented | algo/string_search |  |
| BSP-tree | structure | implemented | lib/bsp_tree |  |
| B*-tree | structure | implemented | lib/bplus_tree | B* split policy |
| B-tree | structure | implemented | lib/btree |  |
| bubble sort | algorithm | implemented | algo/sort_classic |  |
| bucket | concept | n/a |  |  |
| bucket array | concept | n/a |  |  |
| bucketing method | concept | n/a |  |  |
| bucket sort | algorithm | implemented | algo/sort_dist |  |
| bucket trie | structure | implemented | lib/elastic_trie |  |
| buddy system | structure | implemented | lib/buddy_allocator |  |
| buddy tree | structure | implemented | lib/hb_tree |  |
| build-heap | structure | implemented | lib/heap, algo/sort | heap sort |
| Burrows-Wheeler transform | structure | implemented | lib/suffix_tree |  |
| busy beaver | structure | implemented | lib/turing_machine |  |
| BV-tree | structure | implemented | lib/hb_tree |  |
| BWT | structure | implemented | lib/suffix_tree |  |
| Byzantine generals | concept | n/a |  |  |

### C

| entry | kind | state | module | note |
|---|---|---|---|---|
| cactus stack | structure | implemented | lib/cactus_stack |  |
| Calculus of Communicating Systems | concept | n/a |  |  |
| calendar queue | structure | implemented | lib/calendar_queue |  |
| candidate consistency testing | concept | n/a |  |  |
| candidate verification | concept | n/a |  |  |
| canonical complexity class | concept | n/a |  |  |
| capacitated facility location | algorithm | implemented | algo/facility |  |
| capacity | concept | n/a |  |  |
| capacity constraint | concept | n/a |  |  |
| cartesian tree | algorithm | implemented | algo/cartesian_tree |  |
| Caverphone | algorithm | implemented | algo/phonetic |  |
| CCS | concept | n/a |  |  |
| cell probe model | concept | n/a |  |  |
| cell tree | structure | implemented | lib/bsp_tree |  |
| cellular automaton | structure | implemented | lib/cellular_automaton |  |
| centroid | algorithm | implemented | algo/graph_struct |  |
| certificate | concept | n/a |  |  |
| chain | concept | n/a |  |  |
| chaining | structure | implemented | lib/hash_table | chaining mode |
| child | concept | n/a |  |  |
| Chinese postman problem | problem | implemented | algo/graph_path |  |
| Chinese remainder theorem | algorithm | implemented | algo/number_theory |  |
| Christofides algorithm | algorithm | implemented | algo/tsp |  |
| chromatic index | algorithm | implemented | algo/graph_np |  |
| chromatic number | algorithm | implemented | algo/graph_np |  |
| circuit | concept | n/a |  |  |
| circuit complexity | concept | n/a |  |  |
| circuit value problem | concept | n/a |  |  |
| circular list | structure | implemented | lib/ring_list |  |
| circular queue | structure | implemented | lib/circular_buffer |  |
| clique | algorithm | implemented | algo/graph_np |  |
| clique problem | problem | implemented | algo/graph_np |  |
| clustering | concept | n/a |  |  |
| clustering free | concept | n/a |  |  |
| coalesced chaining | structure | implemented | lib/coalesced_map |  |
| coarsening | concept | n/a |  |  |
| cocktail shaker sort | algorithm | implemented | algo/sort_classic |  |
| codeword | concept | n/a |  |  |
| coding tree | algorithm | implemented | algo/prefix_code |  |
| Collatz problem | problem | implemented | algo/combinatorics |  |
| collective recursion | concept | n/a |  |  |
| collision | concept | n/a |  |  |
| collision resolution scheme | structure | implemented | lib/hash_table, lib/rh_map |  |
| Colussi | algorithm | implemented | algo/string_search |  |
| combination | algorithm | implemented | algo/combinatorics |  |
| comb sort | algorithm | implemented | algo/sort_classic |  |
| Commentz-Walter | algorithm | implemented | algo/string_search |  |
| Communicating Sequential Processes | concept | n/a |  |  |
| commutative | concept | n/a |  |  |
| compact data structure | concept | n/a |  |  |
| compact DAWG | structure | implemented | lib/suffix_automaton |  |
| compact trie | structure | implemented | lib/elastic_trie |  |
| comparison sort | algorithm | implemented | algo/sort, algo/sort_classic |  |
| competitive analysis | concept | n/a |  |  |
| competitive ratio | concept | n/a |  |  |
| complement | concept | n/a |  |  |
| complete binary tree | concept | n/a |  |  |
| complete graph | concept | n/a |  |  |
| completely connected graph | concept | n/a |  |  |
| complete tree | concept | n/a |  |  |
| complexity | concept | n/a |  |  |
| complexity class | concept | n/a |  |  |
| compound algorithm | concept | n/a |  |  |
| computable | concept | n/a |  |  |
| concave function | concept | n/a |  |  |
| concurrent data structure | concept | n/a |  |  |
| concurrent flow | concept | n/a |  |  |
| concurrent read, concurrent write | concept | n/a |  |  |
| concurrent read, exclusive write | concept | n/a |  |  |
| confluently persistent data structure | structure | implemented | lib/confluent_list |  |
| conjunction | concept | n/a |  |  |
| connected components | structure | implemented | lib/graph | weak components |
| connected graph | concept | n/a |  |  |
| constant function | concept | n/a |  |  |
| continuous knapsack problem | problem | implemented | algo/knapsack |  |
| Cook reduction | concept | n/a |  |  |
| Cook's theorem | concept | n/a |  |  |
| CORDIC | algorithm | implemented | algo/numerical |  |
| counting sort | algorithm | implemented | algo/sort_classic | ds_counting_sort_keyed |
| covering | concept | n/a |  |  |
| CRC | algorithm | implemented | algo/hash | CRC-32, CRC-32C |
| CRCW | concept | n/a |  |  |
| CREW | concept | n/a |  |  |
| critical path problem | problem | implemented | algo/graph_path |  |
| CSP | concept | n/a |  |  |
| CTL | algorithm | implemented | algo/model_check |  |
| cube root | algorithm | implemented | algo/numerical |  |
| cuckoo hashing | structure | implemented | lib/cuckoo_map |  |
| Cupif-Giannini tree traversal | structure | implemented | lib/threaded_tree |  |
| cut | concept | n/a |  |  |
| cutting plane | algorithm | implemented | algo/lp |  |
| cutting stock problem | problem | implemented | algo/knapsack |  |
| cutting theorem | concept | n/a |  |  |
| cut vertex | algorithm | implemented | algo/graph_struct |  |
| cycle | concept | n/a |  |  |
| cyclic redundancy check | algorithm | implemented | algo/hash |  |

### D

| entry | kind | state | module | note |
|---|---|---|---|---|
| D-adjacent | concept | n/a |  |  |
| DAG | concept | n/a |  |  |
| DAG shortest paths | algorithm | implemented | algo/graph_path |  |
| data structure | concept | n/a |  |  |
| DAWG | structure | implemented | lib/suffix_automaton |  |
| decidable language | concept | n/a |  |  |
| decidable problem | concept | n/a |  |  |
| decimation | concept | n/a |  |  |
| decision problem | concept | n/a |  |  |
| decomposable searching problem | concept | n/a |  |  |
| degree | concept | n/a |  |  |
| dense graph | concept | n/a |  |  |
| depoissonization | concept | n/a |  |  |
| depth | concept | n/a |  |  |
| depth-first search | structure | implemented | lib/graph |  |
| deque | structure | implemented | lib/deque, lib/array_deque |  |
| derangement | algorithm | implemented | algo/combinatorics |  |
| descendant | concept | n/a |  |  |
| deterministic | concept | n/a |  |  |
| deterministic algorithm | concept | n/a |  |  |
| deterministic finite automata string search | algorithm | implemented | algo/string_search |  |
| deterministic finite automaton | structure | implemented | lib/automaton |  |
| deterministic finite state machine | concept | n/a |  |  |
| deterministic finite tree automaton | structure | implemented | lib/tree_automaton |  |
| deterministic pushdown automaton | structure | implemented | lib/pushdown |  |
| deterministic random bit generator | algorithm | implemented | algo/random | xoshiro, not cryptographic |
| deterministic tree automaton | structure | implemented | lib/tree_automaton |  |
| Deutsch-Jozsa algorithm | concept | n/a |  |  |
| DFA | structure | implemented | lib/automaton |  |
| DFS | structure | implemented | lib/graph |  |
| DFS forest | algorithm | implemented | algo/graph_struct |  |
| DFTA | alias | implemented | lib/tree_automaton | alias of deterministic finite tree automaton |
| diagonalization | concept | n/a |  |  |
| diameter | algorithm | implemented | algo/graph_struct |  |
| Dianna | concept | n/a |  |  |
| dichotomic search | algorithm | implemented | algo/search |  |
| dictionary | structure | implemented | lib/hash_table, lib/rh_map, lib/avl, ... |  |
| diet | structure | implemented | lib/diet |  |
| difference | algorithm | implemented | algo/sequence | ds_set_difference |
| digital search tree | structure | implemented | lib/trie |  |
| digital tree | structure | implemented | lib/trie |  |
| digraph | concept | n/a |  |  |
| Dijkstra's algorithm | algorithm | implemented | algo/graph_view, lib/graph |  |
| diminishing increment sort | algorithm | implemented | algo/sort | Shell sort |
| dining philosophers | concept | n/a |  |  |
| direct chaining | structure | implemented | lib/hash_table |  |
| directed acyclic graph | concept | n/a |  |  |
| directed acyclic word graph | structure | implemented | lib/suffix_automaton |  |
| directed graph | structure | implemented | lib/graph |  |
| discrete interval encoding tree | structure | implemented | lib/diet |  |
| discrete p-center | algorithm | implemented | algo/facility |  |
| disjoint set | structure | implemented | lib/disjoint_set, lib/rollback_union_find |  |
| disjunction | concept | n/a |  |  |
| distributional complexity | concept | n/a |  |  |
| distribution sort | algorithm | implemented | algo/sort_dist |  |
| distributive partitioning sort | algorithm | implemented | algo/sort_dist |  |
| divide and conquer | concept | n/a |  |  |
| divide and marriage before conquest | concept | n/a |  |  |
| domain | concept | n/a |  |  |
| dominance tree sort | algorithm | implemented | algo/sort_heap |  |
| don't care | concept | n/a |  |  |
| Doomsday rule | algorithm | implemented | algo/number_theory |  |
| double-direction bubble sort | algorithm | implemented | algo/sort_classic | cocktail |
| double-ended priority queue | structure | implemented | lib/min_max_heap |  |
| double hashing | structure | implemented | lib/open_map |  |
| double left rotation | structure | implemented | lib/avl |  |
| double metaphone | algorithm | implemented | algo/phonetic |  |
| double right rotation | structure | implemented | lib/avl |  |
| doubly-chained tree | structure | implemented | lib/binary_tree_repr |  |
| doubly-ended queue | structure | implemented | lib/deque |  |
| doubly linked list | structure | implemented | lib/dlist |  |
| DPDA | alias | implemented | lib/pushdown | alias of deterministic pushdown automaton |
| DRBG | algorithm | implemented | algo/random |  |
| D-tree | structure | implemented | lib/d_tree |  |
| dual | concept | n/a |  |  |
| dual linear program | algorithm | implemented | algo/lp |  |
| dual-pivot quicksort | algorithm | implemented | algo/sort_misc |  |
| Dutch national flag | algorithm | implemented | algo/sort_misc |  |
| dyadic tree | concept | n/a |  |  |
| dynamic | concept | n/a |  |  |
| dynamic array | structure | implemented | lib/vec |  |
| dynamic hashing | structure | implemented | lib/linear_hash_map |  |
| dynamic Huffman coding | algorithm | implemented | algo/adaptive_huffman |  |
| dynamic programming | concept | n/a |  |  |
| dynamization transformation | algorithm | implemented | algo/dynamization |  |

### E

| entry | kind | state | module | note |
|---|---|---|---|---|
| easy split, hard merge | concept | n/a |  |  |
| edge | concept | n/a |  |  |
| edge coloring | algorithm | implemented | algo/graph_np |  |
| edge connectivity | algorithm | implemented | algo/flow |  |
| edge crossing | concept | n/a |  |  |
| edge-weighted graph | concept | n/a |  |  |
| edit distance | algorithm | implemented | algo/stringology | Levenshtein, OSA |
| edit operation | algorithm | implemented | algo/string_approx |  |
| edit script | algorithm | implemented | algo/string_approx |  |
| efficiency | concept | n/a |  |  |
| elastic-bucket trie | structure | implemented | lib/elastic_trie |  |
| element uniqueness | algorithm | implemented | algo/sequence | ds_unique after sort |
| end-of-string | concept | n/a |  |  |
| enfilade | structure | implemented | lib/enfilade |  |
| ERCW | concept | n/a |  |  |
| EREW | concept | n/a |  |  |
| Euclidean algorithm | algorithm | implemented | algo/numeric | ds_gcd |
| Euclidean distance | algorithm | implemented | algo/geometry |  |
| Euclidean Steiner tree | algorithm | implemented | algo/steiner |  |
| Euclidean traveling salesman problem | problem | implemented | algo/tsp |  |
| Euclid's algorithm | algorithm | implemented | algo/numeric |  |
| Euler cycle | algorithm | implemented | algo/graph_struct |  |
| Eulerian graph | algorithm | implemented | algo/graph_struct |  |
| Eulerian path | algorithm | implemented | algo/graph_struct |  |
| Euler's formula | concept | n/a |  |  |
| exact string matching | algorithm | implemented | algo/string_search |  |
| EXCELL | structure | implemented | lib/bang_file |  |
| exchange sort | algorithm | implemented | algo/sort_classic | bubble |
| exclusive or | concept | n/a |  |  |
| exclusive read, concurrent write | concept | n/a |  |  |
| exclusive read, exclusive write | concept | n/a |  |  |
| exhaustive search | concept | n/a |  |  |
| existential state | concept | n/a |  |  |
| expandable hashing | structure | implemented | lib/dynamic_hash |  |
| expander graph | concept | n/a |  |  |
| exponential | concept | n/a |  |  |
| extended binary tree | concept | n/a |  |  |
| extended Euclid's algorithm | algorithm | implemented | algo/numeric | ds_ext_gcd |
| extended k-d tree | structure | implemented | lib/kdb_tree |  |
| extendible hashing | structure | implemented | lib/extendible_hash |  |
| external chaining | structure | implemented | lib/hash_table |  |
| external index | concept | n/a |  |  |
| external memory algorithm | concept | n/a |  |  |
| external memory data structure | concept | n/a |  |  |
| external merge | algorithm | implemented | algo/external_sort |  |
| external node | concept | n/a |  |  |
| external quicksort | algorithm | implemented | algo/external_sort |  |
| external radix sort | algorithm | implemented | algo/external_sort |  |
| external sort | algorithm | implemented | algo/external_sort |  |
| extrapolation search | algorithm | implemented | algo/search | interpolation |
| extremal | concept | n/a |  |  |
| extreme point | algorithm | implemented | algo/geometry |  |

### F

| entry | kind | state | module | note |
|---|---|---|---|---|
| facility location | algorithm | implemented | algo/facility |  |
| factor | concept | n/a |  |  |
| factorial | algorithm | implemented | algo/combinatorics |  |
| fast fourier transform | algorithm | implemented | algo/fft |  |
| fathoming | concept | n/a |  |  |
| feasible region | concept | n/a |  |  |
| feasible solution | concept | n/a |  |  |
| feedback edge set | algorithm | implemented | algo/graph_struct |  |
| feedback vertex set | algorithm | implemented | algo/graph_np |  |
| Ferguson-Forcade algorithm | algorithm | implemented | algo/lattice |  |
| FFT | algorithm | implemented | algo/fft |  |
| Fibonaccian search | algorithm | implemented | algo/search |  |
| Fibonacci heap | structure | implemented | lib/fibonacci_heap |  |
| Fibonacci number | algorithm | implemented | algo/number_theory |  |
| Fibonacci tree | concept | n/a |  |  |
| FIFO | structure | implemented | lib/queue |  |
| filial-heir chain | structure | implemented | lib/binary_tree_repr |  |
| Find | algorithm | implemented | algo/select |  |
| find kth least element | algorithm | implemented | algo/sort | nth_element |
| finitary tree | concept | n/a |  |  |
| finite Fourier transform | algorithm | implemented | algo/fft |  |
| finite state automaton | structure | implemented | lib/automaton |  |
| finite state machine | structure | implemented | lib/automaton |  |
| finite state machine minimization | structure | implemented | lib/automaton |  |
| finite state transducer | structure | implemented | lib/automaton |  |
| first child-next sibling binary tree | structure | implemented | lib/binary_tree_repr |  |
| first come, first served | concept | n/a |  |  |
| first-in, first-out | structure | implemented | lib/queue |  |
| Fisher-Yates shuffle | algorithm | implemented | algo/sequence | ds_shuffle |
| fixed-grid method | structure | implemented | lib/grid_file |  |
| flash sort | algorithm | implemented | algo/sort_dist |  |
| flow | concept | n/a |  |  |
| flow conservation | concept | n/a |  |  |
| flow function | concept | n/a |  |  |
| flow network | algorithm | implemented | algo/flow |  |
| Floyd-Warshall algorithm | algorithm | implemented | algo/graph_view |  |
| Ford-Bellman | algorithm | implemented | algo/graph_view |  |
| Ford-Fulkerson method | algorithm | implemented | algo/flow |  |
| forest | concept | n/a |  |  |
| forest editing problem | problem | implemented | algo/tree_edit |  |
| formal language | concept | n/a |  |  |
| formal methods | concept | n/a |  |  |
| formal verification | concept | n/a |  |  |
| forward index | structure | implemented | lib/inverted_index |  |
| fractional knapsack problem | problem | implemented | algo/knapsack |  |
| fractional solution | concept | n/a |  |  |
| free edge | concept | n/a |  |  |
| free tree | concept | n/a |  |  |
| free vertex | concept | n/a |  |  |
| frequency count heuristic | structure | implemented | lib/self_org_list |  |
| full array | concept | n/a |  |  |
| full binary tree | concept | n/a |  |  |
| full inverted index | structure | implemented | lib/inverted_index |  |
| fully dynamic graph problem | concept | n/a |  |  |
| fully persistent data structure | structure | implemented | lib/history | branching timelines |
| fully polynomial approximation scheme | concept | n/a |  |  |
| function | concept | n/a |  |  |
| functional data structure | concept | n/a |  |  |

### G

| entry | kind | state | module | note |
|---|---|---|---|---|
| Galil-Giancarlo | algorithm | implemented | algo/string_search |  |
| Galil-Seiferas | algorithm | implemented | algo/string_search |  |
| gamma function | algorithm | implemented | algo/numerical |  |
| GBD-tree | structure | implemented | lib/hb_tree |  |
| GCD | algorithm | implemented | algo/numeric |  |
| geometric optimization problem | problem | implemented | algo/geo_opt |  |
| global optimum | concept | n/a |  |  |
| gnome sort | algorithm | implemented | algo/sort_classic |  |
| graph | concept | n/a |  |  |
| graph coloring | algorithm | implemented | algo/graph_np |  |
| graph concentration | concept | n/a |  |  |
| graph drawing | concept | n/a |  |  |
| graph isomorphism | algorithm | implemented | algo/graph_np |  |
| graph partition | algorithm | implemented | algo/graph_np |  |
| Gray code | algorithm | implemented | algo/bits |  |
| greatest common divisor | algorithm | implemented | algo/numeric |  |
| greedy algorithm | concept | n/a |  |  |
| greedy heuristic | concept | n/a |  |  |
| grid drawing | concept | n/a |  |  |
| grid file | structure | implemented | lib/grid_file |  |
| Grover's algorithm | concept | n/a |  |  |

### H

| entry | kind | state | module | note |
|---|---|---|---|---|
| halting problem | concept | n/a |  |  |
| Hamiltonian cycle | algorithm | implemented | algo/graph_np |  |
| Hamiltonian path | algorithm | implemented | algo/graph_np |  |
| Hamming distance | algorithm | implemented | algo/stringology |  |
| hard split, easy merge | concept | n/a |  |  |
| hash | algorithm | implemented | algo/hash |  |
| hashbelt | structure | implemented | lib/dynamic_hash |  |
| hash function | algorithm | implemented | algo/hash |  |
| hash heap | structure | implemented | lib/hash_heap |  |
| hash table | structure | implemented | lib/hash_table, lib/rh_map |  |
| hash table delete | structure | implemented | lib/hash_table, lib/rh_map | backward shift |
| hash tree | structure | implemented | lib/merkle_tree |  |
| Hausdorff distance | algorithm | implemented | algo/geometry |  |
| hB-tree | structure | implemented | lib/hb_tree |  |
| head | concept | n/a |  |  |
| heap | structure | implemented | lib/heap |  |
| heapify | structure | implemented | lib/heap |  |
| heap property | concept | n/a |  |  |
| heapsort | algorithm | implemented | algo/sort |  |
| heaviest common subsequence | algorithm | implemented | algo/string_approx |  |
| height | concept | n/a |  |  |
| height-balanced binary search tree | structure | implemented | lib/avl |  |
| height-balanced tree | structure | implemented | lib/avl |  |
| heuristic | concept | n/a |  |  |
| hidden Markov model | algorithm | implemented | algo/hmm |  |
| highest common factor | algorithm | implemented | algo/numeric |  |
| histogram sort | algorithm | implemented | algo/sort_dist |  |
| HMM | algorithm | implemented | algo/hmm |  |
| homeomorphic | concept | n/a |  |  |
| horizontal visibility map | algorithm | implemented | algo/visibility |  |
| Horner's rule | algorithm | implemented | algo/numerical |  |
| Horspool | algorithm | implemented | algo/string_search |  |
| hsadelta | concept | n/a |  |  |
| Huffman coding | algorithm | implemented | algo/prefix_code |  |
| huge sparse array | structure | implemented | lib/sparse_matrix |  |
| Hungarian algorithm | algorithm | implemented | algo/matching |  |
| hybrid algorithm | concept | n/a |  |  |
| hyperedge | concept | n/a |  |  |
| hypergraph | concept | n/a |  |  |
| HyperLogLog | structure | implemented | lib/hyperloglog |  |

### I

| entry | kind | state | module | note |
|---|---|---|---|---|
| IBLT | structure | implemented | lib/iblt |  |
| ideal merge | algorithm | implemented | algo/kway_merge |  |
| ideal random shuffle | algorithm | implemented | algo/sequence | ds_shuffle |
| implication | concept | n/a |  |  |
| implies | concept | n/a |  |  |
| inclusion-exclusion principle | concept | n/a |  |  |
| inclusive or | concept | n/a |  |  |
| incompressible string | concept | n/a |  |  |
| incremental hashing | algorithm | implemented | algo/hash_family |  |
| in-degree | concept | n/a |  |  |
| independent set | algorithm | implemented | algo/graph_np |  |
| index file | concept | n/a |  |  |
| information theoretic bound | concept | n/a |  |  |
| in-order traversal | structure | implemented | lib/avl, lib/rbt, lib/bst walks |  |
| in-place sort | algorithm | implemented | algo/sort |  |
| insertion sort | algorithm | implemented | algo/sort |  |
| integer linear program | algorithm | implemented | algo/lp |  |
| integer multi-commodity flow | concept | n/a |  |  |
| integer polyhedron | concept | n/a |  |  |
| interactive proof system | concept | n/a |  |  |
| interior-based representation | concept | n/a |  |  |
| internal node | concept | n/a |  |  |
| internal sort | concept | n/a |  |  |
| interpolation search | algorithm | implemented | algo/search |  |
| interpolation-sequential search | algorithm | implemented | algo/search |  |
| interpolation sort | algorithm | implemented | algo/sort_dist |  |
| intersection | algorithm | implemented | algo/sequence | ds_set_intersection |
| interval tree | structure | implemented | lib/interval_tree |  |
| intractable | concept | n/a |  |  |
| introsort | algorithm | implemented | algo/sort |  |
| introspective sort | algorithm | implemented | algo/sort |  |
| inverse Ackermann function | algorithm | implemented | algo/number_theory |  |
| inverse suffix array | structure | implemented | lib/suffix_tree |  |
| inversion list | structure | implemented | lib/inversion_list |  |
| inverted file index | structure | implemented | lib/inverted_index |  |
| inverted index | structure | implemented | lib/inverted_index |  |
| invertible Bloom lookup table | structure | implemented | lib/iblt |  |
| irreflexive | concept | n/a |  |  |
| isomorphic | concept | n/a |  |  |
| iteration | concept | n/a |  |  |

### J

| entry | kind | state | module | note |
|---|---|---|---|---|
| Jaro-Winkler | algorithm | implemented | algo/string_approx |  |
| jelly-fish | concept | n/a |  |  |
| Johnson's algorithm | algorithm | implemented | algo/graph_path |  |
| Johnson-Trotter | algorithm | implemented | algo/combinatorics |  |
| JSort | algorithm | implemented | algo/sort_heap |  |
| J sort | algorithm | implemented | algo/sort_dist |  |
| jump list | structure | implemented | lib/jump_list |  |
| jump search | algorithm | implemented | algo/search |  |

### K

| entry | kind | state | module | note |
|---|---|---|---|---|
| Karnaugh map | concept | n/a |  |  |
| Karp-Rabin | algorithm | implemented | algo/stringology |  |
| Karp reduction | concept | n/a |  |  |
| k-ary heap | structure | implemented | lib/indexed_heap | d-ary |
| k-ary Huffman coding | algorithm | implemented | algo/prefix_code |  |
| k-ary tree | concept | n/a |  |  |
| k-clustering | algorithm | implemented | algo/facility |  |
| k-coloring | algorithm | implemented | algo/graph_np |  |
| k-connected graph | algorithm | implemented | algo/flow |  |
| k-d-B-tree | structure | implemented | lib/kdb_tree |  |
| k-dimensional | concept | n/a |  |  |
| K-dominant match | algorithm | implemented | algo/string_approx |  |
| k-d tree | structure | implemented | lib/kd_tree |  |
| key | concept | n/a |  |  |
| KMP | algorithm | implemented | algo/stringology |  |
| KmpSkip Search | algorithm | implemented | algo/string_search |  |
| knapsack problem | problem | implemented | algo/knapsack |  |
| knight's tour | algorithm | implemented | algo/combinatorics |  |
| Knuth-Morris-Pratt algorithm | algorithm | implemented | algo/stringology |  |
| Königsberg bridges problem | problem | implemented | algo/graph_struct |  |
| Kolmogorov complexity | concept | n/a |  |  |
| Kraft's inequality | concept | n/a |  |  |
| Kripke structure | algorithm | implemented | algo/model_check |  |
| Kruskal's algorithm | algorithm | implemented | algo/graph_path |  |
| kth order Fibonacci numbers | algorithm | implemented | algo/number_theory |  |
| kth shortest path | algorithm | implemented | algo/graph_path |  |
| kth smallest element | algorithm | implemented | algo/sort | nth_element |
| k²-tree | structure | implemented | lib/k2_tree |  |
| KV diagram | concept | n/a |  |  |
| k-way merge | algorithm | implemented | algo/kway_merge |  |
| k-way merge sort | algorithm | implemented | algo/sort_misc |  |
| k-way tree | concept | n/a |  |  |

### L

| entry | kind | state | module | note |
|---|---|---|---|---|
| labeled graph | concept | n/a |  |  |
| language | concept | n/a |  |  |
| last-in, first-out | structure | implemented | lib/stack |  |
| Las Vegas algorithm | concept | n/a |  |  |
| lattice | concept | n/a |  |  |
| layered graph | algorithm | implemented | algo/flow |  |
| LCFS hashing | structure | implemented | lib/open_map |  |
| LCM | algorithm | implemented | algo/numeric |  |
| LCS | algorithm | implemented | algo/stringology |  |
| LDS | concept | n/a |  |  |
| leaf | concept | n/a |  |  |
| least common multiple | algorithm | implemented | algo/numeric |  |
| leftist tree | structure | implemented | lib/meldable_heap |  |
| left rotation | structure | implemented | lib/avl, lib/rbt |  |
| Lempel-Ziv-Welch | algorithm | implemented | algo/compress |  |
| level | concept | n/a |  |  |
| level-order traversal | concept | n/a |  |  |
| Levenshtein distance | algorithm | implemented | algo/stringology |  |
| lexicographical order | algorithm | implemented | algo/sequence | ds_lexicographic_compare |
| LIFO | structure | implemented | lib/stack |  |
| linear | concept | n/a |  |  |
| linear congruential generator | algorithm | implemented | algo/random |  |
| linear hash | algorithm | implemented | algo/hash_family |  |
| linear hashing | structure | implemented | lib/linear_hash_map |  |
| linear insertion sort | algorithm | implemented | algo/sort |  |
| linear order | concept | n/a |  |  |
| linear probing | structure | implemented | lib/hash_table, lib/rh_map |  |
| linear probing sort | algorithm | implemented | algo/sort_dist |  |
| linear product | concept | n/a |  |  |
| linear program | algorithm | implemented | algo/lp |  |
| linear quadtree | structure | implemented | lib/quadtree |  |
| linear search | algorithm | implemented | algo/search | ds_find |
| link | concept | n/a |  |  |
| linked data signature | structure | restricted | lib/ld_signature | colour-refinement canonicalisation, not RDFC-1.0; shared-key MAC suite by default |
| linked list | structure | implemented | lib/list, lib/dlist |  |
| list | structure | implemented | lib/list |  |
| list contraction | algorithm | reference | algo/parallel_sim | sequential simulation of the PRAM algorithm |
| little-o notation | concept | n/a |  |  |
| Lm distance | algorithm | implemented | algo/geometry |  |
| load factor | structure | implemented | lib/hash_table, lib/rh_map |  |
| local alignment | algorithm | implemented | algo/string_approx |  |
| locality-sensitive hashing | algorithm | implemented | algo/lsh |  |
| local optimum | concept | n/a |  |  |
| logarithmic | concept | n/a |  |  |
| longest common subsequence | algorithm | implemented | algo/stringology |  |
| longest common substring | algorithm | implemented | algo/string_approx |  |
| Lotka's law | concept | n/a |  |  |
| lower bound | algorithm | implemented | algo/search | ds_lower_bound |
| lower triangular matrix | algorithm | implemented | algo/matrix |  |
| lowest common ancestor | algorithm | implemented | algo/lca |  |
| l-reduction | concept | n/a |  |  |
| lucky sort | algorithm | implemented | algo/sort_misc |  |
| LZW compression | algorithm | implemented | algo/compress |  |

### M

| entry | kind | state | module | note |
|---|---|---|---|---|
| Malhotra-Kumar-Maheshwari blocking flow | algorithm | implemented | algo/flow |  |
| Manhattan distance | algorithm | implemented | algo/geometry |  |
| many-one reduction | concept | n/a |  |  |
| map | structure | implemented | lib/hash_table, lib/rh_map, lib/avl, ... |  |
| Markov chain | algorithm | implemented | algo/markov |  |
| Marlena | concept | n/a |  |  |
| marriage problem | problem | implemented | algo/matching |  |
| Master theorem | concept | n/a |  |  |
| matched edge | concept | n/a |  |  |
| matched vertex | concept | n/a |  |  |
| matching | algorithm | implemented | algo/matching |  |
| matrix | concept | n/a |  |  |
| matrix-chain multiplication problem | problem | implemented | algo/knapsack |  |
| matrix multiplication | algorithm | implemented | algo/matrix |  |
| max-heap property | concept | n/a |  |  |
| maximal independent set | algorithm | implemented | algo/graph_np |  |
| maximally connected component | concept | n/a |  |  |
| Maximal Shift | algorithm | implemented | algo/string_search |  |
| maximum bipartite matching | algorithm | implemented | algo/matching |  |
| maximum-flow problem | problem | implemented | algo/flow |  |
| MBB | algorithm | implemented | algo/geometry |  |
| Mealy machine | structure | implemented | lib/automaton |  |
| mean | algorithm | implemented | algo/numeric | Welford |
| median | algorithm | implemented | algo/sort | nth_element |
| meld | structure | implemented | lib/meldable_heap |  |
| memoization | concept | n/a |  |  |
| merge | algorithm | implemented | algo/sequence |  |
| merge sort | algorithm | implemented | algo/sort |  |
| Merkle tree | structure | implemented | lib/merkle_tree |  |
| metaheuristic | concept | n/a |  |  |
| metaphone | algorithm | implemented | algo/phonetic |  |
| midrange | algorithm | implemented | algo/numerical |  |
| Miller-Rabin | algorithm | implemented | algo/numeric |  |
| min-heap property | concept | n/a |  |  |
| minimal perfect hashing | algorithm | implemented | algo/perfect_hash |  |
| minimax | algorithm | implemented | algo/game |  |
| minimum bounding box | algorithm | implemented | algo/geometry |  |
| minimum cut | algorithm | implemented | algo/flow |  |
| minimum spanning tree | algorithm | implemented | algo/graph_view (Prim), lib/graph |  |
| minimum vertex cut | algorithm | implemented | algo/flow |  |
| Minkowski distance | algorithm | implemented | algo/geometry |  |
| mixed integer linear program | algorithm | implemented | algo/lp |  |
| mode | algorithm | implemented | algo/numerical |  |
| model checking | algorithm | implemented | algo/model_check |  |
| model of computation | concept | n/a |  |  |
| moderately exponential | concept | n/a |  |  |
| MODIFIND | algorithm | implemented | algo/select |  |
| monotone priority queue | structure | implemented | lib/radix_heap |  |
| monotonically decreasing | concept | n/a |  |  |
| monotonically increasing | concept | n/a |  |  |
| Monte Carlo algorithm | concept | n/a |  |  |
| Moore machine | structure | implemented | lib/automaton |  |
| Morris-Pratt algorithm | algorithm | implemented | algo/string_search |  |
| move | algorithm | implemented | algo/compress |  |
| move-to-front heuristic | structure | implemented | lib/self_org_list |  |
| move-to-root heuristic | structure | implemented | lib/self_org_list |  |
| MST | algorithm | implemented | algo/graph_view, lib/graph |  |
| multi-commodity flow | concept | n/a |  |  |
| multigraph | concept | n/a |  |  |
| multikey Quicksort | algorithm | implemented | algo/sort_dist |  |
| multilayer grid file | structure | implemented | lib/grid_file |  |
| multiplication method | algorithm | implemented | algo/hash_family |  |
| multiprefix | algorithm | implemented | algo/scan |  |
| multiprocessor model | concept | n/a |  |  |
| multi-set | structure | implemented | lib/multiset |  |
| multi suffix tree | structure | implemented | lib/suffix_tree |  |
| multiway decision | concept | n/a |  |  |
| multiway merge | algorithm | implemented | algo/kway_merge |  |
| multiway search tree | structure | implemented | lib/btree |  |
| multiway tree | structure | implemented | lib/btree |  |
| Munkres' assignment algorithm | algorithm | implemented | algo/matching |  |

### N

| entry | kind | state | module | note |
|---|---|---|---|---|
| naive string search | algorithm | implemented | algo/string_search |  |
| nand | concept | n/a |  |  |
| n-ary function | concept | n/a |  |  |
| NC many-one reducibility | concept | n/a |  |  |
| nearest neighbor | algorithm | implemented | algo/geometry |  |
| negation | concept | n/a |  |  |
| network flow | algorithm | implemented | algo/flow |  |
| network flow problem | problem | implemented | algo/flow |  |
| next state | concept | n/a |  |  |
| NFA | structure | implemented | lib/automaton |  |
| NFTA | alias | implemented | lib/tree_automaton | alias of nondeterministic finite tree automaton |
| NIST | concept | n/a |  |  |
| node | concept | n/a |  |  |
| nonbalanced merge | algorithm | implemented | algo/external_sort |  |
| nonbalanced merge sort | algorithm | implemented | algo/external_sort |  |
| nondeterministic | concept | n/a |  |  |
| nondeterministic algorithm | concept | n/a |  |  |
| nondeterministic finite automaton | structure | implemented | lib/automaton |  |
| nondeterministic finite state machine | concept | n/a |  |  |
| nondeterministic finite tree automaton | structure | implemented | lib/tree_automaton |  |
| nondeterministic polynomial time | concept | n/a |  |  |
| nondeterministic tree automaton | structure | implemented | lib/tree_automaton |  |
| nondeterministic Turing machine | structure | implemented | lib/turing_machine |  |
| nonterminal node | concept | n/a |  |  |
| nor | concept | n/a |  |  |
| not | concept | n/a |  |  |
| Not So Naive | algorithm | implemented | algo/string_search |  |
| NP | concept | n/a |  |  |
| NP-complete | concept | n/a |  |  |
| NP-complete language | concept | n/a |  |  |
| NP-hard | concept | n/a |  |  |
| n queens | algorithm | implemented | algo/combinatorics |  |
| nullary function | concept | n/a |  |  |
| null tree | concept | n/a |  |  |
| NYSIIS | algorithm | implemented | algo/phonetic |  |

### O

| entry | kind | state | module | note |
|---|---|---|---|---|
| O | concept | n/a |  |  |
| OBDD | structure | implemented | lib/bdd |  |
| objective function | concept | n/a |  |  |
| oblivious algorithm | concept | n/a |  |  |
| occurrence | concept | n/a |  |  |
| octree | structure | implemented | lib/quadtree |  |
| off-line algorithm | concept | n/a |  |  |
| offset | concept | n/a |  |  |
| omega | concept | n/a |  |  |
| omicron | concept | n/a |  |  |
| one-dimensional | concept | n/a |  |  |
| on-line algorithm | concept | n/a |  |  |
| open addressing | structure | implemented | lib/hash_table, lib/rh_map |  |
| optimal | concept | n/a |  |  |
| optimal cost | concept | n/a |  |  |
| optimal hashing | algorithm | implemented | algo/perfect_hash |  |
| optimal merge | algorithm | implemented | algo/kway_merge |  |
| optimal mismatch | algorithm | implemented | algo/string_search |  |
| optimal polygon triangulation problem | problem | implemented | algo/geometry |  |
| optimal polyphase merge | algorithm | implemented | algo/external_sort |  |
| optimal polyphase merge sort | algorithm | implemented | algo/external_sort |  |
| optimal solution | concept | n/a |  |  |
| optimal triangulation problem | problem | implemented | algo/geometry |  |
| optimal value | concept | n/a |  |  |
| optimization problem | concept | n/a |  |  |
| or | concept | n/a |  |  |
| oracle set | concept | n/a |  |  |
| oracle tape | concept | n/a |  |  |
| oracle Turing machine | concept | n/a |  |  |
| order | concept | n/a |  |  |
| ordered array | structure | implemented | lib/vec | sort, binary search |
| ordered binary decision diagram | structure | implemented | lib/bdd |  |
| ordered linked list | structure | implemented | lib/sorted_list |  |
| ordered tree | concept | n/a |  |  |
| order-preserving hash | algorithm | implemented | algo/hash_family |  |
| order-preserving Huffman coding | algorithm | implemented | algo/prefix_code |  |
| order-preserving minimal perfect hashing | algorithm | implemented | algo/perfect_hash |  |
| oriented acyclic graph | concept | n/a |  |  |
| oriented graph | concept | n/a |  |  |
| oriented tree | concept | n/a |  |  |
| orthogonal drawing | concept | n/a |  |  |
| orthogonal lists | structure | implemented | lib/sparse_matrix |  |
| orthogonally convex rectilinear polygon | algorithm | implemented | algo/geometry |  |
| oscillating merge sort | algorithm | implemented | algo/external_sort |  |
| out-degree | concept | n/a |  |  |

### P

| entry | kind | state | module | note |
|---|---|---|---|---|
| P | concept | n/a |  |  |
| packing | concept | n/a |  |  |
| padding argument | concept | n/a |  |  |
| pagoda | structure | implemented | lib/pagoda |  |
| pairing heap | structure | implemented | lib/pairing_heap |  |
| PAM | structure | implemented | lib/quadtree |  |
| parallel computation thesis | concept | n/a |  |  |
| parallel prefix computation | algorithm | implemented | algo/scan |  |
| parallel random-access machine | concept | n/a |  |  |
| parametric searching | concept | n/a |  |  |
| parent | concept | n/a |  |  |
| partial function | concept | n/a |  |  |
| partially decidable problem | concept | n/a |  |  |
| partially dynamic graph problem | concept | n/a |  |  |
| partially ordered set | concept | n/a |  |  |
| partially persistent data structure | structure | implemented | lib/persistent_rbt, lib/persistent_trie |  |
| partial order | concept | n/a |  |  |
| partial recursive function | concept | n/a |  |  |
| partition | algorithm | implemented | algo/combinatorics |  |
| passive data structure | concept | n/a |  |  |
| path | concept | n/a |  |  |
| path cover | algorithm | implemented | algo/graph_np |  |
| path system problem | concept | n/a |  |  |
| Patricia tree | structure | implemented | lib/radix_trie, lib/critbit_tree |  |
| pattern | concept | n/a |  |  |
| pattern element | concept | n/a |  |  |
| P-complete | concept | n/a |  |  |
| PCP | concept | n/a |  |  |
| PDA | alias | implemented | lib/pushdown | alias of pushdown automaton |
| Pearson's hash | algorithm | implemented | algo/hash_family |  |
| perfect binary tree | concept | n/a |  |  |
| perfect hashing | algorithm | implemented | algo/perfect_hash |  |
| perfect k-ary tree | concept | n/a |  |  |
| perfect matching | algorithm | implemented | algo/matching |  |
| perfect shuffle | algorithm | implemented | algo/sequence |  |
| performance guarantee | concept | n/a |  |  |
| performance ratio | concept | n/a |  |  |
| permutation | algorithm | implemented | algo/sequence | next/prev permutation |
| permutation sort | algorithm | implemented | algo/sort_misc |  |
| persistent data structure | structure | implemented | lib/persistent_rbt, lib/persistent_trie |  |
| phonetic coding | algorithm | implemented | algo/phonetic |  |
| pigeonhole sort | algorithm | implemented | algo/sort_dist |  |
| pile | concept | n/a |  |  |
| pipelined divide and conquer | concept | n/a |  |  |
| planar graph | concept | n/a |  |  |
| planarization | concept | n/a |  |  |
| planar straight-line graph | concept | n/a |  |  |
| PLOP-hashing | structure | implemented | lib/plop_hash |  |
| point access method | structure | implemented | lib/quadtree |  |
| pointer jumping | algorithm | reference | algo/parallel_sim | sequential simulation of the PRAM algorithm |
| pointer machine | concept | n/a |  |  |
| poissonization | concept | n/a |  |  |
| polychotomy | concept | n/a |  |  |
| polyhedron | concept | n/a |  |  |
| polylogarithmic | concept | n/a |  |  |
| polynomial | concept | n/a |  |  |
| polynomial approximation scheme | concept | n/a |  |  |
| polynomial hierarchy | concept | n/a |  |  |
| polynomial time | concept | n/a |  |  |
| polynomial-time reduction | concept | n/a |  |  |
| polyphase merge | algorithm | implemented | algo/external_sort |  |
| polyphase merge sort | algorithm | implemented | algo/external_sort |  |
| polytope | concept | n/a |  |  |
| poset | concept | n/a |  |  |
| postfix traversal | concept | n/a |  |  |
| Post machine | concept | n/a |  |  |
| postman's sort | algorithm | implemented | algo/sort_dist |  |
| postorder traversal | structure | implemented | lib/avl, lib/bst walks |  |
| Post's correspondence problem | concept | n/a |  |  |
| potential function | concept | n/a |  |  |
| PRAM | concept | n/a |  |  |
| predicate | concept | n/a |  |  |
| prefix | concept | n/a |  |  |
| prefix code | algorithm | implemented | algo/prefix_code |  |
| prefix sums | algorithm | implemented | algo/scan |  |
| prefix traversal | concept | n/a |  |  |
| preorder traversal | structure | implemented | lib/avl, lib/bst walks |  |
| primary clustering | concept | n/a |  |  |
| primitive algorithm | concept | n/a |  |  |
| primitive recursive | concept | n/a |  |  |
| Prim-Jarnik algorithm | algorithm | implemented | algo/graph_view |  |
| principle of optimality | concept | n/a |  |  |
| priority queue | structure | implemented | lib/heap, lib/indexed_heap, ... |  |
| prisoner's dilemma | concept | n/a |  |  |
| PRNG | algorithm | implemented | algo/random |  |
| probabilistic algorithm | concept | n/a |  |  |
| probabilistically checkable proof | concept | n/a |  |  |
| probabilistic Turing machine | concept | n/a |  |  |
| probe sequence | structure | implemented | lib/open_map |  |
| procedure | concept | n/a |  |  |
| process algebra | concept | n/a |  |  |
| proper | concept | n/a |  |  |
| proper binary tree | concept | n/a |  |  |
| proper coloring | concept | n/a |  |  |
| proper subset | concept | n/a |  |  |
| property list | structure | implemented | lib/association_list |  |
| prune and search | concept | n/a |  |  |
| pseudo-random number generator | algorithm | implemented | algo/random |  |
| PTAS | concept | n/a |  |  |
| pth order Fibonacci numbers | algorithm | implemented | algo/number_theory |  |
| P-tree | structure | implemented | lib/p_tree |  |
| purely functional language | concept | n/a |  |  |
| pushdown automaton | structure | implemented | lib/pushdown |  |
| pushdown transducer | structure | implemented | lib/pushdown |  |
| p-way merge sort | algorithm | implemented | algo/sort_misc |  |

### Q

| entry | kind | state | module | note |
|---|---|---|---|---|
| qm sort | algorithm | implemented | algo/sort_misc |  |
| q sort | algorithm | variant | algo/sort_misc | one member of the quicksort + insertion sort family (ds_q_sort preset) |
| quadratic probing | structure | implemented | lib/hash_table |  |
| quadtree | structure | implemented | lib/quadtree |  |
| quadtree complexity theorem | concept | n/a |  |  |
| quad trie | structure | implemented | lib/quadtree |  |
| quantum computation | concept | n/a |  |  |
| queue | structure | implemented | lib/queue |  |
| quick search | algorithm | implemented | algo/string_search |  |
| quicksort | algorithm | implemented | algo/sort_misc |  |

### R

| entry | kind | state | module | note |
|---|---|---|---|---|
| Rabin-Karp | algorithm | implemented | algo/stringology |  |
| radix sort | algorithm | implemented | algo/sort | LSD radix |
| radix tree | structure | implemented | lib/radix_trie |  |
| ragged matrix | structure | implemented | lib/sparse_matrix |  |
| Raita | algorithm | implemented | algo/string_search |  |
| random access machine | concept | n/a |  |  |
| randomization | concept | n/a |  |  |
| randomized algorithm | concept | n/a |  |  |
| randomized binary search tree | structure | implemented | lib/rbst |  |
| randomized complexity | concept | n/a |  |  |
| randomized polynomial time | concept | n/a |  |  |
| randomized rounding | concept | n/a |  |  |
| randomized search tree | structure | implemented | lib/treap |  |
| Randomized-Select | algorithm | implemented | algo/select |  |
| random number generator | algorithm | implemented | algo/random | xoshiro128** |
| random sampling | algorithm | implemented | algo/random |  |
| random search | concept | n/a |  |  |
| range | concept | n/a |  |  |
| range sort | algorithm | implemented | algo/sort_dist |  |
| rank | structure | implemented | lib/order_statistic_tree, lib/bitvector |  |
| rapid sort | algorithm | implemented | algo/sort_dist |  |
| Ratcliff/Obershelp pattern recognition | algorithm | implemented | algo/string_approx |  |
| RBST | structure | implemented | lib/rbst |  |
| reachability | algorithm | implemented | algo/graph_view | BFS |
| reachable | algorithm | implemented | algo/graph_view | BFS |
| rebalance | structure | implemented | lib/avl, lib/rbt |  |
| recognizer | concept | n/a |  |  |
| rectangular matrix | concept | n/a |  |  |
| rectilinear | concept | n/a |  |  |
| rectilinear Steiner tree | algorithm | implemented | algo/steiner |  |
| recurrence equations | concept | n/a |  |  |
| recurrence relation | concept | n/a |  |  |
| recursion | concept | n/a |  |  |
| recursion termination | concept | n/a |  |  |
| recursion tree | concept | n/a |  |  |
| recursive | concept | n/a |  |  |
| recursive data structure | concept | n/a |  |  |
| recursive doubling | algorithm | reference | algo/parallel_sim | sequential simulation of the PRAM algorithm |
| recursive language | concept | n/a |  |  |
| recursively enumerable language | concept | n/a |  |  |
| recursively solvable | concept | n/a |  |  |
| red-black tree | structure | implemented | lib/rbt, lib/persistent_rbt |  |
| reduced basis | algorithm | implemented | algo/lattice |  |
| reduced digraph | algorithm | implemented | algo/graph_struct |  |
| reduced ordered binary decision diagram | structure | implemented | lib/bdd |  |
| reduction | concept | n/a |  |  |
| reflexive | concept | n/a |  |  |
| regular decomposition | structure | implemented | lib/quadtree |  |
| rehashing | structure | implemented | lib/open_map |  |
| relation | concept | n/a |  |  |
| relational structure | concept | n/a |  |  |
| relative performance guarantee | concept | n/a |  |  |
| relaxation | concept | n/a |  |  |
| relaxed balance | concept | n/a |  |  |
| repeated squaring | algorithm | implemented | algo/numeric | ds_powmod |
| rescalable | concept | n/a |  |  |
| reservoir sampling | algorithm | implemented | algo/random |  |
| restricted universe sort | algorithm | implemented | algo/sort_dist |  |
| Reverse Colussi | algorithm | implemented | algo/string_search |  |
| Reverse Factor | algorithm | implemented | algo/string_search |  |
| R-file | structure | implemented | lib/bang_file |  |
| Rice's method | algorithm | implemented | algo/rice_integral |  |
| right rotation | structure | implemented | lib/avl, lib/rbt |  |
| right-threaded tree | structure | implemented | lib/threaded_tree |  |
| RNG | algorithm | implemented | algo/random |  |
| ROBDD | structure | implemented | lib/bdd |  |
| Robin Hood hashing | structure | implemented | lib/rh_map |  |
| root | concept | n/a |  |  |
| root balance | structure | implemented | lib/wb_tree |  |
| rooted tree | concept | n/a |  |  |
| rotate left | structure | implemented | lib/avl, lib/rbt |  |
| rotate right | structure | implemented | lib/avl, lib/rbt |  |
| rotation | structure | implemented | lib/avl, lib/rbt, lib/treap |  |
| rough graph | concept | n/a |  |  |
| RP | concept | n/a |  |  |
| R+-tree | structure | implemented | lib/rtree |  |
| R*-tree | structure | implemented | lib/rtree |  |
| R-tree | structure | implemented | lib/rtree |  |
| run time | concept | n/a |  |  |

### S

| entry | kind | state | module | note |
|---|---|---|---|---|
| saguaro stack | structure | implemented | lib/cactus_stack |  |
| SAM | concept | n/a |  |  |
| saturated edge | concept | n/a |  |  |
| SBB tree | structure | implemented | lib/rbt | symmetric binary B-tree = red-black tree |
| scan | algorithm | implemented | algo/scan |  |
| scapegoat tree | structure | implemented | lib/scapegoat_tree |  |
| scatter storage | concept | n/a |  |  |
| Schorr-Waite graph marking algorithm | algorithm | implemented | algo/schorr_waite |  |
| search | concept | n/a |  |  |
| search tree | concept | n/a |  |  |
| search tree property | concept | n/a |  |  |
| secant search | algorithm | implemented | algo/numerical |  |
| secondary clustering | concept | n/a |  |  |
| segment | concept | n/a |  |  |
| Select | algorithm | implemented | algo/sort (nth_element), lib/order_statistic_tree |  |
| select and partition | algorithm | implemented | algo/select |  |
| selection problem | problem | implemented | algo/sort | nth_element |
| selection sort | algorithm | implemented | algo/sort_classic |  |
| select kth element | algorithm | implemented | algo/sort | nth_element |
| select mode | algorithm | implemented | algo/select |  |
| self-loop | concept | n/a |  |  |
| self-organizing list | structure | implemented | lib/self_org_list |  |
| self-organizing sequential search | structure | implemented | lib/self_org_list |  |
| semidefinite programming | concept | missing |  | a solver is implementable; out of the current scope |
| separate chaining | structure | implemented | lib/hash_table |  |
| separation theorem | concept | n/a |  |  |
| sequential search | algorithm | implemented | algo/search |  |
| set | structure | implemented | lib/bitset, lib/sparse_set, lib/string_map | string set |
| set cover | algorithm | implemented | algo/facility |  |
| set packing | algorithm | implemented | algo/facility |  |
| shadow heap | structure | implemented | lib/shadow_heap |  |
| shadow merge | structure | implemented | lib/shadow_heap |  |
| shadow merge insert | structure | implemented | lib/shadow_heap |  |
| shaker sort | algorithm | implemented | algo/sort_classic | cocktail |
| Shannon-Fano coding | algorithm | implemented | algo/prefix_code |  |
| shared memory | concept | n/a |  |  |
| Shell sort | algorithm | implemented | algo/sort |  |
| Shift-Or | algorithm | implemented | algo/string_search |  |
| Shor's algorithm | concept | n/a |  |  |
| shortcutting | algorithm | reference | algo/parallel_sim | sequential simulation of the PRAM algorithm |
| shortest common supersequence | algorithm | implemented | algo/string_approx |  |
| shortest common superstring | algorithm | implemented | algo/string_approx |  |
| shortest path | algorithm | implemented | algo/graph_path |  |
| shortest spanning tree | algorithm | implemented | algo/graph_path |  |
| shuffle | algorithm | implemented | algo/sequence |  |
| shuffle sort | algorithm | implemented | algo/sort_dist |  |
| sibling | concept | n/a |  |  |
| sieve of Eratosthenes | algorithm | implemented | algo/numeric |  |
| sift up | structure | implemented | lib/heap |  |
| signature | concept | n/a |  |  |
| Simon's algorithm | concept | n/a |  |  |
| simple merge | algorithm | implemented | algo/sequence |  |
| simple path | concept | n/a |  |  |
| simple uniform hashing | concept | n/a |  |  |
| simplex | algorithm | implemented | algo/lp |  |
| simulated annealing | algorithm | implemented | algo/anneal |  |
| simulation theorem | concept | n/a |  |  |
| single-destination shortest-path problem | problem | implemented | algo/graph_conn |  |
| single-pair shortest-path problem | problem | implemented | algo/graph_path |  |
| single program multiple data | concept | n/a |  |  |
| single-source shortest-path problem | problem | implemented | algo/graph_path |  |
| singly linked list | structure | implemented | lib/list |  |
| singularity analysis | concept | n/a |  |  |
| sink | concept | n/a |  |  |
| sinking sort | algorithm | implemented | algo/sort_classic |  |
| skd-tree | structure | implemented | lib/kdb_tree |  |
| skew symmetry | concept | n/a |  |  |
| skip list | structure | implemented | lib/skip_list |  |
| skip search | algorithm | implemented | algo/string_search |  |
| slope selection | algorithm | implemented | algo/geometry |  |
| Smith algorithm | algorithm | implemented | algo/string_search |  |
| Smith-Waterman algorithm | algorithm | implemented | algo/string_approx |  |
| smoothsort | algorithm | implemented | algo/sort_heap |  |
| solvable | concept | n/a |  |  |
| sort | concept | n/a |  |  |
| sorted array | structure | implemented | lib/sorted_list |  |
| sorted list | structure | implemented | lib/sorted_list |  |
| sorted-string table | alias | implemented | lib/sstable | alias of SSTable |
| sort in place | concept | n/a |  |  |
| soundex | algorithm | implemented | algo/phonetic |  |
| source | concept | n/a |  |  |
| space-constructible function | concept | n/a |  |  |
| space ordering method | algorithm | implemented | algo/space_order |  |
| spanning tree | algorithm | implemented | algo/graph_path |  |
| sparse graph | concept | n/a |  |  |
| sparse matrix | structure | implemented | lib/sparse_matrix |  |
| sparsification | concept | n/a |  |  |
| sparsity | concept | n/a |  |  |
| spatial access method | structure | implemented | lib/rtree |  |
| spiral storage | structure | implemented | lib/spiral_storage |  |
| splay tree | structure | implemented | lib/splay_tree |  |
| SPMD | concept | n/a |  |  |
| square matrix | concept | n/a |  |  |
| square root | algorithm | implemented | algo/numerical |  |
| SST | algorithm | implemented | algo/graph_path |  |
| SSTable | structure | implemented | lib/sstable |  |
| stable | concept | n/a |  |  |
| stack | structure | implemented | lib/stack |  |
| stack tree | structure | implemented | lib/binary_tree_repr |  |
| star encoding | algorithm | implemented | algo/compress |  |
| star-shaped polygon | algorithm | implemented | algo/geometry |  |
| start state | concept | n/a |  |  |
| state | concept | n/a |  |  |
| state machine | concept | n/a |  |  |
| state transition | concept | n/a |  |  |
| static | concept | n/a |  |  |
| static Huffman coding | algorithm | implemented | algo/prefix_code |  |
| s-t cut | algorithm | implemented | algo/flow |  |
| st-digraph | concept | n/a |  |  |
| Steiner point | concept | n/a |  |  |
| Steiner ratio | concept | n/a |  |  |
| Steiner tree | algorithm | implemented | algo/steiner |  |
| Steiner vertex | concept | n/a |  |  |
| Steinhaus-Johnson-Trotter | algorithm | implemented | algo/combinatorics |  |
| Stirling's approximation | algorithm | implemented | algo/numerical |  |
| Stirling's formula | algorithm | implemented | algo/numerical |  |
| stooge sort | algorithm | implemented | algo/sort_classic |  |
| straight-line drawing | concept | n/a |  |  |
| strand sort | algorithm | implemented | algo/sort_misc |  |
| strictly decreasing | concept | n/a |  |  |
| strictly increasing | concept | n/a |  |  |
| strictly lower triangular matrix | algorithm | implemented | algo/matrix |  |
| strictly upper triangular matrix | algorithm | implemented | algo/matrix |  |
| string | structure | implemented | lib/str |  |
| string editing problem | problem | implemented | algo/stringology | edit distances |
| string matching | algorithm | implemented | algo/string_search |  |
| string matching on ordered alphabets | algorithm | implemented | algo/string_search |  |
| string matching with errors | algorithm | implemented | algo/string_approx |  |
| string matching with mismatches | algorithm | implemented | algo/string_search |  |
| string searching | algorithm | implemented | algo/string_search |  |
| strip packing | algorithm | implemented | algo/knapsack |  |
| strongly connected component | algorithm | implemented | algo/graph_view (Tarjan), lib/graph |  |
| strongly connected graph | concept | n/a |  |  |
| strongly NP-hard | concept | n/a |  |  |
| strsrch | algorithm | implemented | algo/string_search | brute force |
| stupid sort | alias | implemented | algo/sort_classic | alias of gnome sort / bogosort; gnome sort), algo/sort_misc (bogosort |
| subadditive ergodic theorem | concept | n/a |  |  |
| subgraph | concept | n/a |  |  |
| subgraph isomorphism | algorithm | implemented | algo/graph_np |  |
| sublinear time algorithm | concept | n/a |  |  |
| subsequence | concept | n/a |  |  |
| subset | concept | n/a |  |  |
| substring | concept | n/a |  |  |
| subtree | concept | n/a |  |  |
| suffix | concept | n/a |  |  |
| suffix array | algorithm | implemented | algo/stringology |  |
| suffix automaton | structure | implemented | lib/suffix_automaton |  |
| suffix tree | structure | implemented | lib/suffix_tree |  |
| superimposed code | structure | implemented | lib/signature_file |  |
| superset | concept | n/a |  |  |
| supersink | concept | n/a |  |  |
| supersource | concept | n/a |  |  |
| symmetric | concept | n/a |  |  |
| symmetrically linked list | structure | implemented | lib/dlist |  |
| symmetric binary B-tree | structure | implemented | lib/rbt |  |
| symmetric set difference | algorithm | implemented | algo/sequence | ds_set_symmetric_difference |
| symmetric traversal | alias | implemented | lib/bst | alias of in-order traversal; in-order traversal |
| symmetry breaking | concept | n/a |  |  |

### T

| entry | kind | state | module | note |
|---|---|---|---|---|
| tabulation hashing | algorithm | implemented | algo/hash_family |  |
| taco sort | algorithm | implemented | algo/sort_misc |  |
| tail | concept | n/a |  |  |
| tail recursion | concept | n/a |  |  |
| target | concept | n/a |  |  |
| temporal logic | concept | n/a |  |  |
| terminal | concept | n/a |  |  |
| terminal node | concept | n/a |  |  |
| ternary search tree | structure | implemented | lib/tst |  |
| text | structure | implemented | lib/str, lib/rope, lib/gap_buffer |  |
| text searching | algorithm | implemented | algo/string_search |  |
| theta | concept | n/a |  |  |
| threaded binary tree | structure | implemented | lib/threaded_tree |  |
| threaded tree | structure | implemented | lib/threaded_tree |  |
| three-dimensional | concept | n/a |  |  |
| three-way merge sort | algorithm | implemented | algo/sort_misc |  |
| three-way radix quicksort | algorithm | implemented | algo/sort_dist |  |
| time-constructible function | concept | n/a |  |  |
| time/space complexity | concept | n/a |  |  |
| top-down radix sort | algorithm | implemented | algo/sort_dist |  |
| top-down tree automaton | structure | implemented | lib/tree_automaton |  |
| topological order | algorithm | implemented | algo/graph_view |  |
| topological sort | algorithm | implemented | algo/graph_view, lib/graph |  |
| topology tree | structure | restricted | lib/topology_tree | updates cost O(log^2 n); no path aggregates |
| total function | concept | n/a |  |  |
| totally decidable language | concept | n/a |  |  |
| totally decidable problem | concept | n/a |  |  |
| totally undecidable problem | concept | n/a |  |  |
| total order | concept | n/a |  |  |
| tour | concept | n/a |  |  |
| tournament | algorithm | implemented | algo/kway_merge |  |
| tournament sort | algorithm | implemented | algo/sort_heap |  |
| towers of Hanoi | algorithm | implemented | algo/combinatorics |  |
| tractable | concept | n/a |  |  |
| transition | concept | n/a |  |  |
| transition function | concept | n/a |  |  |
| transitive | concept | n/a |  |  |
| transitive closure | algorithm | implemented | algo/graph_struct |  |
| transitive reduction | algorithm | implemented | algo/graph_struct |  |
| transpose sequential search | structure | implemented | lib/self_org_list |  |
| traveling salesman | algorithm | implemented | algo/tsp |  |
| treap | structure | implemented | lib/treap |  |
| tree | structure | implemented | lib/bst |  |
| tree automaton | structure | implemented | lib/tree_automaton |  |
| tree contraction | algorithm | reference | algo/parallel_sim | sequential simulation of the PRAM algorithm |
| tree editing problem | problem | implemented | algo/tree_edit |  |
| treesort (1) | algorithm | implemented | algo/sort_heap | tree sort |
| treesort (2) | algorithm | implemented | algo/sort_heap | heapsort variants, tournament sort |
| tree traversal | structure | implemented | lib/bst walks |  |
| triangle inequality | concept | n/a |  |  |
| triconnected graph | algorithm | implemented | algo/graph_conn |  |
| trie | structure | implemented | lib/trie |  |
| trinary function | concept | n/a |  |  |
| tripartition | algorithm | implemented | algo/sort_misc | Dutch national flag |
| Trotter-Johnson | algorithm | implemented | algo/combinatorics |  |
| TSP | algorithm | implemented | algo/tsp |  |
| TST | alias | implemented | lib/tst | alias of ternary search tree |
| Turbo-BM | algorithm | implemented | algo/string_search |  |
| Turbo Reverse Factor | algorithm | implemented | algo/string_search |  |
| Turing machine | structure | implemented | lib/turing_machine |  |
| Turing reduction | concept | n/a |  |  |
| Turing transducer | structure | implemented | lib/turing_machine |  |
| twin grid file | structure | implemented | lib/grid_file |  |
| twisted tabulation hashing | algorithm | implemented | algo/hash_family |  |
| two-dimensional | concept | n/a |  |  |
| two-level grid file | structure | implemented | lib/grid_file |  |
| Two Way algorithm | algorithm | implemented | algo/string_search |  |
| two-way linked list | structure | implemented | lib/dlist |  |
| two-way merge sort | algorithm | implemented | algo/sort_misc |  |

### U

| entry | kind | state | module | note |
|---|---|---|---|---|
| UB-tree | structure | implemented | lib/ub_tree |  |
| UKP | alias | implemented | algo/knapsack | alias of unbounded knapsack problem |
| unary function | concept | n/a |  |  |
| unbiased coin flipping algorithm | algorithm | implemented | algo/random |  |
| unbounded knapsack problem | problem | implemented | algo/knapsack |  |
| uncomputable function | concept | n/a |  |  |
| uncomputable problem | concept | n/a |  |  |
| undecidable language | concept | n/a |  |  |
| undecidable problem | concept | n/a |  |  |
| undirected graph | concept | n/a |  |  |
| uniform circuit complexity | concept | n/a |  |  |
| uniform circuit family | concept | n/a |  |  |
| uniform hashing | concept | n/a |  |  |
| uniform matrix | concept | n/a |  |  |
| union | algorithm | implemented | algo/sequence | ds_set_union |
| union of automata | structure | implemented | lib/automaton |  |
| universal B-tree | structure | implemented | lib/ub_tree |  |
| universal hashing | algorithm | implemented | algo/hash_family |  |
| universal state | concept | n/a |  |  |
| universal Turing machine | concept | n/a |  |  |
| universe | concept | n/a |  |  |
| unlimited branching tree | structure | implemented | lib/ub_tree |  |
| unranking | algorithm | implemented | algo/combinatorics |  |
| UnShuffle sort | algorithm | implemented | algo/sort_dist |  |
| unsolvable problem | concept | n/a |  |  |
| unsorted list | structure | implemented | lib/sorted_list |  |
| upper triangular matrix | algorithm | implemented | algo/matrix |  |

### V

| entry | kind | state | module | note |
|---|---|---|---|---|
| van Emde-Boas priority queue | structure | implemented | lib/veb_tree |  |
| vehicle routing problem | problem | implemented | algo/tsp |  |
| Veitch diagram | concept | n/a |  |  |
| Venn diagram | concept | n/a |  |  |
| vertex | concept | n/a |  |  |
| vertex coloring | algorithm | implemented | algo/graph_np |  |
| vertex connectivity | algorithm | implemented | algo/flow |  |
| vertex cover | algorithm | implemented | algo/graph_np |  |
| vertical visibility map | algorithm | implemented | algo/visibility |  |
| virtual hashing | structure | implemented | lib/dynamic_hash |  |
| visibility map | algorithm | implemented | algo/visibility |  |
| visible | concept | n/a |  |  |
| Viterbi algorithm | algorithm | implemented | algo/hmm |  |
| Vitter's algorithm | algorithm | implemented | algo/adaptive_huffman |  |
| VRP | algorithm | implemented | algo/tsp |  |

### W

| entry | kind | state | module | note |
|---|---|---|---|---|
| walk | concept | n/a |  |  |
| WCET | alias | n/a |  | alias of worst-case execution time |
| weak-heap | structure | implemented | lib/weak_heap |  |
| weak-heap sort | structure | implemented | lib/weak_heap |  |
| weight-balanced tree | structure | implemented | lib/wb_tree |  |
| weighted, directed graph | concept | n/a |  |  |
| weighted graph | concept | n/a |  |  |
| window | concept | n/a |  |  |
| witness | concept | n/a |  |  |
| work | concept | n/a |  |  |
| work-depth model | concept | n/a |  |  |
| work-efficient | concept | n/a |  |  |
| work-preserving | concept | n/a |  |  |
| worst case | concept | n/a |  |  |
| worst-case cost | concept | n/a |  |  |
| worst-case execution time | concept | n/a |  |  |
| worst-case minimum access | concept | n/a |  |  |

### X

| entry | kind | state | module | note |
|---|---|---|---|---|
| xor | concept | n/a |  |  |

### Y

| entry | kind | state | module | note |
|---|---|---|---|---|
| Yule distribution | algorithm | implemented | algo/random |  |

### Z

| entry | kind | state | module | note |
|---|---|---|---|---|
| Zeller's congruence | algorithm | implemented | algo/number_theory |  |
| Zhu-Takaoka | algorithm | implemented | algo/string_search |  |
| Zipfian distribution | algorithm | implemented | algo/random |  |
| Zipf's law | concept | n/a |  |  |
| zipper | structure | implemented | lib/zipper |  |
| ZPP | concept | n/a |  |  |


## Supplemental entries

| entry | kind | state | module | note |
|---|---|---|---|---|
| skew heap | structure | implemented | `lib/meldable_heap` | self-adjusting meldable heap policy |
| spectral test | algorithm | implemented | `algo/lattice`, `algo/random` | 2 <= t <= 8 dimensions, m < 2^26 (algo/lattice); 2-D in algo/random |
| stable marriage problem | problem | implemented | `algo/matching` | Gale-Shapley (the DADS entry is "marriage problem") |
| ternary search | algorithm | implemented | `algo/search` | extremum search on unimodal functions/arrays |
| tree sort | algorithm | implemented | `algo/sort_heap` | BST-insertion tree sort; see treesort (1) and (2) |
| transducer | concept | implemented | `lib/automaton`, `lib/pushdown`, `lib/turing_machine` | Mealy/Moore, finite state, pushdown and Turing transducers |
| tree transducer | structure | implemented | `lib/tree_automaton` | bottom-up tree transducers |
| Roaring bitmap | structure | implemented | `lib/roaring` | array, bitmap and run containers; rank, select, set algebra |
| robust geometric predicates | algorithm | implemented | `algo/robust_predicates`, `algo/geometry` | filtered exact orient2d/orient3d/incircle/insphere over expansion arithmetic, per-predicate input ranges; algo/geometry orientation uses orient2d |
| floating-point expansion arithmetic | algorithm | implemented | `algo/robust_predicates` | error-free sum/product, expansion sum/scale/product/compress |
| CHAMP (persistent hash trie) | structure | implemented | `lib/persistent_hamt` | canonical deletes, structural sharing, in-place edits of unshared versions |
| addressable priority queue | concept | implemented | `lib/pq_iface` | capability with binary, 4-ary, binomial, leftist, weight-biased, skew, randomized and pairing backends; per-operation cost classes |
| FM-index | structure | implemented | `lib/fm_index` | count/locate/extract with occurrence-table or wavelet-matrix rank |
| wavelet matrix | structure | implemented | `lib/wavelet_matrix`, `lib/fm_index` | quantile and range counting over any rank/select backend; also the FM-index rank backend |
| Euler tour tree | structure | implemented | `lib/euler_tour_tree` | dynamic forest with component and subtree aggregates |
| persistent vector | structure | implemented | `lib/persistent_vector` | bit-partitioned trie with tail and transients |
| finger tree | structure | implemented | `lib/finger_tree` | 2-3 finger tree with a caller-defined monoid |
| ordered-map capability | concept | implemented | `lib/ordered_iface` | split/join containers (sorted array, treap); Y-fast buckets |
| rank/select dictionary | structure | implemented | `lib/rank_select` | plain, RRR and Elias-Fano backends |
| RRR compressed bit vector | structure | implemented | `lib/rank_select` | fixed 15-bit class/offset blocks: nH0 + n/3.75 bits plus a rank and pointer per 32 blocks |
| buffer pool (pager) | structure | implemented | `lib/pager` | page-store capability, LRU with pinning and write-back |
| log-structured merge tree | structure | implemented | `lib/lsm_tree` | in memory: memtable, level 0, leveled runs over SSTables; no WAL, manifest, recovery or storage-backed compaction |
| quotient filter | structure | implemented | `lib/quotient_filter` | packed remainders, deletion (multiset), ordered enumeration |
| xor filter | structure | implemented | `lib/xor_filter` | static, peeling construction |
| approximate membership query | concept | implemented | `lib/amq_iface` | capability over Bloom, cuckoo, quotient and xor filters |
| cache replacement policy | concept | implemented | `lib/replacement` | LRU, FIFO, CLOCK, SIEVE; drives lib/pager |
| paged B+ tree | structure | implemented | `lib/paged_btree` | B+ tree persisted through lib/pager |
| space-constrained Lempel-Ziv-Welch | algorithm | implemented | `algo/compress` | LZW with freeze/reset dictionary policies |
| Wu's line algorithm | algorithm | implemented | `algo/geometry` | anti-aliased line rasterisation |
| succinct data structure | concept | n/a | `lib/bitvector`, `lib/wavelet_tree`, `lib/k2_tree` | family; members implemented as listed |
| weak cluster | concept | n/a |  | definition |
