#ifndef DS_IO_POSIX_H
#define DS_IO_POSIX_H
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ud is the file descriptor, cast to a pointer through long. Both write
 * everything they are given, up to LONG_MAX bytes (retrying short writes
 * and EINTR), and return the bytes written, as str_io.h's callbacks do;
 * after an error they return what was written before it, or -1. */
long ds_posix_write_cb(void *ud, const void *buf, size_t n);
long ds_posix_writev_cb(void *ud, const void *const *bufs, const size_t *lens,
                        size_t count);

#ifdef __cplusplus
}
#endif

#endif /* DS_IO_POSIX_H */
