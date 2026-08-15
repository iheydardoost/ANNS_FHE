#include "mod_arith.h"

uint64_t mod_add(uint64_t a, uint64_t b, uint64_t q) {
#pragma HLS INLINE
    uint64_t sum = a + b;
    return (sum >= q) ? (sum - q) : sum;
}

uint64_t mod_sub(uint64_t a, uint64_t b, uint64_t q) {
#pragma HLS INLINE
    return (a >= b) ? (a - b) : (a + q - b);
}

// Standard Barrett Reduction for a * b mod q
uint64_t mod_mul(uint64_t a, uint64_t b, uint64_t q, ap_uint<128> m, uint32_t k) {
#pragma HLS INLINE
    ap_uint<128> p = (ap_uint<128>)a * b;
    ap_uint<192> p_ext = p;
    ap_uint<192> q1_ext = (p_ext * m) >> k;
    uint64_t q2 = (uint64_t)q1_ext * q;
    uint64_t r = (uint64_t)p - q2;
    if (r >= q) r -= q;
    if (r >= q) r -= q;
    return r;
}
