#include "fhe_accel_top.h"
#include "ntt.h"
#include "poly_arith.h"
#include "automorphism.h"
#include "key_switch.h"
#include "rescale.h"

// Context constants in BRAM/URAM
static uint64_t g_twiddles_q[MAX_LIMBS * N];
static uint64_t g_inv_twiddles_q[MAX_LIMBS * N];
static uint64_t g_twiddles_p[4 * N];
static uint64_t g_inv_twiddles_p[4 * N];

static uint64_t g_qHatInvModq_0[4];
static uint64_t g_qHatModp_0[4 * 15];
static uint64_t g_qHatInvModq_1[4];
static uint64_t g_qHatModp_1[4 * 15];
static uint64_t g_qHatInvModq_2[4];
static uint64_t g_qHatModp_2[4 * 15];

static uint64_t g_PInvModq[MAX_LIMBS];
static uint64_t g_PHatInvModp[4];
static uint64_t g_PHatModq[4 * MAX_LIMBS];

static uint64_t g_QlQlInvModqlDivqlModq[MAX_LIMBS];
static uint64_t g_qlInvModq[MAX_LIMBS];

// Working buffers in URAM
static uint64_t g_work_0[MAX_LIMBS * N];
static uint64_t g_work_1[MAX_LIMBS * N];
static uint64_t g_work_2[MAX_LIMBS * N];
static uint64_t g_work_3[MAX_LIMBS * N];
static uint64_t g_work_4[MAX_LIMBS * N];
static uint64_t g_work_5[MAX_LIMBS * N];

void burst_load(ap_uint<512>* gmem, uint32_t offset_words, uint64_t* local_buf, uint32_t num_words) {
    uint32_t num_full_beats = num_words / 8;
    uint32_t remainder = num_words % 8;
    uint32_t beat_offset = offset_words / 8;
    for (uint32_t i = 0; i < num_full_beats; i++) {
#pragma HLS LOOP_TRIPCOUNT min=0 max=30720
#pragma HLS PIPELINE II=1
        ap_uint<512> beat = gmem[beat_offset + i];
        for (int j = 0; j < 8; j++) {
#pragma HLS UNROLL
            local_buf[i * 8 + j] = beat(64*j + 63, 64*j);
        }
    }
    if (remainder > 0) {
        ap_uint<512> beat = gmem[beat_offset + num_full_beats];
        for (uint32_t j = 0; j < 8; j++) {
#pragma HLS UNROLL
            if (j < remainder) {
                local_buf[num_full_beats * 8 + j] = beat(64*j + 63, 64*j);
            }
        }
    }
}

void burst_store(ap_uint<512>* gmem, uint32_t offset_words, const uint64_t* local_buf, uint32_t num_words) {
    uint32_t num_full_beats = num_words / 8;
    uint32_t remainder = num_words % 8;
    uint32_t beat_offset = offset_words / 8;
    for (uint32_t i = 0; i < num_full_beats; i++) {
#pragma HLS LOOP_TRIPCOUNT min=0 max=30720
#pragma HLS PIPELINE II=1
        ap_uint<512> beat;
        for (int j = 0; j < 8; j++) {
#pragma HLS UNROLL
            beat(64*j + 63, 64*j) = local_buf[i * 8 + j];
        }
        gmem[beat_offset + i] = beat;
    }
    if (remainder > 0) {
        ap_uint<512> beat;
        for (uint32_t j = 0; j < 8; j++) {
#pragma HLS UNROLL
            if (j < remainder) {
                beat(64*j + 63, 64*j) = local_buf[num_full_beats * 8 + j];
            } else {
                beat(64*j + 63, 64*j) = 0;
            }
        }
        gmem[beat_offset + num_full_beats] = beat;
    }
}

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
) {
#pragma HLS INTERFACE m_axi port=poly_gmem bundle=gmem0 depth=1000000
#pragma HLS INTERFACE m_axi port=key_gmem  bundle=gmem1 depth=1000000

#pragma HLS INTERFACE s_axilite port=op_code
#pragma HLS INTERFACE s_axilite port=src_a_offset
#pragma HLS INTERFACE s_axilite port=src_b_offset
#pragma HLS INTERFACE s_axilite port=dst_offset
#pragma HLS INTERFACE s_axilite port=evk_offset
#pragma HLS INTERFACE s_axilite port=active_limbs
#pragma HLS INTERFACE s_axilite port=prime_idx
#pragma HLS INTERFACE s_axilite port=galois_elt
#pragma HLS INTERFACE s_axilite port=sizeQl
#pragma HLS INTERFACE s_axilite port=sizeP
#pragma HLS INTERFACE s_axilite port=numPartQ
#pragma HLS INTERFACE s_axilite port=alpha
#pragma HLS INTERFACE s_axilite port=rns_primes
#pragma HLS INTERFACE s_axilite port=p_primes
#pragma HLS INTERFACE s_axilite port=barrett_m_q
#pragma HLS INTERFACE s_axilite port=barrett_k_q
#pragma HLS INTERFACE s_axilite port=barrett_m_p
#pragma HLS INTERFACE s_axilite port=barrett_k_p
#pragma HLS INTERFACE s_axilite port=n_inv_q
#pragma HLS INTERFACE s_axilite port=n_inv_p
#pragma HLS INTERFACE s_axilite port=n_inv_mod_q_last
#pragma HLS INTERFACE s_axilite port=twiddles_q_offset
#pragma HLS INTERFACE s_axilite port=inv_twiddles_q_offset
#pragma HLS INTERFACE s_axilite port=twiddles_p_offset
#pragma HLS INTERFACE s_axilite port=inv_twiddles_p_offset
#pragma HLS INTERFACE s_axilite port=qHatInvModq_0_offset
#pragma HLS INTERFACE s_axilite port=qHatModp_0_offset
#pragma HLS INTERFACE s_axilite port=qHatInvModq_1_offset
#pragma HLS INTERFACE s_axilite port=qHatModp_1_offset
#pragma HLS INTERFACE s_axilite port=qHatInvModq_2_offset
#pragma HLS INTERFACE s_axilite port=qHatModp_2_offset
#pragma HLS INTERFACE s_axilite port=PInvModq_offset
#pragma HLS INTERFACE s_axilite port=PHatInvModp_offset
#pragma HLS INTERFACE s_axilite port=PHatModq_offset
#pragma HLS INTERFACE s_axilite port=QlQlInvModqlDivqlModq_offset
#pragma HLS INTERFACE s_axilite port=qlInvModq_offset
#pragma HLS INTERFACE s_axilite port=return

#pragma HLS BIND_STORAGE variable=g_work_0 type=ram_2p impl=uram
#pragma HLS ARRAY_PARTITION variable=g_work_0 cyclic factor=8 dim=1
#pragma HLS BIND_STORAGE variable=g_work_1 type=ram_2p impl=uram
#pragma HLS ARRAY_PARTITION variable=g_work_1 cyclic factor=8 dim=1
#pragma HLS BIND_STORAGE variable=g_work_2 type=ram_2p impl=uram
#pragma HLS ARRAY_PARTITION variable=g_work_2 cyclic factor=8 dim=1
#pragma HLS BIND_STORAGE variable=g_work_3 type=ram_2p impl=uram
#pragma HLS ARRAY_PARTITION variable=g_work_3 cyclic factor=8 dim=1
#pragma HLS BIND_STORAGE variable=g_work_4 type=ram_2p impl=uram
#pragma HLS ARRAY_PARTITION variable=g_work_4 cyclic factor=8 dim=1
#pragma HLS BIND_STORAGE variable=g_work_5 type=ram_2p impl=uram
#pragma HLS ARRAY_PARTITION variable=g_work_5 cyclic factor=8 dim=1

#pragma HLS BIND_STORAGE variable=g_twiddles_q type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=g_inv_twiddles_q type=ram_2p impl=bram

    if (op_code == OP_LOAD_CONTEXT) {
        // Load constants from key_gmem
        burst_load(key_gmem, twiddles_q_offset, g_twiddles_q, active_limbs * N);
        burst_load(key_gmem, inv_twiddles_q_offset, g_inv_twiddles_q, active_limbs * N);
        burst_load(key_gmem, twiddles_p_offset, g_twiddles_p, sizeP * N);
        burst_load(key_gmem, inv_twiddles_p_offset, g_inv_twiddles_p, sizeP * N);

        burst_load(key_gmem, qHatInvModq_0_offset, g_qHatInvModq_0, alpha);
        burst_load(key_gmem, qHatModp_0_offset, g_qHatModp_0, alpha * (active_limbs - alpha + sizeP));
        
        burst_load(key_gmem, qHatInvModq_1_offset, g_qHatInvModq_1, alpha);
        burst_load(key_gmem, qHatModp_1_offset, g_qHatModp_1, alpha * (active_limbs - alpha + sizeP));
        
        uint32_t alpha_last = active_limbs - 2 * alpha;
        burst_load(key_gmem, qHatInvModq_2_offset, g_qHatInvModq_2, alpha_last);
        burst_load(key_gmem, qHatModp_2_offset, g_qHatModp_2, alpha_last * (active_limbs - alpha_last + sizeP));

        burst_load(key_gmem, PInvModq_offset, g_PInvModq, active_limbs);
        burst_load(key_gmem, PHatInvModp_offset, g_PHatInvModp, sizeP);
        burst_load(key_gmem, PHatModq_offset, g_PHatModq, sizeP * active_limbs);
        
        burst_load(key_gmem, QlQlInvModqlDivqlModq_offset, g_QlQlInvModqlDivqlModq, active_limbs);
        burst_load(key_gmem, qlInvModq_offset, g_qlInvModq, active_limbs);
        return;
    }

    switch (op_code) {
        case OP_NTT:
            burst_load(poly_gmem, src_a_offset, g_work_0, active_limbs * N);
            for (uint32_t l = 0; l < active_limbs; l++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=15
                ntt_forward(&g_work_0[l * N], &g_twiddles_q[l * N],
                            rns_primes[l], barrett_m_q[l], barrett_k_q[l]);
            }
            burst_store(poly_gmem, dst_offset, g_work_0, active_limbs * N);
            break;

        case OP_INTT:
            burst_load(poly_gmem, src_a_offset, g_work_0, active_limbs * N);
            for (uint32_t l = 0; l < active_limbs; l++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=15
                intt_inverse(&g_work_0[l * N], &g_inv_twiddles_q[l * N], n_inv_q[l],
                             rns_primes[l], barrett_m_q[l], barrett_k_q[l]);
            }
            burst_store(poly_gmem, dst_offset, g_work_0, active_limbs * N);
            break;

        case OP_POLY_ADD:
            burst_load(poly_gmem, src_a_offset, g_work_0, active_limbs * N);
            burst_load(poly_gmem, src_b_offset, g_work_1, active_limbs * N);
            for (uint32_t l = 0; l < active_limbs; l++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=15
                poly_add(&g_work_0[l * N], &g_work_1[l * N], &g_work_2[l * N], rns_primes[l]);
            }
            burst_store(poly_gmem, dst_offset, g_work_2, active_limbs * N);
            break;

        case OP_POLY_SUB:
            burst_load(poly_gmem, src_a_offset, g_work_0, active_limbs * N);
            burst_load(poly_gmem, src_b_offset, g_work_1, active_limbs * N);
            for (uint32_t l = 0; l < active_limbs; l++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=15
                poly_sub(&g_work_0[l * N], &g_work_1[l * N], &g_work_2[l * N], rns_primes[l]);
            }
            burst_store(poly_gmem, dst_offset, g_work_2, active_limbs * N);
            break;

        case OP_POLY_MUL_NTT:
            burst_load(poly_gmem, src_a_offset, g_work_0, active_limbs * N);
            burst_load(poly_gmem, src_b_offset, g_work_1, active_limbs * N);
            for (uint32_t l = 0; l < active_limbs; l++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=15
                poly_mul(&g_work_0[l * N], &g_work_1[l * N], &g_work_2[l * N], rns_primes[l], barrett_m_q[l], barrett_k_q[l]);
            }
            burst_store(poly_gmem, dst_offset, g_work_2, active_limbs * N);
            break;

        case OP_AUTOMORPHISM:
            burst_load(poly_gmem, src_a_offset, g_work_0, active_limbs * N);
            // Reusing src_b_offset to point to auto_map (which is N uint32_t's, i.e. N/2 uint64_t's)
            burst_load(poly_gmem, src_b_offset, g_work_3, N / 2);
            for (uint32_t l = 0; l < active_limbs; l++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=15
                automorphism(&g_work_0[l * N], &g_work_1[l * N], g_work_3);
            }
            burst_store(poly_gmem, dst_offset, g_work_1, active_limbs * N);
            break;

        case OP_RESCALE:
            burst_load(poly_gmem, src_a_offset, g_work_0, active_limbs * N);
            burst_load(poly_gmem, src_b_offset, g_work_1, active_limbs * N);
            
            rescale(g_work_0, g_work_2, active_limbs, rns_primes, barrett_m_q, barrett_k_q,
                    &g_inv_twiddles_q[(active_limbs - 1) * N], g_twiddles_q, n_inv_mod_q_last,
                    g_QlQlInvModqlDivqlModq, g_qlInvModq);
                    
            rescale(g_work_1, g_work_3, active_limbs, rns_primes, barrett_m_q, barrett_k_q,
                    &g_inv_twiddles_q[(active_limbs - 1) * N], g_twiddles_q, n_inv_mod_q_last,
                    g_QlQlInvModqlDivqlModq, g_qlInvModq);
                    
            burst_store(poly_gmem, dst_offset, g_work_2, (active_limbs - 1) * N);
            burst_store(poly_gmem, dst_offset + (active_limbs - 1) * N, g_work_3, (active_limbs - 1) * N);
            break;

        case OP_KEY_SWITCH:
            burst_load(poly_gmem, src_a_offset, g_work_0, sizeQl * N);
            
            key_switch(
                g_work_0,
                key_gmem,
                evk_offset,
                evk_offset + numPartQ * (sizeQl + sizeP) * N, // evk_b offset
                g_work_0,  // c_out_0
                g_work_5,  // c_out_1
                sizeQl, sizeP, numPartQ, alpha,
                rns_primes, p_primes,
                barrett_m_q, barrett_k_q, barrett_m_p, barrett_k_p,
                g_twiddles_q, g_inv_twiddles_q, g_twiddles_p, g_inv_twiddles_p,
                n_inv_q, n_inv_p,
                g_qHatInvModq_0, g_qHatModp_0,
                g_qHatInvModq_1, g_qHatModp_1,
                g_qHatInvModq_2, g_qHatModp_2,
                g_PInvModq, g_PHatInvModp, g_PHatModq,
                g_work_3, // c_out_0_ext
                g_work_4, // c_out_1_ext
                g_work_1, // partsCt
                g_work_2, // partsCtCompl
                g_work_1  // digit
            );
            
            burst_store(poly_gmem, dst_offset, g_work_0, sizeQl * N);
            burst_store(poly_gmem, dst_offset + sizeQl * N, g_work_5, sizeQl * N);
            break;
    }
}

}
