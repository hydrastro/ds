#include "str_io_posix.h"
#include <unistd.h>
#include <sys/uio.h>

long ds_posix_write_cb(void *ud, const void *buf, size_t n) {
  int fd = (int)(long)ud;
  ssize_t w = write(fd, buf, n);
  return (w < 0) ? -1 : (long)w;
}

long ds_posix_writev_cb(void *ud, const void * const *bufs, const size_t *lens, size_t count) {
  struct iovec vec[16];
  size_t off = 0;
  int fd = (int)(long)ud;

  while (off < count) {
    size_t batch = count - off, i;
    ssize_t w;
    if (batch > 16) batch = 16;

    for (i = 0; i < batch; ++i) {
      vec[i].iov_base = (void*)bufs[off + i];
      vec[i].iov_len  = lens[off + i];
    }

    while (1) {
      w = writev(fd, vec, (int)batch);
      if (w < 0) return -1;
      if (w == 0) return -1;

      {
        ssize_t remain = w;
        size_t k = 0;
        while (k < batch && remain > 0) {
          if (remain >= (ssize_t)vec[k].iov_len) {
            remain -= (ssize_t)vec[k].iov_len;
            ++k;
          } else {
            vec[k].iov_base = (char*)vec[k].iov_base + remain;
            vec[k].iov_len  -= (size_t)remain;
            remain = 0;
          }
        }
        if (k == batch) break;
        if (k > 0) {
          size_t m;
          for (m = 0; k + m < batch; ++m) vec[m] = vec[k + m];
          batch -= k;
        }
      }
    }

    off += (size_t)(batch);
  }

  return 0;
}
