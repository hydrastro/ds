# Completion ledger

Base for this pass: `b983d99dc7e01d83ac51da518ed7b747d4eb82fd`.
The preceding repair tree is present in that upstream commit byte for byte.
Examples, strict compilation and static review provide validation; no new tests
are being added or run.

This ledger reconciles the finalization notes and the older roadmap. An item
is complete only when its implementation, contracts, examples and applicable
build checks are finished. Broad review requests are recorded with their actual
evidence and limits; a coverage label is not a correctness proof.

| Item | Status | Completion target |
|---|---|---|
| Standalone Result/i18n documentation | complete | Explain the C contracts and remove source-language comparisons |
| Addressable min-max and shadow heaps | complete | Reuse the underlying heaps, track item movement, add update/remove and capability backends with honest costs |
| SSTable callback reads and out-of-core LSM | complete | Retain bounded metadata/block buffers, propagate read failures, preserve format compatibility and iterator lifetimes |
| Namespace migration | complete | Canonical prefixed exports plus backward-compatible legacy entry points and an explicit migration path |
| Legacy status, context and callback APIs | complete | Inventory affected APIs; provide compatible checked/context-aware entry points and explicit failure contracts |
| Iterator and recursion policy | complete | Reconcile existing work, identify actual remaining depth/lifetime gaps, fix them or state caller bounds precisely |
| Probing-map tombstone compaction | complete | Verify existing open/Swiss compaction and document thresholds and guarantees |
| Persistent compressed-trie backend | complete | Add selectable compressed storage without weakening persistence, ownership or traversal contracts |
| History file archives and merge adapters | complete | Checked file-backed serialization and reusable container conflict policies |
| Catalog formats and locale policies | complete | Data-only catalog adapters and configurable plural/number/date formatting policies with examples |
| DADS mapping audit | complete | Check indexed mappings against primary definitions and actual API/algorithm scope; correct unsupported classifications |
| Additional graph algorithms | complete | Circuit evaluation, path systems, hyperedge incidence and fractional/integer multi-commodity network adapters |

The compaction and lifetime reviews are recorded in `container-lifetimes.md`;
the recursive-call review and iterative replacements are in `recursion-review.md`.

The index is fully reconciled (1,306 terms, no missing/extra indexed rows).
The six concrete gaps exposed by the semantic review are delivered, with
restricted/reference/variant scopes retained in `coverage-review.md` and the
coverage table. This is not a proof of every algorithm's correctness.
The legacy migration is complete within the compatible checked-path scope
recorded in `legacy-api-migration.md`. All 12 grouped finalization items are
complete. The final validation record is in `completion-notes.md`.

Deliberate limits remain documented: bounded/reference algorithm scopes,
floating-point optimization, legacy unchecked convenience APIs, externally
synchronized contexts, and custom-store/callback preconditions. These are not
silently classified as unrestricted implementations or exhaustively verified
behavior.
