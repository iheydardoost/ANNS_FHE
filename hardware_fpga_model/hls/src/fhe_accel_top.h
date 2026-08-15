#pragma once

#include "hls_params.h"
#include <ap_int.h>

extern "C" {

void fhe_accel_top(
    ap_uint<512>* poly_gmem,
    ap_uint<512>* key_gmem,

    uint32_t op_code,
    uint32_t src_a_offset,
    uint32_t src_b_offset,
    uint32_t dst_offset,
    uint64_t evk_offset,
    uint32_t active_limbs,
    uint32_t prime_idx,
    uint32_t galois_elt,
    
    uint32_t sizeQl,
    uint32_t sizeP,
    uint32_t numPartQ,
    uint32_t alpha,

    uint64_t rns_primes[MAX_LIMBS],
    uint64_t p_primes[4],
    ap_uint<128> barrett_m_q[MAX_LIMBS],
    uint32_t barrett_k_q[MAX_LIMBS],
    ap_uint<128> barrett_m_p[4],
    uint32_t barrett_k_p[4],
    uint64_t n_inv_q[MAX_LIMBS],
    uint64_t n_inv_p[4],
    uint64_t n_inv_mod_q_last,

    // Offsets into key_gmem for constants
    uint32_t twiddles_q_offset,
    uint32_t inv_twiddles_q_offset,
    uint32_t twiddles_p_offset,
    uint32_t inv_twiddles_p_offset,

    uint32_t qHatInvModq_0_offset,
    uint32_t qHatModp_0_offset,
    uint32_t qHatInvModq_1_offset,
    uint32_t qHatModp_1_offset,
    uint32_t qHatInvModq_2_offset,
    uint32_t qHatModp_2_offset,

    uint32_t PInvModq_offset,
    uint32_t PHatInvModp_offset,
    uint32_t PHatModq_offset,
    
    uint32_t QlQlInvModqlDivqlModq_offset,
    uint32_t qlInvModq_offset
);

}
