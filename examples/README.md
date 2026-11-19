# Examples

Each C file is a complete program. Build them with `make examples`, then run a
binary from `build/examples/`. They link to the same library as an application.
The source is deliberately small enough to read alongside its public headers.

## Starting points

| Example | Demonstrates |
|---|---|
| [arena_text.c](arena_text.c) | Unicode normalization using a fixed arena, including scratch buffers. |
| [getting_started.c](getting_started.c) | A growable vector, ordering and cleanup. |
| [intrusive_list.c](intrusive_list.c) | Links embedded in application records. |
| [container_contexts.c](container_contexts.c) | One explicit allocation owner across container families. |
| [intrusive_sequences.c](intrusive_sequences.c) | Callbacks, traversal and owned clones for intrusive sequences. |
| [hash_table_config.c](hash_table_config.c) | Hashing, equality and ownership policies. |
| [composed_callbacks.c](composed_callbacks.c) | Nullable hash/trie values and callback policies. |
| [graph_queues.c](graph_queues.c) | Graph algorithms with different priority-queue backends. |
| [meld_queues.c](meld_queues.c) | Memory/order policies and stable handles during meld. |
| [result_validation.c](result_validation.c) | Independent validation, typed payloads and selectable diagnostic logs. |
| [result_ownership.c](result_ownership.c) | Shared subviews and preserving output on allocation failure. |
| [module_diagnostics.c](module_diagnostics.c) | Typed module diagnostics, bounded buffers and explicit truncation. |
| [error_causes.c](error_causes.c) | Owned native errors and preserved causes in a Result. |
| [result_i18n.c](result_i18n.c) | Owned domain Results, accumulated diagnostics and rendering. |
| [localized_catalog.c](localized_catalog.c) | Catalog import/export and explicit locale policies. |
| [versioned_routes.c](versioned_routes.c) | Persistent trie versions with byte and radix backends. |
| [history_catalog.c](history_catalog.c) | Keyed operation merge and portable file archives. |
| [paged_catalog.c](paged_catalog.c) | A page-backed LSM tree with block reads. |
| [sstable_reader.c](sstable_reader.c) | SSTable callbacks and independent block iterators. |
| [interference_patterns.c](interference_patterns.c) | Bounded classical quantum-algorithm simulations. |

## File examples

Choose output paths you are happy for these demonstrations to create or replace:

```sh
./build/examples/sstable_reader build/example.sst
./build/examples/paged_catalog build/example.pages
./build/examples/history_catalog build/example.history
./build/examples/localized_catalog build/example.tsv
```

`localized_catalog` also runs without a path. See each program's usage message
for its arguments. The other examples run without command-line arguments.

## All programs

| Program | Program | Program |
|---|---|---|
| [addressable_schedulers](addressable_schedulers.c) | [allocators](allocators.c) | [alternation](alternation.c) |
| [art_dictionary](art_dictionary.c) | [avl_set](avl_set.c) | [binary_key_maps](binary_key_maps.c) |
| [bounded_ranges](bounded_ranges.c) | [byte_visitors](byte_visitors.c) | [composed_callbacks](composed_callbacks.c) |
| [composed_ranges](composed_ranges.c) | [constrained_mesh](constrained_mesh.c) | [container_contexts](container_contexts.c) |
| [controller_map](controller_map.c) | [crash_recovery](crash_recovery.c) | [dense_graph](dense_graph.c) |
| [deployment_rules](deployment_rules.c) | [durable_log](durable_log.c) | [dynamic_clusters](dynamic_clusters.c) |
| [dynamic_graph](dynamic_graph.c) | [evolving_network](evolving_network.c) | [executor_prefixes](executor_prefixes.c) |
| [failing_links](failing_links.c) | [filter_sizing](filter_sizing.c) | [filtered_tables](filtered_tables.c) |
| [finger_tree](finger_tree.c) | [forest_actions](forest_actions.c) | [getting_started](getting_started.c) |
| [graph_bounds](graph_bounds.c) | [graph_queues](graph_queues.c) | [hash_table_config](hash_table_config.c) |
| [heap_priority_queue](heap_priority_queue.c) | [history_catalog](history_catalog.c) | [integer_recurrences](integer_recurrences.c) |
| [interference_patterns](interference_patterns.c) | [intrusive_indexes](intrusive_indexes.c) | [intrusive_list](intrusive_list.c) |
| [intrusive_policies](intrusive_policies.c) | [intrusive_sequences](intrusive_sequences.c) | [jellyfish_body](jellyfish_body.c) |
| [key_agreement](key_agreement.c) | [localized_catalog](localized_catalog.c) | [matching_policy](matching_policy.c) |
| [meld_queues](meld_queues.c) | [nearest_regions](nearest_regions.c) | [ordered_policies](ordered_policies.c) |
| [owned_buffers](owned_buffers.c) | [owned_elements](owned_elements.c) | [owned_list_clone](owned_list_clone.c) |
| [paged_catalog](paged_catalog.c) | [parallel_rounds](parallel_rounds.c) | [persistent_hamt](persistent_hamt.c) |
| [persistent_sequences](persistent_sequences.c) | [persistent_trie](persistent_trie.c) | [planar_graphs](planar_graphs.c) |
| [predecessor_buckets](predecessor_buckets.c) | [ranked_search](ranked_search.c) | [relaxed_balance](relaxed_balance.c) |
| [release_delta](release_delta.c) | [repetitive_text](repetitive_text.c) | [result_i18n](result_i18n.c) |
| [search_index](search_index.c) | [sequence_containers](sequence_containers.c) | [sequence_policies](sequence_policies.c) |
| [set_families](set_families.c) | [shared_projects](shared_projects.c) | [shared_routes](shared_routes.c) |
| [signed_datasets](signed_datasets.c) | [sstable_reader](sstable_reader.c) | [storage_stack](storage_stack.c) |
| [string_map](string_map.c) | [succinct_structures](succinct_structures.c) | [succinct_trees](succinct_trees.c) |
| [text_editing](text_editing.c) | [text_index](text_index.c) | [versioned_routes](versioned_routes.c) |
| [word_sets](word_sets.c) | [worst_case_heap](worst_case_heap.c) |  |
