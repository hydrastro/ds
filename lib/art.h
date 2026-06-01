#ifndef DS_ART_H
#define DS_ART_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Adaptive Radix Tree (ART): a space-efficient radix tree mapping byte-string
 * keys to void* values. Its defining trick is adaptive node sizing: each inner
 * node is one of four layouts -- Node4, Node16, Node48, Node256 -- and grows or
 * shrinks to the smallest layout that fits its current child count, so sparse
 * nodes stay tiny while dense nodes stay O(1) to index. Path compression
 * collapses chains of single-child nodes. This is a portable scalar
 * implementation (no SIMD). Keys should be distinct byte strings; for correct
 * prefix behavior callers typically include a terminating byte or use
 * fixed-length keys.
 */

typedef struct art ds_art_t;

ds_art_t *FUNC(art_create)(void);
ds_art_t *FUNC(art_create_alloc)(void *(*allocator)(size_t),
                                 void (*deallocator)(void *));
/* insert or update; returns true if a new key was added, false on update */
bool FUNC(art_insert)(ds_art_t *art, const unsigned char *key,
                      size_t key_length, void *value);
bool FUNC(art_contains)(ds_art_t *art, const unsigned char *key,
                        size_t key_length);
void *FUNC(art_get)(ds_art_t *art, const unsigned char *key, size_t key_length);
bool FUNC(art_remove)(ds_art_t *art, const unsigned char *key,
                      size_t key_length, void **out_value);
size_t FUNC(art_size)(ds_art_t *art);
bool FUNC(art_is_empty)(ds_art_t *art);
/* iterate keys in lexicographic order */
void FUNC(art_walk)(ds_art_t *art,
                    void (*callback)(const unsigned char *, size_t, void *,
                                     void *),
                    void *user);
void FUNC(art_destroy)(ds_art_t *art, void (*destroy_value)(void *));

#ifdef __cplusplus
}
#endif

#endif /* DS_ART_H */
