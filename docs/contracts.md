# Contracts between components

The capability interfaces (maps, ordered maps, priority queues, rank/select,
approximate membership, replacement policies, page stores) let one structure
be built from another chosen by the caller. An operation table alone does not
make two components compatible, though: they also have to agree on element
layout and ownership, on order, on memory, on what a failure leaves behind,
on how long cursors and handles live, and on what each operation costs. This
document collects those agreements and points to where each is implemented.

## Element schema

`lib/schema` describes the values a container stores: size, alignment, copy
and destroy callbacks, user data and a representation id.

- **Policy follows from the callbacks.**
  - *Plain*: no callbacks. Elements are copied as bytes and nothing is released.
  - *Copy only*: a copy callback and no destroy. Copies are made by the callback, and nothing is released per element.
  - *Owned*: copy and destroy. Every copy is a clone that is released exactly once.
  - Destroy without copy is rejected: a structure that shares or hands out elements could not give out an owned copy.
- **Transfer services.** `ds_schema_transfer` names the service for moving
  elements between two containers:
  - *move*: the schemas are equal, so nodes or bytes move as they are and ownership travels with them.
  - *copy*: layout and id agree but the callbacks differ, so each element is cloned with the destination's copy and released with the source's destroy.
  - *none*: a conversion is the caller's job.

  Structural O(log n) joining and O(n) conversion are different services, and the API keeps them apart.
- **Containers copy the schema at initialization.**
  `ds_pvec_config_from_schema` and `ds_ftree_config_set_schema` build those
  structures' configurations from a schema.

## Order

- **SSTables and LSM trees.** Both take a `ds_sstable_comparator_t` (compare
  function, user data, id). The id is written into every table, and a table
  opened with a different order is rejected, so persisted data cannot be read
  in the wrong order.
- **Priority queues.** A queue records its compare function and user data. A
  native meld that moves nodes happens only when both queues use the same
  order; otherwise items are copied in the receiving queue's order.
- **Ordered maps.** The ordered capability's `compatible` operation checks
  layout and memory before `split` and `join`.

## Resources

A `ds_context_t` describes its allocator through resource traits:

| Trait | Meaning |
|---|---|
| `DS_RESOURCE_REALLOC` | resizes any live block |
| `DS_RESOURCE_FREE` | individual blocks can be freed |
| `DS_RESOURCE_FALLIBLE` | allocation may return NULL |
| `DS_RESOURCE_BOUNDED` | total capacity is limited |
| `DS_RESOURCE_FIXED_BLOCK` | only one block size is served |

The C heap, arena, pool, buddy and debug contexts describe themselves; custom
functions should be described with `ds_context_set_resource`.

- `ds_context_require` lets a structure refuse a context early. The map
  capability does this: every backend grows by `realloc`.
- `ds_context_compatible` says whether memory from one context may be
  released through the other. Moving nodes between containers needs exactly
  that, not the same context object. Priority-queue meld and ordered
  split/join check it.

## Mutation and failure

`docs/ownership.md` defines the two allocation profiles. Where an operation
cannot leave its structure unchanged after a failure, the module's header
says what remains:

| Module | Guarantee |
|---|---|
| `lib/paged_btree` | put prepares every page before changing any |
| `lib/topology_tree` | reserves work space before committing; a failed commit leaves an explicit broken state, and `ds_topology_tree_rebuild` repairs it |
| `lib/pq_iface` | a copying meld stops at the failing item, with both queues valid and every item in exactly one of them |
| `lib/pager` | `ds_pager_close` keeps dirty pages and can be retried; `ds_pager_discard` drops them |
| `lib/map_iface` | put is all or nothing; removing a present key always succeeds |
| `lib/yfast_trie` | a skipped rebalance leaves a bucket outside its bounds until a later update retries it |
| `lib/zipper`, `lib/trie_stores` | release and destruction never need fresh memory |
| `lib/sstable`, `lib/paged_btree` | malformed stored data gives `DS_ERR_INVALID`, never a read outside the data; each also has a separate full verification call |
| `lib/amq_iface` images | a load checks the whole image, including a quotient filter's slot structure, so a loaded filter is one its backend could have built; images from another word width are `DS_ERR_UNSUPPORTED` |

## Cursors and handles

| Kind | Valid until | Notes |
|---|---|---|
| map cursors | the next put or remove | a position a packed cursor cannot hold is reported (`DS_ERR_OVERFLOW` from `ds_map_next_status`) instead of ending the iteration silently |
| priority-queue handles | their item is popped or removed | |
| priority-queue item pointers (`top`, `item`) | the item leaves; for `DS_PQ_MOVING_ITEMS` backends, the next modification | |
| ordered-map cursors | the next modification | `ds_ordered_for_each` walks in linear time without allocating a cursor per step |
| SSTable iterators | the table bytes are freed | they borrow the bytes |
| LSM iterators and returned values | the next modification | |
| graph-view workspaces | reuse across calls | one call at a time |

## Costs

- **Priority queues.** Each operation states a worst-case and an amortized
  (or expected) cost class. Algorithms that take a queue inherit those costs:
  the graph-view `*_with` functions say what that means for Dijkstra, Prim
  and A*.
- **Succinct structures.** The Elias–Fano and rank/select headers give
  practical cost and space formulas, not only asymptotic ones.

## Algebraic laws

Augmented structures state the laws their callbacks must satisfy:

- **Combine.** Associativity, whether an identity is needed, and whether the
  output may alias an input. The finger tree calls `combine(out, out, b)`;
  the treaps, link-cut, Euler-tour and topology trees never alias.
- **Reversal.** How the reversed aggregate is kept.
- **Tags.** Lazy tags are uniform per-element actions whose summary update
  agrees with per-element application.
- **Order.** Which order an aggregate follows. Euler-tour aggregates follow
  the current tour, so only order-independent combines are stable across
  link and cut.
- **Roots.** Which operations reroot. Link-cut path operations, link and cut
  all reroot.

## Outcomes

Statuses already separate several outcomes:

- `DS_OK`, `DS_EXISTS` (an insert that replaced an existing value),
  `DS_NOT_FOUND`, `DS_EMPTY` and `DS_STOP` are successes.
- Errors are negative.
- `DS_SUCCEEDED` only says the status is not an error. It does not say that a
  payload exists, so lookups that can miss report `DS_NOT_FOUND` explicitly.

The outcomes listed below are what a richer result type will need to carry.
Its design is still to come; this list records the distinctions the
operations already make.

- **Found, absent, could not finish.** Finger-tree lookup returns `DS_OK`,
  `DS_NOT_FOUND` or `DS_ERR_ALLOC` instead of NULL for both of the last two.
  Map iteration separates its end from a cursor overflow.
- **Committed or not.** Prepare-then-commit operations either change
  everything or nothing. Operations that cannot promise that say what
  remains (see the table above).
- **Borrowed, cloned or transferred.**
  - Finger-tree pops return a clone for owned elements.
  - SSTable values and LSM values point into the structure and are borrowed.
  - `ds_pq_pop` copies the item out.
  - A native meld transfers nodes and their handles; a copying meld
    invalidates the absorbed queue's handles.
- **Exact, numerically qualified, heuristic.**
  - Robust predicates give exact signs within stated input ranges.
  - Weighted matching is exact in integer mode and within a stated tolerance
    otherwise.
  - Markov hitting times are numerical.
  - The TSP heuristics are approximations with stated bounds where they
    exist.
