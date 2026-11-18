#ifndef DS_UNICODE_TABLES_H
#define DS_UNICODE_TABLES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  unsigned long start;
  unsigned long end;
  unsigned long prop;
} ucd_range_t;

extern const unsigned long ds_ucd_ccc_keys[];
extern const unsigned long ds_ucd_ccc_vals[];
extern const size_t ds_ucd_ccc_len;

extern const unsigned long ds_ucd_decomp_keys[];
extern const unsigned int ds_ucd_decomp_cnt[];
extern const unsigned int ds_ucd_decomp_off[];
extern const unsigned int ds_ucd_decomp_iscompat[];
extern const unsigned long ds_ucd_decomp_pool[];
extern const size_t ds_ucd_decomp_len;
extern const size_t ds_ucd_decomp_pool_len;

extern const unsigned long ds_ucd_comp_keys_a[];
extern const unsigned long ds_ucd_comp_keys_b[];
extern const unsigned long ds_ucd_comp_vals[];
extern const size_t ds_ucd_comp_len;

extern const unsigned long ds_ucd_tolower_keys[];
extern const unsigned long ds_ucd_tolower_vals[];
extern const size_t ds_ucd_tolower_len;
extern const unsigned long ds_ucd_toupper_keys[];
extern const unsigned long ds_ucd_toupper_vals[];
extern const size_t ds_ucd_toupper_len;

extern const unsigned long ds_ucd_fold_keys[];
extern const unsigned int ds_ucd_fold_cnt[];
extern const unsigned int ds_ucd_fold_off[];
extern const unsigned long ds_ucd_fold_pool[];
extern const size_t ds_ucd_fold_len;
extern const size_t ds_ucd_fold_pool_len;

extern const ucd_range_t ds_ucd_gb_ranges[];
extern const size_t ds_ucd_gb_len;
extern const ucd_range_t ds_ucd_ep_ranges[];
extern const size_t ds_ucd_ep_len;
extern const ucd_range_t ds_ucd_incb_ranges[];
extern const size_t ds_ucd_incb_len;

#define U_COMP_A ds_ucd_comp_keys_a
#define U_COMP_B ds_ucd_comp_keys_b
#define U_COMP_VAL ds_ucd_comp_vals



#ifndef DS_NO_LEGACY_NAMES
#define U_CCC_KEYS ds_ucd_ccc_keys
#define U_CCC_VALS ds_ucd_ccc_vals
#define U_CCC_LEN ds_ucd_ccc_len
#define U_DECOMP_KEYS ds_ucd_decomp_keys
#define U_DECOMP_CNT ds_ucd_decomp_cnt
#define U_DECOMP_OFF ds_ucd_decomp_off
#define U_DECOMP_ISCOMPAT ds_ucd_decomp_iscompat
#define U_DECOMP_POOL ds_ucd_decomp_pool
#define U_DECOMP_LEN ds_ucd_decomp_len
#define U_DECOMP_POOL_LEN ds_ucd_decomp_pool_len
#define U_COMP_KEYS_A ds_ucd_comp_keys_a
#define U_COMP_KEYS_B ds_ucd_comp_keys_b
#define U_COMP_VALS ds_ucd_comp_vals
#define U_COMP_LEN ds_ucd_comp_len
#define U_TOLOWER_KEYS ds_ucd_tolower_keys
#define U_TOLOWER_VALS ds_ucd_tolower_vals
#define U_TOLOWER_LEN ds_ucd_tolower_len
#define U_TOUPPER_KEYS ds_ucd_toupper_keys
#define U_TOUPPER_VALS ds_ucd_toupper_vals
#define U_TOUPPER_LEN ds_ucd_toupper_len
#define U_FOLD_KEYS ds_ucd_fold_keys
#define U_FOLD_CNT ds_ucd_fold_cnt
#define U_FOLD_OFF ds_ucd_fold_off
#define U_FOLD_POOL ds_ucd_fold_pool
#define U_FOLD_LEN ds_ucd_fold_len
#define U_FOLD_POOL_LEN ds_ucd_fold_pool_len
#define U_GB_RANGES ds_ucd_gb_ranges
#define U_GB_LEN ds_ucd_gb_len
#define U_EP_RANGES ds_ucd_ep_ranges
#define U_EP_LEN ds_ucd_ep_len
#define U_INCB_RANGES ds_ucd_incb_ranges
#define U_INCB_LEN ds_ucd_incb_len
#endif

#ifdef __cplusplus
}
#endif

#endif /* DS_UNICODE_TABLES_H */
