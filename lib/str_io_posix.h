#ifndef DS_IO_POSIX_H
#define DS_IO_POSIX_H
#pragma once
#include <stddef.h>

long ds_posix_write_cb(void *ud, const void *buf, size_t n);
long ds_posix_writev_cb(void *ud, const void *const *bufs, const size_t *lens,
                        size_t count);

#endif /* DS_IO_POSIX_H */
