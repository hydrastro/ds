#include "aho_corasick.h"
#include <stdlib.h>

static ds_aho_corasick_node_t *
FUNC(aho_corasick_new_node)(ds_aho_corasick_t *ac) {
  ds_aho_corasick_node_t *node =
      (ds_aho_corasick_node_t *)ac->allocator(sizeof(ds_aho_corasick_node_t));
  int i;
  for (i = 0; i < 256; i++) {
    node->children[i] = NULL;
  }
  node->fail = NULL;
  node->output = NULL;
  node->pattern_id = -1;
  node->pattern_length = 0;
  ac->num_nodes += 1;
  return node;
}

ds_aho_corasick_t *FUNC(ds_aho_corasick_create)(void) {
  return FUNC(ds_aho_corasick_create_alloc)(malloc, free);
}

ds_aho_corasick_t *
FUNC(ds_aho_corasick_create_alloc)(void *(*allocator)(size_t),
                                   void (*deallocator)(void *)) {
  ds_aho_corasick_t *ac =
      (ds_aho_corasick_t *)allocator(sizeof(ds_aho_corasick_t));
  ac->allocator = allocator;
  ac->deallocator = deallocator;
  ac->num_patterns = 0;
  ac->num_nodes = 0;
  ac->compiled = false;
  ac->root = FUNC(aho_corasick_new_node)(ac);
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(ac);
#endif
  return ac;
}

int FUNC(ds_aho_corasick_add)(ds_aho_corasick_t *ac, const void *pattern,
                              size_t length) {
  const unsigned char *bytes = (const unsigned char *)pattern;
  ds_aho_corasick_node_t *node;
  int id = -1;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(ac);
#endif
  if (length == 0) {
#ifdef DS_THREAD_SAFE
    UNLOCK(ac);
#endif
    return -1;
  }
  node = ac->root;
  for (i = 0; i < length; i++) {
    unsigned char c = bytes[i];
    if (node->children[c] == NULL) {
      node->children[c] = FUNC(aho_corasick_new_node)(ac);
    }
    node = node->children[c];
  }
  if (node->pattern_id < 0) {
    id = (int)ac->num_patterns;
    node->pattern_id = id;
    node->pattern_length = length;
    ac->num_patterns += 1;
    ac->compiled = false;
  } else {
    id = node->pattern_id;
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(ac);
#endif
  return id;
}

void FUNC(ds_aho_corasick_compile)(ds_aho_corasick_t *ac) {
  ds_aho_corasick_node_t **queue;
  size_t head = 0;
  size_t tail = 0;
  int c;
#ifdef DS_THREAD_SAFE
  LOCK(ac);
#endif
  queue = (ds_aho_corasick_node_t **)ac->allocator(
      ac->num_nodes * sizeof(ds_aho_corasick_node_t *));
  /* depth-1 nodes fail to root */
  for (c = 0; c < 256; c++) {
    ds_aho_corasick_node_t *child = ac->root->children[c];
    if (child != NULL) {
      child->fail = ac->root;
      queue[tail] = child;
      tail += 1;
    }
  }
  while (head < tail) {
    ds_aho_corasick_node_t *current = queue[head];
    head += 1;
    for (c = 0; c < 256; c++) {
      ds_aho_corasick_node_t *child = current->children[c];
      ds_aho_corasick_node_t *fail;
      if (child == NULL) {
        continue;
      }
      fail = current->fail;
      while (fail != ac->root && fail->children[c] == NULL) {
        fail = fail->fail;
      }
      if (fail->children[c] != NULL && fail->children[c] != child) {
        child->fail = fail->children[c];
      } else {
        child->fail = ac->root;
      }
      /* output link: nearest fail-ancestor that is itself a pattern end */
      if (child->fail->pattern_id >= 0) {
        child->output = child->fail;
      } else {
        child->output = child->fail->output;
      }
      queue[tail] = child;
      tail += 1;
    }
  }
  ac->deallocator(queue);
  ac->compiled = true;
#ifdef DS_THREAD_SAFE
  UNLOCK(ac);
#endif
}

static ds_aho_corasick_node_t *
FUNC(aho_corasick_step)(ds_aho_corasick_t *ac, ds_aho_corasick_node_t *node,
                        unsigned char c) {
  while (node != ac->root && node->children[c] == NULL) {
    node = node->fail;
  }
  if (node->children[c] != NULL) {
    node = node->children[c];
  }
  return node;
}

size_t FUNC(ds_aho_corasick_search)(
    ds_aho_corasick_t *ac, const void *haystack, size_t length,
    void (*callback)(const ds_aho_corasick_match_t *, void *), void *user) {
  const unsigned char *bytes = (const unsigned char *)haystack;
  ds_aho_corasick_node_t *node;
  size_t count = 0;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(ac);
#endif
  if (!ac->compiled) {
    FUNC(ds_aho_corasick_compile)(ac);
  }
  node = ac->root;
  for (i = 0; i < length; i++) {
    ds_aho_corasick_node_t *out;
    node = FUNC(aho_corasick_step)(ac, node, bytes[i]);
    if (node->pattern_id >= 0) {
      ds_aho_corasick_match_t match;
      match.pattern_id = node->pattern_id;
      match.end = i + 1;
      match.start = i + 1 - node->pattern_length;
      count += 1;
      if (callback != NULL) {
        callback(&match, user);
      }
    }
    for (out = node->output; out != NULL; out = out->output) {
      ds_aho_corasick_match_t match;
      match.pattern_id = out->pattern_id;
      match.end = i + 1;
      match.start = i + 1 - out->pattern_length;
      count += 1;
      if (callback != NULL) {
        callback(&match, user);
      }
    }
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(ac);
#endif
  return count;
}

bool FUNC(ds_aho_corasick_contains_any)(ds_aho_corasick_t *ac,
                                        const void *haystack, size_t length) {
  const unsigned char *bytes = (const unsigned char *)haystack;
  ds_aho_corasick_node_t *node;
  bool found = false;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(ac);
#endif
  if (!ac->compiled) {
    FUNC(ds_aho_corasick_compile)(ac);
  }
  node = ac->root;
  for (i = 0; i < length; i++) {
    node = FUNC(aho_corasick_step)(ac, node, bytes[i]);
    if (node->pattern_id >= 0 || node->output != NULL) {
      found = true;
      break;
    }
  }
#ifdef DS_THREAD_SAFE
  UNLOCK(ac);
#endif
  return found;
}

size_t FUNC(ds_aho_corasick_pattern_count)(ds_aho_corasick_t *ac) {
  size_t result;
#ifdef DS_THREAD_SAFE
  LOCK(ac);
#endif
  result = ac->num_patterns;
#ifdef DS_THREAD_SAFE
  UNLOCK(ac);
#endif
  return result;
}

static void FUNC(aho_corasick_destroy_recursive)(ds_aho_corasick_t *ac,
                                                 ds_aho_corasick_node_t *node) {
  int c;
  for (c = 0; c < 256; c++) {
    if (node->children[c] != NULL) {
      FUNC(aho_corasick_destroy_recursive)(ac, node->children[c]);
    }
  }
  ac->deallocator(node);
}

void FUNC(ds_aho_corasick_destroy)(ds_aho_corasick_t *ac) {
  FUNC(aho_corasick_destroy_recursive)(ac, ac->root);
#ifdef DS_THREAD_SAFE
  LOCK_DESTROY(ac);
#endif
  ac->deallocator(ac);
}
