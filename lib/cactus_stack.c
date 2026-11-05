#include "cactus_stack.h"
#include <string.h>

#define CS_EMPTY DS_CACTUS_EMPTY

static bool cs_valid(const ds_cactus_stack_t *cs, size_t v) {
  return v < cs->slots && cs->refs[v] > 0;
}

ds_status_t ds_cactus_stack_init(ds_cactus_stack_t *cs, size_t elem_size,
                                 ds_context_t *context) {
  if (cs == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  memset(cs, 0, sizeof(*cs));
  cs->free_list = CS_EMPTY;
  cs->elem_size = elem_size;
  cs->context = context;
  return DS_OK;
}

void ds_cactus_stack_destroy(ds_cactus_stack_t *cs) {
  if (cs == NULL) {
    return;
  }
  ds_context_free(cs->context, cs->data);
  ds_context_free(cs->context, cs->parent);
  ds_context_free(cs->context, cs->refs);
  ds_context_free(cs->context, cs->depth);
  cs->data = NULL;
  cs->parent = cs->refs = cs->depth = NULL;
  cs->slots = cs->capacity = cs->live = 0;
  cs->free_list = CS_EMPTY;
}

static ds_status_t cs_node(ds_cactus_stack_t *cs, size_t *out) {
  size_t cap;
  void *p;
  if (cs->free_list != CS_EMPTY) {
    *out = cs->free_list;
    cs->free_list = cs->parent[*out];
    return DS_OK;
  }
  if (cs->slots == cs->capacity) {
    cap = cs->capacity < 8 ? 8 : cs->capacity * 2;
    if (cap <= cs->capacity || cap > ((size_t)-1) / cs->elem_size ||
        cap > ((size_t)-1) / sizeof(size_t)) {
      return DS_ERR_OVERFLOW;
    }
    p = ds_context_realloc(cs->context, cs->data, cap * cs->elem_size);
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    cs->data = (unsigned char *)p;
    p = ds_context_realloc(cs->context, cs->parent, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    cs->parent = (size_t *)p;
    p = ds_context_realloc(cs->context, cs->refs, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    cs->refs = (size_t *)p;
    p = ds_context_realloc(cs->context, cs->depth, cap * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    cs->depth = (size_t *)p;
    cs->capacity = cap;
  }
  *out = cs->slots++;
  return DS_OK;
}

ds_status_t ds_cactus_stack_push(ds_cactus_stack_t *cs, size_t top,
                                 const void *elem, size_t *out_top) {
  size_t v;
  ds_status_t st;
  if (cs == NULL || elem == NULL || out_top == NULL) {
    return DS_ERR_NULL;
  }
  if (top != CS_EMPTY && !cs_valid(cs, top)) {
    return DS_ERR_RANGE;
  }
  if (top != CS_EMPTY && cs->refs[top] == (size_t)-1) {
    return DS_ERR_OVERFLOW;
  }
  st = cs_node(cs, &v);
  if (st != DS_OK) {
    return st;
  }
  memcpy(cs->data + v * cs->elem_size, elem, cs->elem_size);
  cs->parent[v] = top;
  cs->refs[v] = 1;
  cs->depth[v] = top == CS_EMPTY ? 1 : cs->depth[top] + 1;
  if (top != CS_EMPTY) {
    cs->refs[top]++;
  }
  cs->live++;
  *out_top = v;
  return DS_OK;
}

ds_status_t ds_cactus_stack_retain(ds_cactus_stack_t *cs, size_t top) {
  if (cs == NULL) {
    return DS_ERR_NULL;
  }
  if (top == CS_EMPTY) {
    return DS_OK;
  }
  if (!cs_valid(cs, top)) {
    return DS_ERR_RANGE;
  }
  if (cs->refs[top] == (size_t)-1) {
    return DS_ERR_OVERFLOW;
  }
  cs->refs[top]++;
  return DS_OK;
}

ds_status_t ds_cactus_stack_release(ds_cactus_stack_t *cs, size_t top) {
  size_t v = top;
  size_t p;
  if (cs == NULL) {
    return DS_ERR_NULL;
  }
  if (top == CS_EMPTY) {
    return DS_OK;
  }
  if (!cs_valid(cs, top)) {
    return DS_ERR_RANGE;
  }
  while (v != CS_EMPTY && --cs->refs[v] == 0) {
    p = cs->parent[v];
    cs->parent[v] = cs->free_list;
    cs->free_list = v;
    cs->live--;
    v = p;
  }
  return DS_OK;
}

ds_status_t ds_cactus_stack_pop(ds_cactus_stack_t *cs, size_t top,
                                void *out_elem, size_t *out_below) {
  size_t below;
  ds_status_t st;
  if (cs == NULL || out_below == NULL) {
    return DS_ERR_NULL;
  }
  if (top == CS_EMPTY) {
    return DS_EMPTY;
  }
  if (!cs_valid(cs, top)) {
    return DS_ERR_RANGE;
  }
  below = cs->parent[top];
  if (out_elem != NULL) {
    memcpy(out_elem, cs->data + top * cs->elem_size, cs->elem_size);
  }
  st = ds_cactus_stack_retain(cs, below);
  if (st != DS_OK) {
    return st;
  }
  (void)ds_cactus_stack_release(cs, top);
  *out_below = below;
  return DS_OK;
}

void *ds_cactus_stack_peek(const ds_cactus_stack_t *cs, size_t top) {
  if (cs == NULL || !cs_valid(cs, top)) {
    return NULL;
  }
  return cs->data + top * cs->elem_size;
}

size_t ds_cactus_stack_below(const ds_cactus_stack_t *cs, size_t top) {
  if (cs == NULL || !cs_valid(cs, top)) {
    return CS_EMPTY;
  }
  return cs->parent[top];
}

size_t ds_cactus_stack_depth(const ds_cactus_stack_t *cs, size_t top) {
  if (cs == NULL || !cs_valid(cs, top)) {
    return 0;
  }
  return cs->depth[top];
}

size_t ds_cactus_stack_live(const ds_cactus_stack_t *cs) {
  return cs == NULL ? 0 : cs->live;
}

void ds_cactus_stack_walk(const ds_cactus_stack_t *cs, size_t top,
                          bool (*visit)(const void *elem, void *user),
                          void *user) {
  size_t v;
  if (cs == NULL || visit == NULL || (top != CS_EMPTY && !cs_valid(cs, top))) {
    return;
  }
  for (v = top; v != CS_EMPTY; v = cs->parent[v]) {
    if (!visit(cs->data + v * cs->elem_size, user)) {
      return;
    }
  }
}
