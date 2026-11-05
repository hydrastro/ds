#ifndef DS_ALGO_STRING_SEARCH_H
#define DS_ALGO_STRING_SEARCH_H

/*
 * Exact string matching: the classic single-pattern algorithms over byte
 * strings (after Charras and Lecroq's catalogue), all with the
 * stringology.h contract: every occurrence position is reported in
 * increasing order through on_match (return false to stop), and the number
 * reported is returned. An empty pattern matches nowhere; a scratch
 * allocation failure reports nothing (0).
 *
 *   brute force          O(nm), no preprocessing
 *   Morris-Pratt         border table, O(n + m)
 *   Boyer-Moore          bad character + good suffix, sublinear on average
 *   Horspool             bad character of the window's last byte only
 *   Raita                Horspool comparing last, first, middle bytes first
 *   Quick Search         Sunday: bad character of the byte after the window
 *   Smith                maximum of the Horspool and Quick Search shifts
 *   Zhu-Takaoka          Boyer-Moore with a two-byte bad character table
 *   Turbo-BM             Boyer-Moore remembering the last matched factor,
 *                        at most 2n comparisons
 *   Apostolico-Giancarlo Boyer-Moore with a skip table, at most 1.5n
 *   Apostolico-Crochemore KMP variant scanning from the first byte that
 *                        differs from its predecessor, at most 1.5n
 *   Not So Naive         brute force with two precomputed shifts
 *   Two Way              Crochemore-Perrin critical factorisation: linear
 *                        time, constant extra space
 *   Shift-Or             bit-parallel automaton (patterns up to the bits of
 *                        unsigned long; longer ones fall back to
 *                        Morris-Pratt)
 *   DFA                  the pattern's matching automaton, one table
 *                        lookup per text byte
 *   Skip Search          buckets of byte positions, windows of m bytes
 *   Alpha Skip Search    the same with factors of length ceil(log256 m)
 *   KmpSkip Search       Skip Search with KMP to avoid rescanning, linear
 *   Colussi              refined KMP: "no hole" positions first, at most
 *                        1.5n comparisons
 *   Reverse Colussi      Boyer-Moore family, compares in an order derived
 *                        from the pattern's periods, at most 2n
 *   Galil-Giancarlo      Colussi skipping runs of the first byte, at most
 *                        4n/3
 *   Galil-Seiferas       linear time, constant extra space
 *   Maximal Shift        positions compared by decreasing minimal shift
 *   Optimal Mismatch     positions compared rarest text byte first
 *   Reverse Factor       suffix automaton of the reversed pattern, optimal
 *                        O(n log m / m) on average
 *   Turbo Reverse Factor Reverse Factor remembering the matched prefix,
 *                        linear worst case
 *   ordered alphabets    Crochemore: maximal suffixes on the fly, constant
 *                        extra space
 *   ds_kmismatch_search  occurrences with at most k mismatches
 *   ds_commentz_walter_search  several patterns at once: Boyer-Moore shifts
 *                        over the trie of the reversed patterns; reports
 *                        (pattern, position) by increasing end position
 *
 * Scratch comes from the ds_context_t (NULL for the default).
 */

#include "../lib/context.h"
#include "stringology.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_string_search_algorithm {
  DS_SEARCH_BRUTE_FORCE = 0,
  DS_SEARCH_MORRIS_PRATT,
  DS_SEARCH_BOYER_MOORE,
  DS_SEARCH_HORSPOOL,
  DS_SEARCH_RAITA,
  DS_SEARCH_QUICK_SEARCH,
  DS_SEARCH_SMITH,
  DS_SEARCH_ZHU_TAKAOKA,
  DS_SEARCH_TURBO_BM,
  DS_SEARCH_APOSTOLICO_GIANCARLO,
  DS_SEARCH_APOSTOLICO_CROCHEMORE,
  DS_SEARCH_NOT_SO_NAIVE,
  DS_SEARCH_TWO_WAY,
  DS_SEARCH_SHIFT_OR,
  DS_SEARCH_DFA,
  DS_SEARCH_SKIP,
  DS_SEARCH_ALPHA_SKIP,
  DS_SEARCH_KMP_SKIP,
  DS_SEARCH_COLUSSI,
  DS_SEARCH_REVERSE_COLUSSI,
  DS_SEARCH_GALIL_GIANCARLO,
  DS_SEARCH_GALIL_SEIFERAS,
  DS_SEARCH_MAXIMAL_SHIFT,
  DS_SEARCH_OPTIMAL_MISMATCH,
  DS_SEARCH_REVERSE_FACTOR,
  DS_SEARCH_TURBO_REVERSE_FACTOR,
  DS_SEARCH_ORDERED_ALPHABET,
  DS_SEARCH_ALGORITHM_COUNT
} ds_string_search_algorithm_t;

size_t ds_string_search(ds_string_search_algorithm_t algorithm,
                        const unsigned char *text, size_t n,
                        const unsigned char *pattern, size_t m,
                        ds_match_func_t on_match, void *user,
                        ds_context_t *context);
const char *ds_string_search_name(ds_string_search_algorithm_t algorithm);

/* Positions where pattern matches text with at most k mismatching bytes
 * (Hamming distance), O(nm) worst case with early exit. */
size_t ds_kmismatch_search(const unsigned char *text, size_t n,
                           const unsigned char *pattern, size_t m, size_t k,
                           ds_match_func_t on_match, void *user);

/* Multi-pattern match: pattern index and start position. */
typedef bool (*ds_multi_match_func_t)(size_t pattern, size_t position,
                                      void *user);

/* Commentz-Walter over count patterns (empty ones never match): every
 * occurrence, ordered by end position (patterns ending at the same place
 * shortest first, ties by index descending). Returns the number reported;
 * 0 on allocation failure. */
size_t ds_commentz_walter_search(const unsigned char *text, size_t n,
                                 const unsigned char *const *patterns,
                                 const size_t *lengths, size_t count,
                                 ds_multi_match_func_t on_match, void *user,
                                 ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_STRING_SEARCH_H */
