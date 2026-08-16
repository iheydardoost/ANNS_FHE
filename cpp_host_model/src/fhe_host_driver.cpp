#include "fhe_host_driver.h"
#include "fhe_accel_top.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>

FHEHostDriver::FHEHostDriver(SimMemoryBus* bus) : m_bus(bus) {
    m_cfg = FHEContextConfig();
    m_cfg.N = N;
    m_cfg.max_limbs = MAX_LIMBS;
    m_cfg.sizeQl = 11;
    m_cfg.sizeP = 4;
    m_cfg.numPartQ = 3;
    m_cfg.alpha = 4;
}

void FHEHostDriver::initialize_context_from_directory(const std::string& vec_dir) {
    std::cout << "[FHEHostDriver] Initializing context from: " << vec_dir << std::endl;

    // 1. RNS Primes
    std::ifstream f_q(vec_dir + "/rns_primes_Q.bin", std::ios::binary);
    if (f_q) f_q.read(reinterpret_cast<char*>(m_cfg.rns_primes), 11 * sizeof(uint64_t));
    std::ifstream f_p(vec_dir + "/rns_primes_P.bin", std::ios::binary);
    if (f_p) f_p.read(reinterpret_cast<char*>(m_cfg.p_primes), 4 * sizeof(uint64_t));

    // 2. Barrett Constants
    std::ifstream f_barrett(vec_dir + "/barrett_constants.bin", std::ios::binary);
    if (f_barrett) {
        for (int i = 0; i < 11; ++i) {
            uint64_t m_low, m_high;
            uint32_t k;
            f_barrett.read(reinterpret_cast<char*>(&m_low), 8);
            f_barrett.read(reinterpret_cast<char*>(&m_high), 8);
            f_barrett.read(reinterpret_cast<char*>(&k), 4);
            ap_uint<128> m = m_high;
            m = (m << 64) | m_low;
            m_cfg.barrett_m_q[i] = m;
            m_cfg.barrett_k_q[i] = k;
        }
        for (int i = 0; i < 4; ++i) {
            uint64_t m_low, m_high;
            uint32_t k;
            f_barrett.read(reinterpret_cast<char*>(&m_low), 8);
            f_barrett.read(reinterpret_cast<char*>(&m_high), 8);
            f_barrett.read(reinterpret_cast<char*>(&k), 4);
            ap_uint<128> m = m_high;
            m = (m << 64) | m_low;
            m_cfg.barrett_m_p[i] = m;
            m_cfg.barrett_k_p[i] = k;
        }
    }

    // 3. N Inverses
    std::ifstream f_ninv(vec_dir + "/n_inv.bin", std::ios::binary);
    if (f_ninv) {
        f_ninv.read(reinterpret_cast<char*>(m_cfg.n_inv_q), 11 * sizeof(uint64_t));
        f_ninv.read(reinterpret_cast<char*>(m_cfg.n_inv_p), 4 * sizeof(uint64_t));
    }

    // 4. Load Twiddles into key_gmem
    uint64_t cur_key_offset = 0;
    m_cfg.twiddles_q_offset = cur_key_offset;
    m_bus->load_key_file(cur_key_offset, vec_dir + "/twiddles.bin");
    cur_key_offset += 15 * N;

    m_cfg.inv_twiddles_q_offset = cur_key_offset;
    m_bus->load_key_file(cur_key_offset, vec_dir + "/inv_twiddles.bin");
    cur_key_offset += 15 * N;

    m_cfg.twiddles_p_offset = m_cfg.twiddles_q_offset + 11 * N;
    m_cfg.inv_twiddles_p_offset = m_cfg.inv_twiddles_q_offset + 11 * N;

    // 5. Precomputed CRT & Rescale Table Offsets (aligned)
    m_cfg.qHatInvModq_0_offset = cur_key_offset; cur_key_offset += 64;
    m_cfg.qHatModp_0_offset = cur_key_offset; cur_key_offset += 64;
    m_cfg.qHatInvModq_1_offset = cur_key_offset; cur_key_offset += 64;
    m_cfg.qHatModp_1_offset = cur_key_offset; cur_key_offset += 64;
    m_cfg.qHatInvModq_2_offset = cur_key_offset; cur_key_offset += 64;
    m_cfg.qHatModp_2_offset = cur_key_offset; cur_key_offset += 64;

    m_cfg.PInvModq_offset = cur_key_offset; cur_key_offset += 64;
    m_cfg.PHatInvModp_offset = cur_key_offset; cur_key_offset += 64;
    m_cfg.PHatModq_offset = cur_key_offset; cur_key_offset += 128;
    m_cfg.QlQlInvModqlDivqlModq_offset = cur_key_offset; cur_key_offset += 64;
    m_cfg.qlInvModq_offset = cur_key_offset; cur_key_offset += 64;

    // Load tables if present in test_vectors
    std::string root_tv = vec_dir + "/../";
    if (std::ifstream(root_tv + "tv_keyswitch_PartQlHatInvModq_0.bin")) {
        m_bus->load_key_file(m_cfg.qHatInvModq_0_offset, root_tv + "tv_keyswitch_PartQlHatInvModq_0.bin");
        m_bus->load_key_file(m_cfg.qHatModp_0_offset, root_tv + "tv_keyswitch_PartQlHatModp_0.bin");
        m_bus->load_key_file(m_cfg.qHatInvModq_1_offset, root_tv + "tv_keyswitch_PartQlHatInvModq_1.bin");
        m_bus->load_key_file(m_cfg.qHatModp_1_offset, root_tv + "tv_keyswitch_PartQlHatModp_1.bin");
        m_bus->load_key_file(m_cfg.qHatInvModq_2_offset, root_tv + "tv_keyswitch_PartQlHatInvModq_2.bin");
        m_bus->load_key_file(m_cfg.qHatModp_2_offset, root_tv + "tv_keyswitch_PartQlHatModp_2.bin");
        m_bus->load_key_file(m_cfg.PInvModq_offset, root_tv + "tv_keyswitch_PInvModq.bin");
        m_bus->load_key_file(m_cfg.PHatInvModp_offset, root_tv + "tv_keyswitch_PHatInvModp.bin");
        m_bus->load_key_file(m_cfg.PHatModq_offset, root_tv + "tv_keyswitch_PHatModq.bin");
        m_bus->load_key_file(m_cfg.QlQlInvModqlDivqlModq_offset, root_tv + "QlQlInvModqlDivqlModq.bin");
        m_bus->load_key_file(m_cfg.qlInvModq_offset, root_tv + "qlInvModq.bin");
    }

    // 6. Relinearization Key (EvalMult)
    cur_key_offset = (cur_key_offset + 511) & ~511ULL; // Align to 512-word boundary
    m_cfg.relin_key_offset = cur_key_offset;
    for (int d = 0; d < 3; ++d) {
        std::string fa = vec_dir + "/evalkey_mult_a_" + std::to_string(d) + ".bin";
        if (std::ifstream(fa)) {
            m_bus->load_key_file(cur_key_offset + d * (15 * N), fa);
        }
        std::string fb = vec_dir + "/evalkey_mult_b_" + std::to_string(d) + ".bin";
        if (std::ifstream(fb)) {
            m_bus->load_key_file(cur_key_offset + 3 * (15 * N) + d * (15 * N), fb);
        }
    }
    cur_key_offset += 6 * (15 * N);

    // 7. Load Rotation Keys Directory
    std::ifstream f_rot_meta(vec_dir + "/rotation_keys_meta.txt");
    if (f_rot_meta) {
        std::string line;
        uint32_t cur_auto_map_poly_offset = SCRATCH_AUTO_MAP;
        while (std::getline(f_rot_meta, line)) {
            if (line.empty()) continue;
            std::istringstream iss(line);
            std::string s_step, s_idx;
            iss >> s_step >> s_idx;
            int32_t step = std::stoi(s_step.substr(s_step.find('=') + 1));
            uint32_t auto_idx = std::stoul(s_idx.substr(s_idx.find('=') + 1));

            RotationKeyMeta meta;
            meta.step = step;
            meta.auto_idx = auto_idx;
            meta.auto_map_offset = cur_auto_map_poly_offset;
            cur_auto_map_poly_offset += N; // 16384 words

            cur_key_offset = (cur_key_offset + 511) & ~511ULL;
            meta.evk_offset = cur_key_offset;

            // Load auto_map into poly_gmem: pack two uint32_t into each uint64_t (N/2 words)
            std::string map_file = vec_dir + "/auto_map_step" + std::to_string(step) + ".bin";
            if (std::ifstream(map_file)) {
                std::vector<uint32_t> map_u32(N);
                std::ifstream fm(map_file, std::ios::binary);
                fm.read(reinterpret_cast<char*>(map_u32.data()), N * sizeof(uint32_t));
                std::vector<uint64_t> map_packed(N / 2);
                for (size_t i = 0; i < N / 2; ++i) {
                    uint64_t low = map_u32[2 * i];
                    uint64_t high = map_u32[2 * i + 1];
                    map_packed[i] = (high << 32) | low;
                }
                m_bus->write_poly(meta.auto_map_offset, map_packed.data(), N / 2);
            }

            // Load rotkey polynomials into key_gmem: all 3 a-digits then all 3 b-digits (14 limbs per digit for Level 1)
            const uint32_t ROT_LIMBS = 14;
            for (int d = 0; d < 3; ++d) {
                std::string ra = vec_dir + "/rotkey_step" + std::to_string(step) + "_a_" + std::to_string(d) + ".bin";
                if (std::ifstream(ra)) {
                    m_bus->load_key_file(cur_key_offset + d * (ROT_LIMBS * N), ra);
                }
                std::string rb = vec_dir + "/rotkey_step" + std::to_string(step) + "_b_" + std::to_string(d) + ".bin";
                if (std::ifstream(rb)) {
                    m_bus->load_key_file(cur_key_offset + 3 * (ROT_LIMBS * N) + d * (ROT_LIMBS * N), rb);
                }
            }
            cur_key_offset += 6 * (ROT_LIMBS * N);

            m_rot_keys[step] = meta;
        }
    }

    std::cout << "[FHEHostDriver] Context initialized. Loaded " << m_rot_keys.size() << " rotation keys." << std::endl;
}

void FHEHostDriver::dispatch(
    uint32_t op_code, uint32_t src_a, uint32_t src_b, uint32_t dst,
    uint64_t evk_off, uint32_t active_limbs, uint32_t prime_idx,
    uint32_t galois_elt, uint32_t sizeQl
) {
    uint64_t n_inv_last = (active_limbs > 0 && active_limbs <= 15) ? m_cfg.n_inv_q[active_limbs - 1] : 0;

    fhe_accel_top(
        m_bus->poly_gmem_ptr(),
        m_bus->key_gmem_ptr(),
        op_code,
        src_a, src_b, dst,
        evk_off,
        active_limbs, prime_idx, galois_elt,
        sizeQl, m_cfg.sizeP, m_cfg.numPartQ, m_cfg.alpha,
        m_cfg.rns_primes, m_cfg.p_primes,
        m_cfg.barrett_m_q, m_cfg.barrett_k_q,
        m_cfg.barrett_m_p, m_cfg.barrett_k_p,
        m_cfg.n_inv_q, m_cfg.n_inv_p, n_inv_last,
        m_cfg.twiddles_q_offset, m_cfg.inv_twiddles_q_offset,
        m_cfg.twiddles_p_offset, m_cfg.inv_twiddles_p_offset,
        m_cfg.qHatInvModq_0_offset, m_cfg.qHatModp_0_offset,
        m_cfg.qHatInvModq_1_offset, m_cfg.qHatModp_1_offset,
        m_cfg.qHatInvModq_2_offset, m_cfg.qHatModp_2_offset,
        m_cfg.PInvModq_offset, m_cfg.PHatInvModp_offset, m_cfg.PHatModq_offset,
        m_cfg.QlQlInvModqlDivqlModq_offset, m_cfg.qlInvModq_offset
    );
}

void FHEHostDriver::load_context_to_hardware() {
    dispatch(99, 0, 0, 0, 0, m_cfg.sizeQl, 0, 0, m_cfg.sizeQl);
}

void FHEHostDriver::ntt(uint32_t src_off, uint32_t dst_off, uint32_t active_limbs) {
    dispatch(1, src_off, 0, dst_off, 0, active_limbs, 0, 0, m_cfg.sizeQl);
}

void FHEHostDriver::intt(uint32_t src_off, uint32_t dst_off, uint32_t active_limbs) {
    dispatch(2, src_off, 0, dst_off, 0, active_limbs, 0, 0, m_cfg.sizeQl);
}

void FHEHostDriver::poly_add(uint32_t src_a_off, uint32_t src_b_off, uint32_t dst_off, uint32_t active_limbs) {
    dispatch(3, src_a_off, src_b_off, dst_off, 0, active_limbs, 0, 0, m_cfg.sizeQl);
}

void FHEHostDriver::poly_sub(uint32_t src_a_off, uint32_t src_b_off, uint32_t dst_off, uint32_t active_limbs) {
    dispatch(4, src_a_off, src_b_off, dst_off, 0, active_limbs, 0, 0, m_cfg.sizeQl);
}

void FHEHostDriver::poly_mul_ntt(uint32_t src_a_off, uint32_t src_b_off, uint32_t dst_off, uint32_t active_limbs) {
    dispatch(5, src_a_off, src_b_off, dst_off, 0, active_limbs, 0, 0, m_cfg.sizeQl);
}

void FHEHostDriver::automorphism(uint32_t src_off, uint32_t auto_map_off, uint32_t dst_off, uint32_t active_limbs, uint32_t galois_elt) {
    dispatch(6, src_off, auto_map_off, dst_off, 0, active_limbs, 0, galois_elt, m_cfg.sizeQl);
}

void FHEHostDriver::key_switch(uint32_t src_c1_off, uint64_t evk_off, uint32_t dst_c0_off, uint32_t dst_c1_off, uint32_t sizeQl) {
    (void)dst_c1_off;
    dispatch(7, src_c1_off, 0, dst_c0_off, evk_off, sizeQl, 0, 0, sizeQl);
}

void FHEHostDriver::rescale(uint32_t ct_in_c0_off, uint32_t ct_in_c1_off, uint32_t ct_out_c0_off, uint32_t ct_out_c1_off, uint32_t active_limbs) {
    (void)ct_out_c1_off;
    dispatch(8, ct_in_c0_off, ct_in_c1_off, ct_out_c0_off, 0, active_limbs, 0, 0, active_limbs);
}

// ---------------------------------------------------------------------------
// High-Level Composite FHE Operations
// ---------------------------------------------------------------------------

void FHEHostDriver::eval_add(
    uint32_t ct_a_c0, uint32_t ct_a_c1,
    uint32_t ct_b_c0, uint32_t ct_b_c1,
    uint32_t ct_dst_c0, uint32_t ct_dst_c1,
    uint32_t limbs
) {
    poly_add(ct_a_c0, ct_b_c0, ct_dst_c0, limbs);
    poly_add(ct_a_c1, ct_b_c1, ct_dst_c1, limbs);
}

void FHEHostDriver::eval_sub_plain(
    uint32_t ct_a_c0, uint32_t ct_a_c1,
    uint32_t pt_b_off,
    uint32_t ct_dst_c0, uint32_t ct_dst_c1,
    uint32_t limbs
) {
    poly_sub(ct_a_c0, pt_b_off, ct_dst_c0, limbs);
    if (ct_a_c1 != ct_dst_c1) {
        std::vector<uint64_t> tmp(limbs * N);
        m_bus->read_poly(ct_a_c1, tmp.data(), tmp.size());
        m_bus->write_poly(ct_dst_c1, tmp.data(), tmp.size());
    }
}

void FHEHostDriver::eval_mult_relin_rescale(
    uint32_t ct_a_c0, uint32_t ct_a_c1,
    uint32_t ct_b_c0, uint32_t ct_b_c1,
    uint32_t ct_dst_c0, uint32_t ct_dst_c1,
    uint32_t in_limbs
) {
    // 1. Cross multiplication
    // d0 = a_c0 * b_c0
    poly_mul_ntt(ct_a_c0, ct_b_c0, SCRATCH_D0, in_limbs);
    // d2 = a_c1 * b_c1
    poly_mul_ntt(ct_a_c1, ct_b_c1, SCRATCH_D2, in_limbs);
    // d1 = (a_c0 * b_c1) + (a_c1 * b_c0)
    poly_mul_ntt(ct_a_c0, ct_b_c1, SCRATCH_D1, in_limbs);
    poly_mul_ntt(ct_a_c1, ct_b_c0, SCRATCH_TMP, in_limbs);
    poly_add(SCRATCH_D1, SCRATCH_TMP, SCRATCH_D1, in_limbs);

    // 2. KeySwitch on d2 (Relinearization): writes ks_c0 at SCRATCH_KS_C0 and ks_c1 at SCRATCH_KS_C0 + in_limbs * N
    key_switch(SCRATCH_D2, m_cfg.relin_key_offset, SCRATCH_KS_C0, 0, in_limbs);

    // 3. Accumulate KeySwitch outputs
    poly_add(SCRATCH_D0, SCRATCH_KS_C0, SCRATCH_D0, in_limbs);
    poly_add(SCRATCH_D1, SCRATCH_KS_C0 + in_limbs * N, SCRATCH_D1, in_limbs);

    // 4. Rescale: drop last limb (in_limbs -> in_limbs - 1)
    // Writes c0 to SCRATCH_TMP and c1 to SCRATCH_TMP + (in_limbs - 1) * N
    rescale(SCRATCH_D0, SCRATCH_D1, SCRATCH_TMP, 0, in_limbs);

    uint32_t out_words = (in_limbs - 1) * N;
    std::vector<uint64_t> tmp(out_words);
    m_bus->read_poly(SCRATCH_TMP, tmp.data(), out_words);
    m_bus->write_poly(ct_dst_c0, tmp.data(), out_words);

    m_bus->read_poly(SCRATCH_TMP + out_words, tmp.data(), out_words);
    m_bus->write_poly(ct_dst_c1, tmp.data(), out_words);
}

void FHEHostDriver::eval_mult_plain_rescale(
    uint32_t ct_a_c0, uint32_t ct_a_c1,
    uint32_t pt_b_off,
    uint32_t ct_dst_c0, uint32_t ct_dst_c1,
    uint32_t in_limbs
) {
    // c0 = a_c0 * pt_b
    poly_mul_ntt(ct_a_c0, pt_b_off, SCRATCH_D0, in_limbs);
    // c1 = a_c1 * pt_b
    poly_mul_ntt(ct_a_c1, pt_b_off, SCRATCH_D1, in_limbs);

    // Rescale
    rescale(SCRATCH_D0, SCRATCH_D1, SCRATCH_TMP, 0, in_limbs);

    uint32_t out_words = (in_limbs - 1) * N;
    std::vector<uint64_t> tmp(out_words);
    m_bus->read_poly(SCRATCH_TMP, tmp.data(), out_words);
    m_bus->write_poly(ct_dst_c0, tmp.data(), out_words);

    m_bus->read_poly(SCRATCH_TMP + out_words, tmp.data(), out_words);
    m_bus->write_poly(ct_dst_c1, tmp.data(), out_words);
}

void FHEHostDriver::eval_rotate_single_step(
    uint32_t ct_in_c0, uint32_t ct_in_c1,
    uint32_t ct_dst_c0, uint32_t ct_dst_c1,
    int32_t step, uint32_t limbs
) {
    auto it = m_rot_keys.find(step);
    if (it == m_rot_keys.end()) {
        throw std::runtime_error("Rotation key not found for step: " + std::to_string(step));
    }
    const auto& meta = it->second;

    // 1. Automorphism on c0 -> write to SCRATCH_D0
    automorphism(ct_in_c0, meta.auto_map_offset, SCRATCH_D0, limbs, meta.auto_idx);

    // 2. Automorphism on c1 -> write to SCRATCH_D1
    automorphism(ct_in_c1, meta.auto_map_offset, SCRATCH_D1, limbs, meta.auto_idx);

    // 3. KeySwitch on c1 (using streamed rotation key from meta.evk_offset)
    // Writes ks_c0 at SCRATCH_KS_C0 and ks_c1 at SCRATCH_KS_C0 + limbs * N
    key_switch(SCRATCH_D1, meta.evk_offset, SCRATCH_KS_C0, 0, limbs);

    // 4. Final c0 = auto(c0) + ks_c0
    poly_add(SCRATCH_D0, SCRATCH_KS_C0, ct_dst_c0, limbs);

    // 5. Final c1 = ks_c1
    std::vector<uint64_t> tmp(limbs * N);
    m_bus->read_poly(SCRATCH_KS_C0 + limbs * N, tmp.data(), limbs * N);
    m_bus->write_poly(ct_dst_c1, tmp.data(), limbs * N);
}

void FHEHostDriver::eval_rotate(
    uint32_t ct_in_c0, uint32_t ct_in_c1,
    uint32_t ct_dst_c0, uint32_t ct_dst_c1,
    int32_t total_rot, uint32_t limbs
) {
    if (total_rot == 0) {
        if (ct_in_c0 != ct_dst_c0) {
            std::vector<uint64_t> tmp(limbs * N);
            m_bus->read_poly(ct_in_c0, tmp.data(), tmp.size());
            m_bus->write_poly(ct_dst_c0, tmp.data(), tmp.size());
        }
        if (ct_in_c1 != ct_dst_c1) {
            std::vector<uint64_t> tmp(limbs * N);
            m_bus->read_poly(ct_in_c1, tmp.data(), tmp.size());
            m_bus->write_poly(ct_dst_c1, tmp.data(), tmp.size());
        }
        return;
    }

    uint32_t cur_c0 = ct_in_c0;
    uint32_t cur_c1 = ct_in_c1;
    uint32_t tmp_c0 = SCRATCH_TMP;
    uint32_t tmp_c1 = SCRATCH_TMP + limbs * N;

    int abs_rot = std::abs(total_rot);
    int sign = (total_rot > 0) ? 1 : -1;

    while (abs_rot > 0) {
        int step_size = 1;
        while ((step_size << 1) <= abs_rot) {
            step_size <<= 1;
        }
        int step = sign * step_size;

        uint32_t next_c0 = (abs_rot == step_size) ? ct_dst_c0 : tmp_c0;
        uint32_t next_c1 = (abs_rot == step_size) ? ct_dst_c1 : tmp_c1;

        eval_rotate_single_step(cur_c0, cur_c1, next_c0, next_c1, step, limbs);

        cur_c0 = next_c0;
        cur_c1 = next_c1;
        abs_rot -= step_size;
    }
}
