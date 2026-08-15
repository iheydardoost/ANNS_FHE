#include "automorphism.h"

void automorphism(const uint64_t* a, uint64_t* out, const uint64_t* auto_map_64) {
    uint32_t N = 16384;
    for (uint32_t i = 0; i < N; i++) {
#pragma HLS LOOP_TRIPCOUNT min=16384 max=16384
#pragma HLS PIPELINE II=1
        uint64_t packed = auto_map_64[i / 2];
        uint32_t idx = (i % 2 == 0) ? (uint32_t)(packed & 0xFFFFFFFF) : (uint32_t)(packed >> 32);
        out[i] = a[idx];
    }
}
