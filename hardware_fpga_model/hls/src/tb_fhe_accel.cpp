#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <cstring>
#include "fhe_accel_top.h"

using namespace std;

static void read_bin(const string& filename, vector<uint64_t>& vec, size_t elements) {
    ifstream in(filename, ios::binary);
    if (!in) {
        cerr << "Error opening " << filename << endl;
        exit(1);
    }
    vec.resize(elements);
    in.read(reinterpret_cast<char*>(vec.data()), elements * sizeof(uint64_t));
    in.close();
}

// Helper to pack uint64_t array into ap_uint<512> array at a 64-bit word offset
static void pack_words(const uint64_t* src, ap_uint<512>* dst_gmem, uint32_t offset_words, uint32_t num_words) {
    uint32_t num_full_beats = num_words / 8;
    uint32_t remainder = num_words % 8;
    uint32_t beat_offset = offset_words / 8;
    
    for (uint32_t i = 0; i < num_full_beats; i++) {
        ap_uint<512> beat = 0;
        for (int j = 0; j < 8; j++) {
            beat(64*j + 63, 64*j) = src[i * 8 + j];
        }
        dst_gmem[beat_offset + i] = beat;
    }
    if (remainder > 0) {
        ap_uint<512> beat = 0;
        for (uint32_t j = 0; j < remainder; j++) {
            beat(64*j + 63, 64*j) = src[num_full_beats * 8 + j];
        }
        dst_gmem[beat_offset + num_full_beats] = beat;
    }
}

// Helper to unpack ap_uint<512> array into uint64_t array
static void unpack_words(const ap_uint<512>* src_gmem, uint64_t* dst, uint32_t offset_words, uint32_t num_words) {
    uint32_t num_full_beats = num_words / 8;
    uint32_t remainder = num_words % 8;
    uint32_t beat_offset = offset_words / 8;
    
    for (uint32_t i = 0; i < num_full_beats; i++) {
        ap_uint<512> beat = src_gmem[beat_offset + i];
        for (int j = 0; j < 8; j++) {
            dst[i * 8 + j] = (uint64_t)beat(64*j + 63, 64*j);
        }
    }
    if (remainder > 0) {
        ap_uint<512> beat = src_gmem[beat_offset + num_full_beats];
        for (uint32_t j = 0; j < remainder; j++) {
            dst[num_full_beats * 8 + j] = (uint64_t)beat(64*j + 63, 64*j);
        }
    }
}

int main() {
    cout << "=================================================================" << endl;
    cout << "   Full System Functional Testbench for fhe_accel_top (Vitis HLS)" << endl;
    cout << "=================================================================" << endl;
    
    const string tv_dir = "../../../integration_tools/test_vectors/";
    const uint32_t N_val = 16384;
    const uint32_t num_limbs = 11;
    const uint32_t sizeQl = 10;
    const uint32_t sizeP = 4;
    const uint32_t numPartQ = 3;
    const uint32_t alpha = 4;
    const uint32_t num_file_total = 15;
    
    // Allocate global memories (in 512-bit beats)
    vector<ap_uint<512>> poly_gmem(500000, 0); // ~32 MB
    vector<ap_uint<512>> key_gmem(500000, 0);  // ~32 MB
    
    // 1. Read Primes and Constants
    vector<uint64_t> Q_primes_all, P_primes;
    read_bin(tv_dir + "rns_primes.bin", Q_primes_all, num_limbs);
    read_bin(tv_dir + "rns_primes_P.bin", P_primes, sizeP);
    
    uint64_t rns_primes[MAX_LIMBS] = {0};
    uint64_t p_primes_arr[4] = {0};
    for (uint32_t i = 0; i < num_limbs; i++) rns_primes[i] = Q_primes_all[i];
    for (uint32_t i = 0; i < sizeP; i++) p_primes_arr[i] = P_primes[i];
    
    // Read Barrett constants
    ifstream in_barrett(tv_dir + "barrett_constants.bin", ios::binary);
    ap_uint<128> barrett_m_q[MAX_LIMBS] = {0};
    uint32_t barrett_k_q[MAX_LIMBS] = {0};
    ap_uint<128> barrett_m_p[4] = {0};
    uint32_t barrett_k_p[4] = {0};
    
    for (uint32_t i = 0; i < num_file_total; i++) {
        uint64_t m_buf[2];
        uint32_t k_val;
        in_barrett.read((char*)m_buf, 16);
        in_barrett.read((char*)&k_val, 4);
        ap_uint<128> m_val = ((ap_uint<128>)m_buf[1] << 64) | m_buf[0];
        if (i < num_limbs) {
            barrett_m_q[i] = m_val;
            barrett_k_q[i] = k_val;
        } else {
            barrett_m_p[i - num_limbs] = m_val;
            barrett_k_p[i - num_limbs] = k_val;
        }
    }
    in_barrett.close();
    
    // Read N inverse
    vector<uint64_t> n_inv_all;
    read_bin(tv_dir + "n_inv.bin", n_inv_all, num_file_total);
    uint64_t n_inv_q[MAX_LIMBS] = {0};
    uint64_t n_inv_p[4] = {0};
    for (uint32_t i = 0; i < num_limbs; i++) n_inv_q[i] = n_inv_all[i];
    for (uint32_t i = 0; i < sizeP; i++) n_inv_p[i] = n_inv_all[num_limbs + i];
    uint64_t n_inv_mod_q_last = n_inv_q[num_limbs - 1];
    
    // Read Twiddles & Inv Twiddles
    vector<uint64_t> twiddles_all, inv_twiddles_all;
    read_bin(tv_dir + "twiddles.bin", twiddles_all, num_file_total * N_val);
    read_bin(tv_dir + "inv_twiddles.bin", inv_twiddles_all, num_file_total * N_val);
    
    vector<uint64_t> twiddles_q(num_limbs * N_val), inv_twiddles_q(num_limbs * N_val);
    vector<uint64_t> twiddles_p(sizeP * N_val), inv_twiddles_p(sizeP * N_val);
    memcpy(twiddles_q.data(), twiddles_all.data(), num_limbs * N_val * sizeof(uint64_t));
    memcpy(inv_twiddles_q.data(), inv_twiddles_all.data(), num_limbs * N_val * sizeof(uint64_t));
    memcpy(twiddles_p.data(), twiddles_all.data() + num_limbs * N_val, sizeP * N_val * sizeof(uint64_t));
    memcpy(inv_twiddles_p.data(), inv_twiddles_all.data() + num_limbs * N_val, sizeP * N_val * sizeof(uint64_t));
    
    // Read FastBasesConv tables
    vector<uint64_t> qHatInvModq_0, qHatModp_0;
    vector<uint64_t> qHatInvModq_1, qHatModp_1;
    vector<uint64_t> qHatInvModq_2, qHatModp_2;
    read_bin(tv_dir + "tv_keyswitch_PartQlHatInvModq_0.bin", qHatInvModq_0, alpha);
    read_bin(tv_dir + "tv_keyswitch_PartQlHatModp_0.bin", qHatModp_0, alpha * 10);
    read_bin(tv_dir + "tv_keyswitch_PartQlHatInvModq_1.bin", qHatInvModq_1, alpha);
    read_bin(tv_dir + "tv_keyswitch_PartQlHatModp_1.bin", qHatModp_1, alpha * 10);
    read_bin(tv_dir + "tv_keyswitch_PartQlHatInvModq_2.bin", qHatInvModq_2, sizeQl - 2*alpha);
    read_bin(tv_dir + "tv_keyswitch_PartQlHatModp_2.bin", qHatModp_2, (sizeQl - 2*alpha) * 12);
    
    // Read ApproxModDown tables
    vector<uint64_t> PInvModq, PHatInvModp, PHatModq;
    read_bin(tv_dir + "tv_keyswitch_PInvModq.bin", PInvModq, sizeQl);
    read_bin(tv_dir + "tv_keyswitch_PHatInvModp.bin", PHatInvModp, sizeP);
    read_bin(tv_dir + "tv_keyswitch_PHatModq.bin", PHatModq, sizeP * sizeQl);
    
    // Read Rescale tables
    vector<uint64_t> QlQlInv, qlInv;
    read_bin(tv_dir + "QlQlInvModqlDivqlModq.bin", QlQlInv, num_limbs - 1);
    read_bin(tv_dir + "qlInvModq.bin", qlInv, num_limbs - 1);
    
    // Read EvalKey
    uint32_t sizeQlP = sizeQl + sizeP; // 14
    uint32_t sizeQlP_file = num_limbs + sizeP; // 15
    vector<uint64_t> evk_a(numPartQ * sizeQlP * N_val);
    vector<uint64_t> evk_b(numPartQ * sizeQlP * N_val);
    for (uint32_t j = 0; j < numPartQ; ++j) {
        vector<uint64_t> a_full, b_full;
        read_bin(tv_dir + "tv_evalkey_a_" + to_string(j) + ".bin", a_full, sizeQlP_file * N_val);
        read_bin(tv_dir + "tv_evalkey_b_" + to_string(j) + ".bin", b_full, sizeQlP_file * N_val);
        memcpy(&evk_a[j * sizeQlP * N_val], a_full.data(), sizeQl * N_val * sizeof(uint64_t));
        memcpy(&evk_b[j * sizeQlP * N_val], b_full.data(), sizeQl * N_val * sizeof(uint64_t));
        memcpy(&evk_a[j * sizeQlP * N_val + sizeQl * N_val], a_full.data() + num_limbs * N_val, sizeP * N_val * sizeof(uint64_t));
        memcpy(&evk_b[j * sizeQlP * N_val + sizeQl * N_val], b_full.data() + num_limbs * N_val, sizeP * N_val * sizeof(uint64_t));
    }
    
    // 2. Setup key_gmem Layout (all offsets aligned to 8 words)
    uint32_t off = 0;
    auto alloc_key_words = [&](uint32_t words) -> uint32_t {
        uint32_t cur = off;
        uint32_t aligned_words = ((words + 7) / 8) * 8;
        off += aligned_words;
        return cur;
    };
    
    uint32_t twiddles_q_off = alloc_key_words(twiddles_q.size());
    pack_words(twiddles_q.data(), key_gmem.data(), twiddles_q_off, twiddles_q.size());
    
    uint32_t inv_twiddles_q_off = alloc_key_words(inv_twiddles_q.size());
    pack_words(inv_twiddles_q.data(), key_gmem.data(), inv_twiddles_q_off, inv_twiddles_q.size());
    
    uint32_t twiddles_p_off = alloc_key_words(twiddles_p.size());
    pack_words(twiddles_p.data(), key_gmem.data(), twiddles_p_off, twiddles_p.size());
    
    uint32_t inv_twiddles_p_off = alloc_key_words(inv_twiddles_p.size());
    pack_words(inv_twiddles_p.data(), key_gmem.data(), inv_twiddles_p_off, inv_twiddles_p.size());
    
    uint32_t qHatInvModq_0_off = alloc_key_words(qHatInvModq_0.size());
    pack_words(qHatInvModq_0.data(), key_gmem.data(), qHatInvModq_0_off, qHatInvModq_0.size());
    
    uint32_t qHatModp_0_off = alloc_key_words(qHatModp_0.size());
    pack_words(qHatModp_0.data(), key_gmem.data(), qHatModp_0_off, qHatModp_0.size());
    
    uint32_t qHatInvModq_1_off = alloc_key_words(qHatInvModq_1.size());
    pack_words(qHatInvModq_1.data(), key_gmem.data(), qHatInvModq_1_off, qHatInvModq_1.size());
    
    uint32_t qHatModp_1_off = alloc_key_words(qHatModp_1.size());
    pack_words(qHatModp_1.data(), key_gmem.data(), qHatModp_1_off, qHatModp_1.size());
    
    uint32_t qHatInvModq_2_off = alloc_key_words(qHatInvModq_2.size());
    pack_words(qHatInvModq_2.data(), key_gmem.data(), qHatInvModq_2_off, qHatInvModq_2.size());
    
    uint32_t qHatModp_2_off = alloc_key_words(qHatModp_2.size());
    pack_words(qHatModp_2.data(), key_gmem.data(), qHatModp_2_off, qHatModp_2.size());
    
    uint32_t PInvModq_off = alloc_key_words(PInvModq.size());
    pack_words(PInvModq.data(), key_gmem.data(), PInvModq_off, PInvModq.size());
    
    uint32_t PHatInvModp_off = alloc_key_words(PHatInvModp.size());
    pack_words(PHatInvModp.data(), key_gmem.data(), PHatInvModp_off, PHatInvModp.size());
    
    uint32_t PHatModq_off = alloc_key_words(PHatModq.size());
    pack_words(PHatModq.data(), key_gmem.data(), PHatModq_off, PHatModq.size());
    
    uint32_t QlQlInv_off = alloc_key_words(QlQlInv.size());
    pack_words(QlQlInv.data(), key_gmem.data(), QlQlInv_off, QlQlInv.size());
    
    uint32_t qlInv_off = alloc_key_words(qlInv.size());
    pack_words(qlInv.data(), key_gmem.data(), qlInv_off, qlInv.size());
    
    uint32_t evk_off = alloc_key_words(evk_a.size() + evk_b.size());
    pack_words(evk_a.data(), key_gmem.data(), evk_off, evk_a.size());
    pack_words(evk_b.data(), key_gmem.data(), evk_off + evk_a.size(), evk_b.size());
    
    // 3. Setup poly_gmem buffer offsets
    const uint32_t poly_words = MAX_LIMBS * N_val;
    const uint32_t src_a_off = 0;
    const uint32_t src_b_off = poly_words;
    const uint32_t dst_off   = 2 * poly_words;
    
    cout << "-> Loading Context into On-Chip Memory (OP_LOAD_CONTEXT)..." << endl;
    fhe_accel_top(
        poly_gmem.data(), key_gmem.data(),
        OP_LOAD_CONTEXT,
        0, 0, 0, 0,
        num_limbs, 0, 0,
        sizeQl, sizeP, numPartQ, alpha,
        rns_primes, p_primes_arr,
        barrett_m_q, barrett_k_q, barrett_m_p, barrett_k_p,
        n_inv_q, n_inv_p, n_inv_mod_q_last,
        twiddles_q_off, inv_twiddles_q_off, twiddles_p_off, inv_twiddles_p_off,
        qHatInvModq_0_off, qHatModp_0_off,
        qHatInvModq_1_off, qHatModp_1_off,
        qHatInvModq_2_off, qHatModp_2_off,
        PInvModq_off, PHatInvModp_off, PHatModq_off,
        QlQlInv_off, qlInv_off
    );
    cout << "   Context loaded successfully." << endl;
    
    int total_errors = 0;
    
    // ========================================================================
    // TEST 1: OP_NTT
    // ========================================================================
    cout << "\n=== TEST 1: OP_NTT ===" << endl;
    vector<uint64_t> ntt_in, ntt_expected;
    read_bin(tv_dir + "tv_ntt_in.bin", ntt_in, num_limbs * N_val);
    read_bin(tv_dir + "tv_ntt_out.bin", ntt_expected, num_limbs * N_val);
    
    pack_words(ntt_in.data(), poly_gmem.data(), src_a_off, num_limbs * N_val);
    
    fhe_accel_top(
        poly_gmem.data(), key_gmem.data(),
        OP_NTT,
        src_a_off, 0, dst_off, 0,
        num_limbs, 0, 0,
        sizeQl, sizeP, numPartQ, alpha,
        rns_primes, p_primes_arr,
        barrett_m_q, barrett_k_q, barrett_m_p, barrett_k_p,
        n_inv_q, n_inv_p, n_inv_mod_q_last,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    );
    
    vector<uint64_t> ntt_actual(num_limbs * N_val);
    unpack_words(poly_gmem.data(), ntt_actual.data(), dst_off, num_limbs * N_val);
    
    int ntt_errors = 0;
    for (size_t i = 0; i < num_limbs * N_val; i++) {
        if (ntt_actual[i] != ntt_expected[i]) ntt_errors++;
    }
    if (ntt_errors == 0) {
        cout << ">>> OP_NTT PASSED (bit-exact on all " << num_limbs * N_val << " values) <<<" << endl;
    } else {
        cout << ">>> OP_NTT FAILED with " << ntt_errors << " errors <<<" << endl;
        total_errors += ntt_errors;
    }
    
    // ========================================================================
    // TEST 2: OP_INTT
    // ========================================================================
    cout << "\n=== TEST 2: OP_INTT ===" << endl;
    pack_words(ntt_expected.data(), poly_gmem.data(), src_a_off, num_limbs * N_val);
    
    fhe_accel_top(
        poly_gmem.data(), key_gmem.data(),
        OP_INTT,
        src_a_off, 0, dst_off, 0,
        num_limbs, 0, 0,
        sizeQl, sizeP, numPartQ, alpha,
        rns_primes, p_primes_arr,
        barrett_m_q, barrett_k_q, barrett_m_p, barrett_k_p,
        n_inv_q, n_inv_p, n_inv_mod_q_last,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    );
    
    vector<uint64_t> intt_actual(num_limbs * N_val);
    unpack_words(poly_gmem.data(), intt_actual.data(), dst_off, num_limbs * N_val);
    
    int intt_errors = 0;
    for (size_t i = 0; i < num_limbs * N_val; i++) {
        if (intt_actual[i] != ntt_in[i]) intt_errors++;
    }
    if (intt_errors == 0) {
        cout << ">>> OP_INTT PASSED (bit-exact on all " << num_limbs * N_val << " values) <<<" << endl;
    } else {
        cout << ">>> OP_INTT FAILED with " << intt_errors << " errors <<<" << endl;
        total_errors += intt_errors;
    }
    
    // ========================================================================
    // TEST 3: OP_POLY_ADD, OP_POLY_SUB, OP_POLY_MUL_NTT
    // ========================================================================
    cout << "\n=== TEST 3: OP_POLY_ADD, SUB, MUL_NTT ===" << endl;
    vector<uint64_t> poly_a, poly_b, poly_add_exp, poly_sub_exp, poly_mul_exp;
    read_bin(tv_dir + "tv_poly_a.bin", poly_a, num_limbs * N_val);
    read_bin(tv_dir + "tv_poly_b.bin", poly_b, num_limbs * N_val);
    read_bin(tv_dir + "tv_poly_add.bin", poly_add_exp, num_limbs * N_val);
    read_bin(tv_dir + "tv_poly_sub.bin", poly_sub_exp, num_limbs * N_val);
    read_bin(tv_dir + "tv_poly_mul.bin", poly_mul_exp, num_limbs * N_val);
    
    pack_words(poly_a.data(), poly_gmem.data(), src_a_off, num_limbs * N_val);
    pack_words(poly_b.data(), poly_gmem.data(), src_b_off, num_limbs * N_val);
    
    // ADD
    fhe_accel_top(
        poly_gmem.data(), key_gmem.data(),
        OP_POLY_ADD,
        src_a_off, src_b_off, dst_off, 0,
        num_limbs, 0, 0,
        sizeQl, sizeP, numPartQ, alpha,
        rns_primes, p_primes_arr,
        barrett_m_q, barrett_k_q, barrett_m_p, barrett_k_p,
        n_inv_q, n_inv_p, n_inv_mod_q_last,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    );
    vector<uint64_t> add_act(num_limbs * N_val);
    unpack_words(poly_gmem.data(), add_act.data(), dst_off, num_limbs * N_val);
    int add_err = 0;
    for (size_t i = 0; i < num_limbs * N_val; i++) if (add_act[i] != poly_add_exp[i]) add_err++;
    if (add_err == 0) cout << ">>> OP_POLY_ADD PASSED! <<<" << endl;
    else { cout << ">>> OP_POLY_ADD FAILED with " << add_err << " errors <<<" << endl; total_errors += add_err; }
    
    // SUB
    fhe_accel_top(
        poly_gmem.data(), key_gmem.data(),
        OP_POLY_SUB,
        src_a_off, src_b_off, dst_off, 0,
        num_limbs, 0, 0,
        sizeQl, sizeP, numPartQ, alpha,
        rns_primes, p_primes_arr,
        barrett_m_q, barrett_k_q, barrett_m_p, barrett_k_p,
        n_inv_q, n_inv_p, n_inv_mod_q_last,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    );
    vector<uint64_t> sub_act(num_limbs * N_val);
    unpack_words(poly_gmem.data(), sub_act.data(), dst_off, num_limbs * N_val);
    int sub_err = 0;
    for (size_t i = 0; i < num_limbs * N_val; i++) if (sub_act[i] != poly_sub_exp[i]) sub_err++;
    if (sub_err == 0) cout << ">>> OP_POLY_SUB PASSED! <<<" << endl;
    else { cout << ">>> OP_POLY_SUB FAILED with " << sub_err << " errors <<<" << endl; total_errors += sub_err; }
    
    // MUL
    fhe_accel_top(
        poly_gmem.data(), key_gmem.data(),
        OP_POLY_MUL_NTT,
        src_a_off, src_b_off, dst_off, 0,
        num_limbs, 0, 0,
        sizeQl, sizeP, numPartQ, alpha,
        rns_primes, p_primes_arr,
        barrett_m_q, barrett_k_q, barrett_m_p, barrett_k_p,
        n_inv_q, n_inv_p, n_inv_mod_q_last,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    );
    vector<uint64_t> mul_act(num_limbs * N_val);
    unpack_words(poly_gmem.data(), mul_act.data(), dst_off, num_limbs * N_val);
    int mul_err = 0;
    for (size_t i = 0; i < num_limbs * N_val; i++) if (mul_act[i] != poly_mul_exp[i]) mul_err++;
    if (mul_err == 0) cout << ">>> OP_POLY_MUL_NTT PASSED! <<<" << endl;
    else { cout << ">>> OP_POLY_MUL_NTT FAILED with " << mul_err << " errors <<<" << endl; total_errors += mul_err; }
    
    // ========================================================================
    // TEST 4: OP_AUTOMORPHISM
    // ========================================================================
    cout << "\n=== TEST 4: OP_AUTOMORPHISM ===" << endl;
    vector<uint64_t> auto_map(N_val), auto_exp(num_limbs * N_val);
    read_bin(tv_dir + "auto_map.bin", auto_map, N_val);
    read_bin(tv_dir + "tv_poly_auto.bin", auto_exp, num_limbs * N_val);
    
    pack_words(poly_a.data(), poly_gmem.data(), src_a_off, num_limbs * N_val);
    pack_words(auto_map.data(), poly_gmem.data(), src_b_off, N_val / 2);
    
    fhe_accel_top(
        poly_gmem.data(), key_gmem.data(),
        OP_AUTOMORPHISM,
        src_a_off, src_b_off, dst_off, 0,
        num_limbs, 0, 0,
        sizeQl, sizeP, numPartQ, alpha,
        rns_primes, p_primes_arr,
        barrett_m_q, barrett_k_q, barrett_m_p, barrett_k_p,
        n_inv_q, n_inv_p, n_inv_mod_q_last,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    );
    vector<uint64_t> auto_act(num_limbs * N_val);
    unpack_words(poly_gmem.data(), auto_act.data(), dst_off, num_limbs * N_val);
    int auto_err = 0;
    for (size_t i = 0; i < num_limbs * N_val; i++) if (auto_act[i] != auto_exp[i]) auto_err++;
    if (auto_err == 0) cout << ">>> OP_AUTOMORPHISM PASSED! <<<" << endl;
    else { cout << ">>> OP_AUTOMORPHISM FAILED with " << auto_err << " errors <<<" << endl; total_errors += auto_err; }
    
    // ========================================================================
    // TEST 5: OP_RESCALE
    // ========================================================================
    cout << "\n=== TEST 5: OP_RESCALE ===" << endl;
    vector<uint64_t> rescale_in, rescale_exp;
    read_bin(tv_dir + "tv_rescale_in.bin", rescale_in, num_limbs * N_val);
    read_bin(tv_dir + "tv_rescale_out.bin", rescale_exp, (num_limbs - 1) * N_val);
    
    pack_words(rescale_in.data(), poly_gmem.data(), src_a_off, num_limbs * N_val);
    pack_words(rescale_in.data(), poly_gmem.data(), src_b_off, num_limbs * N_val);
    
    fhe_accel_top(
        poly_gmem.data(), key_gmem.data(),
        OP_RESCALE,
        src_a_off, src_b_off, dst_off, 0,
        num_limbs, 0, 0,
        sizeQl, sizeP, numPartQ, alpha,
        rns_primes, p_primes_arr,
        barrett_m_q, barrett_k_q, barrett_m_p, barrett_k_p,
        n_inv_q, n_inv_p, n_inv_mod_q_last,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    );
    vector<uint64_t> rescale_act((num_limbs - 1) * N_val);
    unpack_words(poly_gmem.data(), rescale_act.data(), dst_off, (num_limbs - 1) * N_val);
    int rescale_err = 0;
    for (size_t i = 0; i < (num_limbs - 1) * N_val; i++) if (rescale_act[i] != rescale_exp[i]) rescale_err++;
    if (rescale_err == 0) cout << ">>> OP_RESCALE PASSED! <<<" << endl;
    else { cout << ">>> OP_RESCALE FAILED with " << rescale_err << " errors <<<" << endl; total_errors += rescale_err; }
    
    // ========================================================================
    // TEST 6: OP_KEY_SWITCH
    // ========================================================================
    cout << "\n=== TEST 6: OP_KEY_SWITCH ===" << endl;
    vector<uint64_t> ks_in, ks_exp0, ks_exp1;
    read_bin(tv_dir + "tv_keyswitch_in.bin", ks_in, sizeQl * N_val);
    read_bin(tv_dir + "tv_keyswitch_final_res0.bin", ks_exp0, sizeQl * N_val);
    read_bin(tv_dir + "tv_keyswitch_final_res1.bin", ks_exp1, sizeQl * N_val);
    
    pack_words(ks_in.data(), poly_gmem.data(), src_a_off, sizeQl * N_val);
    
    fhe_accel_top(
        poly_gmem.data(), key_gmem.data(),
        OP_KEY_SWITCH,
        src_a_off, 0, dst_off, evk_off,
        num_limbs, 0, 0,
        sizeQl, sizeP, numPartQ, alpha,
        rns_primes, p_primes_arr,
        barrett_m_q, barrett_k_q, barrett_m_p, barrett_k_p,
        n_inv_q, n_inv_p, n_inv_mod_q_last,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    );
    
    vector<uint64_t> ks_act0(sizeQl * N_val), ks_act1(sizeQl * N_val);
    unpack_words(poly_gmem.data(), ks_act0.data(), dst_off, sizeQl * N_val);
    unpack_words(poly_gmem.data(), ks_act1.data(), dst_off + sizeQl * N_val, sizeQl * N_val);
    
    int ks_err0 = 0, ks_err1 = 0;
    for (size_t i = 0; i < sizeQl * N_val; i++) {
        if (ks_act0[i] != ks_exp0[i]) ks_err0++;
        if (ks_act1[i] != ks_exp1[i]) ks_err1++;
    }
    if (ks_err0 == 0 && ks_err1 == 0) {
        cout << ">>> OP_KEY_SWITCH PASSED (bit-exact on both c0 and c1 output polynomials)! <<<" << endl;
    } else {
        cout << ">>> OP_KEY_SWITCH FAILED with c0 errors: " << ks_err0 << ", c1 errors: " << ks_err1 << " <<<" << endl;
        total_errors += (ks_err0 + ks_err1);
    }
    
    cout << "\n=================================================================" << endl;
    if (total_errors == 0) {
        cout << "   ALL END-TO-END SYSTEM TESTS PASSED BIT-ACCURATELY! [100%]" << endl;
    } else {
        cout << "   SYSTEM TESTBENCH FAILED WITH " << total_errors << " TOTAL ERRORS!" << endl;
    }
    cout << "=================================================================" << endl;
    
    return total_errors;
}
