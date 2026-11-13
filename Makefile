CC ?= cc
CXX ?= c++
AR ?= ar
VALGRIND ?= valgrind
CLANG_FORMAT ?= clang-format
CPPCHECK ?= cppcheck

CFLAGS = -std=c89 -Wall -Wextra -Werror -pedantic -pedantic-errors \
         -Waggregate-return -Wbad-function-cast -Wcast-align -Wcast-qual \
         -Wdeclaration-after-statement -Wfloat-equal -Wlogical-op \
         -Wmissing-declarations -Wmissing-include-dirs -Wmissing-prototypes \
         -Wnested-externs -Wpointer-arith -Wredundant-decls -Wsequence-point \
         -Wstrict-prototypes -Wswitch -Wundef -Wunreachable-code \
         -Wwrite-strings -Wconversion \
         -O2 -fPIC
TEST_CFLAGS = -std=c99 -Wall -Wextra -Werror -pedantic -O2
# header dependency tracking: each object also writes a .d file listing the
# headers it includes, so editing a header rebuilds its users
DEPFLAGS = -MMD -MP
# the expansion arithmetic of algo/robust_predicates needs unfused
# floating-point operations on every compiler configuration
FP_STRICT_CFLAGS = -ffp-contract=off
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2
LDFLAGS = -shared
LDLIBS = -lm
ARFLAGS = rcs
VALGRIND_FLAGS ?= --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=definite,possible --error-exitcode=99 --track-origins=yes

PREFIX ?= /usr/local
INCLUDEDIR ?= $(PREFIX)/include
LIBDIR ?= $(PREFIX)/lib
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig
INSTALL ?= install
# headers live in a directory of their own: $(INCLUDEDIR)/ds/{lib,algo}
DS_INCLUDEDIR = $(INCLUDEDIR)/ds

LIB_DIR   := lib
GEN_DIR   := gen
TEST_DIR  := test
BUILD_DIR := build
DATA_DIR  := data
EXAMPLE_DIR := examples
ALGO_DIR  := algo
TEST_BUILD_DIR := $(BUILD_DIR)/test
INCLUDES  := -I. -I$(LIB_DIR) -I$(ALGO_DIR) -I$(BUILD_DIR)

LIB_SRC := \
  $(LIB_DIR)/allocators.c \
  $(LIB_DIR)/avl.c \
  $(LIB_DIR)/bst.c \
  $(LIB_DIR)/btree.c \
  $(LIB_DIR)/aho_corasick.c \
  $(LIB_DIR)/bitset.c \
  $(LIB_DIR)/bloom_filter.c \
  $(LIB_DIR)/bplus_tree.c \
  $(LIB_DIR)/circular_buffer.c \
  $(LIB_DIR)/deque.c \
  $(LIB_DIR)/disjoint_set.c \
  $(LIB_DIR)/dlist.c \
  $(LIB_DIR)/fenwick_tree.c \
  $(LIB_DIR)/hash_table.c \
  $(LIB_DIR)/heap.c \
  $(LIB_DIR)/history.c \
  $(LIB_DIR)/list.c \
  $(LIB_DIR)/context.c \
  $(LIB_DIR)/diagnostic.c \
  $(LIB_DIR)/error.c \
  $(LIB_DIR)/graph.c \
  $(LIB_DIR)/iter.c \
  $(LIB_DIR)/pairing_heap.c \
  $(LIB_DIR)/persistent_rbt.c \
  $(LIB_DIR)/persistent_trie.c \
  $(LIB_DIR)/queue.c \
  $(LIB_DIR)/rbt.c \
  $(LIB_DIR)/radix_trie.c \
  $(LIB_DIR)/ring_list.c \
  $(LIB_DIR)/skip_list.c \
  $(LIB_DIR)/splay_tree.c \
  $(LIB_DIR)/stack.c \
  $(LIB_DIR)/status.c \
  $(LIB_DIR)/string_map.c \
  $(LIB_DIR)/str.c \
  $(LIB_DIR)/str_algo.c \
  $(LIB_DIR)/str_io.c \
  $(LIB_DIR)/str_io_posix.c \
  $(LIB_DIR)/str_unicode.c \
  $(LIB_DIR)/treap.c \
  $(LIB_DIR)/trie.c \
  $(LIB_DIR)/art.c \
  $(LIB_DIR)/bitvector.c \
  $(LIB_DIR)/fibonacci_heap.c \
  $(LIB_DIR)/hamt.c \
  $(LIB_DIR)/order_statistic_tree.c \
  $(LIB_DIR)/rope.c \
  $(LIB_DIR)/segment_tree.c \
  $(LIB_DIR)/wavelet_tree.c \
  $(LIB_DIR)/unicode_runtime.c \
  $(LIB_DIR)/vec.c \
  $(LIB_DIR)/array_deque.c \
  $(LIB_DIR)/sparse_set.c \
  $(LIB_DIR)/slot_map.c \
  $(LIB_DIR)/indexed_heap.c \
  $(LIB_DIR)/min_max_heap.c \
  $(LIB_DIR)/sparse_table.c \
  $(LIB_DIR)/rollback_union_find.c \
  $(LIB_DIR)/timer_wheel.c \
  $(LIB_DIR)/packed_array.c \
  $(LIB_DIR)/rh_map.c \
  $(LIB_DIR)/lru_cache.c \
  $(LIB_DIR)/hyperloglog.c \
  $(LIB_DIR)/count_min_sketch.c \
  $(LIB_DIR)/cuckoo_filter.c \
  $(LIB_DIR)/bip_buffer.c \
  $(LIB_DIR)/gap_buffer.c \
  $(LIB_DIR)/sieve_cache.c \
  $(LIB_DIR)/monotonic_queue.c \
  $(LIB_DIR)/critbit_tree.c \
  $(LIB_DIR)/kd_tree.c \
  $(LIB_DIR)/trie_stores.c \
  $(LIB_DIR)/implicit_treap.c \
  $(LIB_DIR)/aug_treap.c \
  $(LIB_DIR)/interval_tree.c \
  $(LIB_DIR)/binomial_heap.c \
  $(LIB_DIR)/meldable_heap.c \
  $(LIB_DIR)/weak_heap.c \
  $(LIB_DIR)/veb_tree.c \
  $(LIB_DIR)/radix_heap.c \
  $(LIB_DIR)/calendar_queue.c \
  $(LIB_DIR)/swiss_map.c \
  $(LIB_DIR)/open_map.c \
  $(LIB_DIR)/cuckoo_map.c \
  $(LIB_DIR)/coalesced_map.c \
  $(LIB_DIR)/linear_hash_map.c \
  $(LIB_DIR)/extendible_hash.c \
  $(LIB_DIR)/iblt.c \
  $(LIB_DIR)/merkle_tree.c \
  $(LIB_DIR)/inversion_list.c \
  $(LIB_DIR)/shadow_heap.c \
  $(LIB_DIR)/automaton.c \
  $(LIB_DIR)/suffix_automaton.c \
  $(LIB_DIR)/suffix_tree.c \
  $(LIB_DIR)/bdd.c \
  $(LIB_DIR)/self_org_list.c \
  $(LIB_DIR)/sorted_list.c \
  $(LIB_DIR)/binary_tree_repr.c \
  $(LIB_DIR)/threaded_tree.c \
  $(LIB_DIR)/wb_tree.c \
  $(LIB_DIR)/rbst.c \
  $(LIB_DIR)/scapegoat_tree.c \
  $(LIB_DIR)/multiset.c \
  $(LIB_DIR)/association_list.c \
  $(LIB_DIR)/cactus_stack.c \
  $(LIB_DIR)/diet.c \
  $(LIB_DIR)/tst.c \
  $(LIB_DIR)/two_choice_map.c \
  $(LIB_DIR)/sparse_matrix.c \
  $(LIB_DIR)/elastic_trie.c \
  $(LIB_DIR)/dynamic_hash.c \
  $(LIB_DIR)/mway_btree.c \
  $(LIB_DIR)/pagoda.c \
  $(LIB_DIR)/buddy_allocator.c \
  $(LIB_DIR)/hash_heap.c \
  $(LIB_DIR)/enfilade.c \
  $(LIB_DIR)/signature_file.c \
  $(LIB_DIR)/quadtree.c \
  $(LIB_DIR)/rtree.c \
  $(LIB_DIR)/kdb_tree.c \
  $(LIB_DIR)/grid_file.c \
  $(LIB_DIR)/bang_file.c \
  $(LIB_DIR)/hb_tree.c \
  $(LIB_DIR)/bsp_tree.c \
  $(LIB_DIR)/p_tree.c \
  $(LIB_DIR)/d_tree.c \
  $(LIB_DIR)/ub_tree.c \
  $(LIB_DIR)/k2_tree.c \
  $(LIB_DIR)/jump_list.c \
  $(LIB_DIR)/ld_signature.c \
  $(LIB_DIR)/spiral_storage.c \
  $(LIB_DIR)/sstable.c \
  $(LIB_DIR)/zipper.c \
  $(LIB_DIR)/cellular_automaton.c \
  $(LIB_DIR)/turing_machine.c \
  $(LIB_DIR)/pushdown.c \
  $(LIB_DIR)/tree_automaton.c \
  $(LIB_DIR)/inverted_index.c \
  $(LIB_DIR)/plop_hash.c \
  $(LIB_DIR)/confluent_list.c \
  $(LIB_DIR)/topology_tree.c \
  $(LIB_DIR)/checked.c \
  $(LIB_DIR)/map_iface.c \
  $(LIB_DIR)/xfast_trie.c \
  $(LIB_DIR)/yfast_trie.c \
  $(LIB_DIR)/link_cut_tree.c \
  $(LIB_DIR)/lazy_segment_tree.c \
  $(LIB_DIR)/elias_fano.c \
  $(LIB_DIR)/roaring.c \
  $(LIB_DIR)/persistent_hamt.c \
  $(LIB_DIR)/pq_iface.c \
  $(LIB_DIR)/fm_index.c \
  $(LIB_DIR)/euler_tour_tree.c \
  $(LIB_DIR)/persistent_vector.c \
  $(LIB_DIR)/ordered_iface.c \
  $(LIB_DIR)/rank_select.c \
  $(LIB_DIR)/wavelet_matrix.c \
  $(LIB_DIR)/finger_tree.c \
  $(LIB_DIR)/pager.c \
  $(LIB_DIR)/lsm_tree.c \
  $(LIB_DIR)/paged_btree.c \
  $(LIB_DIR)/replacement.c \
  $(LIB_DIR)/quotient_filter.c \
  $(LIB_DIR)/xor_filter.c \
  $(LIB_DIR)/amq_iface.c \
  $(LIB_DIR)/adj_matrix.c \
  $(LIB_DIR)/schema.c \
  $(LIB_DIR)/hollow_heap.c \
  $(LIB_DIR)/louds.c \
  $(LIB_DIR)/zdd.c \
  $(LIB_DIR)/rank_pairing_heap.c \
  $(LIB_DIR)/bp_tree.c \
  $(LIB_DIR)/dfuds.c \
  $(LIB_DIR)/dynamic_connectivity.c \
  $(LIB_DIR)/rrb_vector.c \
  $(LIB_DIR)/chromatic_tree.c \
  $(LIB_DIR)/suffix_array.c

# Algorithms live in algo/ and are built into the same library.
ALGO_SRC := \
  $(ALGO_DIR)/elem.c \
  $(ALGO_DIR)/sort.c \
  $(ALGO_DIR)/search.c \
  $(ALGO_DIR)/sequence.c \
  $(ALGO_DIR)/hash.c \
  $(ALGO_DIR)/bits.c \
  $(ALGO_DIR)/random.c \
  $(ALGO_DIR)/numeric.c \
  $(ALGO_DIR)/graph_view.c \
  $(ALGO_DIR)/sort_classic.c \
  $(ALGO_DIR)/stringology.c \
  $(ALGO_DIR)/hash_family.c \
  $(ALGO_DIR)/perfect_hash.c \
  $(ALGO_DIR)/lsh.c \
  $(ALGO_DIR)/flow.c \
  $(ALGO_DIR)/matching.c \
  $(ALGO_DIR)/graph_struct.c \
  $(ALGO_DIR)/graph_path.c \
  $(ALGO_DIR)/geometry.c \
  $(ALGO_DIR)/combinatorics.c \
  $(ALGO_DIR)/knapsack.c \
  $(ALGO_DIR)/number_theory.c \
  $(ALGO_DIR)/numerical.c \
  $(ALGO_DIR)/string_search.c \
  $(ALGO_DIR)/kway_merge.c \
  $(ALGO_DIR)/scan.c \
  $(ALGO_DIR)/select.c \
  $(ALGO_DIR)/sort_misc.c \
  $(ALGO_DIR)/sort_dist.c \
  $(ALGO_DIR)/sort_heap.c \
  $(ALGO_DIR)/external_sort.c \
  $(ALGO_DIR)/string_approx.c \
  $(ALGO_DIR)/prefix_code.c \
  $(ALGO_DIR)/compress.c \
  $(ALGO_DIR)/adaptive_huffman.c \
  $(ALGO_DIR)/phonetic.c \
  $(ALGO_DIR)/cartesian_tree.c \
  $(ALGO_DIR)/schorr_waite.c \
  $(ALGO_DIR)/game.c \
  $(ALGO_DIR)/fft.c \
  $(ALGO_DIR)/matrix.c \
  $(ALGO_DIR)/hmm.c \
  $(ALGO_DIR)/parallel_sim.c \
  $(ALGO_DIR)/lca.c \
  $(ALGO_DIR)/graph_conn.c \
  $(ALGO_DIR)/graph_np.c \
  $(ALGO_DIR)/tsp.c \
  $(ALGO_DIR)/facility.c \
  $(ALGO_DIR)/steiner.c \
  $(ALGO_DIR)/lp.c \
  $(ALGO_DIR)/lattice.c \
  $(ALGO_DIR)/rice_integral.c \
  $(ALGO_DIR)/geo_opt.c \
  $(ALGO_DIR)/space_order.c \
  $(ALGO_DIR)/markov.c \
  $(ALGO_DIR)/anneal.c \
  $(ALGO_DIR)/model_check.c \
  $(ALGO_DIR)/tree_edit.c \
  $(ALGO_DIR)/visibility.c \
  $(ALGO_DIR)/dynamization.c \
  $(ALGO_DIR)/weighted_matching.c \
  $(ALGO_DIR)/robust_predicates.c \
  $(ALGO_DIR)/delaunay.c \
  $(ALGO_DIR)/voronoi.c \
  $(ALGO_DIR)/planarity.c

LIB_SRC += $(ALGO_SRC)

GEN_SRC_C := $(BUILD_DIR)/unicode_tables.c
GEN_SRC_H := $(BUILD_DIR)/unicode_tables.h

# Sources maintained to the canonical clang-format style. `make format` and
# `make format-check` operate on these. The wider legacy tree can be formatted
# explicitly with `make format-all` when desired.
FORMAT_SRC := \
  $(LIB_DIR)/suffix_array.c $(LIB_DIR)/suffix_array.h \
  $(LIB_DIR)/chromatic_tree.c $(LIB_DIR)/chromatic_tree.h \
  $(LIB_DIR)/rrb_vector.c $(LIB_DIR)/rrb_vector.h \
  $(LIB_DIR)/dynamic_connectivity.c $(LIB_DIR)/dynamic_connectivity.h \
  $(LIB_DIR)/dfuds.c $(LIB_DIR)/dfuds.h \
  $(LIB_DIR)/bp_tree.c $(LIB_DIR)/bp_tree.h \
  $(LIB_DIR)/rank_pairing_heap.c $(LIB_DIR)/rank_pairing_heap.h \
  $(LIB_DIR)/zdd.c $(LIB_DIR)/zdd.h \
  $(LIB_DIR)/louds.c $(LIB_DIR)/louds.h \
  $(LIB_DIR)/hollow_heap.c $(LIB_DIR)/hollow_heap.h \
  $(LIB_DIR)/schema.c $(LIB_DIR)/schema.h \
  $(LIB_DIR)/adj_matrix.c $(LIB_DIR)/adj_matrix.h \
  $(LIB_DIR)/amq_iface.c $(LIB_DIR)/amq_iface.h \
  $(LIB_DIR)/xor_filter.c $(LIB_DIR)/xor_filter.h \
  $(LIB_DIR)/quotient_filter.c $(LIB_DIR)/quotient_filter.h \
  $(LIB_DIR)/replacement.c $(LIB_DIR)/replacement.h \
  $(LIB_DIR)/paged_btree.c $(LIB_DIR)/paged_btree.h \
  $(LIB_DIR)/lsm_tree.c $(LIB_DIR)/lsm_tree.h \
  $(LIB_DIR)/pager.c $(LIB_DIR)/pager.h \
  $(LIB_DIR)/finger_tree.c $(LIB_DIR)/finger_tree.h \
  $(LIB_DIR)/wavelet_matrix.c $(LIB_DIR)/wavelet_matrix.h \
  $(LIB_DIR)/rank_select.c $(LIB_DIR)/rank_select.h \
  $(LIB_DIR)/ordered_iface.c $(LIB_DIR)/ordered_iface.h \
  $(LIB_DIR)/persistent_vector.c $(LIB_DIR)/persistent_vector.h \
  $(LIB_DIR)/euler_tour_tree.c $(LIB_DIR)/euler_tour_tree.h \
  $(LIB_DIR)/fm_index.c $(LIB_DIR)/fm_index.h \
  $(LIB_DIR)/pq_iface.c $(LIB_DIR)/pq_iface.h \
  $(LIB_DIR)/persistent_hamt.c $(LIB_DIR)/persistent_hamt.h \
  $(LIB_DIR)/roaring.c $(LIB_DIR)/roaring.h \
  $(LIB_DIR)/elias_fano.c $(LIB_DIR)/elias_fano.h \
  $(LIB_DIR)/lazy_segment_tree.c $(LIB_DIR)/lazy_segment_tree.h \
  $(LIB_DIR)/link_cut_tree.c $(LIB_DIR)/link_cut_tree.h \
  $(LIB_DIR)/yfast_trie.c $(LIB_DIR)/yfast_trie.h \
  $(LIB_DIR)/xfast_trie.c $(LIB_DIR)/xfast_trie.h \
  $(LIB_DIR)/map_iface.c $(LIB_DIR)/map_iface.h \
  $(LIB_DIR)/checked.c $(LIB_DIR)/checked.h \
  $(LIB_DIR)/topology_tree.c $(LIB_DIR)/topology_tree.h \
  $(LIB_DIR)/confluent_list.c $(LIB_DIR)/confluent_list.h \
  $(LIB_DIR)/plop_hash.c $(LIB_DIR)/plop_hash.h \
  $(LIB_DIR)/inverted_index.c $(LIB_DIR)/inverted_index.h \
  $(LIB_DIR)/tree_automaton.c $(LIB_DIR)/tree_automaton.h \
  $(LIB_DIR)/pushdown.c $(LIB_DIR)/pushdown.h \
  $(LIB_DIR)/turing_machine.c $(LIB_DIR)/turing_machine.h \
  $(LIB_DIR)/cellular_automaton.c $(LIB_DIR)/cellular_automaton.h \
  $(LIB_DIR)/zipper.c $(LIB_DIR)/zipper.h \
  $(LIB_DIR)/sstable.c $(LIB_DIR)/sstable.h \
  $(LIB_DIR)/spiral_storage.c $(LIB_DIR)/spiral_storage.h \
  $(LIB_DIR)/ld_signature.c $(LIB_DIR)/ld_signature.h \
  $(LIB_DIR)/jump_list.c $(LIB_DIR)/jump_list.h \
  $(LIB_DIR)/k2_tree.c $(LIB_DIR)/k2_tree.h \
  $(LIB_DIR)/ub_tree.c $(LIB_DIR)/ub_tree.h \
  $(LIB_DIR)/d_tree.c $(LIB_DIR)/d_tree.h \
  $(LIB_DIR)/p_tree.c $(LIB_DIR)/p_tree.h \
  $(LIB_DIR)/bsp_tree.c $(LIB_DIR)/bsp_tree.h \
  $(LIB_DIR)/hb_tree.c $(LIB_DIR)/hb_tree.h \
  $(LIB_DIR)/bang_file.c $(LIB_DIR)/bang_file.h \
  $(LIB_DIR)/grid_file.c $(LIB_DIR)/grid_file.h \
  $(LIB_DIR)/kdb_tree.c $(LIB_DIR)/kdb_tree.h \
  $(LIB_DIR)/rtree.c $(LIB_DIR)/rtree.h \
  $(LIB_DIR)/quadtree.c $(LIB_DIR)/quadtree.h \
  $(LIB_DIR)/signature_file.c $(LIB_DIR)/signature_file.h \
  $(LIB_DIR)/enfilade.c $(LIB_DIR)/enfilade.h \
  $(LIB_DIR)/hash_heap.c $(LIB_DIR)/hash_heap.h \
  $(LIB_DIR)/buddy_allocator.c $(LIB_DIR)/buddy_allocator.h \
  $(LIB_DIR)/pagoda.c $(LIB_DIR)/pagoda.h \
  $(LIB_DIR)/mway_btree.c $(LIB_DIR)/mway_btree.h \
  $(LIB_DIR)/dynamic_hash.c $(LIB_DIR)/dynamic_hash.h \
  $(LIB_DIR)/elastic_trie.c $(LIB_DIR)/elastic_trie.h \
  $(LIB_DIR)/sparse_matrix.c $(LIB_DIR)/sparse_matrix.h \
  $(LIB_DIR)/two_choice_map.c $(LIB_DIR)/two_choice_map.h \
  $(LIB_DIR)/tst.c $(LIB_DIR)/tst.h \
  $(LIB_DIR)/diet.c $(LIB_DIR)/diet.h \
  $(LIB_DIR)/cactus_stack.c $(LIB_DIR)/cactus_stack.h \
  $(LIB_DIR)/association_list.c $(LIB_DIR)/association_list.h \
  $(LIB_DIR)/multiset.c $(LIB_DIR)/multiset.h \
  $(LIB_DIR)/scapegoat_tree.c $(LIB_DIR)/scapegoat_tree.h \
  $(LIB_DIR)/rbst.c $(LIB_DIR)/rbst.h \
  $(LIB_DIR)/wb_tree.c $(LIB_DIR)/wb_tree.h \
  $(LIB_DIR)/threaded_tree.c $(LIB_DIR)/threaded_tree.h \
  $(LIB_DIR)/binary_tree_repr.c $(LIB_DIR)/binary_tree_repr.h \
  $(LIB_DIR)/sorted_list.c $(LIB_DIR)/sorted_list.h \
  $(LIB_DIR)/self_org_list.c $(LIB_DIR)/self_org_list.h \
  $(LIB_DIR)/bdd.c $(LIB_DIR)/bdd.h \
  $(LIB_DIR)/suffix_tree.c $(LIB_DIR)/suffix_tree.h \
  $(LIB_DIR)/suffix_automaton.c $(LIB_DIR)/suffix_automaton.h \
  $(LIB_DIR)/automaton.c $(LIB_DIR)/automaton.h \
  $(LIB_DIR)/shadow_heap.c $(LIB_DIR)/shadow_heap.h \
  $(LIB_DIR)/inversion_list.c $(LIB_DIR)/inversion_list.h \
  $(LIB_DIR)/merkle_tree.c $(LIB_DIR)/merkle_tree.h \
  $(LIB_DIR)/iblt.c $(LIB_DIR)/iblt.h \
  $(LIB_DIR)/extendible_hash.c $(LIB_DIR)/extendible_hash.h \
  $(LIB_DIR)/linear_hash_map.c $(LIB_DIR)/linear_hash_map.h \
  $(LIB_DIR)/coalesced_map.c $(LIB_DIR)/coalesced_map.h \
  $(LIB_DIR)/cuckoo_map.c $(LIB_DIR)/cuckoo_map.h \
  $(LIB_DIR)/open_map.c $(LIB_DIR)/open_map.h \
  $(LIB_DIR)/swiss_map.c $(LIB_DIR)/swiss_map.h \
  $(LIB_DIR)/calendar_queue.c $(LIB_DIR)/calendar_queue.h \
  $(LIB_DIR)/radix_heap.c $(LIB_DIR)/radix_heap.h \
  $(LIB_DIR)/veb_tree.c $(LIB_DIR)/veb_tree.h \
  $(LIB_DIR)/weak_heap.c $(LIB_DIR)/weak_heap.h \
  $(LIB_DIR)/meldable_heap.c $(LIB_DIR)/meldable_heap.h \
  $(LIB_DIR)/binomial_heap.c $(LIB_DIR)/binomial_heap.h \
  $(LIB_DIR)/art.c $(LIB_DIR)/art.h \
  $(LIB_DIR)/bitvector.c $(LIB_DIR)/bitvector.h \
  $(LIB_DIR)/fibonacci_heap.c $(LIB_DIR)/fibonacci_heap.h \
  $(LIB_DIR)/hamt.c $(LIB_DIR)/hamt.h \
  $(LIB_DIR)/order_statistic_tree.c $(LIB_DIR)/order_statistic_tree.h \
  $(LIB_DIR)/rope.c $(LIB_DIR)/rope.h \
  $(LIB_DIR)/segment_tree.c $(LIB_DIR)/segment_tree.h \
  $(LIB_DIR)/wavelet_tree.c $(LIB_DIR)/wavelet_tree.h \
  $(LIB_DIR)/interval_tree.c $(LIB_DIR)/interval_tree.h \
  $(LIB_DIR)/aug_treap.c $(LIB_DIR)/aug_treap.h \
  $(LIB_DIR)/implicit_treap.c $(LIB_DIR)/implicit_treap.h \
  $(LIB_DIR)/trie_stores.c $(LIB_DIR)/trie_stores.h \
  $(LIB_DIR)/kd_tree.c $(LIB_DIR)/kd_tree.h \
  $(LIB_DIR)/critbit_tree.c $(LIB_DIR)/critbit_tree.h \
  $(LIB_DIR)/monotonic_queue.c $(LIB_DIR)/monotonic_queue.h \
  $(LIB_DIR)/sieve_cache.c $(LIB_DIR)/sieve_cache.h \
  $(LIB_DIR)/gap_buffer.c $(LIB_DIR)/gap_buffer.h \
  $(LIB_DIR)/bip_buffer.c $(LIB_DIR)/bip_buffer.h \
  $(LIB_DIR)/cuckoo_filter.c $(LIB_DIR)/cuckoo_filter.h \
  $(LIB_DIR)/count_min_sketch.c $(LIB_DIR)/count_min_sketch.h \
  $(LIB_DIR)/hyperloglog.c $(LIB_DIR)/hyperloglog.h \
  $(LIB_DIR)/lru_cache.c $(LIB_DIR)/lru_cache.h \
  $(LIB_DIR)/rh_map.c $(LIB_DIR)/rh_map.h \
  $(LIB_DIR)/packed_array.c $(LIB_DIR)/packed_array.h \
  $(LIB_DIR)/timer_wheel.c $(LIB_DIR)/timer_wheel.h \
  $(LIB_DIR)/rollback_union_find.c $(LIB_DIR)/rollback_union_find.h \
  $(LIB_DIR)/sparse_table.c $(LIB_DIR)/sparse_table.h \
  $(LIB_DIR)/min_max_heap.c $(LIB_DIR)/min_max_heap.h \
  $(LIB_DIR)/indexed_heap.c $(LIB_DIR)/indexed_heap.h \
  $(LIB_DIR)/slot_map.c $(LIB_DIR)/slot_map.h \
  $(LIB_DIR)/sparse_set.c $(LIB_DIR)/sparse_set.h \
  $(LIB_DIR)/array_deque.c $(LIB_DIR)/array_deque.h \
  $(LIB_DIR)/vec.c $(LIB_DIR)/vec.h \
  $(TEST_DIR)/art_test.c $(TEST_DIR)/bitvector_test.c \
  $(TEST_DIR)/fibonacci_heap_test.c $(TEST_DIR)/hamt_test.c \
  $(TEST_DIR)/order_statistic_tree_test.c $(TEST_DIR)/rope_test.c \
  $(TEST_DIR)/segment_tree_test.c $(TEST_DIR)/wavelet_tree_test.c \
  $(TEST_DIR)/graph_test.c $(TEST_DIR)/persistent_rbt_test.c \
  $(TEST_DIR)/persistent_trie_test.c \
  $(EXAMPLE_DIR)/intrusive_list.c $(EXAMPLE_DIR)/avl_set.c \
  $(EXAMPLE_DIR)/heap_priority_queue.c $(EXAMPLE_DIR)/art_dictionary.c \
  $(EXAMPLE_DIR)/sequence_containers.c $(EXAMPLE_DIR)/succinct_structures.c

FORMAT_SRC += ds_algo.h $(wildcard $(ALGO_DIR)/*.c) $(wildcard $(ALGO_DIR)/*.h)

# Every hand-written source and header, excluding generated tables.
FORMAT_ALL_SRC := ds.h \
  $(filter-out $(LIB_DIR)/unicode_tables.c $(LIB_DIR)/unicode_tables.h \
               $(LIB_DIR)/unicode_tables_glue.c, \
    $(wildcard $(LIB_DIR)/*.c) $(wildcard $(LIB_DIR)/*.h)) \
  $(wildcard $(TEST_DIR)/*.c) $(wildcard $(TEST_DIR)/*.cpp) \
  $(wildcard $(EXAMPLE_DIR)/*.c)

FORMAT_ALL_SRC += ds_algo.h $(wildcard $(ALGO_DIR)/*.c) $(wildcard $(ALGO_DIR)/*.h)

TEST_UCD_DIR := $(TEST_DIR)/ucd
TEST_UCD_FILES := \
  $(TEST_UCD_DIR)/UnicodeData.txt \
  $(TEST_UCD_DIR)/CaseFolding.txt \
  $(TEST_UCD_DIR)/CompositionExclusions.txt \
  $(TEST_UCD_DIR)/GraphemeBreakProperty.txt \
  $(TEST_UCD_DIR)/emoji-data.txt
TEST_GEN_SRC_C := $(TEST_BUILD_DIR)/unicode_tables.c
TEST_GEN_SRC_H := $(TEST_BUILD_DIR)/unicode_tables.h

LIB_OBJ      := $(LIB_SRC:.c=.o)
GEN_OBJ      := $(GEN_SRC_C:.c=.o)
TEST_GEN_OBJ := $(TEST_GEN_SRC_C:.c=.o)

LIB_STATIC      := libds.a
LIB_SHARED      := libds.so
TEST_LIB_STATIC := $(TEST_BUILD_DIR)/libds_test.a

TEST_SRC := $(wildcard $(TEST_DIR)/*_test.c)
TEST_BIN := $(patsubst $(TEST_DIR)/%.c,$(TEST_BUILD_DIR)/%,$(TEST_SRC))
EXAMPLE_BUILD_DIR := $(BUILD_DIR)/examples
EXAMPLE_SRC := $(wildcard $(EXAMPLE_DIR)/*.c)
EXAMPLE_BIN := $(patsubst $(EXAMPLE_DIR)/%.c,$(EXAMPLE_BUILD_DIR)/%,$(EXAMPLE_SRC))

GEN_BIN := $(BUILD_DIR)/gen_ucd
GEN_SRC := $(GEN_DIR)/gen_ucd.c

UCD_FILES := \
  $(DATA_DIR)/UnicodeData.txt \
  $(DATA_DIR)/CaseFolding.txt \
  $(DATA_DIR)/CompositionExclusions.txt \
  $(DATA_DIR)/GraphemeBreakProperty.txt \
  $(DATA_DIR)/emoji-data.txt

UCD_BASE := https://www.unicode.org/Public/UCD/latest/ucd
UCD_AUX  := $(UCD_BASE)/auxiliary
EMOJI_URL := https://www.unicode.org/Public/UCD/latest/ucd/emoji

.PHONY: all clean distclean unicode-data install uninstall test test-build check check-cpp sanitize examples valgrind valgrind-test valgrind-examples format format-all format-check lint lint-all lint-style coverage coverage-detail coverage-html summary
all: $(LIB_STATIC) $(LIB_SHARED)

# Install only into ds's own include directory and uninstall only the
# files installed here (never whole shared directories).
INSTALL_LIB_HEADERS = $(filter-out $(LIB_DIR)/unicode_tables%.h,$(wildcard $(LIB_DIR)/*.h))
INSTALL_ALGO_HEADERS = $(wildcard $(ALGO_DIR)/*.h)

install: all
	$(INSTALL) -d $(DESTDIR)$(DS_INCLUDEDIR)/lib $(DESTDIR)$(DS_INCLUDEDIR)/algo $(DESTDIR)$(LIBDIR) $(DESTDIR)$(PKGCONFIGDIR)
	$(INSTALL) -m 0644 ds.h ds_algo.h $(DESTDIR)$(DS_INCLUDEDIR)
	$(INSTALL) -m 0644 $(INSTALL_LIB_HEADERS) $(DESTDIR)$(DS_INCLUDEDIR)/lib
	$(INSTALL) -m 0644 $(INSTALL_ALGO_HEADERS) $(DESTDIR)$(DS_INCLUDEDIR)/algo
	$(INSTALL) -m 0644 $(LIB_STATIC) $(DESTDIR)$(LIBDIR)
	$(INSTALL) -m 0755 $(LIB_SHARED) $(DESTDIR)$(LIBDIR)
	printf 'prefix=%s\nincludedir=%s\nlibdir=%s\n\nName: ds\nDescription: data structures and algorithms library\nVersion: 0\nCflags: -I$${includedir}/ds -I$${includedir}/ds/lib -I$${includedir}/ds/algo\nLibs: -L$${libdir} -lds -lm\n' '$(PREFIX)' '$(INCLUDEDIR)' '$(LIBDIR)' > $(DESTDIR)$(PKGCONFIGDIR)/ds.pc

uninstall:
	rm -f $(DESTDIR)$(DS_INCLUDEDIR)/ds.h $(DESTDIR)$(DS_INCLUDEDIR)/ds_algo.h
	rm -f $(addprefix $(DESTDIR)$(DS_INCLUDEDIR)/lib/,$(notdir $(INSTALL_LIB_HEADERS)))
	rm -f $(addprefix $(DESTDIR)$(DS_INCLUDEDIR)/algo/,$(notdir $(INSTALL_ALGO_HEADERS)))
	-rmdir $(DESTDIR)$(DS_INCLUDEDIR)/lib $(DESTDIR)$(DS_INCLUDEDIR)/algo $(DESTDIR)$(DS_INCLUDEDIR) 2>/dev/null
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_STATIC)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_SHARED)
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/ds.pc

$(BUILD_DIR) $(DATA_DIR) $(TEST_BUILD_DIR) $(EXAMPLE_BUILD_DIR):
	@mkdir -p $@

$(DATA_DIR)/UnicodeData.txt: | $(DATA_DIR)
	@[ -f $@ ] || curl -fsSL -o $@ $(UCD_BASE)/UnicodeData.txt
$(DATA_DIR)/CaseFolding.txt: | $(DATA_DIR)
	@[ -f $@ ] || curl -fsSL -o $@ $(UCD_BASE)/CaseFolding.txt
$(DATA_DIR)/CompositionExclusions.txt: | $(DATA_DIR)
	@[ -f $@ ] || curl -fsSL -o $@ $(UCD_BASE)/CompositionExclusions.txt
$(DATA_DIR)/GraphemeBreakProperty.txt: | $(DATA_DIR)
	@[ -f $@ ] || curl -fsSL -o $@ $(UCD_AUX)/GraphemeBreakProperty.txt
$(DATA_DIR)/emoji-data.txt: | $(DATA_DIR)
	@[ -f $@ ] || curl -fsSL -o $@ $(EMOJI_URL)/emoji-data.txt

unicode-data: $(UCD_FILES)

$(GEN_BIN): $(GEN_SRC) | $(BUILD_DIR)
	$(CC) -std=c89 -Wall -Wextra -O2 $(INCLUDES) -o $@ $<

# The generator writes the .c and .h together. One stamp target runs it
# once (a plain two-target rule may run it twice in parallel builds); the
# outputs depend on the stamp and regenerate if either goes missing.
GEN_STAMP := $(BUILD_DIR)/unicode_tables.stamp
TEST_GEN_STAMP := $(TEST_BUILD_DIR)/unicode_tables.stamp

$(GEN_SRC_C) $(GEN_SRC_H): $(GEN_STAMP)
	@[ -f $@ ] || { rm -f $(GEN_STAMP); $(MAKE) $(GEN_STAMP); }

$(TEST_GEN_SRC_C) $(TEST_GEN_SRC_H): $(TEST_GEN_STAMP)
	@[ -f $@ ] || { rm -f $(TEST_GEN_STAMP); $(MAKE) $(TEST_GEN_STAMP); }

$(GEN_STAMP): $(GEN_BIN) $(UCD_FILES) | $(BUILD_DIR)
	$(GEN_BIN) \
	  --unicode-data $(DATA_DIR)/UnicodeData.txt \
	  --case-folding $(DATA_DIR)/CaseFolding.txt \
	  --comp-excl    $(DATA_DIR)/CompositionExclusions.txt \
	  --gb-prop      $(DATA_DIR)/GraphemeBreakProperty.txt \
	  --emoji-data   $(DATA_DIR)/emoji-data.txt \
	  --out-c        $(GEN_SRC_C) \
	  --out-h        $(GEN_SRC_H)
	@touch $@

$(TEST_GEN_STAMP): $(GEN_BIN) $(TEST_UCD_FILES) | $(TEST_BUILD_DIR)
	$(GEN_BIN) \
	  --unicode-data $(TEST_UCD_DIR)/UnicodeData.txt \
	  --case-folding $(TEST_UCD_DIR)/CaseFolding.txt \
	  --comp-excl    $(TEST_UCD_DIR)/CompositionExclusions.txt \
	  --gb-prop      $(TEST_UCD_DIR)/GraphemeBreakProperty.txt \
	  --emoji-data   $(TEST_UCD_DIR)/emoji-data.txt \
	  --out-c        $(TEST_GEN_SRC_C) \
	  --out-h        $(TEST_GEN_SRC_H)
	@touch $@

%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(INCLUDES) -c -o $@ $<

$(ALGO_DIR)/robust_predicates.o: $(ALGO_DIR)/robust_predicates.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(FP_STRICT_CFLAGS) $(DEPFLAGS) $(INCLUDES) -c -o $@ $<

-include $(LIB_OBJ:.o=.d)

$(GEN_OBJ): $(GEN_SRC_C) $(GEN_SRC_H)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(GEN_SRC_C)

$(TEST_GEN_OBJ): $(TEST_GEN_SRC_C) $(TEST_GEN_SRC_H)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(TEST_GEN_SRC_C)

$(LIB_STATIC): $(LIB_OBJ) $(GEN_OBJ)
	$(AR) $(ARFLAGS) $@ $^

$(LIB_SHARED): $(LIB_OBJ) $(GEN_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_LIB_STATIC): $(LIB_OBJ) $(TEST_GEN_OBJ) | $(TEST_BUILD_DIR)
	$(AR) $(ARFLAGS) $@ $^

$(TEST_BUILD_DIR)/%: $(TEST_DIR)/%.c $(TEST_LIB_STATIC) | $(TEST_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) $(INCLUDES) -o $@ $< $(TEST_LIB_STATIC) $(LDLIBS)

test-build: $(TEST_BIN)

test: $(TEST_BIN)
	@set -e; \
	for test_bin in $(TEST_BIN); do \
	  printf '\n== %s ==\n' "$$test_bin"; \
	  "$$test_bin"; \
	done

check: test

$(TEST_BUILD_DIR)/cpp_smoke: $(TEST_DIR)/cpp_smoke.cpp $(LIB_STATIC) | $(TEST_BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(LIB_STATIC) $(LDLIBS)

check-cpp: $(TEST_BUILD_DIR)/cpp_smoke
	$(TEST_BUILD_DIR)/cpp_smoke

# Line-coverage report. Builds an instrumented copy of the library, links and
# runs every test against it, then aggregates with gcovr.
# Requires gcovr (pip install gcovr).
COV_DIR := $(BUILD_DIR)/coverage
COV_FLAGS := -std=c99 -g -O0 --coverage $(INCLUDES)
COV_TEST_SRC := $(TEST_SRC)

coverage: $(GEN_SRC_C)
	@command -v gcovr >/dev/null 2>&1 || { \
	  echo "gcovr not found; install it with: pip install gcovr"; exit 1; }
	@rm -rf $(COV_DIR)
	@mkdir -p $(COV_DIR)
	@echo "Instrumenting library ($(words $(LIB_SRC)) sources)..."
	@for src in $(LIB_SRC) $(GEN_SRC_C); do \
	  $(CC) $(COV_FLAGS) -c $$src -o $(COV_DIR)/$$(basename $$src .c).o; \
	done
	@echo "Building and running $(words $(COV_TEST_SRC)) tests under coverage..."
	@set -e; for t in $(COV_TEST_SRC); do \
	  bin=$$(basename $$t .c); \
	  $(CC) $(COV_FLAGS) -o $(COV_DIR)/$$bin $$t $(COV_DIR)/*.o $(LDLIBS); \
	  ( cd $(COV_DIR) && ./$$bin >/dev/null 2>&1 ) \
	    || echo "  WARNING: $$bin exited nonzero"; \
	done
	@echo ""
	@echo "==================== COVERAGE SUMMARY ===================="
	@gcovr --root . --filter '$(LIB_DIR)/.*' \
	  --gcov-ignore-parse-errors=all --print-summary \
	  --object-directory $(COV_DIR) $(COV_DIR) 2>/dev/null | tail -3
	@echo "=========================================================="
	@echo "Per-file detail: make coverage-detail"

coverage-detail: coverage
	@gcovr --root . --filter '$(LIB_DIR)/.*' --gcov-ignore-parse-errors=all \
	  --sort uncovered-number --object-directory $(COV_DIR) $(COV_DIR) 2>/dev/null

coverage-html: coverage
	@gcovr --root . --filter '$(LIB_DIR)/.*' --gcov-ignore-parse-errors=all \
	  --html-details -o $(COV_DIR)/index.html \
	  --object-directory $(COV_DIR) $(COV_DIR) 2>/dev/null
	@echo "HTML report: $(COV_DIR)/index.html"

# One-shot health verdict: build, tests, examples, lint,
# format, valgrind, and coverage, each reported pass/fail, with a final line
# coverage figure. Intended as the single "is everything OK?" command.
summary:
	@echo "Running full health check (this takes a few minutes)..."
	@pass="PASS"; fail="FAIL"; status=0; \
	gate() { name="$$1"; shift; \
	  if "$$@" >/dev/null 2>&1; then printf "  %-22s %s\n" "$$name" "$$pass"; \
	  else printf "  %-22s %s\n" "$$name" "$$fail"; status=1; fi; }; \
	advisory() { name="$$1"; shift; \
	  if "$$@" >/dev/null 2>&1; then printf "  %-22s %s\n" "$$name" "$$pass"; \
	  else printf "  %-22s %s\n" "$$name" "WARN (advisory)"; fi; }; \
	echo "==================== ds health summary ===================="; \
	echo "  correctness gates (determine the verdict):"; \
	gate "build"               $(MAKE) all; \
	gate "unit tests"          $(MAKE) test; \
	gate "examples"            $(MAKE) examples; \
	if command -v $(VALGRIND) >/dev/null 2>&1; then \
	  gate "valgrind (tests)"  $(MAKE) valgrind-test; \
	else printf "  %-22s %s\n" "valgrind" "SKIP (not installed)"; fi; \
	echo "  style gates (advisory; tool-version sensitive):"; \
	if command -v $(CPPCHECK) >/dev/null 2>&1; then \
	  advisory "lint (cppcheck)"  $(MAKE) lint; \
	else printf "  %-22s %s\n" "lint (cppcheck)" "SKIP (not installed)"; fi; \
	if command -v $(CLANG_FORMAT) >/dev/null 2>&1; then \
	  advisory "format check"     $(MAKE) format-check; \
	else printf "  %-22s %s\n" "format check" "SKIP (not installed)"; fi; \
	echo "----------------------------------------------------------"; \
	if command -v gcovr >/dev/null 2>&1; then \
	  $(MAKE) coverage >/tmp/ds_cov.$$$$ 2>/dev/null; \
	  grep -E "^(lines|functions|branches):" /tmp/ds_cov.$$$$ | \
	    sed 's/^/  coverage /'; \
	  rm -f /tmp/ds_cov.$$$$; \
	else printf "  %-22s %s\n" "coverage" "SKIP (gcovr not installed)"; fi; \
	echo "=========================================================="; \
	if [ $$status -eq 0 ]; then \
	  echo "  VERDICT: ALL CORRECTNESS CHECKS PASSED"; \
	  echo "  (style WARNs above, if any, are advisory - see 'make lint' /"; \
	  echo "   'make format-check' for detail; they are tool-version sensitive)"; \
	else echo "  VERDICT: CORRECTNESS FAILURES PRESENT (see above)"; fi; \
	echo "=========================================================="; \
	exit $$status

$(EXAMPLE_BUILD_DIR)/%: $(EXAMPLE_DIR)/%.c $(TEST_LIB_STATIC) | $(EXAMPLE_BUILD_DIR)
	$(CC) $(TEST_CFLAGS) $(INCLUDES) -o $@ $< $(TEST_LIB_STATIC) $(LDLIBS)

examples: $(EXAMPLE_BIN)

clean:
	rm -f $(LIB_DIR)/*.o $(ALGO_DIR)/*.o $(BUILD_DIR)/*.o \
	      $(LIB_DIR)/*.d $(ALGO_DIR)/*.d $(BUILD_DIR)/*.d \
	      $(LIB_STATIC) $(LIB_SHARED) \
	      $(GEN_BIN)
	rm -f $(GEN_SRC_C) $(GEN_SRC_H) $(GEN_STAMP)
	rm -rf $(TEST_BUILD_DIR)

distclean: clean
	rm -rf $(BUILD_DIR)
	rm -f valgrind.log *.log
	rm -f $(LIB_DIR)/*.gch *.gch


sanitize:
	$(MAKE) clean
	@status=0; \
	$(MAKE) CFLAGS='-std=c89 -Wall -Wextra -Werror -pedantic -pedantic-errors -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined' TEST_CFLAGS='-std=c99 -Wall -Wextra -Werror -pedantic -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined' LDLIBS='-lm -fsanitize=address,undefined' test || status=$$?; \
	$(MAKE) clean; \
	exit $$status

# Rewrite the maintained sources in place using clang-format.
format:
	@if ! command -v $(CLANG_FORMAT) >/dev/null 2>&1; then \
	  echo "$(CLANG_FORMAT) not found; install clang-format or set CLANG_FORMAT=/path/to/clang-format"; \
	  exit 1; \
	fi
	@echo "Formatting $(words $(FORMAT_SRC)) files with $$($(CLANG_FORMAT) --version)"
	@$(CLANG_FORMAT) -i $(FORMAT_SRC)
	@echo "Done."

# Rewrite every hand-written source in the tree (legacy modules included).
format-all:
	@if ! command -v $(CLANG_FORMAT) >/dev/null 2>&1; then \
	  echo "$(CLANG_FORMAT) not found; install clang-format or set CLANG_FORMAT=/path/to/clang-format"; \
	  exit 1; \
	fi
	@echo "Formatting $(words $(FORMAT_ALL_SRC)) files with $$($(CLANG_FORMAT) --version)"
	@$(CLANG_FORMAT) -i $(FORMAT_ALL_SRC)
	@echo "Done."

# Verify formatting without modifying files (suitable for CI). Fails if any
# file would change.
format-check:
	@if ! command -v $(CLANG_FORMAT) >/dev/null 2>&1; then \
	  echo "$(CLANG_FORMAT) not found; install clang-format or set CLANG_FORMAT=/path/to/clang-format"; \
	  exit 1; \
	fi
	@status=0; \
	for f in $(FORMAT_SRC); do \
	  if ! $(CLANG_FORMAT) --dry-run --Werror "$$f" >/dev/null 2>&1; then \
	    echo "needs formatting: $$f"; \
	    status=1; \
	  fi; \
	done; \
	if [ $$status -eq 0 ]; then echo "all files correctly formatted"; fi; \
	exit $$status

# Sources covered by the strict lint gate. These are the structures added in
# this line of work; they are held to a no-findings standard. The wider legacy
# tree is checked informationally via `make lint-all`.
LINT_SRC := \
  $(LIB_DIR)/art.c $(LIB_DIR)/bitvector.c $(LIB_DIR)/fibonacci_heap.c \
  $(LIB_DIR)/hamt.c $(LIB_DIR)/order_statistic_tree.c $(LIB_DIR)/rope.c \
  $(LIB_DIR)/segment_tree.c $(LIB_DIR)/wavelet_tree.c \
  $(LIB_DIR)/graph.c $(LIB_DIR)/persistent_rbt.c $(LIB_DIR)/persistent_trie.c

# Static analysis with cppcheck over the maintained structures. Warnings,
# performance, and portability findings fail the build.
lint:
	@if ! command -v $(CPPCHECK) >/dev/null 2>&1; then \
	  echo "$(CPPCHECK) not found; install cppcheck or set CPPCHECK=/path/to/cppcheck"; \
	  exit 1; \
	fi
	$(CPPCHECK) --std=c89 --enable=warning,performance,portability \
	  --error-exitcode=1 --quiet --inline-suppr \
	  --suppress=missingIncludeSystem --suppress=unusedFunction \
	  --suppress=checkersReport --suppress=unmatchedSuppression \
	  $(INCLUDES) $(LINT_SRC)

# Informational static analysis across the entire library (does not fail the
# build; legacy modules surface known macro/callback false positives).
lint-all:
	@if ! command -v $(CPPCHECK) >/dev/null 2>&1; then \
	  echo "$(CPPCHECK) not found; install cppcheck or set CPPCHECK=/path/to/cppcheck"; \
	  exit 1; \
	fi
	-$(CPPCHECK) --std=c89 --enable=warning,performance,portability,style \
	  --quiet --inline-suppr \
	  --suppress=missingIncludeSystem --suppress=unusedFunction \
	  --suppress=va_list_usedBeforeStarted \
	  $(INCLUDES) $(wildcard $(LIB_DIR)/*.c)

# Stylistic suggestions for the maintained structures; informational only.
lint-style:
	@if ! command -v $(CPPCHECK) >/dev/null 2>&1; then \
	  echo "$(CPPCHECK) not found; install cppcheck or set CPPCHECK=/path/to/cppcheck"; \
	  exit 1; \
	fi
	-$(CPPCHECK) --std=c89 --enable=style --quiet --inline-suppr \
	  --suppress=missingIncludeSystem --suppress=unusedFunction \
	  $(INCLUDES) $(LINT_SRC)


valgrind-test: test-build
	@command -v $(VALGRIND) >/dev/null 2>&1 || { \
	  echo "valgrind not found; install Valgrind or set VALGRIND=/path/to/valgrind"; \
	  exit 127; \
	}
	@set -e; \
	for test_bin in $(TEST_BIN); do \
	  printf '\n== valgrind %s ==\n' "$$test_bin"; \
	  $(VALGRIND) $(VALGRIND_FLAGS) "$$test_bin"; \
	done

valgrind-examples: examples
	@command -v $(VALGRIND) >/dev/null 2>&1 || { \
	  echo "valgrind not found; install Valgrind or set VALGRIND=/path/to/valgrind"; \
	  exit 127; \
	}
	@set -e; \
	for example_bin in $(EXAMPLE_BIN); do \
	  printf '\n== valgrind %s ==\n' "$$example_bin"; \
	  $(VALGRIND) $(VALGRIND_FLAGS) "$$example_bin"; \
	done

valgrind: valgrind-test valgrind-examples
