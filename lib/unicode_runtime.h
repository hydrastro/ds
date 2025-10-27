#ifndef DS_UNICODE_RUNTIME_H
#define DS_UNICODE_RUNTIME_H

#include "str.h"

int ds__ucd_normalize(ds_str_t *dst, const ds_str_t *src, int compat,
                      int compose);
int ds__ucd_casefold(ds_str_t *dst, const ds_str_t *src);
int ds__ucd_tolower(ds_str_t *dst, const ds_str_t *src);
int ds__ucd_toupper(ds_str_t *dst, const ds_str_t *src);
long ds__ucd_grapheme_len(const ds_str_t *s);


enum {
  GB_Other = 0,
  GB_CR,
  GB_LF,
  GB_Control,
  GB_Extend,
  GB_ZWJ,
  GB_SpacingMark,
  GB_Prepend,
  GB_Regional_Indicator,
  GB_L, GB_V, GB_T, GB_LV, GB_LVT
};

int load_EmojiData(void);

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
  u32map ep_prop;
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


/* ---- internal grapheme iterator (rule engine lives here) ---- */
typedef struct {
  const char *buf;
  size_t len;
  size_t i;                 /* next read position (byte) */
  size_t cur_start;         /* current cluster start (byte) */
  int have_prev;

  /* UAX #29 state */
  unsigned long prev_prop;
  int ri_run_len;           /* consecutive RI count in current run */
  int prev_is_zwj_ign_ext;  /* we saw ZWJ with only Extend between bases */
  int last_base_is_EP;      /* last non-Extend/ZWJ base was Extended_Pictographic */

  /* scratch (avoid re-decl in C89 loops) */
  unsigned long cp, prop;
  int break_here;
  int prev_is_Control, cur_is_Control;
} ds__grapheme_iter_t;

/* Initialize iterator over a byte buffer */
int ds__grapheme_iter_init(ds__grapheme_iter_t *it, const char *buf, size_t len);

/* Produce the next cluster as [start,len] in bytes.
   Returns 1 if a cluster was produced, 0 on end, or -1 on error. */
int ds__grapheme_iter_next(ds__grapheme_iter_t *it, size_t *out_start, size_t *out_len);



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
void ensure_ucd_once_lock(void);
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
int is_EP(unsigned long cp);

#endif /* DS_UNICODE_RUNTIME_H */
