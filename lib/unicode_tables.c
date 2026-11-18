const unsigned long ds_ucd_ccc_keys[] = { #include "ccc_keys.inc" };
const unsigned long ds_ucd_ccc_vals[] = { #include "ccc_vals.inc" };
const size_t ds_ucd_ccc_len = sizeof(ds_ucd_ccc_keys)/sizeof(ds_ucd_ccc_keys[0]);

struct U_Pair { unsigned long a, b; };

const struct U_Pair U_COMP_KEYS[] = { #include "comp_keys_pair.inc" };
const unsigned long ds_ucd_comp_vals[]  = { #include "comp_vals.inc" };
const size_t        ds_ucd_comp_len     = sizeof(U_COMP_KEYS)/sizeof(U_COMP_KEYS[0]);

struct U_GB_Range3 { unsigned long start, end, prop; };
struct U_EP_Range2 { unsigned long start, end; };

const struct U_GB_Range3 ds_ucd_gb_ranges[] = { #include "gb_ranges.inc" };
const size_t             ds_ucd_gb_len      = sizeof(ds_ucd_gb_ranges)/sizeof(ds_ucd_gb_ranges[0]);

const struct U_EP_Range2 ds_ucd_ep_ranges[] = { #include "ep_ranges.inc" };
const size_t             ds_ucd_ep_len      = sizeof(ds_ucd_ep_ranges)/sizeof(ds_ucd_ep_ranges[0]);
