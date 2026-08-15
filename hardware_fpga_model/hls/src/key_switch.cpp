#include "key_switch.h"
#ifndef __SYNTHESIS__
#include <iostream>
#include <fstream>
#endif

void fast_bases_conv(
    const uint64_t* in_poly,      // Input polynomial in Q_part basis (size: alpha * N)
    uint64_t* out_poly,           // Output polynomial in complementary basis (size: (sizeQl - alpha + sizeP) * N)
    uint32_t num_in_primes,       // alpha
    uint32_t num_out_primes,      // sizeQl - alpha + sizeP
    const uint64_t* in_primes,    // Moduli for in_poly
    const uint64_t* out_primes,   // Moduli for out_poly
    const uint64_t* qHatInvModq,  // size: alpha
    const uint64_t* qHatModp,     // size: alpha * num_out_primes
    const ap_uint<128>* barrett_m_in,
    const uint32_t* barrett_k_in,
    const ap_uint<128>* barrett_m_out,
    const uint32_t* barrett_k_out,
    uint64_t* xQHatInvModqi_buf
) {
    uint32_t N = 16384;
    
    // We assume in_poly is in COEFFICIENT format!
    // Precompute xQHatInvModqi (max 4 input primes * 16384)
    for (uint32_t i = 0; i < num_in_primes; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=4
        uint64_t qi = in_primes[i];
        uint64_t qHatInv = qHatInvModq[i];
        for (uint32_t ri = 0; ri < N; ++ri) {
#pragma HLS LOOP_TRIPCOUNT min=16384 max=16384
#pragma HLS PIPELINE II=1
            uint64_t xi = in_poly[i * N + ri];
            xQHatInvModqi_buf[i * N + ri] = mod_mul(xi, qHatInv, qi, barrett_m_in[i], barrett_k_in[i]);
        }
    }
    
    for (uint32_t j = 0; j < num_out_primes; ++j) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=15
        uint64_t pj = out_primes[j];
        for (uint32_t ri = 0; ri < N; ++ri) {
#pragma HLS LOOP_TRIPCOUNT min=16384 max=16384
#pragma HLS PIPELINE II=1
            uint64_t sum = 0;
            for (uint32_t i = 0; i < num_in_primes; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=4
                uint64_t xQHatInvModqi = xQHatInvModqi_buf[i * N + ri];
                // Reduce xQHatInvModqi modulo pj using Barrett to avoid % operator
                // This is needed because xQHatInvModqi can be up to 60 bits, and QHatModp_ij is 45 bits,
                // so their product can be 105 bits which exceeds the Barrett k=93 limit for pj.
                uint64_t xi_mod_pj = mod_mul(xQHatInvModqi, 1, pj, barrett_m_out[j], barrett_k_out[j]);
                
                uint64_t QHatModp_ij = qHatModp[i * num_out_primes + j];
                uint64_t prod_mod = mod_mul(xi_mod_pj, QHatModp_ij, pj, barrett_m_out[j], barrett_k_out[j]);
                sum = mod_add(sum, prod_mod, pj);
            }
            out_poly[j * N + ri] = sum;
        }
    }
}

void approx_mod_down(
    const uint64_t* in_poly,      // Input polynomial in QlP basis (size: (num_q_primes + num_p_primes) * N)
    uint64_t* out_poly,           // Output polynomial in Ql basis (size: num_q_primes * N)
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
    uint64_t* buf_P,
    uint64_t* partPSwitchedToQ,
    uint64_t* xQHatInvModqi_buf
) {
    uint32_t N = 16384;
    
    // The P part of the polynomial is at the end
    const uint64_t* in_poly_P = in_poly + num_q_primes * N;
    
    // Copy P part to working buffer
    for (uint32_t i = 0; i < num_p_primes; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=4 max=4
        for (uint32_t ri = 0; ri < N; ++ri) {
            buf_P[i * N + ri] = in_poly_P[i * N + ri];
        }
    }
    
    // INTT on P part
    for (uint32_t i = 0; i < num_p_primes; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=4 max=4
        intt_inverse(
            buf_P + i * N,
            inv_twiddles_p + i * N,
            n_inv_p[i],
            p_primes[i],
            barrett_m_p[i],
            barrett_k_p[i]
        );
    }
    
    // Convert basis from P to Q
    fast_bases_conv(
        buf_P, 
        partPSwitchedToQ, 
        num_p_primes, 
        num_q_primes, 
        p_primes, 
        q_primes, 
        PHatInvModp, 
        PHatModq,
        barrett_m_p,
        barrett_k_p,
        barrett_m_q,
        barrett_k_q,
        xQHatInvModqi_buf
    );
    

    
    // NTT on Q part
    for (uint32_t i = 0; i < num_q_primes; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=11
        ntt_forward(
            partPSwitchedToQ + i * N,
            twiddles_q + i * N,
            q_primes[i],
            barrett_m_q[i],
            barrett_k_q[i]
        );
    }
    
    // Compute (in_poly_Q - partPSwitchedToQ) * PInvModq mod q
    for (uint32_t i = 0; i < num_q_primes; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=11
        uint64_t qi = q_primes[i];
        uint64_t pinv = PInvModq[i];
        for (uint32_t ri = 0; ri < N; ++ri) {
#pragma HLS PIPELINE II=1
            uint64_t val_q = in_poly[i * N + ri];
            uint64_t val_p_switched = partPSwitchedToQ[i * N + ri];
            
            uint64_t diff;
            if (val_q >= val_p_switched) {
                diff = val_q - val_p_switched;
            } else {
                diff = val_q + qi - val_p_switched;
            }
            
            // Multiply by PInvModq
            out_poly[i * N + ri] = mod_mul(diff, pinv, qi, barrett_m_q[i], barrett_k_q[i]);
        }
    }
}

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
    
    uint64_t* c_out_0_ext,
    uint64_t* c_out_1_ext,
    uint64_t* partsCt,
    uint64_t* partsCtCompl,
    uint64_t* digit
) {
    uint32_t N = 16384;
    uint32_t sizeQlP = sizeQl + sizeP;
    
    // Initialize output buffers to 0
    for (uint32_t i = 0; i < sizeQlP * N; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=16384 max=245760
        c_out_0_ext[i] = 0;
        c_out_1_ext[i] = 0;
    }
    
    // Loop over partitions
    for (uint32_t part = 0; part < numPartQ; ++part) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=3
        uint32_t sizePartQl = (part == numPartQ - 1) ? (sizeQl - alpha * part) : alpha;
        uint32_t startPartIdx = alpha * part;
        
        // Extract limbs from c_in (which is already in evaluation format)
        for (uint32_t i = 0; i < sizePartQl; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=4
            uint32_t idx = startPartIdx + i;
            for (uint32_t ri = 0; ri < N; ++ri) {
                partsCt[i * N + ri] = c_in[idx * N + ri];
            }
        }
        
        // INTT on partsCt to convert to coefficient domain
        for (uint32_t i = 0; i < sizePartQl; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=4
            uint32_t idx = startPartIdx + i;
            intt_inverse(
                partsCt + i * N,
                inv_twiddles_q + idx * N,
                n_inv_q[idx],
                q_primes[idx],
                barrett_m_q[idx],
                barrett_k_q[idx]
            );
        }
        
        // Output primes for this partition are the remaining Q primes and all P primes
        uint32_t num_out_primes = sizeQl - sizePartQl + sizeP;
        uint64_t out_primes_part[15];
        uint32_t out_idx = 0;
        for (uint32_t i = 0; i < sizeQl; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=11
            if (i < startPartIdx || i >= startPartIdx + sizePartQl) {
                out_primes_part[out_idx++] = q_primes[i];
            }
        }
        for (uint32_t i = 0; i < sizeP; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=4 max=4
            out_primes_part[out_idx++] = p_primes[i];
        }
        
        // FastBasesConv
        const uint64_t* qHatInvModq_ptr = (part == 0) ? qHatInvModq_0 : ((part == 1) ? qHatInvModq_1 : qHatInvModq_2);
        const uint64_t* qHatModp_ptr = (part == 0) ? qHatModp_0 : ((part == 1) ? qHatModp_1 : qHatModp_2);
        
        ap_uint<128> barrett_m_out_part[15];
        uint32_t barrett_k_out_part[15];
        uint32_t out_idx_b = 0;
        for (uint32_t i = 0; i < sizeQl; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=11
            if (i < startPartIdx || i >= startPartIdx + sizePartQl) {
                barrett_m_out_part[out_idx_b] = barrett_m_q[i];
                barrett_k_out_part[out_idx_b] = barrett_k_q[i];
                out_idx_b++;
            }
        }
        for (uint32_t i = 0; i < sizeP; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=4 max=4
            barrett_m_out_part[out_idx_b] = barrett_m_p[i];
            barrett_k_out_part[out_idx_b] = barrett_k_p[i];
            out_idx_b++;
        }
        
        fast_bases_conv(
            partsCt,
            partsCtCompl,
            sizePartQl,
            num_out_primes,
            q_primes + startPartIdx, // in_primes
            out_primes_part,         // out_primes
            qHatInvModq_ptr,
            qHatModp_ptr,
            barrett_m_q + startPartIdx,
            barrett_k_q + startPartIdx,
            barrett_m_out_part,
            barrett_k_out_part,
            digit // reuse digit buffer for xQHatInvModqi_buf
        );
        
        // NTT on partsCtCompl to convert back to evaluation domain
        out_idx = 0;
        for (uint32_t i = 0; i < sizeQl; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=11
            if (i < startPartIdx || i >= startPartIdx + sizePartQl) {
                uint64_t q = q_primes[i];
                ap_uint<128> m = barrett_m_q[i];
                uint32_t k = barrett_k_q[i];
                const uint64_t* tw = twiddles_q + i * N;
                ntt_forward(partsCtCompl + out_idx * N, tw, q, m, k);
                out_idx++;
            }
        }
        for (uint32_t i = 0; i < sizeP; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=4 max=4
            uint64_t q = p_primes[i];
            ap_uint<128> m = barrett_m_p[i];
            uint32_t k = barrett_k_p[i];
            const uint64_t* tw = twiddles_p + i * N;
            ntt_forward(partsCtCompl + out_idx * N, tw, q, m, k);
            out_idx++;
        }
        
        // Reassemble the digit in evaluation domain
        out_idx = 0;
        for (uint32_t i = 0; i < sizeQl; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=11
            if (i >= startPartIdx && i < startPartIdx + sizePartQl) {
                // Copy directly from c_in (already in EVALUATION)
                for (uint32_t ri = 0; ri < N; ++ri) digit[i * N + ri] = c_in[i * N + ri];
            } else {
                // Copy from partsCtCompl
                for (uint32_t ri = 0; ri < N; ++ri) digit[i * N + ri] = partsCtCompl[out_idx * N + ri];
                out_idx++;
            }
        }
        // Copy P part
        for (uint32_t i = 0; i < sizeP; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=4 max=4
            for (uint32_t ri = 0; ri < N; ++ri) digit[(sizeQl + i) * N + ri] = partsCtCompl[out_idx * N + ri];
            out_idx++;
        }
        
        // Multiply by evk_a and evk_b and accumulate
        for (uint32_t i = 0; i < sizeQlP; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=15
            uint64_t q = (i < sizeQl) ? q_primes[i] : p_primes[i - sizeQl];
            ap_uint<128> m = (i < sizeQl) ? barrett_m_q[i] : barrett_m_p[i - sizeQl];
            uint32_t k = (i < sizeQl) ? barrett_k_q[i] : barrett_k_p[i - sizeQl];
            
            uint64_t limb_offset = (part * sizeQlP + i) * N;
            uint64_t beat_offset_a = (evk_a_offset + limb_offset) / 8;
            uint64_t beat_offset_b = (evk_b_offset + limb_offset) / 8;
            
            for (uint32_t beat_idx = 0; beat_idx < N / 8; ++beat_idx) {
#pragma HLS PIPELINE II=1
                ap_uint<512> beat_a = key_gmem[beat_offset_a + beat_idx];
                ap_uint<512> beat_b = key_gmem[beat_offset_b + beat_idx];
                
                for (int j = 0; j < 8; ++j) {
#pragma HLS UNROLL
                    uint32_t ri = beat_idx * 8 + j;
                    uint64_t cji = digit[i * N + ri];
                    uint64_t bji = beat_b(64*j + 63, 64*j);
                    uint64_t aji = beat_a(64*j + 63, 64*j);
                    
                    uint64_t cb = mod_mul(cji, bji, q, m, k);
                    uint64_t ca = mod_mul(cji, aji, q, m, k);
                    
                    c_out_0_ext[i * N + ri] = mod_add(c_out_0_ext[i * N + ri], cb, q);
                    c_out_1_ext[i * N + ri] = mod_add(c_out_1_ext[i * N + ri], ca, q);
                }
            }
        }
    }
    
    // ApproxModDown
    approx_mod_down(
        c_out_0_ext,
        c_out_0,
        sizeQl,
        sizeP,
        q_primes,
        p_primes,
        PInvModq,
        PHatInvModp,
        PHatModq,
        inv_twiddles_p,
        twiddles_q,
        barrett_m_p,
        barrett_k_p,
        barrett_m_q,
        barrett_k_q,
        n_inv_p,
        partsCt,
        partsCtCompl,
        digit
    );
    
    approx_mod_down(
        c_out_1_ext,
        c_out_1,
        sizeQl,
        sizeP,
        q_primes,
        p_primes,
        PInvModq,
        PHatInvModp,
        PHatModq,
        inv_twiddles_p,
        twiddles_q,
        barrett_m_p,
        barrett_k_p,
        barrett_m_q,
        barrett_k_q,
        n_inv_p,
        partsCt,
        partsCtCompl,
        digit
    );
    
    // NOTE: The c_in addition is performed at a higher level (EvalKeySwitchCore),
    // not inside EvalFastKeySwitchCore. The golden vectors don't include it.
    // Uncomment when integrating into fhe_accel_top.
    // for (uint32_t i = 0; i < sizeQl; ++i) {
    //     uint64_t q = q_primes[i];
    //     for (uint32_t ri = 0; ri < N; ++ri) {
    // #pragma HLS PIPELINE II=1
    //         c_out_0[i * N + ri] = mod_add(c_out_0[i * N + ri], c_in[i * N + ri], q);
    //     }
    // }
}
