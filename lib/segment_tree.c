#include "segment_tree.h"
#include "checked.h"
#include "legacy_alloc.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static long seg_identity(const ds_segment_tree_t *st) {
  return st->op == DS_SEGMENT_TREE_MIN   ? LONG_MAX
         : st->op == DS_SEGMENT_TREE_MAX ? LONG_MIN
                                         : 0L;
}
static bool seg_combine(const ds_segment_tree_t *st, long a, long b,
                        long *out) {
  if (st->op == DS_SEGMENT_TREE_SUM)
    return ds_long_add(a, b, out);
  *out = st->op == DS_SEGMENT_TREE_MIN ? (a < b ? a : b) : (a > b ? a : b);
  return true;
}
static unsigned long seg_magnitude(long value) {
  return value < 0 ? (unsigned long)(-(value + 1)) + 1UL : (unsigned long)value;
}
/* base + value * count, allowing the product to exceed LONG_MAX when it
 * cancels against base. An unsigned magnitude can hold every fitting case. */
static bool seg_scaled(long base, long value, size_t count, long *out) {
  unsigned long product, magnitude, n, limit;
  bool negative;
  if (value == 0 || count == 0) {
    *out = base;
    return true;
  }
  n = (unsigned long)count;
  if ((size_t)n != count)
    return false;
  magnitude = seg_magnitude(value);
  if (magnitude > ULONG_MAX / n)
    return false;
  product = magnitude * n;
  magnitude = seg_magnitude(base);
  negative = value < 0;
  if ((base < 0) == negative) {
    if (product > ULONG_MAX - magnitude)
      return false;
    product += magnitude;
  } else if (product >= magnitude) {
    product -= magnitude;
  } else {
    product = magnitude - product;
    negative = base < 0;
  }
  limit = negative ? seg_magnitude(LONG_MIN) : (unsigned long)LONG_MAX;
  if (product > limit)
    return false;
  if (negative && product == seg_magnitude(LONG_MIN))
    *out = LONG_MIN;
  else
    *out = negative ? -(long)product : (long)product;
  return true;
}

typedef struct seg_snapshot {
  size_t node;
  long aggregate, add, set, minimum, maximum;
  bool has_set;
} seg_snapshot_t;
#define SEG_LOCAL_UNDO 24

typedef struct seg_journal {
  ds_segment_tree_t *tree;
  seg_snapshot_t *data;
  size_t size, capacity;
  seg_snapshot_t local[SEG_LOCAL_UNDO];
} seg_journal_t;

static void seg_journal_init(seg_journal_t *journal, ds_segment_tree_t *tree) {
  journal->tree = tree;
  journal->data = journal->local;
  journal->size = 0;
  journal->capacity = SEG_LOCAL_UNDO;
}
static ds_status_t seg_record(seg_journal_t *journal, size_t node) {
  ds_segment_tree_t *st = journal->tree;
  seg_snapshot_t *entry;
  if (journal->size == journal->capacity) {
    size_t capacity;
    seg_snapshot_t *data;
    if (journal->capacity > (size_t)-1 / 2 / sizeof(*data))
      return DS_ERR_OVERFLOW;
    capacity = journal->capacity * 2;
    data = (seg_snapshot_t *)DS_LEGACY_ALLOC(st, capacity * sizeof(*data));
    if (data == NULL)
      return DS_ERR_ALLOC;
    memcpy(data, journal->data, journal->size * sizeof(*data));
    if (journal->data != journal->local)
      DS_LEGACY_FREE(st, journal->data);
    journal->data = data;
    journal->capacity = capacity;
  }
  entry = &journal->data[journal->size++];
  entry->node = node;
  entry->aggregate = st->tree[node];
  entry->add = st->lazy_add[node];
  entry->set = st->lazy_set[node];
  entry->has_set = st->has_set[node];
  entry->minimum = st->minimum[node];
  entry->maximum = st->maximum[node];
  return DS_OK;
}
static void seg_journal_end(seg_journal_t *journal, bool rollback) {
  ds_segment_tree_t *st = journal->tree;
  if (rollback) {
    while (journal->size != 0) {
      seg_snapshot_t *entry = &journal->data[--journal->size];
      size_t node = entry->node;
      st->tree[node] = entry->aggregate;
      st->lazy_add[node] = entry->add;
      st->lazy_set[node] = entry->set;
      st->has_set[node] = entry->has_set;
      st->minimum[node] = entry->minimum;
      st->maximum[node] = entry->maximum;
    }
  }
  if (journal->data != journal->local)
    DS_LEGACY_FREE(st, journal->data);
}
static ds_status_t seg_apply(seg_journal_t *journal, size_t node, size_t count,
                             long value, bool assign) {
  ds_segment_tree_t *st = journal->tree;
  long aggregate, minimum, maximum, add, set;
  bool has_set;
  ds_status_t status;
  if (assign) {
    if (!seg_scaled(0, value, st->op == DS_SEGMENT_TREE_SUM ? count : 1,
                    &aggregate))
      return DS_ERR_OVERFLOW;
    minimum = maximum = value;
    set = value;
    add = 0;
    has_set = true;
  } else {
    if (!ds_long_add(st->minimum[node], value, &minimum) ||
        !ds_long_add(st->maximum[node], value, &maximum) ||
        !seg_scaled(st->tree[node], value,
                    st->op == DS_SEGMENT_TREE_SUM ? count : 1, &aggregate))
      return DS_ERR_OVERFLOW;
    has_set = st->has_set[node];
    add = st->lazy_add[node];
    set = st->lazy_set[node];
    if (count > 1) {
      if (has_set) {
        if (!ds_long_add(set, value, &set))
          return DS_ERR_OVERFLOW;
      } else if (!ds_long_add(add, value, &add))
        return DS_ERR_OVERFLOW;
    }
  }
  if (count == 1) {
    has_set = false;
    set = 0;
    add = 0;
  }
  status = seg_record(journal, node);
  if (status != DS_OK)
    return status;
  st->tree[node] = aggregate;
  st->minimum[node] = minimum;
  st->maximum[node] = maximum;
  st->lazy_set[node] = set;
  st->lazy_add[node] = add;
  st->has_set[node] = has_set;
  return DS_OK;
}
static ds_status_t seg_push(seg_journal_t *journal, size_t node, size_t left,
                            size_t mid, size_t right) {
  ds_segment_tree_t *st = journal->tree;
  ds_status_t status;
  if (!st->has_set[node] && st->lazy_add[node] == 0)
    return DS_OK;
  if (st->has_set[node]) {
    status =
        seg_apply(journal, 2 * node, mid - left + 1, st->lazy_set[node], true);
    if (status != DS_OK)
      return status;
    status =
        seg_apply(journal, 2 * node + 1, right - mid, st->lazy_set[node], true);
    if (status != DS_OK)
      return status;
  }
  if (st->lazy_add[node] != 0) {
    status =
        seg_apply(journal, 2 * node, mid - left + 1, st->lazy_add[node], false);
    if (status != DS_OK)
      return status;
    status = seg_apply(journal, 2 * node + 1, right - mid, st->lazy_add[node],
                       false);
    if (status != DS_OK)
      return status;
  }
  status = seg_record(journal, node);
  if (status != DS_OK)
    return status;
  st->has_set[node] = false;
  st->lazy_set[node] = 0;
  st->lazy_add[node] = 0;
  return DS_OK;
}
static ds_status_t seg_build(ds_segment_tree_t *st, const long *values,
                             size_t node, size_t left, size_t right) {
  size_t mid;
  ds_status_t status;
  if (left == right) {
    st->tree[node] = st->minimum[node] = st->maximum[node] = values[left];
    return DS_OK;
  }
  mid = left + (right - left) / 2;
  status = seg_build(st, values, node * 2, left, mid);
  if (status != DS_OK)
    return status;
  status = seg_build(st, values, node * 2 + 1, mid + 1, right);
  if (status != DS_OK)
    return status;
  if (!seg_combine(st, st->tree[node * 2], st->tree[node * 2 + 1],
                   &st->tree[node]))
    return DS_ERR_OVERFLOW;
  st->minimum[node] = st->minimum[node * 2] < st->minimum[node * 2 + 1]
                          ? st->minimum[node * 2]
                          : st->minimum[node * 2 + 1];
  st->maximum[node] = st->maximum[node * 2] > st->maximum[node * 2 + 1]
                          ? st->maximum[node * 2]
                          : st->maximum[node * 2 + 1];
  return DS_OK;
}
static ds_status_t
seg_create_internal(const long *values, size_t n, ds_segment_tree_op_t op,
                    void *(*allocator)(size_t), void (*deallocator)(void *),
                    ds_context_t *context, ds_segment_tree_t **out) {
  ds_segment_tree_t *st;
  ds_status_t status;
  size_t capacity, i;
  *out = NULL;
  if (allocator == NULL || deallocator == NULL || (values == NULL && n != 0))
    return DS_ERR_NULL;
  if (op != DS_SEGMENT_TREE_SUM && op != DS_SEGMENT_TREE_MIN &&
      op != DS_SEGMENT_TREE_MAX)
    return DS_ERR_INVALID;
  if (n > (size_t)-1 / 4 / sizeof(long) || n > (size_t)-1 / 4 / sizeof(bool))
    return DS_ERR_OVERFLOW;
  capacity = (n == 0 ? 1 : n) * 4;
  st = (ds_segment_tree_t *)DS_LEGACY_ALLOCATE(context, allocator, sizeof(*st));
  if (st == NULL)
    return DS_ERR_ALLOC;
  st->context = context;
  st->allocator = allocator;
  st->deallocator = deallocator;
  st->n = n;
  st->capacity = capacity;
  st->op = op;
  st->tree = (long *)DS_LEGACY_ALLOC(st, capacity * sizeof(long));
  st->lazy_add = (long *)DS_LEGACY_ALLOC(st, capacity * sizeof(long));
  st->lazy_set = (long *)DS_LEGACY_ALLOC(st, capacity * sizeof(long));
  st->minimum = (long *)DS_LEGACY_ALLOC(st, capacity * sizeof(long));
  st->maximum = (long *)DS_LEGACY_ALLOC(st, capacity * sizeof(long));
  st->has_set = (bool *)DS_LEGACY_ALLOC(st, capacity * sizeof(bool));
  if (st->tree == NULL || st->lazy_add == NULL || st->lazy_set == NULL ||
      st->minimum == NULL || st->maximum == NULL || st->has_set == NULL) {
    ds_segment_tree_destroy(st);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < capacity; ++i) {
    st->tree[i] = st->minimum[i] = st->maximum[i] = 0;
    st->lazy_add[i] = st->lazy_set[i] = 0;
    st->has_set[i] = false;
  }
  status = n != 0 ? seg_build(st, values, 1, 0, n - 1) : DS_OK;
  if (status != DS_OK) {
    ds_segment_tree_destroy(st);
    return status;
  }
  *out = st;
  return DS_OK;
}
ds_status_t ds_segment_tree_create_context(const long *values, size_t n,
                                           ds_segment_tree_op_t op,
                                           ds_context_t *context,
                                           ds_segment_tree_t **out) {
  if (out == NULL)
    return DS_ERR_NULL;
  return seg_create_internal(values, n, op, malloc, free,
                             context != NULL ? context : ds_default_context(),
                             out);
}
ds_segment_tree_t *ds_segment_tree_create_alloc(const long *values, size_t n,
                                                ds_segment_tree_op_t op,
                                                void *(*allocator)(size_t),
                                                void (*deallocator)(void *)) {
  ds_segment_tree_t *out;
  (void)seg_create_internal(values, n, op, allocator, deallocator, NULL, &out);
  return out;
}
ds_segment_tree_t *ds_segment_tree_create(const long *values, size_t n,
                                          ds_segment_tree_op_t op) {
  return ds_segment_tree_create_alloc(values, n, op, malloc, free);
}
static ds_status_t seg_update(seg_journal_t *journal, size_t node, size_t left,
                              size_t right, size_t ql, size_t qr, long value,
                              bool assign) {
  ds_segment_tree_t *st = journal->tree;
  size_t mid;
  long aggregate;
  ds_status_t status;
  if (right < ql || qr < left)
    return DS_OK;
  if (ql <= left && right <= qr)
    return seg_apply(journal, node, right - left + 1, value, assign);
  mid = left + (right - left) / 2;
  status = seg_push(journal, node, left, mid, right);
  if (status != DS_OK)
    return status;
  status = seg_update(journal, node * 2, left, mid, ql, qr, value, assign);
  if (status != DS_OK)
    return status;
  status =
      seg_update(journal, node * 2 + 1, mid + 1, right, ql, qr, value, assign);
  if (status != DS_OK)
    return status;
  if (!seg_combine(st, st->tree[node * 2], st->tree[node * 2 + 1], &aggregate))
    return DS_ERR_OVERFLOW;
  status = seg_record(journal, node);
  if (status != DS_OK)
    return status;
  st->tree[node] = aggregate;
  st->minimum[node] = st->minimum[node * 2] < st->minimum[node * 2 + 1]
                          ? st->minimum[node * 2]
                          : st->minimum[node * 2 + 1];
  st->maximum[node] = st->maximum[node * 2] > st->maximum[node * 2 + 1]
                          ? st->maximum[node * 2]
                          : st->maximum[node * 2 + 1];
  return DS_OK;
}
static ds_status_t seg_update_checked(ds_segment_tree_t *st, size_t left,
                                      size_t right, long value, bool assign) {
  seg_journal_t journal;
  ds_status_t status;
  if (st == NULL)
    return DS_ERR_NULL;
  if (left > right || right >= st->n)
    return DS_ERR_RANGE;
  seg_journal_init(&journal, st);
  status = seg_update(&journal, 1, 0, st->n - 1, left, right, value, assign);
  seg_journal_end(&journal, status != DS_OK);
  return status;
}
ds_status_t ds_segment_tree_update_add_ex(ds_segment_tree_t *st, size_t left,
                                          size_t right, long value) {
  return seg_update_checked(st, left, right, value, false);
}
ds_status_t ds_segment_tree_update_assign_ex(ds_segment_tree_t *st, size_t left,
                                             size_t right, long value) {
  return seg_update_checked(st, left, right, value, true);
}
void ds_segment_tree_update_add(ds_segment_tree_t *st, size_t left,
                                size_t right, long value) {
  (void)ds_segment_tree_update_add_ex(st, left, right, value);
}
void ds_segment_tree_update_assign(ds_segment_tree_t *st, size_t left,
                                   size_t right, long value) {
  (void)ds_segment_tree_update_assign_ex(st, left, right, value);
}
static ds_status_t seg_query(seg_journal_t *journal, size_t node, size_t left,
                             size_t right, size_t ql, size_t qr, long *out) {
  size_t mid;
  long a, b;
  ds_status_t status;
  if (right < ql || qr < left) {
    *out = seg_identity(journal->tree);
    return DS_OK;
  }
  if (ql <= left && right <= qr) {
    *out = journal->tree->tree[node];
    return DS_OK;
  }
  mid = left + (right - left) / 2;
  status = seg_push(journal, node, left, mid, right);
  if (status != DS_OK)
    return status;
  status = seg_query(journal, node * 2, left, mid, ql, qr, &a);
  if (status != DS_OK)
    return status;
  status = seg_query(journal, node * 2 + 1, mid + 1, right, ql, qr, &b);
  if (status != DS_OK)
    return status;
  return seg_combine(journal->tree, a, b, out) ? DS_OK : DS_ERR_OVERFLOW;
}
ds_status_t ds_segment_tree_query_ex(ds_segment_tree_t *st, size_t left,
                                     size_t right, long *out) {
  seg_journal_t journal;
  ds_status_t status;
  long result;
  if (st == NULL || out == NULL)
    return DS_ERR_NULL;
  if (left > right || right >= st->n)
    return DS_ERR_RANGE;
  seg_journal_init(&journal, st);
  status = seg_query(&journal, 1, 0, st->n - 1, left, right, &result);
  seg_journal_end(&journal, true);
  if (status == DS_OK)
    *out = result;
  return status;
}
long ds_segment_tree_query(ds_segment_tree_t *st, size_t left, size_t right) {
  long result;
  if (st == NULL)
    return 0;
  result = seg_identity(st);
  (void)ds_segment_tree_query_ex(st, left, right, &result);
  return result;
}
size_t ds_segment_tree_size(ds_segment_tree_t *st) {
  return st != NULL ? st->n : 0;
}
void ds_segment_tree_destroy(ds_segment_tree_t *st) {
  if (st == NULL)
    return;
  if (st->tree != NULL)
    DS_LEGACY_FREE(st, st->tree);
  if (st->lazy_add != NULL)
    DS_LEGACY_FREE(st, st->lazy_add);
  if (st->lazy_set != NULL)
    DS_LEGACY_FREE(st, st->lazy_set);
  if (st->minimum != NULL)
    DS_LEGACY_FREE(st, st->minimum);
  if (st->maximum != NULL)
    DS_LEGACY_FREE(st, st->maximum);
  if (st->has_set != NULL)
    DS_LEGACY_FREE(st, st->has_set);
  DS_LEGACY_FREE(st, st);
}
