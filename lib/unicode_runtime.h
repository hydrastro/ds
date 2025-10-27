#ifndef DS_UNICODE_RUNTIME_H
#define DS_UNICODE_RUNTIME_H

#include "str.h"

int ds__ucd_normalize(ds_str_t *dst, const ds_str_t *src, int compat,
                      int compose);
int ds__ucd_casefold(ds_str_t *dst, const ds_str_t *src);
int ds__ucd_tolower(ds_str_t *dst, const ds_str_t *src);
int ds__ucd_toupper(ds_str_t *dst, const ds_str_t *src);
long ds__ucd_grapheme_len(const ds_str_t *s);

typedef struct {
  unsigned long *p;
  size_t n, cap;
} u32vec_t;
typedef struct {
  unsigned long k;
  unsigned long v;
  int used;
} u32map_pair;
typedef struct {
  u32map_pair *a;
  size_t n_used, cap;
} u32map;
typedef struct {
  unsigned long k;
  unsigned long v;
  int used;
} pairmap_pair;
typedef struct {
  pairmap_pair *a;
  size_t cap;
} pairmap;
typedef struct {
  u32map ccc;
  u32map decomp_head;
  u32vec_t decomp_pool;

  u32map tolower_map;
  u32map toupper_map;

  u32map fold_head;
  u32vec_t fold_pool;

  pairmap comp_map;
  u32map comp_exclusions;

  u32map gb_prop;
  int inited;
} ucd_t;

#define SBase 0xAC00ul
#define LBase 0x1100ul
#define VBase 0x1161ul
#define TBase 0x11A7ul
#define LCount 19ul
#define VCount 21ul
#define TCount 28ul
#define NCount (VCount * TCount)
#define SCount (LCount * NCount)

int u32vec_push(u32vec_t *v, unsigned long x);
void u32vec_free(u32vec_t *v);
unsigned long uh32(unsigned long x);
int u32map_init(u32map *m, size_t cap);
void u32map_free(u32map *m);
int u32map_put(u32map *m, unsigned long k, unsigned long v);
int u32map_get(const u32map *m, unsigned long k, unsigned long *out);
int pairmap_init(pairmap *m, size_t cap);
void pairmap_free(pairmap *m);
unsigned long keypair(unsigned long a, unsigned long b);
int pairmap_put(pairmap *m, unsigned long a, unsigned long b, unsigned long v);
int pairmap_get(const pairmap *m, unsigned long a, unsigned long b,
                unsigned long *out);
unsigned long uh_from_hex(const char *s, int *ok);
int hex_to_u32(const char *s, unsigned long *out);
FILE *ucd_fopen(const char *name);
int load_UnicodeData(void);
int load_CaseFolding(void);
int load_CompositionExclusions(void);
int load_DerivedNormalizationProps(void);
int load_GraphemeBreakProperty(void);
int build_comp_pairs(void);
void ucd_free(void);
int ucd_init_once(void);
int ucd_do_init(void);
int u8_next(const char *buf, size_t len, size_t *ioff, unsigned long *out_cp);
int u8_push(ds_str_t *s, unsigned long cp);
int is_Hangul_S(unsigned long cp);
int is_Hangul_L(unsigned long cp);
int is_Hangul_V(unsigned long cp);
int is_Hangul_T(unsigned long cp);
void decompose_Hangul(unsigned long s, u32vec_t *out);
int compose_Hangul(unsigned long *LVT, unsigned long next, size_t *consumed);
unsigned long get_ccc(unsigned long cp);
int decompose_cp(unsigned long cp, int compat, u32vec_t *out);
void reorder_ccc(u32vec_t *v);
void compose_vec(u32vec_t *v);
int fold_cp(unsigned long cp, u32vec_t *out);
unsigned long gb_get(unsigned long cp);

#endif /* DS_UNICODE_RUNTIME_H */
