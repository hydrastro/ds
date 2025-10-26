#ifndef DS_STR_UTF8_H
#define DS_STR_UTF8_H

#include "common.h"
#include "str.h"
#include <stddef.h>
#include <stdint.h>

#define DS_U8_OK 0
#define DS_U8_EINVAL -1
#define DS_U8_EOVERFLOW -2

int FUNC(str_u8_decode_at)(ds_str_t *s, size_t byte_off, uint32_t *cp,
                           size_t *adv);
int FUNC(str_u8_encode)(uint32_t cp, char out[4]);
int FUNC(str_u8_is_valid)(ds_str_t *s);
long FUNC(str_u8_len)(ds_str_t *s);
size_t FUNC(str_u8_next)(ds_str_t *s, size_t byte_off);
size_t FUNC(str_u8_prev)(ds_str_t *s, size_t byte_off);
int FUNC(str_u8_insert_cp)(ds_str_t *s, size_t cp_index, uint32_t cp);
int FUNC(str_u8_erase_cprange)(ds_str_t *s, size_t cp_index, size_t cp_count);

#endif /* DS_STR_UTF8_H */
