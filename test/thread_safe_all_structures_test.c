#include "../ds.h"
#include <assert.h>
#include <stdlib.h>

typedef struct avl_item {
  ds_avl_node_t node;
  int key;
} avl_item_t;

typedef struct btree_item {
  ds_btree_node_t node;
  int key;
} btree_item_t;

typedef struct dlist_item {
  ds_dlist_node_t node;
  int key;
} dlist_item_t;

typedef struct list_item {
  ds_list_node_t node;
  int key;
} list_item_t;

typedef struct queue_item {
  ds_queue_node_t node;
  int key;
} queue_item_t;

typedef struct stack_item {
  ds_stack_node_t node;
  int key;
} stack_item_t;

typedef struct skip_list_item {
  ds_skip_list_node_t node;
  int key;
} skip_list_item_t;

typedef struct circular_buffer_item {
  ds_circular_buffer_node_t node;
  int key;
} circular_buffer_item_t;

typedef struct disjoint_set_item {
  ds_disjoint_set_node_t node;
  int key;
} disjoint_set_item_t;

typedef struct splay_tree_item {
  ds_splay_tree_node_t node;
  int key;
} splay_tree_item_t;

typedef struct treap_item {
  ds_treap_node_t node;
  int key;
} treap_item_t;

typedef struct pairing_heap_item {
  ds_pairing_heap_node_t node;
  int key;
} pairing_heap_item_t;

typedef struct ring_list_item {
  ds_ring_list_node_t node;
  int key;
} ring_list_item_t;

typedef struct bplus_item {
  ds_bplus_tree_node_t node;
  int key;
} bplus_item_t;

static int avl_compare(ds_avl_node_t *a, ds_avl_node_t *b) {
  return ((avl_item_t *)a)->key - ((avl_item_t *)b)->key;
}

static int btree_compare(ds_btree_node_t *a, ds_btree_node_t *b) {
  return ((btree_item_t *)a)->key - ((btree_item_t *)b)->key;
}

static int skip_list_compare(ds_skip_list_node_t *a, ds_skip_list_node_t *b) {
  return ((skip_list_item_t *)a)->key - ((skip_list_item_t *)b)->key;
}

static int splay_tree_compare(ds_splay_tree_node_t *a,
                              ds_splay_tree_node_t *b) {
  return ((splay_tree_item_t *)a)->key - ((splay_tree_item_t *)b)->key;
}

static int treap_compare(ds_treap_node_t *a, ds_treap_node_t *b) {
  return ((treap_item_t *)a)->key - ((treap_item_t *)b)->key;
}

static int pairing_heap_compare(ds_pairing_heap_node_t *a,
                                ds_pairing_heap_node_t *b) {
  return ((pairing_heap_item_t *)a)->key - ((pairing_heap_item_t *)b)->key;
}

static int ring_list_compare(ds_ring_list_node_t *a, ds_ring_list_node_t *b) {
  return ((ring_list_item_t *)a)->key - ((ring_list_item_t *)b)->key;
}

static int bplus_compare(ds_bplus_tree_node_t *a, ds_bplus_tree_node_t *b) {
  return ((bplus_item_t *)a)->key - ((bplus_item_t *)b)->key;
}

typedef struct ost_item {
  ds_ost_node_t node;
  int key;
} ost_item_t;

typedef struct fib_item {
  ds_fibonacci_heap_node_t node;
  int key;
} fib_item_t;

static int ost_compare(ds_ost_node_t *a, ds_ost_node_t *b) {
  return ((ost_item_t *)a)->key - ((ost_item_t *)b)->key;
}

static int fib_compare(ds_fibonacci_heap_node_t *a,
                       ds_fibonacci_heap_node_t *b) {
  return ((fib_item_t *)a)->key - ((fib_item_t *)b)->key;
}

static void destroy_avl_payload(ds_avl_node_t *node) {
  free(node);
}
static void destroy_btree_payload(ds_btree_node_t *node) {
  free(node);
}
static void noop_avl_walk(ds_avl_node_t *node) {
  (void)node;
}
static void noop_btree_walk(ds_btree_node_t *node) {
  (void)node;
}
static void destroy_queue_payload(ds_queue_node_t *node) {
  (void)node;
}
static void destroy_stack_payload(ds_stack_node_t *node) {
  (void)node;
}
static void destroy_skip_list_payload(ds_skip_list_node_t *node) {
  free(node);
}
static void noop_skip_list_walk(ds_skip_list_node_t *node) {
  (void)node;
}
static void noop_circular_buffer_walk(ds_circular_buffer_node_t *node) {
  (void)node;
}
static void destroy_splay_tree_payload(ds_splay_tree_node_t *node) {
  free(node);
}
static void noop_splay_tree_walk(ds_splay_tree_node_t *node) {
  (void)node;
}
static void destroy_treap_payload(ds_treap_node_t *node) {
  free(node);
}
static void noop_treap_walk(ds_treap_node_t *node) {
  (void)node;
}
static void destroy_ring_list_payload(ds_ring_list_node_t *node) {
  (void)node;
}

int main(void) {
  ds_dlist_t *dlist;
  ds_list_t *list;
  ds_queue_t *queue;
  ds_stack_t *stack;
  ds_avl_t *avl;
  ds_btree_t *btree;
  ds_skip_list_t *skip_list;
  ds_circular_buffer_t *circular_buffer;
  ds_disjoint_set_t *disjoint_set;
  ds_splay_tree_t *splay_tree;
  ds_treap_t *treap;
  ds_pairing_heap_t *pairing_heap;
  ds_ring_list_t *ring_list;
  dlist_item_t dnode;
  list_item_t lnode;
  queue_item_t qnode;
  stack_item_t snode;
  avl_item_t *avl_node;
  btree_item_t *btree_node;
  btree_item_t probe;
  skip_list_item_t *skip_list_node;
  skip_list_item_t skip_probe;
  circular_buffer_item_t cb_nodes[8];
  disjoint_set_item_t ds_nodes[8];
  splay_tree_item_t *splay_node;
  splay_tree_item_t splay_probe;
  treap_item_t *treap_node;
  treap_item_t treap_probe;
  pairing_heap_item_t *ph_nodes;
  ring_list_item_t rl_nodes[8];
  ring_list_item_t rl_probe;
  ds_bplus_tree_t *bplus;
  ds_bitset_t *bitset;
  ds_bloom_filter_t *bloom;
  ds_radix_trie_t *radix;
  ds_aho_corasick_t *ac;
  bplus_item_t *bplus_nodes;
  bplus_item_t bplus_probe;
  ds_bitvector_t *bitvector;
  ds_segment_tree_t *segment_tree;
  ds_wavelet_tree_t *wavelet_tree;
  ds_hamt_t *hamt;
  ds_art_t *art;
  ds_rope_t *rope;
  ds_ost_t *ost;
  ost_item_t *ost_nodes;
  ost_item_t ost_probe;
  ds_fibonacci_heap_t *fib;
  fib_item_t *fib_nodes;
  size_t i;

  dlist = FUNC(dlist_create)();
  assert(FUNC(dlist_is_empty)(dlist));
  FUNC(dlist_append)(dlist, &dnode.node);
  assert(!FUNC(dlist_is_empty)(dlist));
  FUNC(dlist_delete)(dlist);
  assert(dlist->size == 0U);
  FUNC(dlist_destroy)(dlist, NULL);

  list = FUNC(list_create)();
  FUNC(list_append)(list, &lnode.node);
  FUNC(list_delete)(list);
  assert(list->size == 0U);
  FUNC(list_destroy)(list, NULL);

  queue = FUNC(queue_create)();
  FUNC(queue_enqueue)(queue, &qnode.node);
  FUNC(queue_pop_destroy)(queue, destroy_queue_payload);
  assert(FUNC(queue_is_empty)(queue));
  FUNC(queue_destroy)(queue, NULL);

  stack = FUNC(stack_create)();
  FUNC(stack_push)(stack, stack->nil);
  assert(FUNC(stack_is_empty)(stack));
  assert(FUNC(stack_pop)(stack) == stack->nil);
  FUNC(stack_push)(stack, &snode.node);
  FUNC(stack_pop_destroy)(stack, destroy_stack_payload);
  assert(FUNC(stack_is_empty)(stack));
  FUNC(stack_walk_backwards)(stack, stack->nil, destroy_stack_payload);
  FUNC(stack_destroy)(stack, NULL);

  avl = FUNC(avl_create)();
  for (i = 0U; i < 32U; i++) {
    avl_node = (avl_item_t *)malloc(sizeof(*avl_node));
    assert(avl_node != NULL);
    avl_node->key = (int)((i * 7U) % 97U);
    FUNC(avl_insert)(avl, &avl_node->node, avl_compare);
  }
  FUNC(avl_inorder_walk_tree)(avl, noop_avl_walk);
  FUNC(avl_preorder_walk_tree)(avl, noop_avl_walk);
  FUNC(avl_postorder_walk_tree)(avl, noop_avl_walk);
  FUNC(avl_destroy_tree)(avl, destroy_avl_payload);

  btree = FUNC(btree_create)(2);
  for (i = 0U; i < 16U; i++) {
    btree_node = (btree_item_t *)malloc(sizeof(*btree_node));
    assert(btree_node != NULL);
    btree_node->key = (int)i;
    FUNC(btree_insert)(btree, &btree_node->node, btree_compare);
  }
  probe.key = 99;
  assert(FUNC(btree_search)(btree, &probe.node, btree_compare) == btree->nil);
  FUNC(btree_inorder_walk_tree)(btree, noop_btree_walk);
  FUNC(btree_preorder_walk_tree)(btree, noop_btree_walk);
  FUNC(btree_postorder_walk_tree)(btree, noop_btree_walk);
  FUNC(btree_destroy_tree)(btree, destroy_btree_payload);

  skip_list = FUNC(skip_list_create)(skip_list_compare);
  assert(FUNC(skip_list_is_empty)(skip_list));
  for (i = 0U; i < 32U; i++) {
    skip_list_node = (skip_list_item_t *)malloc(sizeof(*skip_list_node));
    assert(skip_list_node != NULL);
    FUNC(skip_list_node_init)(&skip_list_node->node);
    skip_list_node->key = (int)((i * 7U) % 97U);
    FUNC(skip_list_insert)(skip_list, &skip_list_node->node);
  }
  assert(!FUNC(skip_list_is_empty)(skip_list));
  skip_probe.key = 7;
  assert(FUNC(skip_list_search)(skip_list, &skip_probe.node) != skip_list->nil);
  skip_probe.key = 1000;
  assert(FUNC(skip_list_search)(skip_list, &skip_probe.node) == skip_list->nil);
  FUNC(skip_list_walk_forward)(skip_list, skip_list->nil, noop_skip_list_walk);
  FUNC(skip_list_destroy)(skip_list, destroy_skip_list_payload);

  circular_buffer = FUNC(circular_buffer_create)(4);
  assert(FUNC(circular_buffer_is_empty)(circular_buffer));
  for (i = 0U; i < 4U; i++) {
    cb_nodes[i].key = (int)i;
    assert(FUNC(circular_buffer_push)(circular_buffer, &cb_nodes[i].node));
  }
  assert(FUNC(circular_buffer_is_full)(circular_buffer));
  assert(FUNC(circular_buffer_peek)(circular_buffer) != circular_buffer->nil);
  FUNC(circular_buffer_walk_forward)
  (circular_buffer, noop_circular_buffer_walk);
  assert(FUNC(circular_buffer_pop)(circular_buffer) != circular_buffer->nil);
  FUNC(circular_buffer_destroy)(circular_buffer, NULL);

  disjoint_set = FUNC(disjoint_set_create)();
  for (i = 0U; i < 8U; i++) {
    ds_nodes[i].key = (int)i;
    FUNC(disjoint_set_make)(disjoint_set, &ds_nodes[i].node);
  }
  FUNC(disjoint_set_union)(disjoint_set, &ds_nodes[0].node, &ds_nodes[1].node);
  FUNC(disjoint_set_union)(disjoint_set, &ds_nodes[1].node, &ds_nodes[2].node);
  assert(FUNC(disjoint_set_connected)(disjoint_set, &ds_nodes[0].node,
                                      &ds_nodes[2].node));
  assert(FUNC(disjoint_set_count)(disjoint_set) == 6U);
  FUNC(disjoint_set_destroy)(disjoint_set);

  splay_tree = FUNC(splay_tree_create)(splay_tree_compare);
  assert(FUNC(splay_tree_is_empty)(splay_tree));
  for (i = 0U; i < 32U; i++) {
    splay_node = (splay_tree_item_t *)malloc(sizeof(*splay_node));
    assert(splay_node != NULL);
    splay_node->key = (int)((i * 7U) % 97U);
    FUNC(splay_tree_insert)(splay_tree, &splay_node->node);
  }
  splay_probe.key = 7;
  assert(FUNC(splay_tree_search)(splay_tree, &splay_probe.node) !=
         splay_tree->nil);
  splay_probe.key = 1000;
  assert(FUNC(splay_tree_search)(splay_tree, &splay_probe.node) ==
         splay_tree->nil);
  FUNC(splay_tree_inorder_walk_tree)(splay_tree, noop_splay_tree_walk);
  FUNC(splay_tree_destroy)(splay_tree, destroy_splay_tree_payload);

  treap = FUNC(treap_create)(treap_compare);
  assert(FUNC(treap_is_empty)(treap));
  for (i = 0U; i < 32U; i++) {
    treap_node = (treap_item_t *)malloc(sizeof(*treap_node));
    assert(treap_node != NULL);
    FUNC(treap_node_init)(&treap_node->node);
    treap_node->key = (int)((i * 13U) % 101U);
    FUNC(treap_insert)(treap, &treap_node->node);
  }
  treap_probe.key = 13;
  assert(FUNC(treap_search)(treap, &treap_probe.node) != treap->nil);
  treap_probe.key = 1000;
  assert(FUNC(treap_search)(treap, &treap_probe.node) == treap->nil);
  FUNC(treap_inorder_walk_tree)(treap, noop_treap_walk);
  FUNC(treap_destroy)(treap, destroy_treap_payload);

  pairing_heap = FUNC(pairing_heap_create)(pairing_heap_compare);
  assert(FUNC(pairing_heap_is_empty)(pairing_heap));
  ph_nodes = (pairing_heap_item_t *)malloc(sizeof(*ph_nodes) * 16U);
  assert(ph_nodes != NULL);
  for (i = 0U; i < 16U; i++) {
    FUNC(pairing_heap_node_init)(&ph_nodes[i].node);
    ph_nodes[i].key = (int)((i * 11U) % 89U);
    FUNC(pairing_heap_insert)(pairing_heap, &ph_nodes[i].node);
  }
  assert(FUNC(pairing_heap_size)(pairing_heap) == 16U);
  assert(FUNC(pairing_heap_peek)(pairing_heap) != pairing_heap->nil);
  assert(FUNC(pairing_heap_extract)(pairing_heap) != pairing_heap->nil);
  FUNC(pairing_heap_destroy)(pairing_heap, NULL);
  free(ph_nodes);

  ring_list = FUNC(ring_list_create)();
  assert(FUNC(ring_list_is_empty)(ring_list));
  for (i = 0U; i < 8U; i++) {
    rl_nodes[i].key = (int)i;
    FUNC(ring_list_push_back)(ring_list, &rl_nodes[i].node);
  }
  assert(ring_list->size == 8U);
  rl_probe.key = 3;
  assert(FUNC(ring_list_search)(ring_list, &rl_probe.node, ring_list_compare) !=
         ring_list->nil);
  FUNC(ring_list_rotate)(ring_list);
  assert(FUNC(ring_list_pop_front)(ring_list) != ring_list->nil);
  FUNC(ring_list_destroy)(ring_list, destroy_ring_list_payload);

  bplus = FUNC(bplus_tree_create)(5, bplus_compare);
  bplus_nodes = (bplus_item_t *)malloc(sizeof(*bplus_nodes) * 32U);
  assert(bplus_nodes != NULL);
  for (i = 0U; i < 32U; i++) {
    bplus_nodes[i].key = (int)((i * 13U) % 101U);
    FUNC(bplus_tree_insert)(bplus, &bplus_nodes[i].node);
  }
  bplus_probe.key = bplus_nodes[3].key;
  assert(FUNC(bplus_tree_search)(bplus, &bplus_probe.node) != bplus->nil);
  assert(FUNC(bplus_tree_minimum)(bplus) != bplus->nil);
  assert(FUNC(bplus_tree_maximum)(bplus) != bplus->nil);
  FUNC(bplus_tree_remove)(bplus, &bplus_probe.node, NULL);
  FUNC(bplus_tree_destroy)(bplus, NULL);
  free(bplus_nodes);

  bitset = FUNC(bitset_create)(128);
  for (i = 0U; i < 128U; i += 2U) {
    FUNC(bitset_set)(bitset, i);
  }
  assert(FUNC(bitset_count)(bitset) == 64U);
  assert(FUNC(bitset_test)(bitset, 0U));
  assert(!FUNC(bitset_test)(bitset, 1U));
  assert(FUNC(bitset_find_first)(bitset) == 0U);
  FUNC(bitset_destroy)(bitset);

  bloom = FUNC(bloom_filter_create)(256, 0.01);
  for (i = 0U; i < 100U; i++) {
    FUNC(bloom_filter_add)(bloom, &i, sizeof(i));
  }
  assert(FUNC(bloom_filter_count)(bloom) == 100U);
  for (i = 0U; i < 100U; i++) {
    assert(FUNC(bloom_filter_maybe_contains)(bloom, &i, sizeof(i)));
  }
  FUNC(bloom_filter_destroy)(bloom);

  radix = FUNC(radix_trie_create)();
  FUNC(radix_trie_insert)(radix, "alpha", 5U, NULL);
  FUNC(radix_trie_insert)(radix, "alpine", 6U, NULL);
  FUNC(radix_trie_insert)(radix, "beta", 4U, NULL);
  assert(FUNC(radix_trie_size)(radix) == 3U);
  assert(FUNC(radix_trie_contains)(radix, "alpha", 5U));
  assert(!FUNC(radix_trie_contains)(radix, "alp", 3U));
  assert(FUNC(radix_trie_remove)(radix, "alpha", 5U, NULL));
  assert(FUNC(radix_trie_size)(radix) == 2U);
  FUNC(radix_trie_destroy)(radix, NULL);

  ac = FUNC(aho_corasick_create)();
  FUNC(aho_corasick_add)(ac, "he", 2U);
  FUNC(aho_corasick_add)(ac, "she", 3U);
  FUNC(aho_corasick_add)(ac, "hers", 4U);
  FUNC(aho_corasick_compile)(ac);
  assert(FUNC(aho_corasick_search)(ac, "ushers", 6U, NULL, NULL) == 3U);
  assert(FUNC(aho_corasick_contains_any)(ac, "ushers", 6U));
  assert(!FUNC(aho_corasick_contains_any)(ac, "xyz", 3U));
  FUNC(aho_corasick_destroy)(ac);

  bitvector = FUNC(bitvector_create)(64U);
  for (i = 0U; i < 64U; i++) {
    if (i % 3U == 0U) {
      FUNC(bitvector_set)(bitvector, i, true);
    }
  }
  FUNC(bitvector_build)(bitvector);
  assert(FUNC(bitvector_rank1)(bitvector, 64U) ==
         FUNC(bitvector_count_ones)(bitvector));
  assert(FUNC(bitvector_select1)(bitvector, 0U) == 0U);
  FUNC(bitvector_destroy)(bitvector);

  {
    long seg_vals[8];
    for (i = 0U; i < 8U; i++) {
      seg_vals[i] = (long)i;
    }
    segment_tree = FUNC(segment_tree_create)(seg_vals, 8U, DS_SEGMENT_TREE_SUM);
    FUNC(segment_tree_update_add)(segment_tree, 0U, 3U, 10L);
    assert(FUNC(segment_tree_query)(segment_tree, 0U, 3U) ==
           (0L + 1L + 2L + 3L) + 40L);
    FUNC(segment_tree_destroy)(segment_tree);
  }

  {
    size_t wseq[6];
    wseq[0] = 3U;
    wseq[1] = 1U;
    wseq[2] = 4U;
    wseq[3] = 1U;
    wseq[4] = 5U;
    wseq[5] = 2U;
    wavelet_tree = FUNC(wavelet_tree_create)(wseq, 6U, 6U);
    assert(FUNC(wavelet_tree_access)(wavelet_tree, 2U) == 4U);
    assert(FUNC(wavelet_tree_rank)(wavelet_tree, 1U, 6U) == 2U);
    assert(FUNC(wavelet_tree_select)(wavelet_tree, 1U, 1U) == 3U);
    FUNC(wavelet_tree_destroy)(wavelet_tree);
  }

  hamt = FUNC(hamt_create)();
  FUNC(hamt_insert)(hamt, "alpha", 5U, NULL);
  FUNC(hamt_insert)(hamt, "beta", 4U, NULL);
  assert(FUNC(hamt_size)(hamt) == 2U);
  assert(FUNC(hamt_contains)(hamt, "alpha", 5U));
  assert(!FUNC(hamt_contains)(hamt, "gamma", 5U));
  assert(FUNC(hamt_remove)(hamt, "alpha", 5U, NULL));
  assert(FUNC(hamt_size)(hamt) == 1U);
  FUNC(hamt_destroy)(hamt, NULL);

  art = FUNC(art_create)();
  FUNC(art_insert)(art, (const unsigned char *)"alpha", 5U, NULL);
  FUNC(art_insert)(art, (const unsigned char *)"alpine", 6U, NULL);
  FUNC(art_insert)(art, (const unsigned char *)"beta", 4U, NULL);
  assert(FUNC(art_size)(art) == 3U);
  assert(FUNC(art_contains)(art, (const unsigned char *)"alpha", 5U));
  assert(!FUNC(art_contains)(art, (const unsigned char *)"alp", 3U));
  assert(FUNC(art_remove)(art, (const unsigned char *)"alpha", 5U, NULL));
  assert(FUNC(art_size)(art) == 2U);
  FUNC(art_destroy)(art, NULL);

  rope = FUNC(rope_from_string)("Hello", 5U);
  FUNC(rope_append)(rope, ", world", 7U);
  assert(FUNC(rope_length)(rope) == 12U);
  assert(FUNC(rope_char_at)(rope, 0U) == 'H');
  FUNC(rope_insert)(rope, 5U, " there", 6U);
  assert(FUNC(rope_length)(rope) == 18U);
  FUNC(rope_delete)(rope, 0U, 5U);
  assert(FUNC(rope_length)(rope) == 13U);
  FUNC(rope_destroy)(rope);

  ost = FUNC(ost_create)(ost_compare);
  ost_nodes = (ost_item_t *)malloc(sizeof(ost_item_t) * 16U);
  for (i = 0U; i < 16U; i++) {
    ost_nodes[i].key = (int)(16U - i);
    FUNC(ost_insert)(ost, &ost_nodes[i].node);
  }
  assert(FUNC(ost_size)(ost) == 16U);
  assert(((ost_item_t *)FUNC(ost_select)(ost, 0U))->key == 1);
  ost_probe.key = 5;
  assert(FUNC(ost_rank)(ost, &ost_probe.node) == 4U);
  FUNC(ost_destroy)(ost, NULL);
  free(ost_nodes);

  fib = FUNC(fibonacci_heap_create)(fib_compare);
  fib_nodes = (fib_item_t *)malloc(sizeof(fib_item_t) * 16U);
  for (i = 0U; i < 16U; i++) {
    fib_nodes[i].key = (int)((i * 7U) % 16U);
    FUNC(fibonacci_heap_insert)(fib, &fib_nodes[i].node);
  }
  assert(FUNC(fibonacci_heap_size)(fib) == 16U);
  assert(((fib_item_t *)FUNC(fibonacci_heap_peek)(fib))->key == 0);
  FUNC(fibonacci_heap_extract)(fib);
  assert(FUNC(fibonacci_heap_size)(fib) == 15U);
  FUNC(fibonacci_heap_destroy)(fib, NULL);
  free(fib_nodes);

  return 0;
}
