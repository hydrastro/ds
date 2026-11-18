# Mutation, cursors and external synchronization

Containers do not lock individual calls. Synchronize the entire compound
operation, including lookup followed by use of a borrowed pointer and all steps
of iteration. Sharing a mutable context can require the same lock even across
otherwise independent containers; see `context.h`. The generic `ds_iter_t` shell
provides neither a snapshot nor automatic invalidation detection.

| Family | Borrowed views and mutation |
|---|---|
| Intrusive lists, queues and stacks | Payload/node addresses belong to the caller. Relinking changes next/previous traversal. Removing/freeing a pending node invalidates traversal; callbacks may not mutate unless a specific API permits it. |
| Intrusive BST/AVL/RBT/splay/treap | Rotations preserve node addresses; parent/child relationships and traversal state can change. Splay lookup itself mutates links. Erased/freed nodes cannot be used as cursors. |
| Original intrusive B-tree | User-node addresses stay stable; splits/merges move internal slots and update owner links. Remove clears the removed owner. Tree/node lifetime remains caller-controlled. |
| Open, Swiss and Robin Hood maps | Treat insert/remove/clear/reserve/compact/destroy, including failed mutations, as invalidating slot cursors and borrowed key/value pointers. Replacement keeps no general lifetime promise. |
| Generic hash table | Iterators borrow linked node storage. Do not mutate during a walk; resizing can rebuild nodes. To resume across a completed mutation, retain an application-owned key and look up its node again. |
| Dynamic vectors/deques/heaps | Growth moves backing storage; shifts and heap repair also change element positions without growth. Stable handles exist only in APIs that explicitly provide them. |
| `pq_iface` addressable queues | A handle and its payload survive updates/movement; removal, pop, clear or destruction ends that lifetime. The underlying heap's slot index is not a stable handle. |
| Hypergraphs | Edge handles survive unrelated edits. Member/value pointers belong to the edge; member replacement invalidates member views. Incidence sequences and derived graph views must be reacquired after mutation attempts. |
| Jelly-fish | Successful assignment, insertion or deletion rebuilds the shape and invalidates all views. Failed rebuilds preserve old views. Value replacement retains addresses but changes bytes. |
| Mutable tries | Structural edits may replace compressed/adaptive nodes. Keep keys separately when resuming; use the generic trie's documented preorder pruning protocol where applicable. |
| Persistent versions | Views remain valid while their owning immutable version is retained. Transient mutation has its own contract; a borrowed payload still depends on its external owner. |
| SSTable callback reader | Each iterator owns its block. Advance/seek changes its views; point-get results last until that table's next get or close. Independent iterators do not share a block buffer. |
| LSM iterator | Keep the iterator and tree alive. Do not mutate the tree while walking. Borrowed key/value views are replaced on advance. |
| History branches | Operation/payload views require their operation and history alive. Merge/replay/projection callbacks must not edit either participating history. |
| i18n | Catalog callbacks borrow records. Catalog/locale mutation is forbidden during rendering/walk/export. Rendered text is separately owned and replaced only after successful rendering. |

A more permissive rule in a concrete public header takes precedence. These are
conservative public contracts, not promises that every forbidden mutation would
immediately move a pointer. A callback returning early does not release owned
iterator state; invoke its destroy operation.

## Tombstone policy

| Map | Trigger and behavior |
|---|---|
| `open_map` | Before insertion exceeds 7/8 used slots, compact if tombstones exist and storage is fixed or the new live count is at most half capacity; otherwise grow. Explicit reserve may compact. |
| `swiss_map` | When inserting into an empty slot with no growth budget, compact if tombstones exist and storage is fixed or live size is at most `25 * floor(capacity / 32)`; otherwise grow. Reusing a tombstone consumes no empty-slot budget. Reserve may compact. |
| `hash_table` probing mode | Above `HASH_TABLE_RESIZE_FACTOR` used load (default 0.75), rebuild at the same capacity if the new live load is at most half that threshold; otherwise choose a larger prime capacity. Rebuild allocates and can fail without losing entries. |
| `rh_map` | Backward-shift deletion leaves no tombstones, so there is no compaction phase. |

Open-map compaction retains stored hashes; Swiss compaction calls the configured
hash again. Hash/equality/probe callbacks must remain stable and must not mutate
the map. Open-map custom probes must cover all slots; the original generic hash
table instead has its documented bounded-probe plus linear-sweep fallback.
