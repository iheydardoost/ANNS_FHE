#pragma once

#include "sim_memory_bus.h"
#include <map>
#include <string>
#include <vector>
#include <cstdint>
#include <ap_int.h>

struct RotationKeyMeta {
    int32_t step;
    uint32_t auto_idx;
    uint32_t auto_map_offset; // Word offset in poly_gmem
    uint64_t evk_offset;      // Word offset in key_gmem
};

struct FHEContextConfig {
    uint32_t N = 16384;
    uint32_t max_limbs = 15;
    uint32_t sizeQl = 11;
    uint32_t sizeP = 4;
    uint32_t numPartQ = 3;
    uint32_t alpha = 4;

    uint64_t rns_primes[15];
    uint64_t p_primes[4];
    ap_uint<128> barrett_m_q[15];
    uint32_t barrett_k_q[15];
    ap_uint<128> barrett_m_p[4];
    uint32_t barrett_k_p[4];
    uint64_t n_inv_q[15];
    uint64_t n_inv_p[4];
    uint64_t n_inv_mod_q_last;

    // Table offsets in key_gmem
    uint32_t twiddles_q_offset;
    uint32_t inv_twiddles_q_offset;
    uint32_t twiddles_p_offset;
    uint32_t inv_twiddles_p_offset;

    uint32_t qHatInvModq_0_offset, qHatModp_0_offset;
    uint32_t qHatInvModq_1_offset, qHatModp_1_offset;
    uint32_t qHatInvModq_2_offset, qHatModp_2_offset;

    uint32_t PInvModq_offset;
    uint32_t PHatInvModp_offset;
    uint32_t PHatModq_offset;

    uint32_t QlQlInvModqlDivqlModq_offset;
    uint32_t qlInvModq_offset;

    uint64_t relin_key_offset;
};

class FHEHostDriver {
public:
    // Memory Map Offsets (in 64-bit words)
    static constexpr uint32_t N = 16384;
    static constexpr uint32_t MAX_LIMBS = 15;
    static constexpr uint32_t POLY_WORDS = MAX_LIMBS * N; // 245760 words

    // Dedicated Scratch Spaces in poly_gmem
    static constexpr uint32_t SCRATCH_D0 = 0x00400000;
    static constexpr uint32_t SCRATCH_D1 = 0x00440000;
    static constexpr uint32_t SCRATCH_D2 = 0x00480000;
    static constexpr uint32_t SCRATCH_KS_C0 = 0x004C0000;
    static constexpr uint32_t SCRATCH_KS_C1 = 0x00500000;
    static constexpr uint32_t SCRATCH_TMP = 0x00540000;
    static constexpr uint32_t SCRATCH_AUTO_MAP = 0x00580000;

    FHEHostDriver(SimMemoryBus* bus);

    // Context & Key Material Setup
    void initialize_context_from_directory(const std::string& test_vectors_dir);
    void load_context_to_hardware();

    // Low-Level Accelerator Primitive Wrappers (OP_1 .. OP_8)
    void ntt(uint32_t src_off, uint32_t dst_off, uint32_t active_limbs);
    void intt(uint32_t src_off, uint32_t dst_off, uint32_t active_limbs);
    void poly_add(uint32_t src_a_off, uint32_t src_b_off, uint32_t dst_off, uint32_t active_limbs);
    void poly_sub(uint32_t src_a_off, uint32_t src_b_off, uint32_t dst_off, uint32_t active_limbs);
    void poly_mul_ntt(uint32_t src_a_off, uint32_t src_b_off, uint32_t dst_off, uint32_t active_limbs);
    void automorphism(uint32_t src_off, uint32_t auto_map_off, uint32_t dst_off, uint32_t active_limbs, uint32_t galois_elt);
    void rescale(uint32_t ct_in_c0_off, uint32_t ct_in_c1_off, uint32_t ct_out_c0_off, uint32_t ct_out_c1_off, uint32_t active_limbs);
    void key_switch(uint32_t src_c1_off, uint64_t evk_off, uint32_t dst_c0_off, uint32_t dst_c1_off, uint32_t sizeQl);

    // High-Level Composite FHE Operations
    void eval_add(uint32_t ct_a_c0, uint32_t ct_a_c1, uint32_t ct_b_c0, uint32_t ct_b_c1,
                  uint32_t ct_dst_c0, uint32_t ct_dst_c1, uint32_t limbs);

    void eval_sub_plain(uint32_t ct_a_c0, uint32_t ct_a_c1, uint32_t pt_b_off,
                        uint32_t ct_dst_c0, uint32_t ct_dst_c1, uint32_t limbs);

    void eval_mult_relin_rescale(uint32_t ct_a_c0, uint32_t ct_a_c1,
                                 uint32_t ct_b_c0, uint32_t ct_b_c1,
                                 uint32_t ct_dst_c0, uint32_t ct_dst_c1,
                                 uint32_t in_limbs);

    void eval_mult_plain_rescale(uint32_t ct_a_c0, uint32_t ct_a_c1,
                                 uint32_t pt_b_off,
                                 uint32_t ct_dst_c0, uint32_t ct_dst_c1,
                                 uint32_t in_limbs);

    void eval_rotate_single_step(uint32_t ct_in_c0, uint32_t ct_in_c1,
                                 uint32_t ct_dst_c0, uint32_t ct_dst_c1,
                                 int32_t step, uint32_t limbs);

    void eval_rotate(uint32_t ct_in_c0, uint32_t ct_in_c1,
                     uint32_t ct_dst_c0, uint32_t ct_dst_c1,
                     int32_t total_rot, uint32_t limbs);

    const FHEContextConfig& get_config() const { return m_cfg; }
    SimMemoryBus* get_bus() { return m_bus; }

private:
    SimMemoryBus* m_bus;
    FHEContextConfig m_cfg;
    std::map<int32_t, RotationKeyMeta> m_rot_keys;

    void dispatch(uint32_t op_code, uint32_t src_a, uint32_t src_b, uint32_t dst,
                  uint64_t evk_off, uint32_t active_limbs, uint32_t prime_idx,
                  uint32_t galois_elt, uint32_t sizeQl);
};
