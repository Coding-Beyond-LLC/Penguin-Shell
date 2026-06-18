//
// Shared utility macros for the penguin shell
//

#ifndef PENGUIN_PEN_UTIL_H
#define PENGUIN_PEN_UTIL_H

#include <string.h>

// Binary search over a sorted array of structs.
// arr    - the array
// count  - number of elements
// key    - string key to search for
// key_of - macro or function: (elem_ptr) -> const char *
// out    - pointer variable set to the matching element, or NULL if not found
#define BINARY_SEARCH(arr, count, key, key_of, out) do {           \
    int _bs_lo = 0, _bs_hi = (int)(count) - 1;                     \
    (out) = NULL;                                                  \
    while (_bs_lo <= _bs_hi) {                                     \
        int _bs_mid = _bs_lo + ((_bs_hi - _bs_lo) / 2);            \
        int _bs_cmp = strcmp((key), key_of(&(arr)[_bs_mid]));      \
        if      (_bs_cmp == 0) { (out) = &(arr)[_bs_mid]; break; } \
        else if (_bs_cmp  > 0) _bs_lo = _bs_mid + 1;               \
        else                   _bs_hi = _bs_mid - 1;               \
    }                                                              \
} while (0)

#endif //PENGUIN_PEN_UTIL_H
