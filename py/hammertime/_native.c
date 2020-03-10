#include "stdint.h"

uint64_t parity(uint64_t v) {
    return __builtin_parityl(v);
}


uint64_t ctzl(uint64_t v) {
    return __builtin_ctzl(v);
}
