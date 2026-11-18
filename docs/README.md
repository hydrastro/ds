# Documentation

Start with [building and embedding](building.md) and [using the APIs](api-overview.md).
The [module catalog](catalog.md) links to individual public headers, and the
[examples](../examples/README.md) show complete programs.

| Guide | What it explains |
|---|---|
| [Ownership](ownership.md) | Allocation, borrowing, cloning, destruction and persistent versions |
| [Mutation and lifetimes](container-lifetimes.md) | Cursor invalidation, stable handles and external synchronization |
| [Component contracts](contracts.md) | Interchangeable backends, resource traits, failure guarantees and costs |
| [Results and localization](result-i18n.md) | Domain outcomes, immutable diagnostics, composition and rendering |
| [Diagnostics](diagnostics.md) | Statuses, primary errors, collectors and sinks |
| [Catalogs and locale policies](i18n-catalogs.md) | Data formats, plural selection, number and date formatting |
| [History](history.md) | Branches, checkpoints, retroactive edits, merge and serialization |
| [Quantum algorithm references](quantum-reference.md) | Classical simulation behavior, budgets and costs |

Each header is the reference for its operations and preconditions. The general
guides explain shared conventions; a module's more specific contract takes
precedence.
