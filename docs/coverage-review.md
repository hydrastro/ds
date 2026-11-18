# Coverage reconciliation

The full [DADS A-Z index](https://xlinux.nist.gov/dads/terms.html) contains 1,306
entries matching this repository's 1,306 indexed rows after normalizing
apostrophes and displayed mathematical subscripts/superscripts. No indexed term
is absent and no extra term appears in that section. Supplemental rows are
separate. All 183 explicit “see” aliases now inherit the scope of their target.
This resolves the older 115-entry uncertainty about the truncated index tail.

That is an inventory reconciliation, not proof that each implementation matches
its reference. The [index by type](https://xlinux.nist.gov/dads/termsType.html)
was also compared with rows marked `n/a`. Its own type labels are not enough:
for example, its “definition” category includes computational problems, while
broad families and undecidable problems do not imply a missing concrete routine.

| Corrected scope | Evidence and disposition |
|---|---|
| Boolean circuit value | Added graph-view evaluation with custom gates in `algo/logic_graph` |
| Hypergraph | Added stable hyperedge handles, caller-selected lookup storage and an incidence graph view |
| Multi-commodity flow | Added fractional/integer shared-capacity network formulations over selectable solvers; integer flows may split across paths |
| Karnaugh map / KV / Veitch | Added Gray-code layout, don’t-care grouping and cover minimization for up to six variables, with explicit search budget |
| hsadelta | Added block-hash suffix indexing, quick filter, replaceable map/hash/builder and verified copy/insert scripts |
| Jelly-fish | Added a theoretical-shape reference with balanced binary body, linked tentacles and atomic rebuilds; optional ordered body backend |
| Path-system problem | Added a wrapper over the reusable Horn closure engine |
| Four quantum algorithms | Added bounded classical references; exponential storage and algorithm branches documented |
| Parallel prefix | Added executor-driven Hillis-Steele and Blelloch operations, preserving monoid order |
| Planarization | Existing greedy maximal planar subgraph is a variant; it does not minimize removed edges or insert crossing vertices |
| Universal B-tree | Restricted coordinate representation; deletion may leave underfull nodes, so height is not bounded by current live count alone |
| Topology tree | Explicit maximum-degree-3 forest contract |
| PLOP hashing | Real spatial grid expansion; modeled in-memory bucket pages, not pager-backed storage |
| q sort / qm sort | Distinct implementations; qm is randomized pair compare/exchange, not a quicksort preset |
| Spectral test | Supplemental entry is restricted to the documented dimension/modulus range |
| Pile | Actual ordered-deque representation exists internally in UnShuffle sort; no standalone public container |
| Level-order traversal | Breadth-first graph traversal provides it for tree inputs |
| Graph drawing | Planar straight-line drawing exists; general drawing optimizers are outside that implementation |

The earlier “every term implemented or definition, nothing missing” statement
was false when made. Six concrete missing capabilities were identified and are
now delivered: circuit/path-system repairs are listed separately above, while
the six gaps were hypergraph, both multi-commodity formulations, Karnaugh maps,
hsadelta and jelly-fish. Karnaugh aliases inherit its bounded scope, and the
theoretical jelly-fish is explicitly a reference implementation.

No currently identified indexed capability remains classified `missing`.
Restricted, reference and variant labels remain explicit. No placeholder or
unimplemented proposed interface is counted as delivered. The inventory and
type comparisons do not constitute a complete correctness audit of every
algorithm body; the review's evidence consists of inspected source contracts,
primary definitions for disputed mappings, strict compilation, static analysis
and examples for changed implementations.
