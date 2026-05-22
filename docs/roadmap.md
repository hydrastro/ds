# Roadmap

Implemented foundation:

- status/error/diagnostic/context layer
- config-based hash table and trie constructors
- `ds_`-prefixed APIs for hash table/trie additions
- iterator abstraction
- string map/string set wrappers
- generic graph module
- Tier-1 persistent trie
- Tier-1 persistent red-black tree wrapper
- history transaction batching and operation export hook
- history oracle randomized test
- `make sanitize` and `make test-safe`

Future improvements:

- migrate every legacy API to `ds_status_t`
- convert all complex constructors to config structs
- add richer graph algorithms
- add structural-sharing persistent RBT
- add binary serializers for history payloads
- add arena/pool/debug allocators through `ds_context_t`
