# Traversal and recursive-call review

The completion pass enumerated direct and mutually recursive calls in `lib/*.c`
and `algo/*.c`, then reviewed the resulting function families. The scan is a
source aid: it does not discover indirect callbacks, prove invariants, validate
malformed public structs, or establish a bound for application code.

| Family | Stack policy |
|---|---|
| Original intrusive treap | Parent-link insertion, traversal, clone and destruction; no height-dependent C recursion |
| Implicit and augmented treaps | Iterative split/join and parent-link walks; augmented guided descent uses a checked heap stack |
| Ordered-map treap, Euler-tour tree, enfilade | Iterative split/join and metadata repair; lazy tags pushed before relinking |
| Randomized BST | Iterative edits, shape copy, height, traversal and validation |
| Visibility sweep status | Iterative split/join, with no summary repair needed |
| AVL, red-black, B/B+ trees, weight-balanced and scapegoat trees | Remaining walks/edits follow maintained structural height; balance invariants are preconditions |
| R-tree, k-d-B-tree, multiway/P/fusion trees | Structural-height recursion; R* reinsertion marks each level to prevent repeated reinsertion at that level |
| Rope, confluent list, finger tree, persistent vector/RRB | Balanced structural recursion; reference release follows the same structural height |
| HAMT, persistent HAMT, van Emde Boas and wavelet trees | Recursion bounded by hash/index/alphabet word widths |
| Static k-d tree, segment tree, suffix-array recursion | Subproblems shrink by construction |
| Sorting, selection, closest pair, matrix divide-and-conquer | Shrinking subproblems; quicksort recurses on the smaller side; oscillating external-sort levels follow geometric run capacity |
| CTL parsing, game search, nested i18n | Explicit parser/depth/configuration limits; applications choose suitably small limits |
| Unicode decomposition | Fixed generated decomposition data; no caller-supplied decomposition graph |
| Ackermann | Explicit bounded work stack; the apparent self-call only reaches a nonrecursive closed form |
| Numeric sign/reflection wrappers, string sanitizing | Bounded normalization/retry calls, not data-depth traversal |
| Hash-table rebuild/insert and dynamic hashing | Capacity increases/rebuilds, not a user-key-depth traversal; callback implementations remain outside the bound |
| Generic trie with custom child stores | Built-in stores provide `next` for iterative traversal; a custom store without it retains the documented recursive fallback |

Expected logarithmic height is a running-time argument, not a stack-safety
argument. Randomized structures now remain stack-safe if priorities are poor,
but may take linear time per path. A custom randomized-BST source must provide
uniform bits; rejection sampling is not guaranteed to terminate for a broken
constant source. Callbacks must obey each module's non-reentrancy and mutation
contracts; these modules do not bound recursion initiated by application code.

Parent links increase per-node storage. Enfilade and randomized-BST public
layouts gain a parent-index array; rebuild dependent binaries. Existing cursor
and pointer rules remain those documented in `container-lifetimes.md`.

Validation used strict compilation, standalone examples, ASan/UBSan execution
and GCC static analysis of changed units. No test suite or random model checker
was added or run in this completion pass. Passing examples do not prove every
shape, failure sequence or callback implementation correct.

The later `algo/karnaugh` cover search has at most 64 levels: each recursive
choice covers a previously uncovered required cell, and the public model
supports at most six Boolean variables. Its node budget controls search work
separately from this fixed stack-depth bound.

The later jelly-fish reference builds its balanced binary body by halving the
pivot interval at each recursive call: O(log(body items)) stack depth. Tentacle
walks and rebuild staging are iterative.
