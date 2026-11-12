#ifndef DS_CONTEXT_H
#define DS_CONTEXT_H

#include "diagnostic.h"
#include "error.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A context bundles an allocator (alloc/calloc/realloc/free + user), a
 * diagnostic sink and the last error recorded by operations using it.
 *
 * Sharing: the last error is mutable state of the context. Containers that
 * share a context (in particular every container left on the default
 * context, ds_default_context()) also share it, so locking two containers
 * separately does not make concurrent use safe: give each thread (or each
 * independently locked container) its own context, or set
 * DS_CONTEXT_NO_LAST_ERROR so the context is never written by error
 * reporting (statuses are still returned and diagnostics still emitted).
 * The allocator functions themselves must be safe for however the
 * context is shared.
 */
#define DS_CONTEXT_NO_LAST_ERROR 0x1u

/*
 * Resource traits: what the context's allocator can do. Structures that
 * grow by realloc, free individual nodes or transfer nodes between
 * containers depend on these, and a fixed pool or an arena is not a
 * general heap. ds_context_init describes the C heap; the
 * ds_context_use_* adapters (arena, pool, buddy, debug) set their own; a
 * context given custom functions should describe them with
 * ds_context_set_resource.
 */
#define DS_RESOURCE_REALLOC 0x1u      /* resizes any live block */
#define DS_RESOURCE_FREE 0x2u         /* free returns memory for reuse */
#define DS_RESOURCE_FALLIBLE 0x4u     /* may return NULL */
#define DS_RESOURCE_BOUNDED 0x8u      /* fixed total capacity */
#define DS_RESOURCE_FIXED_BLOCK 0x10u /* one block size only */
#define DS_RESOURCE_HEAP                                                       \
  (DS_RESOURCE_REALLOC | DS_RESOURCE_FREE | DS_RESOURCE_FALLIBLE)

typedef struct ds_context {
  void *(*alloc)(size_t size, void *user);
  void *(*calloc)(size_t count, size_t size, void *user);
  void *(*realloc)(void *ptr, size_t size, void *user);
  void (*free)(void *ptr, void *user);
  void *user;
  ds_diagnostic_sink_t *diagnostic_sink;
  ds_error_t last_error;
  unsigned int flags;
  unsigned int resource; /* DS_RESOURCE_* traits */
} ds_context_t;

void ds_context_init(ds_context_t *context);
ds_context_t *ds_default_context(void);
void ds_context_set_diagnostic_sink(ds_context_t *context,
                                    ds_diagnostic_sink_t *sink);
void *ds_context_alloc(ds_context_t *context, size_t size);
void *ds_context_calloc(ds_context_t *context, size_t count, size_t size);
void *ds_context_realloc(ds_context_t *context, void *ptr, size_t size);
void ds_context_free(ds_context_t *context, void *ptr);
/* the resource traits (NULL: the default context) */
unsigned int ds_context_resource(const ds_context_t *context);
void ds_context_set_resource(ds_context_t *context, unsigned int traits);
/* DS_OK when the context has every trait in `required`, else
 * DS_ERR_UNSUPPORTED */
ds_status_t ds_context_require(const ds_context_t *context,
                               unsigned int required);
/* true when memory allocated through one context may be resized and
 * released through the other: the same allocator functions and user
 * data (NULL is the default context). This is the condition for moving
 * nodes between containers; the contexts' error state and diagnostics
 * may differ. */
bool ds_context_compatible(const ds_context_t *a, const ds_context_t *b);
void ds_context_clear_error(ds_context_t *context);
const ds_error_t *ds_context_last_error(const ds_context_t *context);
ds_status_t ds_context_set_error(ds_context_t *context, ds_status_t status,
                                 const char *id, const char *module,
                                 const char *message, const char *file,
                                 unsigned long line, const char *function);
ds_status_t ds_context_set_error_desc(ds_context_t *context,
                                      const ds_error_desc_t *desc,
                                      const char *file, unsigned long line,
                                      const char *function);
void ds_context_emit_diagnostic(ds_context_t *context,
                                const ds_diagnostic_desc_t *desc,
                                const ds_diagnostic_arg_t *args,
                                size_t arg_count, const char *file,
                                unsigned long line, const char *function);

#define DS_CONTEXT_ERROR(ctx, status, id, module, message)                     \
  ds_context_set_error((ctx), (status), (id), (module), (message), __FILE__,   \
                       (unsigned long)__LINE__, DS_FUNCTION)

#define DS_CONTEXT_ERROR_DESC(ctx, desc)                                       \
  ds_context_set_error_desc((ctx), (desc), __FILE__, (unsigned long)__LINE__,  \
                            DS_FUNCTION)

#define DS_CONTEXT_DIAG(ctx, desc, args, arg_count)                            \
  ds_context_emit_diagnostic((ctx), (desc), (args), (arg_count), __FILE__,     \
                             (unsigned long)__LINE__, DS_FUNCTION)

#ifdef __cplusplus
}
#endif

#endif /* DS_CONTEXT_H */
