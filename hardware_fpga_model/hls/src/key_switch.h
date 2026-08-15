#pragma once

#include "hls_params.h"
#include "mod_arith.h"
#include <stdint.h>
#include <ap_int.h>
#include "ntt.h"
#include "poly_arith.h"

// Hybrid Key Switching functions

// 1. FastBasesConv (ApproxSwitchCRTBasis)
// Converts a polynomial from basis Q_part to the complementary basis (rest of Q, and P)
void fast_bases_conv(
    const uint64_t* in_poly,      // Input polynomial in Q_part basis
    uint64_t* out_poly,           // Output polynomial in complementary basis
    uint32_t num_in_primes,
    uint32_t num_out_primes,
    const uint64_t* in_primes,
    const uint64_t* out_primes,
    // Constants needed for FastBasesConv
    const uint64_t* qHatInvModq,
    const uint64_t* qHatModp,
    const ap_uint<128>* barrett_m_in,
    const uint32_t* barrett_k_in,
    const ap_uint<128>* barrett_m_out,
    const uint32_t* barrett_k_out,
    // Working buffer
    uint64_t* xQHatInvModqi_buf // size: num_in_primes * N (max 4 * N)
);

// 2. ApproxModDown
// Scales down the polynomial from QlP basis to Ql basis by dividing by P
void approx_mod_down(
    const uint64_t* in_poly,      // Input polynomial in QlP basis
    uint64_t* out_poly,           // Output polynomial in Ql basis
    uint32_t num_q_primes,
    uint32_t num_p_primes,
    const uint64_t* q_primes,
    const uint64_t* p_primes,
    // Constants needed for ApproxModDown
    const uint64_t* PInvModq,
    const uint64_t* PHatInvModp,
    const uint64_t* PHatModq,
    // Constants needed for INTT and NTT
    const uint64_t* inv_twiddles_p,
    const uint64_t* twiddles_q,
    const ap_uint<128>* barrett_m_p,
    const uint32_t* barrett_k_p,
    const ap_uint<128>* barrett_m_q,
    const uint32_t* barrett_k_q,
    const uint64_t* n_inv_p,
    // Working buffers
    uint64_t* buf_P,             // size: num_p_primes * N
    uint64_t* partPSwitchedToQ,  // size: num_q_primes * N
    uint64_t* xQHatInvModqi_buf  // size: num_p_primes * N (for FBC)
);

// 3. EvalKeySwitch
// The main Hybrid Key Switch algorithm
void key_switch(
    const uint64_t* c_in,         // Input ciphertext polynomial (sizeQl limbs)
    ap_uint<512>* key_gmem,       // Evaluation key global memory
    uint64_t evk_a_offset,        // Offset in words for evk_a
    uint64_t evk_b_offset,        // Offset in words for evk_b
    uint64_t* c_out_0,            // Output ciphertext polynomial 0 (sizeQl limbs)
    uint64_t* c_out_1,            // Output ciphertext polynomial 1 (sizeQl limbs)
    
    // Size parameters
    uint32_t sizeQl,
    uint32_t sizeP,
    uint32_t numPartQ,
    uint32_t alpha,
    
    // Primes and Barrett constants (full sizeQlP)
    const uint64_t* q_primes,
    const uint64_t* p_primes,
    const ap_uint<128>* barrett_m_q,
    const uint32_t* barrett_k_q,
    const ap_uint<128>* barrett_m_p,
    const uint32_t* barrett_k_p,
    
    // Twiddles for NTT/INTT
    const uint64_t* twiddles_q,
    const uint64_t* inv_twiddles_q,
    const uint64_t* twiddles_p,
    const uint64_t* inv_twiddles_p,
    const uint64_t* n_inv_q,
    const uint64_t* n_inv_p,
    
    // FastBasesConv parameters (max 3 partitions)
    // We pass 1D arrays and index them using offsets
    const uint64_t* qHatInvModq_0,
    const uint64_t* qHatModp_0,
    const uint64_t* qHatInvModq_1,
    const uint64_t* qHatModp_1,
    const uint64_t* qHatInvModq_2,
    const uint64_t* qHatModp_2,
    
    // ApproxModDown parameters
    const uint64_t* PInvModq,
    const uint64_t* PHatInvModp,
    const uint64_t* PHatModq,
    
    // Working buffers (each size MAX_LIMBS * N)
    uint64_t* c_out_0_ext,
    uint64_t* c_out_1_ext,
    uint64_t* partsCt,
    uint64_t* partsCtCompl,
    uint64_t* digit
);
