#ifndef DS_RESULT_H
#define DS_RESULT_H

#include "status.h"
#include <stddef.h>

typedef struct ds_result_ptr {
  ds_status_t status;
  void *value;
} ds_result_ptr_t;

typedef struct ds_result_size {
  ds_status_t status;
  size_t value;
} ds_result_size_t;

#endif /* DS_RESULT_H */
