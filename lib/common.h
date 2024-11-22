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

#ifdef _WIN32
void mutex_lock(mutex_t *lock) { EnterCriticalSection(lock); }

void mutex_unlock(mutex_t *lock) { LeaveCriticalSection(lock); }

void mutex_init(mutex_t *lock) {
  InitializeCriticalSection(lock);
}

void mutex_init_recursive(mutex_t *lock) {
  InitializeCriticalSection(lock);
}

void mutex_destroy(mutex_t *lock) { DeleteCriticalSection(lock); }
#else
void mutex_lock(mutex_t *lock) { pthread_mutex_lock(lock); }

void mutex_unlock(mutex_t *lock) { pthread_mutex_unlock(lock); }

void mutex_init(mutex_t *lock) { pthread_mutex_init(lock, NULL); }

void mutex_init_recursive(mutex_t *lock) {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
#ifdef PTHREAD_MUTEX_RECURSIVE
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
#else
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
#endif
  pthread_mutex_init(lock, &attr);
}

void mutex_destroy(mutex_t *lock) { pthread_mutex_destroy(lock); }
#endif

#define LOCK_INIT(structure)                                                   \
  (structure)->is_thread_safe = true;                                          \
  mutex_init(&(structure)->lock);

#define LOCK_INIT_RECURSIVE(structure)                                         \
  (structure)->is_thread_safe = true;                                          \
  mutex_init_recursive(&(structure)->lock);

#define LOCK_DESTROY(structure) mutex_destroy(&(structure)->lock);

#endif /* DS_COMMON_H */
