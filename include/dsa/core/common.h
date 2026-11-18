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

#ifndef CAST
#define CAST(node, type) ((type *)(node))
#endif

/* Recover the containing object from a non-NULL pointer to an embedded node.
 * The pointer must address the named member of a live object of this type.
 * In C++ the type must be standard-layout. Use the CONST form for read-only
 * nodes; CAST is only suitable when the node is the first member. */
#define DS_CONTAINER_OF(node, type, member)                                    \
  ((type *)((unsigned char *)(node) - offsetof(type, member)))
#define DS_CONTAINER_OF_CONST(node, type, member)                              \
  ((const type *)((const unsigned char *)(node) - offsetof(type, member)))

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

/* printf format checking for a function's format argument fmt and its
 * first variadic argument first (0 for a va_list), where the compiler
 * offers it (GCC, clang); nothing elsewhere */
#ifndef DS_FORMAT_PRINTF
#if defined(__GNUC__)
#define DS_FORMAT_PRINTF(fmt, first) __attribute__((format(printf, fmt, first)))
#else
#define DS_FORMAT_PRINTF(fmt, first)
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* DS_COMMON_H */
