#ifndef DS_COMMON_H
#define DS_COMMON_H

#ifndef _WIN32
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#endif

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef FUNC
#undef FUNC
#endif

#define FUNC(name) name

#ifndef CAST
#define CAST(node, type) ((type *)(node))
#endif

#ifndef DS_VA_COPY
#if defined(__has_builtin)
#if __has_builtin(__builtin_va_copy)
#define DS_VA_COPY(dst, src) __builtin_va_copy((dst), (src))
#endif
#endif
#ifndef DS_VA_COPY
#ifdef __builtin_va_copy
#define DS_VA_COPY(dst, src) __builtin_va_copy((dst), (src))
#elif defined(__va_copy)
#define DS_VA_COPY(dst, src) __va_copy((dst), (src))
#elif defined(va_copy)
#define DS_VA_COPY(dst, src) va_copy((dst), (src))
#else
#error                                                                         \
    "No va_copy/__va_copy available on this platform; cannot safely copy va_list under -std=c89."
#endif
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* DS_COMMON_H */
