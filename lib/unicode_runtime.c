#include "unicode_runtime.h"
#include "str_unicode.h"
#include "str.h"
#include "str_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

int  u32vec_push(u32vec_t *v, unsigned long x) {
  unsigned long *np; size_t nc;
  if (v->n == v->cap) {
    nc = v->cap ? (v->cap * 2u) : 8u;
    np = (unsigned long*)realloc(v->p, nc * sizeof(unsigned long));
    if (!np) return -1;
    v->p = np; v->cap = nc;
  }
  v->p[v->n++] = x;
  return 0;
}
void u32vec_free(u32vec_t *v) {
  if (v->p) free(v->p);
  v->p = NULL; v->n = 0; v->cap = 0;
}

unsigned long uh32(unsigned long x){
  x ^= x >> 33;
  x *= 0xff51afd7u;
  x ^= x >> 29;
  x *= 0xc4ceb9feu;
  x ^= x >> 32;
  return x;
}
int u32map_init(u32map *m, size_t cap) {
  size_t i;
  m->cap = (cap < 16u) ? 16u : cap * 2u;
  m->n_used = 0;
  m->a = (u32map_pair*)calloc(m->cap, sizeof(u32map_pair));
  if (!m->a) return -1;
  for (i = 0; i < m->cap; ++i) m->a[i].used = 0;
  return 0;
}
void u32map_free(u32map *m) {
  if (m->a) free(m->a);
  m->a = NULL; m->cap = 0; m->n_used = 0;
}
int u32map_put(u32map *m, unsigned long k, unsigned long v) {
  size_t i, mask, pos, step;
  if (!m->a) return -1;
  mask = m->cap - 1;
  pos = (size_t)(uh32(k) & mask);
  step = 1;
  for (i = 0; i < m->cap; ++i) {
    u32map_pair *p = &m->a[pos];
    if (!p->used || p->k == k) {
      if (!p->used) m->n_used++;
      p->used = 1; p->k = k; p->v = v;
      return 0;
    }
    pos = (pos + step) & mask; step += 2;
  }
  return -1;
}
int u32map_get(const u32map *m, unsigned long k, unsigned long *out) {
  size_t i, mask, pos, step;
  if (!m->a) return 0;
  mask = m->cap - 1;
  pos = (size_t)(uh32(k) & mask);
  step = 1;
  for (i = 0; i < m->cap; ++i) {
    const u32map_pair *p = &m->a[pos];
    if (!p->used) return 0;
    if (p->k == k) { if (out) *out = p->v; return 1; }
    pos = (pos + step) & mask; step += 2;
  }
  return 0;
}

int pairmap_init(pairmap *m, size_t cap) {
  size_t i;
  m->cap = (cap < 16u) ? 16u : cap * 2u;
  m->a = (pairmap_pair*)calloc(m->cap, sizeof(pairmap_pair));
  if (!m->a) return -1;
  for (i = 0; i < m->cap; ++i) m->a[i].used = 0;
  return 0;
}
void pairmap_free(pairmap *m) {
  if (m->a) free(m->a);
  m->a = NULL; m->cap = 0;
}
unsigned long keypair(unsigned long a, unsigned long b){ return (a << 21) ^ b; }
int pairmap_put(pairmap *m, unsigned long a, unsigned long b, unsigned long v) {
  size_t i, mask, pos, step;
  if (!m->a) return -1;
  mask = m->cap - 1;
  pos = (size_t)(uh32(keypair(a,b)) & mask);
  step = 1;
  for (i = 0; i < m->cap; ++i) {
    pairmap_pair *p = &m->a[pos];
    if (!p->used || p->k == keypair(a,b)) { p->used = 1; p->k = keypair(a,b); p->v = v; return 0; }
    pos = (pos + step) & mask; step += 2;
  }
  return -1;
}
int pairmap_get(const pairmap *m, unsigned long a, unsigned long b, unsigned long *out) {
  size_t i, mask, pos, step;
  if (!m->a) return 0;
  mask = m->cap - 1;
  pos = (size_t)(uh32(keypair(a,b)) & mask);
  step = 1;
  for (i = 0; i < m->cap; ++i) {
    const pairmap_pair *p = &m->a[pos];
    if (!p->used) return 0;
    if (p->k == keypair(a,b)) { if (out) *out = p->v; return 1; }
    pos = (pos + step) & mask; step += 2;
  }
  return 0;
}

ucd_t U;

unsigned long uh_from_hex(const char *s, int *ok) {
  unsigned long v=0; char c; int any=0;
  *ok = 0;
  while ((c=*s++)!=0) {
    unsigned int d;
    if (c>='0'&&c<='9') d=(unsigned int)(c-'0');
    else if (c>='A'&&c<='F') d=(unsigned int)(c-'A'+10);
    else if (c>='a'&&c<='f') d=(unsigned int)(c-'a'+10);
    else break;
    any=1; v=(v<<4)|d;
  }
  if (!any) return 0;
  *ok=1; return v;
}
int hex_to_u32(const char *s, unsigned long *out) {
  int ok; unsigned long v = uh_from_hex(s, &ok);
  if (!ok) return -1;
  *out = v; return 0;
}

FILE *ucd_fopen(const char *name) {
  ds_str_t *p = FUNC(str_create)();
  FILE *f = NULL;
  if (!p) return NULL;
  if (FUNC(str_append_cstr)(p, "data/") != 0 || FUNC(str_append_cstr)(p, name) != 0) { FUNC(str_destroy)(p); return NULL; }
  f = fopen(p->buf, "rb");
  FUNC(str_destroy)(p);
  return f;
}

int load_UnicodeData(void) {
  FILE *f = ucd_fopen("UnicodeData.txt");
  char line[4096];
  if (!f) return -1;

  while (fgets(line, sizeof(line), f)) {
    char *p=line,*fields[15];
    int i;
    for (i=0;i<15;++i) {
      char *semi = strchr(p,';');
      fields[i]=p;
      if (!semi) { if (i<14) fields[i+1]=NULL; break; }
      *semi=0; p=semi+1;
    }
    if (!fields[0]) continue;

    {
      unsigned long cp;
      if (hex_to_u32(fields[0], &cp)!=0) continue;

      if (fields[3] && *fields[3]) {
        unsigned long ccc=0; const char *q=fields[3];
        while (*q>='0' && *q<='9') { ccc = ccc*10 + (unsigned long)(*q - '0'); ++q; }
        (void)u32map_put(&U.ccc, cp, ccc & 0xFFul);
      }

      if (fields[12] && *fields[12]) { unsigned long up; if (hex_to_u32(fields[12], &up)==0) (void)u32map_put(&U.toupper_map, cp, up); }
      if (fields[13] && *fields[13]) { unsigned long lo; if (hex_to_u32(fields[13], &lo)==0) (void)u32map_put(&U.tolower_map, cp, lo); }

      if (fields[5] && *fields[5]) {
        const char *q = fields[5];
        int compat=0;
        u32vec_t tmp; tmp.p=NULL; tmp.n=0; tmp.cap=0;

        if (*q=='<') {
          compat=1;
          while (*q && *q!='>') ++q;
          if (*q=='>') ++q;
          while (*q==' ') ++q;
        }
        while (*q) {
          unsigned long u; const char *start=q; char hex[16]; size_t l;
          while (*q && *q!=' ') ++q;
          l = (size_t)(q - start);
          if (l >= sizeof(hex)) l = sizeof(hex)-1;
          memcpy(hex, start, l); hex[l]='\0';
          if (hex_to_u32(hex, &u)!=0) { u32vec_free(&tmp); tmp.n=0; break; }
          if (u32vec_push(&tmp, u)!=0) { u32vec_free(&tmp); tmp.n=0; break; }
          while (*q==' ') ++q;
        }
        if (tmp.n > 0) {
          unsigned long head = ((unsigned long)compat<<24) | (unsigned long)tmp.n;
          (void)u32vec_push(&U.decomp_pool, head);
          for (i=0; i < (int)tmp.n; ++i) (void)u32vec_push(&U.decomp_pool, tmp.p[i]);
          (void)u32map_put(&U.decomp_head, cp, (unsigned long)(U.decomp_pool.n - (tmp.n + 1u)));
        }
        u32vec_free(&tmp);
      }
    }
  }
  fclose(f);
  return 0;
}

int load_CaseFolding(void) {
  FILE *f = ucd_fopen("CaseFolding.txt");
  char line[4096];
  if (!f) return -1;

  while (fgets(line, sizeof(line), f)) {
    char *p=line; unsigned long cp; char *semi;
    if (*p=='#' || *p=='\n' || *p=='\0') continue;

    semi = strchr(p,';');
    if (!semi) continue;
    *semi=0;
    if (hex_to_u32(p,&cp)!=0) continue;

    p = semi+1; while (*p==' ') ++p;
    if (!*p) continue;

    if (*p!='C' && *p!='S' && *p!='F') continue;
    {
      char status = *p;
      u32vec_t map; map.p=NULL; map.n=0; map.cap=0;

      p = strchr(p,';'); if(!p) continue; ++p; while(*p==' ') ++p;

      while (*p && *p!='#' && *p!='\n') {
        char hex[16]; char *q = p; size_t l;
        while (*q && *q!=' ' && *q!='\n' && *q!='#') ++q;
        l = (size_t)(q - p); if (l >= sizeof(hex)) l = sizeof(hex)-1;
        memcpy(hex, p, l); hex[l]='\0';
        {
          unsigned long u;
          if (hex_to_u32(hex, &u)==0) {
            if (u32vec_push(&map, u)!=0) { u32vec_free(&map); map.n=0; break; }
          }
        }
        p = q;
        while (*p==' ') ++p;
      }
      if (map.n > 0) {
        if (status!='F' && map.n>1) map.n=1;
        {
          unsigned long off = (unsigned long)U.fold_pool.n;
          unsigned long head = (unsigned long)map.n;
          size_t i2;
          (void)u32vec_push(&U.fold_pool, head);
          for (i2=0;i2<map.n;++i2) (void)u32vec_push(&U.fold_pool, map.p[i2]);
          (void)u32map_put(&U.fold_head, cp, off);
        }
      }
      u32vec_free(&map);
    }
  }
  fclose(f);
  return 0;
}

int load_CompositionExclusions(void) {
  FILE *f = ucd_fopen("CompositionExclusions.txt");
  char line[1024];
  if (!f) return -1;
  while (fgets(line,sizeof(line),f)) {
    char *p=line;
    char *hash;
    char *q;
    unsigned long cp;
    if (*p=='#' || *p=='\n' || *p=='\0') continue;
    hash = strchr(p,'#'); if (hash) *hash=0;
    q = p; while (*q && *q!=' ' && *q!='\t' && *q!='\n') ++q; *q=0;
    if (!*p) continue;
    if (hex_to_u32(p,&cp)==0) (void)u32map_put(&U.comp_exclusions, cp, 1ul);
  }
  fclose(f);
  return 0;
}

int load_DerivedNormalizationProps(void) {
  FILE *f = ucd_fopen("DerivedNormalizationProps.txt");
  if (!f) return -1;
  fclose(f);
  return 0;
}

int load_GraphemeBreakProperty(void) {
  FILE *f = ucd_fopen("GraphemeBreakProperty.txt");
  char line[1024];
  if (!f) return -1;

  while (fgets(line,sizeof(line),f)) {
    char *p=line, *semi, *hash, *dots, *end, *prop;
    unsigned long a, b;
    if (*p=='#' || *p=='\n' || *p=='\0') continue;

    semi = strchr(p,';'); if (!semi) continue; *semi=0;
    hash = strchr(semi+1,'#'); if (hash) *hash=0;

    dots = strstr(p,"..");
    if (dots) {
      *dots=0;
      if (hex_to_u32(p,&a)!=0) continue;
      if (hex_to_u32(dots+2,&b)!=0) continue;
    } else {
      if (hex_to_u32(p,&a)!=0) continue;
      b = a;
    }

    prop = semi+1; while (*prop==' '||*prop=='\t') ++prop;
    end = prop; while (*end && *end!=' '&&*end!='\t'&&*end!='\n') ++end; *end=0;

    {
      unsigned long val = GB_Other;

      if (strcmp(prop,"CR")==0) val = GB_CR;
      else if (strcmp(prop,"LF")==0) val = GB_LF;
      else if (strcmp(prop,"Control")==0) val = GB_Control;
      else if (strcmp(prop,"Extend")==0) val = GB_Extend;
      else if (strcmp(prop,"ZWJ")==0) val = GB_ZWJ;
      else if (strcmp(prop,"SpacingMark")==0) val = GB_SpacingMark;
      else if (strcmp(prop,"Prepend")==0) val = GB_Prepend;
      else if (strcmp(prop,"Regional_Indicator")==0) val = GB_Regional_Indicator;
      else if (strcmp(prop,"L")==0) val = GB_L;
      else if (strcmp(prop,"V")==0) val = GB_V;
      else if (strcmp(prop,"T")==0) val = GB_T;
      else if (strcmp(prop,"LV")==0) val = GB_LV;
      else if (strcmp(prop,"LVT")==0) val = GB_LVT;

      while (a <= b) { (void)u32map_put(&U.gb_prop, a, val); ++a; }
    }
  }
  fclose(f);
  return 0;
}

int build_comp_pairs(void) {
  size_t i;
  if (!U.decomp_head.a) return 0;
  for (i=0;i<U.decomp_head.cap;++i) {
    if (U.decomp_head.a[i].used) {
      unsigned long cp = U.decomp_head.a[i].k;
      unsigned long off = U.decomp_head.a[i].v;
      if (off < U.decomp_pool.n) {
        unsigned long head = U.decomp_pool.p[off];
        unsigned long cnt  = head & 0xFFFFFFul;
        unsigned long compat = head >> 24;
        if (!compat && cnt==2) {
          unsigned long a = U.decomp_pool.p[off+1];
          unsigned long b = U.decomp_pool.p[off+2];
          unsigned long dummy;
          if (u32map_get(&U.comp_exclusions, cp, &dummy)) continue;
          if (pairmap_put(&U.comp_map, a, b, cp) != 0) return -1;
        }
      }
    }
  }
  return 0;
}

void ucd_free(void) {
  u32map_free(&U.ccc);
  u32map_free(&U.decomp_head);
  u32vec_free(&U.decomp_pool);
  u32map_free(&U.tolower_map);
  u32map_free(&U.toupper_map);
  u32map_free(&U.fold_head);
  u32vec_free(&U.fold_pool);
  pairmap_free(&U.comp_map);
  u32map_free(&U.comp_exclusions);
  u32map_free(&U.gb_prop);
  u32map_free(&U.ep_prop);
}

int ucd_do_init(void) {
  if (u32map_init(&U.ccc, 4096) != 0) goto fail;
  if (u32map_init(&U.decomp_head, 4096) != 0) goto fail;
  U.decomp_pool.p = NULL; U.decomp_pool.n = 0; U.decomp_pool.cap = 0;

  if (u32map_init(&U.tolower_map, 4096) != 0) goto fail;
  if (u32map_init(&U.toupper_map, 4096) != 0) goto fail;

  if (u32map_init(&U.fold_head, 4096) != 0) goto fail;
  U.fold_pool.p = NULL; U.fold_pool.n = 0; U.fold_pool.cap = 0;

  if (pairmap_init(&U.comp_map, 4096) != 0) goto fail;
  if (u32map_init(&U.comp_exclusions, 1024) != 0) goto fail;

  if (u32map_init(&U.gb_prop, 4096) != 0) goto fail;

  if (u32map_init(&U.ep_prop, 2048) != 0) goto fail;

  if (load_UnicodeData() != 0) goto fail;
  (void)load_DerivedNormalizationProps();
  (void)load_CompositionExclusions();
  if (build_comp_pairs() != 0) goto fail;
  (void)load_CaseFolding();
  (void)load_GraphemeBreakProperty();
  (void)load_EmojiData();

  U.inited = 1;
  atexit(ucd_free);
  return 0;

fail:
  ucd_free();
  return -1;
}

#ifdef DS_THREAD_SAFE
mutex_t g_ucd_once_lock;
int g_ucd_once_lock_inited = 0;

void ensure_ucd_once_lock(void) {
  if (!g_ucd_once_lock_inited) {
    mutex_init(&g_ucd_once_lock);
    g_ucd_once_lock_inited = 1;
  }
}
#endif

int ucd_init_once(void) {
#ifndef DS_THREAD_SAFE
  if (U.inited) return 0;
  return ucd_do_init();
#else
  int rc = 0;
  ensure_ucd_once_lock();
  mutex_lock(&g_ucd_once_lock);
  if (!U.inited) rc = ucd_do_init();
  mutex_unlock(&g_ucd_once_lock);
  return rc;
#endif
}


int u8_next(const char *buf, size_t len, size_t *ioff, unsigned long *out_cp) {
  return FUNC(str_u8_next)(buf, len, ioff, out_cp, DS_U8_STRICT);
}
int u8_push(ds_str_t *s, unsigned long cp) { return FUNC(str_u8_push_cp)(s, cp); }

int is_Hangul_S(unsigned long cp){ return (cp>=SBase && cp<SBase+SCount); }
int is_Hangul_L(unsigned long cp){ return (cp>=LBase && cp<LBase+LCount); }
int is_Hangul_V(unsigned long cp){ return (cp>=VBase && cp<VBase+VCount); }
int is_Hangul_T(unsigned long cp){ return (cp>TBase && cp<=TBase+TCount-1); }

void decompose_Hangul(unsigned long s, u32vec_t *out){
  unsigned long SIndex = s - SBase;
  unsigned long L = LBase + SIndex / NCount;
  unsigned long V = VBase + (SIndex % NCount) / TCount;
  unsigned long T = TBase + (SIndex % TCount);
  (void)u32vec_push(out, L); (void)u32vec_push(out, V);
  if (T != TBase) (void)u32vec_push(out, T);
}

int compose_Hangul(unsigned long *LVT, unsigned long next, size_t *consumed){
  unsigned long s = *LVT;
  if (is_Hangul_L(s) && is_Hangul_V(next)) {
    unsigned long LIndex = s - LBase;
    unsigned long VIndex = next - VBase;
    *LVT = SBase + (LIndex * VCount + VIndex) * TCount;
    *consumed = 1u; return 1;
  }
  if (is_Hangul_S(s) && ((next > TBase) && (next <= TBase + TCount - 1))) {
    unsigned long TIndex = next - TBase;
    *LVT = s + TIndex; *consumed = 1u; return 1;
  }
  return 0;
}

unsigned long get_ccc(unsigned long cp){
  unsigned long v;
  if (u32map_get(&U.ccc, cp, &v)) return v;
  return 0ul;
}

int decompose_cp(unsigned long cp, int compat, u32vec_t *out) {
  unsigned long off;
  if (is_Hangul_S(cp)) { decompose_Hangul(cp, out); return 0; }

  if (u32map_get(&U.decomp_head, cp, &off)) {
    if (off < U.decomp_pool.n) {
      unsigned long head = U.decomp_pool.p[off];
      unsigned long cnt  = head & 0xFFFFFFul;
      unsigned long is_compat = head >> 24;
      size_t i;
      if (!compat && is_compat) {
      } else {
        for (i=0;i<cnt;++i) {
          unsigned long d = U.decomp_pool.p[off+1+i];
          if (decompose_cp(d, compat, out)!=0) return -1;
        }
        return 0;
      }
    }
  }
  return u32vec_push(out, cp);
}

void reorder_ccc(u32vec_t *v) {
  size_t i;
  for (i=1;i<v->n;++i) {
    unsigned long x=v->p[i], cx=get_ccc(x);
    size_t j=i;
    while (j>0) {
      unsigned long y=v->p[j-1], cy=get_ccc(y);
      if (cy<=cx) break;
      v->p[j]=y; --j;
    }
    v->p[j]=x;
  }
}

void compose_vec(u32vec_t *v) {
  size_t r, w;

  if (v->n == 0) return;
  r = 0; w = 0;
  v->p[w++] = v->p[r++];

  while (r < v->n) {
    unsigned long b = v->p[r];
    unsigned long cccb = get_ccc(b);

    {
      size_t consumed = 0u;
      unsigned long a = v->p[w-1];
      if (compose_Hangul(&a, b, &consumed)) {
        v->p[w-1] = a; r += consumed; continue;
      }
    }

    if (cccb == 0) {
      unsigned long a = v->p[w-1], comp;
      if (pairmap_get(&U.comp_map, a, b, &comp)) {
        v->p[w-1] = comp; r++; continue;
      }
    } else {
      size_t j = w;
      while (j > 0) {
        unsigned long a2 = v->p[j-1];
        unsigned long ccca2 = get_ccc(a2);
        if (ccca2 == 0) {
          unsigned long comp;
          if (pairmap_get(&U.comp_map, a2, b, &comp)) {
            v->p[j-1] = comp; r++; goto next_char;
          }
          break;
        }
        if (ccca2 >= cccb) break;
        --j;
      }
    }

    v->p[w++] = v->p[r++];
  next_char:
    ;
  }
  v->n = w;
}

int ds__ucd_normalize(ds_str_t *dst, const ds_str_t *src, int compat, int compose) {
  size_t i=0; u32vec_t tmp; int rc=-1; unsigned long cp;

  tmp.p=NULL; tmp.n=0; tmp.cap=0;
  if (!dst || !src) return -1;
  if (ucd_init_once()!=0) return -1;

  while (u8_next(src->buf, src->len, &i, &cp)==1) {
    if (decompose_cp(cp, compat, &tmp)!=0) goto done;
  }
  reorder_ccc(&tmp);
  if (compose) compose_vec(&tmp);

  FUNC(str_clear)(dst);
  for (i=0;i<tmp.n;++i) {
    if (u8_push(dst, tmp.p[i]) != 0) goto done;
  }
  rc = 0;
done:
  u32vec_free(&tmp);
  return rc;
}

int fold_cp(unsigned long cp, u32vec_t *out) {
  unsigned long off;
  if (u32map_get(&U.fold_head, cp, &off)) {
    if (off < U.fold_pool.n) {
      unsigned long cnt = U.fold_pool.p[off];
      size_t i;
      for (i=0;i<cnt;++i) {
        if (u32vec_push(out, U.fold_pool.p[off+1+i])!=0) return -1;
      }
      return 0;
    }
  }
  return u32vec_push(out, cp);
}

int ds__ucd_casefold(ds_str_t *dst, const ds_str_t *src) {
  size_t i=0; unsigned long cp; u32vec_t out; int rc=-1;
  out.p=NULL; out.n=0; out.cap=0;
  if (!dst || !src) return -1;
  if (ucd_init_once()!=0) return -1;
  while (u8_next(src->buf, src->len, &i, &cp)==1) {
    if (fold_cp(cp, &out)!=0) goto done;
  }
  FUNC(str_clear)(dst);
  for (i=0;i<out.n;++i) {
    if (u8_push(dst, out.p[i])!=0) goto done;
  }
  rc=0;
done:
  u32vec_free(&out);
  return rc;
}

int ds__ucd_tolower(ds_str_t *dst, const ds_str_t *src) {
  size_t i=0; unsigned long cp, v;
  if (!dst || !src) return -1;
  if (ucd_init_once()!=0) return -1;
  FUNC(str_clear)(dst);
  while (u8_next(src->buf, src->len, &i, &cp)==1) {
    if (u32map_get(&U.tolower_map, cp, &v)) cp=v;
    if (u8_push(dst, cp)!=0) return -1;
  }
  return 0;
}

int ds__ucd_toupper(ds_str_t *dst, const ds_str_t *src) {
  size_t i=0; unsigned long cp, v;
  if (!dst || !src) return -1;
  if (ucd_init_once()!=0) return -1;
  FUNC(str_clear)(dst);
  while (u8_next(src->buf, src->len, &i, &cp)==1) {
    if (u32map_get(&U.toupper_map, cp, &v)) cp=v;
    if (u8_push(dst, cp)!=0) return -1;
  }
  return 0;
}

unsigned long gb_get(unsigned long cp){
  unsigned long v;
  if (u32map_get(&U.gb_prop, cp, &v)) return v;
  return GB_Other;
}

int is_EP(unsigned long cp) {
  unsigned long v;
  return u32map_get(&U.ep_prop, cp, &v) && v == 1ul;
}

long ds__ucd_grapheme_len(const ds_str_t *s)
{
  size_t i = 0;
  unsigned long cp = 0;
  long clusters = 0;
  int have_prev = 0;

  unsigned long prev_prop = GB_Other;
  int ri_run_len = 0;
  int prev_is_zwj_ign_ext = 0;
  int last_base_is_EP = 0;

  unsigned long prop;
  int break_here;
  int prev_is_Control;
  int cur_is_Control;

  if (s == 0) return -1;
  if (ucd_init_once() != 0) return -1;

  while (u8_next(s->buf, s->len, &i, &cp) == 1) {
    prop = gb_get(cp);
    break_here = 1;

    if (!have_prev) {
      clusters++;
      have_prev = 1;

      last_base_is_EP = is_EP(cp);
      prev_is_zwj_ign_ext = 0;
      if (prop == GB_Regional_Indicator) {
        ri_run_len = 1;
      } else {
        ri_run_len = 0;
      }
      prev_prop = prop;
      continue;
    }

    prev_is_Control = (prev_prop == GB_Control || prev_prop == GB_CR || prev_prop == GB_LF);
    cur_is_Control = (prop == GB_Control || prop == GB_CR || prop == GB_LF);

    if (prev_prop == GB_CR && prop == GB_LF) {
      break_here = 0;
    }

    else if (prev_is_Control || cur_is_Control) {
      break_here = 1;
    }

    else if (prev_prop == GB_L &&
             (prop == GB_L || prop == GB_V || prop == GB_LV || prop == GB_LVT)) {
      break_here = 0;
    } else if ((prev_prop == GB_LV || prev_prop == GB_V) &&
               (prop == GB_V || prop == GB_T)) {
      break_here = 0;
    } else if ((prev_prop == GB_LVT || prev_prop == GB_T) &&
               (prop == GB_T)) {
      break_here = 0;
    }

    else if (prop == GB_Extend) {
      break_here = 0;
    }

    else if (prop == GB_SpacingMark) {
      break_here = 0;
    }

    else if (prev_prop == GB_Prepend) {
      break_here = 0;
    }

    else if (is_EP(cp) && prev_is_zwj_ign_ext && last_base_is_EP) {
      break_here = 0;
    }

    else if (prev_prop == GB_Regional_Indicator && prop == GB_Regional_Indicator) {
      if ((ri_run_len % 2) == 1) break_here = 0;
    }

    if (break_here) clusters++;

    if (prop == GB_Regional_Indicator) {
      ri_run_len += 1;
    } else {
      ri_run_len = 0;
    }

    if (prop == GB_Extend) {
    } else if (prop == GB_ZWJ) {
      prev_is_zwj_ign_ext = 1;
    } else {
      last_base_is_EP = is_EP(cp);
      prev_is_zwj_ign_ext = 0;
    }

    prev_prop = prop;
  }

  return clusters;
}

int load_EmojiData(void) {
  FILE *f = ucd_fopen("emoji-data.txt");
  char line[1024];
  if (!f) return -1;

  while (fgets(line, sizeof(line), f)) {
    char *p = line, *semi, *hash, *dots, *end, *prop;
    unsigned long a, b;

    if (*p=='#' || *p=='\n' || *p=='\0') continue;
    hash = strchr(p,'#'); if (hash) *hash = 0;

    semi = strchr(p,';'); if (!semi) continue; *semi = 0;

    while (*p==' '||*p=='\t') ++p;
    dots = strstr(p, "..");
    if (dots) {
      *dots = 0;
      if (hex_to_u32(p, &a)!=0) continue;
      if (hex_to_u32(dots+2, &b)!=0) continue;
    } else {
      if (hex_to_u32(p, &a)!=0) continue;
      b = a;
    }

    prop = semi+1; while (*prop==' '||*prop=='\t') ++prop;
    end = prop; while (*end && *end!=' '&&*end!='\t'&&*end!='\n') ++end; *end = 0;

    if (strcmp(prop, "Extended_Pictographic")==0) {
      while (a <= b) { (void)u32map_put(&U.ep_prop, a, 1ul); ++a; }
    }
  }
  fclose(f);
  return 0;
}

int ds__grapheme_iter_init(ds__grapheme_iter_t *it, const char *buf, size_t len) {
  if (!it) return -1;
  it->buf = buf;
  it->len = buf ? len : 0;
  it->i = 0;
  it->cur_start = 0;
  it->have_prev = 0;
  it->prev_prop = GB_Other;
  it->ri_run_len = 0;
  it->prev_is_zwj_ign_ext = 0;
  it->last_base_is_EP = 0;
  it->cp = 0;
  it->prop = GB_Other;
  it->break_here = 0;
  it->prev_is_Control = 0;
  it->cur_is_Control = 0;
  return 0;
}

/* Emit clusters by detecting a boundary BEFORE the current code point.
   On break, we emit the previous cluster range.
   At EOF, we emit the tail cluster if any. */
int ds__grapheme_iter_next(ds__grapheme_iter_t *it, size_t *out_start, size_t *out_len) {
  if (!it || !it->buf) return -1;

  /* If we haven't consumed anything yet, we must read the first cp to seed state,
     but we DON'T emit a cluster yet (GB1). */
  while (1) {
    size_t cp_start;
    unsigned long prop;
    int break_here = 1;

    if (it->i >= it->len) {
      /* End of text: emit tail cluster if any pending */
      if (!it->have_prev && it->cur_start < it->len) {
        /* empty input corner case (shouldn’t happen with valid state) */
      }
      if (it->cur_start < it->len) {
        if (out_start) *out_start = it->cur_start;
        if (out_len)   *out_len   = it->len - it->cur_start;
        it->cur_start = it->len; /* consumed */
        return 1;
      }
      return 0; /* no more clusters */
    }

    cp_start = it->i;
    if (u8_next(it->buf, it->len, &it->i, &it->cp) != 1) return -1; /* invalid UTF-8 */
    prop = gb_get(it->cp);

    if (!it->have_prev) {
      /* GB1: start of text: always a boundary before first cp.
         We *start* the first cluster here but do NOT emit yet. */
      it->have_prev = 1;
      it->prev_prop = prop;
      it->cur_start = cp_start;

      /* init EP/RI/ZWJ tracking */
      it->last_base_is_EP = is_EP(it->cp);
      it->prev_is_zwj_ign_ext = 0;
      it->ri_run_len = (prop == GB_Regional_Indicator) ? 1 : 0;

      /* loop to fetch the *next* code point to decide if we should break before it */
      continue;
    }

    /* Apply rules vs previous cp (same order you used in len counter) */
    it->prev_is_Control = (it->prev_prop == GB_Control || it->prev_prop == GB_CR || it->prev_prop == GB_LF);
    it->cur_is_Control  = (prop          == GB_Control || prop          == GB_CR || prop          == GB_LF);

    /* GB3: CR × LF */
    if (it->prev_prop == GB_CR && prop == GB_LF) break_here = 0;

    /* GB4/GB5: Control/CR/LF boundaries */
    else if (it->prev_is_Control || it->cur_is_Control) break_here = 1;

    /* Hangul GB6–GB8 */
    else if (it->prev_prop == GB_L &&
            (prop == GB_L || prop == GB_V || prop == GB_LV || prop == GB_LVT)) break_here = 0;
    else if ((it->prev_prop == GB_LV || it->prev_prop == GB_V) &&
             (prop == GB_V || prop == GB_T)) break_here = 0;
    else if ((it->prev_prop == GB_LVT || it->prev_prop == GB_T) &&
             (prop == GB_T)) break_here = 0;

    /* GB9: × Extend */
    else if (prop == GB_Extend) break_here = 0;

    /* GB9a: × SpacingMark */
    else if (prop == GB_SpacingMark) break_here = 0;

    /* GB9b: Prepend × */
    else if (it->prev_prop == GB_Prepend) break_here = 0;

    /* GB11: EP Extend* ZWJ × EP */
    else if (is_EP(it->cp) && it->prev_is_zwj_ign_ext && it->last_base_is_EP) break_here = 0;

    /* GB12/13: RI pairing */
    else if (it->prev_prop == GB_Regional_Indicator && prop == GB_Regional_Indicator) {
      if ((it->ri_run_len % 2) == 1) break_here = 0;
    }

    if (break_here) {
      /* Emit previous cluster: [cur_start, cp_start - cur_start] */
      if (out_start) *out_start = it->cur_start;
      if (out_len)   *out_len   = cp_start - it->cur_start;

      /* Start new cluster at cp_start */
      it->cur_start = cp_start;
      /* Update state for next comparisons (treat this cp as "previous") */
      it->prev_prop = prop;

      /* Update RI/EP/ZWJ tracking based on *this* cp (like in len version) */
      if (prop == GB_Regional_Indicator) it->ri_run_len += 1;
      else it->ri_run_len = 0;

      if (prop == GB_Extend) {
        /* keep flags */
      } else if (prop == GB_ZWJ) {
        it->prev_is_zwj_ign_ext = 1;
      } else {
        it->last_base_is_EP = is_EP(it->cp);
        it->prev_is_zwj_ign_ext = 0;
      }
      return 1;
    }

    /* No boundary: merge current cp into running cluster, update state, continue */
    it->prev_prop = prop;

    if (prop == GB_Regional_Indicator) it->ri_run_len += 1;
    else it->ri_run_len = 0;

    if (prop == GB_Extend) {
      /* keep flags */
    } else if (prop == GB_ZWJ) {
      it->prev_is_zwj_ign_ext = 1;
    } else {
      it->last_base_is_EP = is_EP(it->cp);
      it->prev_is_zwj_ign_ext = 0;
    }

    /* loop to read next cp */
  }
}
