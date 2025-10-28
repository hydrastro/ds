#ifndef UNICODE_TABLES_H
#define UNICODE_TABLES_H

#include <stddef.h>

struct U_GB_Range3 {
  unsigned long start, end, prop;
};
struct U_EP_Range2 {
  unsigned long start, end;
};

extern const unsigned long *U_CCC_KEYS;
extern const unsigned long *U_CCC_VALS;
extern const size_t U_CCC_LEN;

extern const unsigned long *U_DECOMP_KEYS;
extern const unsigned int *U_DECOMP_CNT;
extern const unsigned int *U_DECOMP_ISCOMPAT;
extern const unsigned int *U_DECOMP_OFF;
extern const unsigned long *U_DECOMP_POOL;
extern const size_t U_DECOMP_LEN;

extern const unsigned long *U_COMP_KEYS_A;
extern const unsigned long *U_COMP_KEYS_B;
extern const unsigned long *U_COMP_VALS;
extern const size_t U_COMP_LEN;

extern const unsigned long *U_FOLD_KEYS;
extern const unsigned int *U_FOLD_OFF;
extern const unsigned int *U_FOLD_CNT;
extern const unsigned long *U_FOLD_POOL;
extern const size_t U_FOLD_LEN;

extern const unsigned long *U_TOLOWER_KEYS, *U_TOLOWER_VALS;
extern const size_t U_TOLOWER_LEN;
extern const unsigned long *U_TOUPPER_KEYS, *U_TOUPPER_VALS;
extern const size_t U_TOUPPER_LEN;

extern const struct U_GB_Range3 *U_GB_RANGES;
extern const size_t U_GB_LEN;

extern const struct U_EP_Range2 *U_EP_RANGES;
extern const size_t U_EP_LEN;

#endif
