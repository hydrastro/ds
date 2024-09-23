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
static inline void mutex_lock(mutex_t *lock) { EnterCriticalSection(lock); }

static inline void mutex_unlock(mutex_t *lock) { LeaveCriticalSection(lock); }

static inline void mutex_init(mutex_t *lock) {
  InitializeCriticalSection(lock);
}

static inline void mutex_init_recursive(mutex_t *lock) {
  InitializeCriticalSection(lock);
}

static inline void mutex_destroy(mutex_t *lock) { DeleteCriticalSection(lock); }
#else
static inline void mutex_lock(mutex_t *lock) { pthread_mutex_lock(lock); }

static inline void mutex_unlock(mutex_t *lock) { pthread_mutex_unlock(lock); }

static inline void mutex_init(mutex_t *lock) { pthread_mutex_init(lock, NULL); }

static inline void mutex_init_recursive(mutex_t *lock) {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(lock, &attr);
}

static inline void mutex_destroy(mutex_t *lock) { pthread_mutex_destroy(lock); }
#endif

#define LOCK_INIT(structure)                                                   \
  (structure)->is_thread_safe = true;                                          \
  mutex_init(&(structure)->lock);

#define LOCK_INIT_RECURSIVE(structure)                                         \
  (structure)->is_thread_safe = true;                                          \
  mutex_init_recursive(&(structure)->lock);

#define LOCK_DESTROY(structure) mutex_destroy(&(structure)->lock);
