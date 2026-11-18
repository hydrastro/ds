#include "unicode_tables.h"

#include "ccc_keys.inc"
#include "ccc_vals.inc"
#include "decomp_keys.inc"
#include "decomp_cnt.inc"
#include "decomp_compat.inc"
#include "decomp_off.inc"
#include "decomp_pool.inc"
#include "comp_keys_a.inc"
#include "comp_keys_b.inc"
#include "comp_vals.inc"
#include "fold_keys.inc"
#include "fold_off.inc"
#include "fold_cnt.inc"
#include "fold_pool.inc"
#include "tolower_keys.inc"
#include "tolower_vals.inc"
#include "toupper_keys.inc"
#include "toupper_vals.inc"
#include "gb_ranges.inc"
#include "ep_ranges.inc"

const unsigned long *ds_ucd_ccc_keys = (const unsigned long*)ccc_keys_bin;
const unsigned long *ds_ucd_ccc_vals = (const unsigned long*)ccc_vals_bin;
const size_t         ds_ucd_ccc_len  = sizeof(ccc_keys_bin)/sizeof(unsigned long);

const unsigned long *ds_ucd_decomp_keys     = (const unsigned long*)decomp_keys_bin;
const unsigned int  *ds_ucd_decomp_cnt      = (const unsigned int*)decomp_cnt_bin;
const unsigned int  *ds_ucd_decomp_iscompat = (const unsigned int*)decomp_compat_bin;
const unsigned int  *ds_ucd_decomp_off      = (const unsigned int*)decomp_off_bin;
const unsigned long *ds_ucd_decomp_pool     = (const unsigned long*)decomp_pool_bin;
const size_t         ds_ucd_decomp_len      = sizeof(decomp_keys_bin)/sizeof(unsigned long);

const unsigned long *ds_ucd_comp_keys_a = (const unsigned long*)comp_keys_a_bin;
const unsigned long *ds_ucd_comp_keys_b = (const unsigned long*)comp_keys_b_bin;
const unsigned long *ds_ucd_comp_vals   = (const unsigned long*)comp_vals_bin;
const size_t         ds_ucd_comp_len    = sizeof(comp_vals_bin)/sizeof(unsigned long);

const unsigned long *ds_ucd_fold_keys = (const unsigned long*)fold_keys_bin;
const unsigned int  *ds_ucd_fold_off  = (const unsigned int*)fold_off_bin;
const unsigned int  *ds_ucd_fold_cnt  = (const unsigned int*)fold_cnt_bin;
const unsigned long *ds_ucd_fold_pool = (const unsigned long*)fold_pool_bin;
const size_t         ds_ucd_fold_len  = sizeof(fold_keys_bin)/sizeof(unsigned long);

const unsigned long *ds_ucd_tolower_keys = (const unsigned long*)tolower_keys_bin;
const unsigned long *ds_ucd_tolower_vals = (const unsigned long*)tolower_vals_bin;
const size_t         ds_ucd_tolower_len  = sizeof(tolower_keys_bin)/sizeof(unsigned long);

const unsigned long *ds_ucd_toupper_keys = (const unsigned long*)toupper_keys_bin;
const unsigned long *ds_ucd_toupper_vals = (const unsigned long*)toupper_vals_bin;
const size_t         ds_ucd_toupper_len  = sizeof(toupper_keys_bin)/sizeof(unsigned long);

const struct U_GB_Range3 *ds_ucd_gb_ranges = (const struct U_GB_Range3*)gb_ranges_bin;
const size_t              ds_ucd_gb_len    = sizeof(gb_ranges_bin)/sizeof(struct U_GB_Range3);

const struct U_EP_Range2 *ds_ucd_ep_ranges = (const struct U_EP_Range2*)ep_ranges_bin;
const size_t              ds_ucd_ep_len    = sizeof(ep_ranges_bin)/sizeof(struct U_EP_Range2);
