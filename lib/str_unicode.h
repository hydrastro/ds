#ifndef DS_STR_UNICODE_H
#define DS_STR_UNICODE_H

#include "common.h"
#include "str.h"
#include "str_io.h"
#include <stddef.h>

#define DS_U8_REJECT_SURROGATE   0x01u
#define DS_U8_REJECT_NONCHAR     0x02u
#define DS_U8_REJECT_OVERLONG    0x04u
#define DS_U8_REJECT_OUT_OF_RANGE 0x08u
#define DS_U8_STRICT (DS_U8_REJECT_SURROGATE | DS_U8_REJECT_NONCHAR | DS_U8_REJECT_OVERLONG | DS_U8_REJECT_OUT_OF_RANGE)

#define DS_U8_REPLACEMENT_CHAR 0xFFFDu

int  FUNC(str_u8_push_cp)(ds_str_t *s, unsigned long cp);

int  FUNC(str_u8_pop_cp)(ds_str_t *s, unsigned long *out_cp);

size_t FUNC(str_u8_count)(const ds_str_t *s, unsigned int flags, int *valid_out);
size_t FUNC(str_view_u8_count)(ds_str_view_t v, unsigned int flags, int *valid_out);

int  FUNC(str_u8_validate)(const ds_str_t *s, unsigned int flags);
int  FUNC(str_view_u8_validate)(ds_str_view_t v, unsigned int flags);

int  FUNC(str_u8_next)(const char *buf, size_t len, size_t *ioff, unsigned long *out_cp, unsigned int flags);

int  FUNC(str_u8_prev)(const char *buf, size_t len, size_t *ioff, unsigned long *out_cp, unsigned int flags);

size_t FUNC(str_u8_cp_to_byte)(const ds_str_t *s, size_t cp_index, unsigned int flags);

int  FUNC(str_u8_slice_cp)(ds_str_t *dst, const ds_str_t *src, size_t start_cp, size_t count_cp, unsigned int flags);

int  FUNC(str_u8_sanitize)(ds_str_t *dst, const ds_str_t *src, unsigned int flags);

int  FUNC(str_u8_strip_bom)(ds_str_t *s);

int  FUNC(str_u8_ascii_tolower)(ds_str_t *dst, const ds_str_t *src);
int  FUNC(str_u8_ascii_toupper)(ds_str_t *dst, const ds_str_t *src);
int  FUNC(str_u8_ascii_casefold)(ds_str_t *dst, const ds_str_t *src);

int  FUNC(str_view_u8_next)(ds_str_view_t v, size_t *ioff, unsigned long *out_cp, unsigned int flags);
int  FUNC(str_view_u8_prev)(ds_str_view_t v, size_t *ioff, unsigned long *out_cp, unsigned int flags);

#endif /* DS_STR_UNICODE_H */
