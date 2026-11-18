#include "fenwick_tree.h"
#include "checked.h"
#include "legacy_alloc.h"
#include <limits.h>
#include <stdlib.h>

typedef struct fw_term {
  unsigned long magnitude;
  bool negative;
} fw_term_t;

/* Cancel opposite signs before accumulating like signs. Only a final sum
 * outside long's range is rejected; LONG_MIN need never be negated as long. */
static ds_status_t fw_sum_terms(const fw_term_t *terms, size_t count,
                                long *out) {
  size_t positive = 0, negative = 0;
  unsigned long magnitude = 0, negative_limit = 0UL - (unsigned long)LONG_MIN;
  bool sign = false;
  while (positive < count || negative < count) {
    const fw_term_t *term;
    unsigned long limit;
    while (positive < count && terms[positive].negative)
      positive++;
    while (negative < count && !terms[negative].negative)
      negative++;
    if (positive == count && negative == count)
      break;
    if ((sign && positive < count) || negative == count)
      term = &terms[positive++];
    else
      term = &terms[negative++];
    if (term->negative == sign) {
      limit = sign ? negative_limit : (unsigned long)LONG_MAX;
      if (magnitude > limit || term->magnitude > limit - magnitude)
        return DS_ERR_OVERFLOW;
      magnitude += term->magnitude;
    } else if (term->magnitude <= magnitude)
      magnitude -= term->magnitude;
    else {
      magnitude = term->magnitude - magnitude;
      sign = term->negative;
    }
  }
  if ((!sign && magnitude > (unsigned long)LONG_MAX) ||
      (sign && magnitude > negative_limit))
    return DS_ERR_OVERFLOW;
  *out = sign ? (magnitude == negative_limit ? LONG_MIN : -(long)magnitude)
              : (long)magnitude;
  return DS_OK;
}
static size_t fw_collect(const ds_fenwick_tree_t *tree, size_t end, bool negate,
                         fw_term_t *terms, size_t count) {
  while (end > 0) {
    long value = tree->data[end];
    terms[count].magnitude =
        value < 0 ? 0UL - (unsigned long)value : (unsigned long)value;
    terms[count].negative = value != 0 && ((value < 0) != negate);
    count++;
    end -= end & (~end + 1);
  }
  return count;
}
ds_status_t ds_fenwick_tree_range_ex(const ds_fenwick_tree_t *tree, size_t left,
                                     size_t right, long *out) {
  fw_term_t terms[2 * sizeof(size_t) * CHAR_BIT];
  size_t count;
  if (tree == NULL || out == NULL)
    return DS_ERR_NULL;
  if (left > right || right > tree->size)
    return DS_ERR_RANGE;
  count = fw_collect(tree, right, false, terms, 0);
  count = fw_collect(tree, left, true, terms, count);
  return fw_sum_terms(terms, count, out);
}
ds_status_t ds_fenwick_tree_prefix_ex(const ds_fenwick_tree_t *tree,
                                      size_t count, long *out) {
  return ds_fenwick_tree_range_ex(tree, 0, count, out);
}
ds_status_t ds_fenwick_tree_at_ex(const ds_fenwick_tree_t *tree, size_t index,
                                  long *out) {
  if (tree == NULL || out == NULL)
    return DS_ERR_NULL;
  if (index >= tree->size)
    return DS_ERR_RANGE;
  return ds_fenwick_tree_range_ex(tree, index, index + 1, out);
}
static ds_status_t fw_mutate(ds_fenwick_tree_t *tree, size_t index, long value,
                             bool assign) {
  size_t indices[sizeof(size_t) * CHAR_BIT];
  long values[sizeof(size_t) * CHAR_BIT], current = 0;
  size_t count = 0, i;
  ds_status_t st;
  if (tree == NULL)
    return DS_ERR_NULL;
  if (index >= tree->size)
    return DS_ERR_RANGE;
  if (assign) {
    st = ds_fenwick_tree_at_ex(tree, index, &current);
    if (st != DS_OK)
      return st;
  }
  i = index + 1;
  for (;;) {
    long next, part;
    size_t step = i & (~i + 1);
    if (!assign) {
      if (!ds_long_add(tree->data[i], value, &next))
        return DS_ERR_OVERFLOW;
    } else if (ds_long_sub(tree->data[i], current, &part)) {
      if (!ds_long_add(part, value, &next))
        return DS_ERR_OVERFLOW;
    } else if (!ds_long_add(tree->data[i], value, &part) ||
               !ds_long_sub(part, current, &next))
      return DS_ERR_OVERFLOW;
    indices[count] = i;
    values[count++] = next;
    if (step > tree->size - i)
      break;
    i += step;
  }
  for (i = 0; i < count; i++)
    tree->data[indices[i]] = values[i];
  return DS_OK;
}
ds_status_t ds_fenwick_tree_update_ex(ds_fenwick_tree_t *tree, size_t index,
                                      long delta) {
  return fw_mutate(tree, index, delta, false);
}
ds_status_t ds_fenwick_tree_set_ex(ds_fenwick_tree_t *tree, size_t index,
                                   long value) {
  return fw_mutate(tree, index, value, true);
}

ds_fenwick_tree_t *ds_fenwick_tree_create(size_t size) {
  return ds_fenwick_tree_create_alloc(size, malloc, free);
}

static ds_fenwick_tree_t *
fenwick_tree_create_internal(size_t size, void *(*allocator)(size_t),
                             void (*deallocator)(void *),
                             ds_context_t *context) {
  ds_fenwick_tree_t *tree;
  size_t i;
  if (allocator == NULL || deallocator == NULL ||
      size >= (size_t)-1 / sizeof(long)) {
    return NULL;
  }
  tree = (ds_fenwick_tree_t *)DS_LEGACY_ALLOCATE(context, allocator,
                                                 sizeof(ds_fenwick_tree_t));
  if (tree == NULL) {
    return NULL;
  }
  tree->context = context;
  tree->allocator = allocator;
  tree->deallocator = deallocator;
  tree->size = size;
  tree->data =
      (long *)DS_LEGACY_ALLOCATE(context, allocator, (size + 1) * sizeof(long));
  if (tree->data == NULL) {
    DS_LEGACY_RELEASE(context, deallocator, tree);
    return NULL;
  }
  for (i = 0; i <= size; i++) {
    tree->data[i] = 0;
  }
  return tree;
}
ds_fenwick_tree_t *ds_fenwick_tree_create_alloc(size_t size,
                                                void *(*allocator)(size_t),
                                                void (*deallocator)(void *)) {
  return fenwick_tree_create_internal(size, allocator, deallocator, NULL);
}
ds_status_t ds_fenwick_tree_create_context(size_t size, ds_context_t *context,
                                           ds_fenwick_tree_t **out) {

  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (size >= (size_t)-1 / sizeof(long))
    return DS_ERR_OVERFLOW;
  *out = fenwick_tree_create_internal(
      size, malloc, free, context != NULL ? context : ds_default_context());
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}

void ds_fenwick_tree_update(ds_fenwick_tree_t *tree, size_t index, long delta) {
  size_t i;
  if (index >= tree->size) {
    return;
  }
  i = index + 1;
  while (i <= tree->size) {
    tree->data[i] += delta;
    i += i & (~i + 1);
  }
}

void ds_fenwick_tree_set(ds_fenwick_tree_t *tree, size_t index, long value) {
  long current;
  if (index >= tree->size) {
    return;
  }
  current = ds_fenwick_tree_at(tree, index);
  ds_fenwick_tree_update(tree, index, value - current);
}

long ds_fenwick_tree_prefix_sum(ds_fenwick_tree_t *tree, size_t index) {
  long sum = 0;
  size_t i;
  if (tree->size == 0) {
    return 0;
  }
  i = index < tree->size ? index + 1 : tree->size;
  while (i > 0) {
    sum += tree->data[i];
    i -= i & (~i + 1);
  }
  return sum;
}

long ds_fenwick_tree_range_sum(ds_fenwick_tree_t *tree, size_t left,
                               size_t right) {
  long result;
  if (left > right || left >= tree->size) {
    return 0;
  }
  if (left == 0) {
    result = ds_fenwick_tree_prefix_sum(tree, right);
  } else {
    result = ds_fenwick_tree_prefix_sum(tree, right) -
             ds_fenwick_tree_prefix_sum(tree, left - 1);
  }
  return result;
}

long ds_fenwick_tree_at(ds_fenwick_tree_t *tree, size_t index) {
  long result;
  if (index >= tree->size) {
    return 0;
  }
  if (index == 0) {
    result = ds_fenwick_tree_prefix_sum(tree, 0);
  } else {
    result = ds_fenwick_tree_prefix_sum(tree, index) -
             ds_fenwick_tree_prefix_sum(tree, index - 1);
  }
  return result;
}

size_t ds_fenwick_tree_size(ds_fenwick_tree_t *tree) {
  size_t result;
  result = tree->size;
  return result;
}

void ds_fenwick_tree_clear(ds_fenwick_tree_t *tree) {
  size_t i;
  for (i = 0; i <= tree->size; i++) {
    tree->data[i] = 0;
  }
}

ds_fenwick_tree_t *ds_fenwick_tree_clone(ds_fenwick_tree_t *tree) {
  ds_fenwick_tree_t *new_tree;
  size_t i;
  if (tree == NULL) {
    return NULL;
  }
  new_tree = fenwick_tree_create_internal(tree->size, tree->allocator,
                                          tree->deallocator, tree->context);
  if (new_tree == NULL) {
    return NULL;
  }
  for (i = 0; i <= tree->size; i++) {
    new_tree->data[i] = tree->data[i];
  }
  return new_tree;
}

void ds_fenwick_tree_destroy(ds_fenwick_tree_t *tree) {
  DS_LEGACY_FREE(tree, tree->data);
  DS_LEGACY_FREE(tree, tree);
}
