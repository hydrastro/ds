#include <dsa/structures/aho_corasick.h>
#include <limits.h>
#include <stdlib.h>

static ds_aho_corasick_node_t *aho_corasick_new_node(ds_aho_corasick_t *ac) {
  ds_aho_corasick_node_t *node = (ds_aho_corasick_node_t *)ds_context_alloc(
      ac->context, sizeof(ds_aho_corasick_node_t));
  int i;
  if (node == NULL) {
    return NULL;
  }
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

static ds_aho_corasick_t *aho_corasick_create_internal(ds_context_t *context) {
  ds_aho_corasick_t *ac;

  ac = (ds_aho_corasick_t *)ds_context_alloc(context,
                                             sizeof(ds_aho_corasick_t));
  if (ac == NULL) {
    return NULL;
  }
  ac->context = context != NULL ? context : ds_default_context();

  ac->num_patterns = 0;
  ac->num_nodes = 0;
  ac->compiled = false;
  ac->root = aho_corasick_new_node(ac);
  if (ac->root == NULL) {
    ds_context_free(context, ac);
    return NULL;
  }
  return ac;
}

ds_aho_corasick_t *ds_aho_corasick_create(ds_context_t *context) {
  return aho_corasick_create_internal(context);
}
ds_status_t ds_aho_corasick_create_context(ds_context_t *context,
                                           ds_aho_corasick_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  *out = aho_corasick_create_internal(context != NULL ? context
                                                      : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

ds_status_t ds_aho_corasick_add_ex(ds_aho_corasick_t *ac, const void *pattern,
                                   size_t length, int *out_id) {
  const unsigned char *bytes = (const unsigned char *)pattern;
  ds_aho_corasick_node_t *node, *first = NULL, *tail = NULL, *pending = NULL;
  size_t i, j, old_nodes;
  if (ac == NULL || pattern == NULL || out_id == NULL)
    return DS_ERR_NULL;
  if (length == 0)
    return DS_ERR_INVALID;
  node = ac->root;
  for (i = 0; i < length && node->children[bytes[i]] != NULL; i++)
    node = node->children[bytes[i]];
  if (i == length && node->pattern_id >= 0) {
    *out_id = node->pattern_id;
    return DS_EXISTS;
  }
  if (ac->num_patterns == (size_t)-1 || ac->num_patterns > (size_t)INT_MAX ||
      length - i > (size_t)-1 - ac->num_nodes)
    return DS_ERR_OVERFLOW;
  old_nodes = ac->num_nodes;
  for (j = i; j < length; j++) {
    ds_aho_corasick_node_t *fresh = aho_corasick_new_node(ac);
    if (fresh == NULL) {
      while (pending != NULL) {
        ds_aho_corasick_node_t *next = pending->fail;
        ds_context_free(ac->context, pending);
        pending = next;
      }
      ac->num_nodes = old_nodes;
      return DS_ERR_ALLOC;
    }
    fresh->fail = pending; /* private rollback list until compilation */
    pending = fresh;
    if (tail != NULL)
      tail->children[bytes[j]] = fresh;
    else
      first = fresh;
    tail = fresh;
  }
  if (first != NULL) {
    node->children[bytes[i]] = first;
    node = tail;
  }
  node->pattern_id = (int)ac->num_patterns++;
  node->pattern_length = length;
  ac->compiled = false;
  *out_id = node->pattern_id;
  return DS_OK;
}

int ds_aho_corasick_add(ds_aho_corasick_t *ac, const void *pattern,
                        size_t length) {
  int id = -1;
  (void)ds_aho_corasick_add_ex(ac, pattern, length, &id);
  return id;
}

ds_status_t ds_aho_corasick_compile_ex(ds_aho_corasick_t *ac) {
  ds_aho_corasick_node_t **queue;
  size_t head = 0;
  size_t tail = 0;
  int c;
  if (ac == NULL)
    return DS_ERR_NULL;
  if (ac->num_nodes > (size_t)-1 / sizeof(*queue))
    return DS_ERR_OVERFLOW;
  queue = (ds_aho_corasick_node_t **)ds_context_alloc(
      ac->context, ac->num_nodes * sizeof(ds_aho_corasick_node_t *));
  if (queue == NULL)
    return DS_ERR_ALLOC;
  /* depth-1 nodes fail to root */
  for (c = 0; c < 256; c++) {
    ds_aho_corasick_node_t *child = ac->root->children[c];
    if (child != NULL) {
      child->fail = ac->root;
      child->output = NULL;
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
  ds_context_free(ac->context, queue);
  ac->compiled = true;
  return DS_OK;
}

void ds_aho_corasick_compile(ds_aho_corasick_t *ac) {
  (void)ds_aho_corasick_compile_ex(ac);
}

static ds_aho_corasick_node_t *aho_corasick_step(ds_aho_corasick_t *ac,
                                                 ds_aho_corasick_node_t *node,
                                                 unsigned char c) {
  while (node != ac->root && node->children[c] == NULL) {
    node = node->fail;
  }
  if (node->children[c] != NULL) {
    node = node->children[c];
  }
  return node;
}

ds_status_t ds_aho_corasick_search_with(ds_aho_corasick_t *ac,
                                        const void *haystack, size_t length,
                                        ds_aho_corasick_visit_fn callback,
                                        void *user, size_t *out_count) {
  const unsigned char *bytes = (const unsigned char *)haystack;
  ds_aho_corasick_node_t *node;
  size_t count = 0;
  size_t i;
  ds_status_t st;
  if (ac == NULL || out_count == NULL || (haystack == NULL && length != 0))
    return DS_ERR_NULL;
  if (!ac->compiled) {
    st = ds_aho_corasick_compile_ex(ac);
    if (st != DS_OK)
      return st;
  }
  node = ac->root;
  for (i = 0; i < length; i++) {
    ds_aho_corasick_node_t *out;
    node = aho_corasick_step(ac, node, bytes[i]);
    if (node->pattern_id >= 0) {
      ds_aho_corasick_match_t match;
      match.pattern_id = node->pattern_id;
      match.end = i + 1;
      match.start = i + 1 - node->pattern_length;
      if (count == (size_t)-1) {
        *out_count = count;
        return DS_ERR_OVERFLOW;
      }
      count += 1;
      if (callback != NULL) {
        st = callback(&match, user);
        if (st != DS_OK) {
          *out_count = count;
          return st;
        }
      }
    }
    for (out = node->output; out != NULL; out = out->output) {
      ds_aho_corasick_match_t match;
      match.pattern_id = out->pattern_id;
      match.end = i + 1;
      match.start = i + 1 - out->pattern_length;
      if (count == (size_t)-1) {
        *out_count = count;
        return DS_ERR_OVERFLOW;
      }
      count += 1;
      if (callback != NULL) {
        st = callback(&match, user);
        if (st != DS_OK) {
          *out_count = count;
          return st;
        }
      }
    }
  }
  *out_count = count;
  return DS_OK;
}

typedef struct aho_direct_visit {
  void (*callback)(const ds_aho_corasick_match_t *, void *);
  void *user;
} aho_direct_visit_t;
static ds_status_t aho_visit_direct(const ds_aho_corasick_match_t *match,
                                    void *user) {
  aho_direct_visit_t *policy = (aho_direct_visit_t *)user;
  policy->callback(match, policy->user);
  return DS_OK;
}
ds_status_t ds_aho_corasick_search_ex(
    ds_aho_corasick_t *ac, const void *haystack, size_t length,
    void (*callback)(const ds_aho_corasick_match_t *, void *), void *user,
    size_t *out_count) {
  aho_direct_visit_t policy;
  ds_status_t st;
  size_t count;
  if (out_count == NULL)
    return DS_ERR_NULL;
  policy.callback = callback;
  policy.user = user;
  st = ds_aho_corasick_search_with(ac, haystack, length,
                                   callback != NULL ? aho_visit_direct : NULL,
                                   &policy, &count);
  if (st == DS_OK)
    *out_count = count;
  return st;
}

size_t ds_aho_corasick_search(
    ds_aho_corasick_t *ac, const void *haystack, size_t length,
    void (*callback)(const ds_aho_corasick_match_t *, void *), void *user) {
  size_t count = 0;
  (void)ds_aho_corasick_search_ex(ac, haystack, length, callback, user,
                                  &count);
  return count;
}

ds_status_t ds_aho_corasick_contains_any_ex(ds_aho_corasick_t *ac,
                                            const void *haystack,
                                            size_t length, bool *out) {
  const unsigned char *bytes = (const unsigned char *)haystack;
  ds_aho_corasick_node_t *node;
  bool found = false;
  size_t i;
  ds_status_t st;
  if (ac == NULL || out == NULL || (haystack == NULL && length != 0))
    return DS_ERR_NULL;
  if (!ac->compiled) {
    st = ds_aho_corasick_compile_ex(ac);
    if (st != DS_OK)
      return st;
  }
  node = ac->root;
  for (i = 0; i < length; i++) {
    node = aho_corasick_step(ac, node, bytes[i]);
    if (node->pattern_id >= 0 || node->output != NULL) {
      found = true;
      break;
    }
  }
  *out = found;
  return DS_OK;
}

bool ds_aho_corasick_contains_any(ds_aho_corasick_t *ac, const void *haystack,
                                  size_t length) {
  bool found = false;
  (void)ds_aho_corasick_contains_any_ex(ac, haystack, length, &found);
  return found;
}

size_t ds_aho_corasick_pattern_count(ds_aho_corasick_t *ac) {
  size_t result;
  result = ac->num_patterns;
  return result;
}

/* Destruction needs no memory and no recursion (the trie is as deep as
 * the longest pattern): nodes waiting to be freed form a list linked
 * through their failure links, which nothing reads any more. */
void ds_aho_corasick_destroy(ds_aho_corasick_t *ac) {
  ds_aho_corasick_node_t *list = ac->root;
  if (list != NULL) {
    list->fail = NULL;
  }
  while (list != NULL) {
    ds_aho_corasick_node_t *node = list;
    int c;
    list = node->fail;
    for (c = 0; c < 256; c++) {
      if (node->children[c] != NULL) {
        node->children[c]->fail = list;
        list = node->children[c];
      }
    }
    ds_context_free(ac->context, node);
  }
  ds_context_free(ac->context, ac);
}
