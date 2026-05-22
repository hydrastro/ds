#include <stddef.h>
#include "unicode_tables.h"

const unsigned long U_CCC_KEYS[] = {0x61ul,0x6Eul,0xC5ul,0xE5ul,0x30Aul,0x391ul,0x3B1ul,0x207Ful};
const unsigned long U_CCC_VALS[] = {0x0ul,0x0ul,0x0ul,0x0ul,0xE6ul,0x0ul,0x0ul,0x0ul};
const size_t U_CCC_LEN = 8u;

const unsigned long U_DECOMP_KEYS[] = {0xC5ul,0xE5ul,0x207Ful};
const unsigned int U_DECOMP_CNT[] = {2,2,1};
const unsigned int U_DECOMP_OFF[] = {0,2,4};
const unsigned int U_DECOMP_ISCOMPAT[] = {0,0,1};
const unsigned long U_DECOMP_POOL[] = {0x41ul,0x30Aul,0x61ul,0x30Aul,0x6Eul};
const size_t U_DECOMP_LEN = 3u;
const size_t U_DECOMP_POOL_LEN = 5u;

const unsigned long U_COMP_KEYS_A[] = {0x41ul,0x61ul};
const unsigned long U_COMP_KEYS_B[] = {0x30Aul,0x30Aul};
const unsigned long U_COMP_VALS[] = {0xC5ul,0xE5ul};
const size_t U_COMP_LEN = 2u;

const unsigned long U_TOLOWER_KEYS[] = {0xC5ul,0x391ul};
const unsigned long U_TOLOWER_VALS[] = {0xE5ul,0x3B1ul};
const size_t U_TOLOWER_LEN = 2u;

const unsigned long U_TOUPPER_KEYS[] = {0x61ul,0x6Eul,0xE5ul,0x3B1ul};
const unsigned long U_TOUPPER_VALS[] = {0x41ul,0x4Eul,0xC5ul,0x391ul};
const size_t U_TOUPPER_LEN = 4u;

const unsigned long U_FOLD_KEYS[] = {0xDFul,0x391ul};
const unsigned int U_FOLD_CNT[] = {2,1};
const unsigned int U_FOLD_OFF[] = {0,2};
const unsigned long U_FOLD_POOL[] = {0x73ul,0x73ul,0x3B1ul};
const size_t U_FOLD_LEN = 2u;
const size_t U_FOLD_POOL_LEN = 3u;

const ucd_range_t U_GB_RANGES[] = {{0x30Aul,0x30Aul,4ul},{0x200Dul,0x200Dul,5ul},{0x1F3FBul,0x1F3FFul,4ul}};
const size_t U_GB_LEN = 3u;

const ucd_range_t U_EP_RANGES[] = {{0x1F3E0ul,0x1F3E0ul,1ul},{0x1F44Bul,0x1F44Bul,1ul}};
const size_t U_EP_LEN = 2u;
