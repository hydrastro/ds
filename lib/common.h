#ifndef DS_COMMON_H
#define DS_COMMON_H

#ifndef CAST
#define CAST(node, type) ((type *)(node))
#endif

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION mutex_t;
#else
#include <pthread.h>
typedef pthread_mutex_t mutex_t;
#endif

#ifndef LOCK
#define LOCK(structure)                                                        \
  if ((structure)->is_thread_safe) {                                           \
    mutex_lock(&(structure)->lock);                                            \
  }
#endif

#ifndef UNLOCK
#define UNLOCK(structure)                                                      \
  if ((structure)->is_thread_safe) {                                           \
    mutex_unlock(&(structure)->lock);                                          \
  }
#endif

#define LOCK_INIT(structure)                                                   \
  (structure)->is_thread_safe = true;                                          \
  mutex_init(&(structure)->lock);

#define LOCK_INIT_RECURSIVE(structure)                                         \
  (structure)->is_thread_safe = true;                                          \
  mutex_init_recursive(&(structure)->lock);

#define LOCK_DESTROY(structure) mutex_destroy(&(structure)->lock);

void mutex_lock(mutex_t *lock);
void mutex_unlock(mutex_t *lock);
void mutex_init(mutex_t *lock);
void mutex_init_recursive(mutex_t *lock);
void mutex_destroy(mutex_t *lock);

#endif /* DS_COMMON_H */
