#include "ntt.h"

// Forward NTT (Decimation-In-Frequency)
void ntt_forward(uint64_t* a, const uint64_t* twiddles, uint64_t q, ap_uint<128> m_barrett, uint32_t k_barrett) {
    uint32_t N = 16384;
    uint32_t t = N >> 1;
    
    // m is the number of butterfly blocks at the current stage
    for (uint32_t m = 1; m < N; m <<= 1) {
#pragma HLS LOOP_TRIPCOUNT min=14 max=14
        for (uint32_t i = 0; i < m; i++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=8192
            uint32_t j1 = 2 * i * t;
            uint32_t j2 = j1 + t - 1;
            uint64_t W = twiddles[m + i];
            
            for (uint32_t i = 0; i < t; i++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=8192
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=a type=inter false
                uint32_t j = j1 + i;
                uint64_t loVal = a[j];
                uint64_t hiVal = a[j + t];
                
                uint64_t omegaFactor = mod_mul(hiVal, W, q, m_barrett, k_barrett);
                
                uint64_t sum = loVal + omegaFactor;
                if (sum >= q) sum -= q;
                
                uint64_t diff;
                if (loVal < omegaFactor) {
                    diff = loVal + q - omegaFactor;
                } else {
                    diff = loVal - omegaFactor;
                }
                
                a[j] = sum;
                a[j + t] = diff;
            }
        }
        t >>= 1;
    }
}

// Inverse NTT (Gentleman-Sande)
void intt_inverse(uint64_t* a, const uint64_t* inv_twiddles, uint64_t N_inv, uint64_t q, ap_uint<128> m_barrett, uint32_t k_barrett) {
    uint32_t N = 16384;
    uint32_t t = 1;
    
    // m is the number of butterfly blocks at the current stage
    for (uint32_t m = (N >> 1); m >= 1; m >>= 1) {
#pragma HLS LOOP_TRIPCOUNT min=14 max=14
        for (uint32_t i = 0; i < m; i++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=8192
            uint32_t j1 = 2 * i * t;
            uint32_t j2 = j1 + t - 1;
            uint64_t W = inv_twiddles[m + i];
            
            for (uint32_t i = 0; i < t; i++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=8192
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=a type=inter false
                uint32_t j = j1 + i;
                uint64_t loVal = a[j];
                uint64_t hiVal = a[j + t];
                
                a[j] = mod_add(loVal, hiVal, q);
                uint64_t diff = mod_sub(loVal, hiVal, q);
                a[j + t] = mod_mul(diff, W, q, m_barrett, k_barrett);
            }
        }
        t <<= 1;
    }
    
    for (uint32_t i = 0; i < N; i++) {
#pragma HLS LOOP_TRIPCOUNT min=16384 max=16384
#pragma HLS PIPELINE II=1
        a[i] = mod_mul(a[i], N_inv, q, m_barrett, k_barrett);
    }
}
