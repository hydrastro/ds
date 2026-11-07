#include "schorr_waite.h"

#define SW_NONE DS_SCHORR_WAITE_NONE

ds_status_t ds_schorr_waite_mark(size_t *child, size_t n, size_t fields,
                                 const size_t *roots, size_t root_count,
                                 size_t *state,
                                 void (*visit)(size_t node, void *user),
                                 void *user, size_t *out_marked) {
  size_t r;
  size_t prev;
  size_t cur;
  size_t next;
  size_t i;
  size_t tmp;
  size_t marked = 0;
  if ((child == NULL && n > 0 && fields > 0) || state == NULL ||
      (roots == NULL && root_count > 0)) {
    return DS_ERR_NULL;
  }
  if (fields >= SW_NONE - 1 || (fields > 0 && n > SW_NONE / fields)) {
    return DS_ERR_OVERFLOW;
  }
  for (r = 0; r < root_count; r++) {
    if (roots[r] != SW_NONE && roots[r] >= n) {
      return DS_ERR_RANGE;
    }
  }
  for (r = 0; r < root_count; r++) {
    cur = roots[r];
    if (cur == SW_NONE || state[cur] != 0) {
      continue;
    }
    prev = SW_NONE;
    state[cur] = 1;
    marked++;
    if (visit != NULL) {
      visit(cur, user);
    }
    for (;;) {
      i = state[cur] - 1;
      if (i < fields) {
        next = child[cur * fields + i];
        if (next != SW_NONE && next < n && state[next] == 0) {
          /* advance: the field now points back to prev */
          child[cur * fields + i] = prev;
          prev = cur;
          cur = next;
          state[cur] = 1;
          marked++;
          if (visit != NULL) {
            visit(cur, user);
          }
        } else {
          state[cur]++;
        }
      } else {
        if (prev == SW_NONE) {
          break;
        }
        /* retreat: restore prev's field and step back */
        i = state[prev] - 1;
        tmp = child[prev * fields + i];
        child[prev * fields + i] = cur;
        cur = prev;
        prev = tmp;
        state[cur]++;
      }
    }
  }
  if (out_marked != NULL) {
    *out_marked = marked;
  }
  return DS_OK;
}
