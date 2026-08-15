#include "poly_arith.h"

void poly_add(
    const uint64_t* a,
    const uint64_t* b,
    uint64_t* out,
    uint64_t q
) {
    uint32_t N = 16384;
    for (uint32_t i = 0; i < N; i++) {
#pragma HLS PIPELINE II=1
        out[i] = mod_add(a[i], b[i], q);
    }
}

void poly_sub(
    const uint64_t* a,
    const uint64_t* b,
    uint64_t* out,
    uint64_t q
) {
    uint32_t N = 16384;
    for (uint32_t i = 0; i < N; i++) {
#pragma HLS PIPELINE II=1
        out[i] = mod_sub(a[i], b[i], q);
    }
}

void poly_mul(
    const uint64_t* a,
    const uint64_t* b,
    uint64_t* out,
    uint64_t q,
    ap_uint<128> m_barrett,
    uint32_t k_barrett
) {
    uint32_t N = 16384;
    for (uint32_t i = 0; i < N; i++) {
#pragma HLS PIPELINE II=1
        out[i] = mod_mul(a[i], b[i], q, m_barrett, k_barrett);
    }
}

void poly_mul_const(
    const uint64_t* a,
    uint64_t c,
    uint64_t* out,
    uint64_t q,
    ap_uint<128> m_barrett,
    uint32_t k_barrett
) {
    uint32_t N = 16384;
    for (uint32_t i = 0; i < N; i++) {
#pragma HLS PIPELINE II=1
        out[i] = mod_mul(a[i], c, q, m_barrett, k_barrett);
    }
}

void poly_add_const(
    const uint64_t* a,
    uint64_t c,
    uint64_t* out,
    uint64_t q
) {
    uint32_t N = 16384;
    for (uint32_t i = 0; i < N; i++) {
#pragma HLS PIPELINE II=1
        out[i] = mod_add(a[i], c, q);
    }
}
