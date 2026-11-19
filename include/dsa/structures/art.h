#ifndef DS_ART_H
#define DS_ART_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <dsa/core/status.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Adaptive Radix Tree (ART): a space-efficient radix tree mapping byte-string
 * keys to void* values. Its defining trick is adaptive node sizing: each inner
 * node is one of four layouts -- Node4, Node16, Node48, Node256 -- and grows
 * or shrinks with hysteresis as its child count changes, so sparse nodes stay
 * small while dense nodes stay O(1) to index. Path compression
 * collapses chains of single-child nodes. This is a portable scalar
 * implementation (no SIMD). Keys are arbitrary byte strings, including empty
 * keys and embedded NULs. A terminal slot is distinct from byte children.
 * Compressed prefixes keep their full size; up to 10 bytes are inline and
 * longer prefixes have an owned buffer. Parent links make walks and
 * destruction iterative with O(1) auxiliary memory.
 */

typedef struct art ds_art_t;

/* NULL when memory runs out */
/* Context owns all internal storage and must outlive the container. NULL
 * context selects the default. On failure *out is NULL. The context is
 * borrowed. */
ds_status_t ds_art_create_context(ds_context_t *context, ds_art_t **out);

ds_art_t *ds_art_create(ds_context_t *context);
/* DS_OK for a new key, DS_EXISTS for an update. Errors leave the tree and
 * borrowed values unchanged; every allocation precedes the commit. */
ds_status_t ds_art_insert_ex(ds_art_t *art, const unsigned char *key,
                             size_t key_length, void *value);
/* true for a new key; false on update or failure (use _ex to distinguish). */
bool ds_art_insert(ds_art_t *art, const unsigned char *key, size_t key_length,
                   void *value);
bool ds_art_contains(ds_art_t *art, const unsigned char *key,
                     size_t key_length);
void *ds_art_get(ds_art_t *art, const unsigned char *key, size_t key_length);
bool ds_art_remove(ds_art_t *art, const unsigned char *key, size_t key_length,
                   void **out_value);
/* Removal succeeds even if optional node shrinking/prefix compression cannot
 * allocate: the larger layout or a unary node remains valid. */
size_t ds_art_size(ds_art_t *art);
bool ds_art_is_empty(ds_art_t *art);
/* iterate keys in lexicographic order without allocation; callbacks must not
 * mutate/destroy the tree. Empty/prefix keys precede their extensions. */
void ds_art_walk(ds_art_t *art,
                 void (*callback)(const unsigned char *, size_t, void *,
                                  void *),
                 void *user);
void ds_art_destroy(ds_art_t *art, void (*destroy_value)(void *));

typedef ds_status_t (*ds_art_visit_fn)(const unsigned char *key, size_t length,
                                       void *value, void *user);
/* Find distinguishes a stored NULL value from absence. Error/DS_NOT_FOUND
 * preserves *out. NULL key is valid only for zero length. */
ds_status_t ds_art_find_ex(ds_art_t *tree, const unsigned char *key,
                           size_t length, void **out);
/* Stops on the visitor's first non-DS_OK status. Visits already performed are
 * not rolled back; borrowed key/value views may not be retained or mutated.
 * Radix traversal also reports allocation/overflow failures in its scratch. */
ds_status_t ds_art_walk_with(ds_art_t *tree, ds_art_visit_fn visit,
                             void *user);
/* Optional destroy receives each stored value and the supplied user data;
 * it must not inspect/traverse/mutate this container during destruction. */
void ds_art_destroy_with(ds_art_t *tree, void (*destroy)(void *, void *),
                         void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_ART_H */
