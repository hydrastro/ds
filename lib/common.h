#ifndef CAST
#define CAST(node, type) ((type *)(node))
#endif

#ifndef LOCK
#define LOCK(structure)                                                        \
  if (structure->is_thread_safe) {                                             \
    pthread_mutex_lock(&(structure)->lock);                                    \
  }
#endif

#ifndef UNLOCK
#define UNLOCK(structure)                                                      \
  if (structure->is_thread_safe) {                                             \
    pthread_mutex_unlock(&(structure)->lock);                                  \
  }
#endif

#define LOCK_INIT(structure)                                                   \
  (structure)->is_thread_safe = true;                                          \
  pthread_mutex_init(&(structure)->lock, NULL);

#define LOCK_INIT_RECURSIVE(structure)                                         \
  (structure)->is_thread_safe = true;                                          \
  pthread_mutexattr_t attr;                                                    \
  pthread_mutexattr_init(&attr);                                               \
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);                   \
  pthread_mutex_init(&(structure)->lock, &attr);

#define LOCK_DESTROY(structure) pthread_mutex_destroy(&(structure)->lock);
