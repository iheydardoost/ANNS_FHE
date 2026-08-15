#include "rescale.h"

void rescale(const uint64_t* a_in, uint64_t* a_out,
             uint32_t num_limbs,
             const uint64_t* q,
             const ap_uint<128>* m_barrett,
             const uint32_t* k_barrett,
             const uint64_t* inv_twiddles,
             const uint64_t* twiddles,
             uint64_t n_inv_mod_q_last,
             const uint64_t* QlQlInvModqlDivqlModq,
             const uint64_t* qlInvModq) {

    uint32_t N = 16384;
    uint32_t last_limb = num_limbs - 1;
    
    // 1. Extract the last limb
    uint64_t tmp[16384];
    for (uint32_t j = 0; j < N; j++) {
#pragma HLS LOOP_TRIPCOUNT min=16384 max=16384
#pragma HLS PIPELINE II=1
        tmp[j] = a_in[last_limb * N + j];
    }
    
    // 2. INTT on the last limb
    intt_inverse(tmp, inv_twiddles, n_inv_mod_q_last, q[last_limb], m_barrett[last_limb], k_barrett[last_limb]);
    
    // 3. For each remaining limb
    for (uint32_t i = 0; i < last_limb; i++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=10
        uint64_t q_i = q[i];
        ap_uint<128> m_i = m_barrett[i];
        uint32_t k_i = k_barrett[i];
        uint64_t scale_tmp = QlQlInvModqlDivqlModq[i];
        uint64_t scale_a = qlInvModq[i];
        const uint64_t* tw_i = &twiddles[i * N];
        
        uint64_t tmp_i[16384];
        uint64_t old_q = q[last_limb];
        uint64_t old_q_half = old_q >> 1;
        uint64_t old_q_mod_qi = mod_mul(old_q, 1, q_i, m_i, k_i);
        
        for (uint32_t j = 0; j < N; j++) {
#pragma HLS LOOP_TRIPCOUNT min=16384 max=16384
#pragma HLS PIPELINE II=1
            // Modulus switch (Centered Reduction)
            uint64_t v = tmp[j];
            uint64_t v_mod = mod_mul(v, 1, q_i, m_i, k_i);
            uint64_t val;
            if (v > old_q_half) {
                val = mod_sub(v_mod, old_q_mod_qi, q_i);
            } else {
                val = v_mod;
            }
            
            // Multiply by QlQlInvModqlDivqlModq[i]
            tmp_i[j] = mod_mul(val, scale_tmp, q_i, m_i, k_i);
        }
        
        // NTT on tmp_i
        ntt_forward(tmp_i, tw_i, q_i, m_i, k_i);
        
        // Scale and add
        for (uint32_t j = 0; j < N; j++) {
#pragma HLS LOOP_TRIPCOUNT min=16384 max=16384
#pragma HLS PIPELINE II=1
            uint64_t a_val = a_in[i * N + j];
            uint64_t a_scaled = mod_mul(a_val, scale_a, q_i, m_i, k_i);
            a_out[i * N + j] = mod_add(a_scaled, tmp_i[j], q_i);
        }
    }
}
