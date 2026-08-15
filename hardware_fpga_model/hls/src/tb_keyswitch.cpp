#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cassert>
#include "hls_params.h"
#include "key_switch.h"

using namespace std;

void read_bin(const string& filename, vector<uint64_t>& vec, size_t elements) {
    ifstream in(filename, ios::binary);
    if (!in) {
        cerr << "Error opening " << filename << endl;
        exit(1);
    }
    vec.resize(elements);
    in.read(reinterpret_cast<char*>(vec.data()), elements * sizeof(uint64_t));
    in.close();
}

int main() {
    const uint32_t N_val = 16384;
    const uint32_t sizeQl = 10;   // After rescale: 11 - 1 = 10
    const uint32_t sizeP = 4;
    const uint32_t numPartQ = 3;
    const uint32_t alpha = 4;

    const string tv_dir = "../../../integration_tools/test_vectors/";

    
    // Load NTT parameters
    // File layout: 15 entries = Q[0..10] + P[0..3] (11 Q + 4 P primes)
    // We need Q[0..9] (sizeQl=10) and P[0..3] (sizeP=4), skipping Q[10]
    const uint32_t num_file_q = 11; // Q primes in file (pre-rescale)
    const uint32_t num_file_total = num_file_q + sizeP; // 15
    
    vector<uint64_t> twiddles_all, inv_twiddles_all;
    read_bin(tv_dir + "twiddles.bin", twiddles_all, num_file_total * N_val);
    read_bin(tv_dir + "inv_twiddles.bin", inv_twiddles_all, num_file_total * N_val);
    
    // Remap twiddles: Q[0..9] then P[0..3] (skip Q[10])
    vector<uint64_t> twiddles_q(sizeQl * N_val);
    vector<uint64_t> twiddles_p(sizeP * N_val);
    vector<uint64_t> inv_twiddles_q(sizeQl * N_val);
    vector<uint64_t> inv_twiddles_p(sizeP * N_val);
    for (uint32_t i = 0; i < sizeQl; ++i) {
        memcpy(&twiddles_q[i * N_val], &twiddles_all[i * N_val], N_val * sizeof(uint64_t));
        memcpy(&inv_twiddles_q[i * N_val], &inv_twiddles_all[i * N_val], N_val * sizeof(uint64_t));
    }
    for (uint32_t i = 0; i < sizeP; ++i) {
        memcpy(&twiddles_p[i * N_val], &twiddles_all[(num_file_q + i) * N_val], N_val * sizeof(uint64_t));
        memcpy(&inv_twiddles_p[i * N_val], &inv_twiddles_all[(num_file_q + i) * N_val], N_val * sizeof(uint64_t));
    }
    
    // Read Barrett constants (15 entries, 20 bytes each: 16-byte m + 4-byte k)
    std::ifstream in_barrett(tv_dir + "barrett_constants.bin", std::ios::binary);
    vector<ap_uint<128>> barrett_m_all(num_file_total);
    vector<uint32_t> barrett_k_all(num_file_total);
    for (uint32_t i = 0; i < num_file_total; ++i) {
        unsigned __int128 m_val;
        uint32_t k_val;
        in_barrett.read(reinterpret_cast<char*>(&m_val), 16);
        in_barrett.read(reinterpret_cast<char*>(&k_val), 4);
        uint64_t m_lo = (uint64_t)m_val;
        uint64_t m_hi = (uint64_t)(m_val >> 64);
        barrett_m_all[i] = (ap_uint<128>(m_hi) << 64) | ap_uint<128>(m_lo);
        barrett_k_all[i] = k_val;
    }
    in_barrett.close();
    
    // Remap Barrett: Q[0..9] then P[0..3]
    vector<ap_uint<128>> barrett_m_q(sizeQl), barrett_m_p(sizeP);
    vector<uint32_t> barrett_k_q(sizeQl), barrett_k_p(sizeP);
    for (uint32_t i = 0; i < sizeQl; ++i) {
        barrett_m_q[i] = barrett_m_all[i];
        barrett_k_q[i] = barrett_k_all[i];
    }
    for (uint32_t i = 0; i < sizeP; ++i) {
        barrett_m_p[i] = barrett_m_all[num_file_q + i];
        barrett_k_p[i] = barrett_k_all[num_file_q + i];
    }
    
    // Read N_inv (15 entries)
    vector<uint64_t> n_inv_all;
    read_bin(tv_dir + "n_inv.bin", n_inv_all, num_file_total);
    vector<uint64_t> n_inv_p(sizeP);
    for (uint32_t i = 0; i < sizeP; ++i) {
        n_inv_p[i] = n_inv_all[num_file_q + i];
    }


    // ========================================================================
    // TEST 1: fast_bases_conv (ApproxSwitchCRTBasis) — Coefficient Domain
    // ========================================================================
    // Strategy: Use OpenFHE-dumped coefficient-domain input (partsCt_0) and
    // expected output (partsCtCompl_0) to verify fast_bases_conv directly.
    // No NTT/INTT needed!
    // ========================================================================

    cout << "=== TEST 1: fast_bases_conv (Coefficient Domain) ===" << endl;

    // Read Q and P primes
    vector<uint64_t> Q_primes_all, P_primes;
    read_bin(tv_dir + "rns_primes.bin", Q_primes_all, 11); // file has 11 primes (pre-rescale)
    read_bin(tv_dir + "rns_primes_P.bin", P_primes, sizeP);

    // For partition 0: input basis = {q0, q1, q2, q3}, complementary = {q4..q9, p0..p3}
    vector<uint64_t> in_primes(Q_primes_all.begin(), Q_primes_all.begin() + alpha);
    vector<uint64_t> out_primes(Q_primes_all.begin() + alpha, Q_primes_all.begin() + sizeQl);
    out_primes.insert(out_primes.end(), P_primes.begin(), P_primes.end());
    uint32_t num_out_primes = out_primes.size(); // should be 10

    cout << "  Input primes (alpha=" << alpha << "):" << endl;
    for (uint32_t i = 0; i < alpha; ++i)
        cout << "    q" << i << " = " << in_primes[i] << endl;
    cout << "  Output primes (" << num_out_primes << "):" << endl;
    for (uint32_t i = 0; i < num_out_primes; ++i)
        cout << "    p" << i << " = " << out_primes[i] << endl;

    // Read coefficient-domain input: partsCt_0 (alpha limbs * N)
    vector<uint64_t> partsCt;
    read_bin(tv_dir + "tv_keyswitch_partsCt_0.bin", partsCt, alpha * N_val);
    cout << "  Read partsCt_0: " << partsCt.size() << " values (" << alpha << " limbs)" << endl;

    // Read coefficient-domain expected output: partsCtCompl_0 (num_out_primes limbs * N)
    vector<uint64_t> partsCtCompl_expected;
    read_bin(tv_dir + "tv_keyswitch_partsCtCompl_0.bin", partsCtCompl_expected, num_out_primes * N_val);
    cout << "  Read partsCtCompl_0: " << partsCtCompl_expected.size() << " values (" << num_out_primes << " limbs)" << endl;

    // Read FastBasesConv precomputed tables
    vector<uint64_t> qHatInvModq, qHatModp;
    read_bin(tv_dir + "tv_keyswitch_PartQlHatInvModq_0.bin", qHatInvModq, alpha);
    read_bin(tv_dir + "tv_keyswitch_PartQlHatModp_0.bin", qHatModp, alpha * num_out_primes);

    // Run HLS fast_bases_conv
    vector<uint64_t> out_poly(num_out_primes * N_val, 0);
    cout << "  Running fast_bases_conv..." << endl;
            vector<ap_uint<128>> barrett_m_in(alpha), barrett_m_out(num_out_primes);
            vector<uint32_t> barrett_k_in(alpha), barrett_k_out(num_out_primes);
            for(uint32_t i=0; i<alpha; i++) { barrett_m_in[i] = barrett_m_q[i]; barrett_k_in[i] = barrett_k_q[i]; }
            for(uint32_t i=0; i<num_out_primes; i++) {
                if (i < num_out_primes-sizeP) {
                    barrett_m_out[i] = barrett_m_q[alpha+i];
                    barrett_k_out[i] = barrett_k_q[alpha+i];
                } else {
                    barrett_m_out[i] = barrett_m_p[i-(num_out_primes-sizeP)];
                    barrett_k_out[i] = barrett_k_p[i-(num_out_primes-sizeP)];
                }
            }
            vector<uint64_t> xQHat_buf(alpha * N_val);
fast_bases_conv(
                partsCt.data(), out_poly.data(),
                alpha, num_out_primes,
                in_primes.data(), out_primes.data(),
                qHatInvModq.data(), qHatModp.data(),
                barrett_m_in.data(), barrett_k_in.data(),
                barrett_m_out.data(), barrett_k_out.data(),
                xQHat_buf.data()
            );
    cout << "  fast_bases_conv complete." << endl;

    // Compare output directly against coefficient-domain golden vector
    uint32_t errors = 0;
    for (uint32_t l = 0; l < num_out_primes; ++l) {
        for (uint32_t i = 0; i < N_val; ++i) {
            uint64_t expected = partsCtCompl_expected[l * N_val + i];
            uint64_t actual = out_poly[l * N_val + i];
            if (actual != expected) {
                if (errors < 10) {
                    cout << "  MISMATCH limb " << l << " idx " << i
                         << ": expected=" << expected << " got=" << actual << endl;
                }
                errors++;
            }
        }
    }

    if (errors == 0) {
        cout << "  >>> fast_bases_conv TEST PASSED! (bit-exact match on all "
             << num_out_primes * N_val << " values) <<<" << endl;
    } else {
        cout << "  >>> fast_bases_conv TEST FAILED with " << errors << " / "
             << num_out_primes * N_val << " errors <<<" << endl;
    }

    // ========================================================================
    // TEST 2: fast_bases_conv on Partition 1
    // ========================================================================
    cout << "\n=== TEST 2: fast_bases_conv (Partition 1) ===" << endl;

    vector<uint64_t> in_primes_1(Q_primes_all.begin() + alpha, Q_primes_all.begin() + 2 * alpha);
    vector<uint64_t> out_primes_1;
    // Complementary for part 1: {q0..q3} + {q8,q9} + {p0..p3}
    for (uint32_t i = 0; i < alpha; ++i) out_primes_1.push_back(Q_primes_all[i]);
    for (uint32_t i = 2 * alpha; i < sizeQl; ++i) out_primes_1.push_back(Q_primes_all[i]);
    out_primes_1.insert(out_primes_1.end(), P_primes.begin(), P_primes.end());
    uint32_t num_out_1 = out_primes_1.size(); // should be 10

    vector<uint64_t> partsCt_1, partsCtCompl_1_expected;
    read_bin(tv_dir + "tv_keyswitch_partsCt_1.bin", partsCt_1, alpha * N_val);
    // Check if file exists for partition 1
    {
        ifstream check(tv_dir + "tv_keyswitch_partsCtCompl_1.bin");
        if (!check.good()) {
            cout << "  Skipping partition 1 test (no golden vector file)." << endl;
        } else {
            read_bin(tv_dir + "tv_keyswitch_partsCtCompl_1.bin", partsCtCompl_1_expected, num_out_1 * N_val);

            vector<uint64_t> qHatInvModq_1, qHatModp_1;
            read_bin(tv_dir + "tv_keyswitch_PartQlHatInvModq_1.bin", qHatInvModq_1, alpha);
            read_bin(tv_dir + "tv_keyswitch_PartQlHatModp_1.bin", qHatModp_1, alpha * num_out_1);

            vector<uint64_t> out_poly_1(num_out_1 * N_val, 0);
            cout << "  Running fast_bases_conv for partition 1..." << endl;
            vector<ap_uint<128>> barrett_m_in(alpha), barrett_m_out(num_out_1);
    vector<uint32_t> barrett_k_in(alpha), barrett_k_out(num_out_1);
    for(uint32_t i=0; i<alpha; i++) { 
        barrett_m_in[i] = barrett_m_q[alpha+i]; 
        barrett_k_in[i] = barrett_k_q[alpha+i]; 
    }
    for(uint32_t i=0; i<num_out_1; i++) {
        if (i < num_out_1-sizeP) {
            uint32_t q_idx = (i < alpha) ? i : (2 * alpha + (i - alpha));
            barrett_m_out[i] = barrett_m_q[q_idx];
            barrett_k_out[i] = barrett_k_q[q_idx];
        } else {
            barrett_m_out[i] = barrett_m_p[i-(num_out_1-sizeP)];
            barrett_k_out[i] = barrett_k_p[i-(num_out_1-sizeP)];
        }
    }
            vector<uint64_t> xQHat_buf(alpha * N_val);
fast_bases_conv(
                partsCt_1.data(), out_poly_1.data(),
                alpha, num_out_1,
                in_primes_1.data(), out_primes_1.data(),
                qHatInvModq_1.data(), qHatModp_1.data(),
                barrett_m_in.data(), barrett_k_in.data(),
                barrett_m_out.data(), barrett_k_out.data(),
                xQHat_buf.data()
            );

            uint32_t errors_1 = 0;
            for (uint32_t l = 0; l < num_out_1; ++l) {
                for (uint32_t i = 0; i < N_val; ++i) {
                    if (out_poly_1[l * N_val + i] != partsCtCompl_1_expected[l * N_val + i]) {
                        if (errors_1 < 5) {
                            cout << "  MISMATCH limb " << l << " idx " << i
                                 << ": expected=" << partsCtCompl_1_expected[l * N_val + i]
                                 << " got=" << out_poly_1[l * N_val + i] << endl;
                        }
                        errors_1++;
                    }
                }
            }
            if (errors_1 == 0)
                cout << "  >>> Partition 1 TEST PASSED! <<<" << endl;
            else
                cout << "  >>> Partition 1 TEST FAILED with " << errors_1 << " errors <<<" << endl;
        }
    }

    // ========================================================================
    // TEST 3: fast_bases_conv on Partition 2 (last partition, smaller)
    // ========================================================================
    cout << "\n=== TEST 3: fast_bases_conv (Partition 2) ===" << endl;

    uint32_t sizePartQl_2 = sizeQl - alpha * 2; // = 2
    vector<uint64_t> in_primes_2(Q_primes_all.begin() + 2 * alpha, Q_primes_all.begin() + sizeQl);
    vector<uint64_t> out_primes_2;
    for (uint32_t i = 0; i < 2 * alpha; ++i) out_primes_2.push_back(Q_primes_all[i]);
    out_primes_2.insert(out_primes_2.end(), P_primes.begin(), P_primes.end());
    uint32_t num_out_2 = out_primes_2.size(); // should be 12

    vector<uint64_t> partsCt_2;
    read_bin(tv_dir + "tv_keyswitch_partsCt_2.bin", partsCt_2, sizePartQl_2 * N_val);

    {
        ifstream check(tv_dir + "tv_keyswitch_partsCtCompl_2.bin");
        if (!check.good()) {
            cout << "  Skipping partition 2 test (no golden vector file)." << endl;
        } else {
            vector<uint64_t> partsCtCompl_2_expected;
            read_bin(tv_dir + "tv_keyswitch_partsCtCompl_2.bin", partsCtCompl_2_expected, num_out_2 * N_val);

            vector<uint64_t> qHatInvModq_2, qHatModp_2;
            read_bin(tv_dir + "tv_keyswitch_PartQlHatInvModq_2.bin", qHatInvModq_2, sizePartQl_2);
            read_bin(tv_dir + "tv_keyswitch_PartQlHatModp_2.bin", qHatModp_2, sizePartQl_2 * num_out_2);

            vector<uint64_t> out_poly_2(num_out_2 * N_val, 0);
            cout << "  Running fast_bases_conv for partition 2 (sizePartQl=" << sizePartQl_2 << ")..." << endl;
            vector<ap_uint<128>> barrett_m_in_2(sizePartQl_2), barrett_m_out_2(num_out_2);
    vector<uint32_t> barrett_k_in_2(sizePartQl_2), barrett_k_out_2(num_out_2);
    for(uint32_t i=0; i<sizePartQl_2; i++) { 
        barrett_m_in_2[i] = barrett_m_q[2*alpha+i]; 
        barrett_k_in_2[i] = barrett_k_q[2*alpha+i]; 
    }
    for(uint32_t i=0; i<num_out_2; i++) {
        if (i < num_out_2-sizeP) {
            barrett_m_out_2[i] = barrett_m_q[i];
            barrett_k_out_2[i] = barrett_k_q[i];
        } else {
            barrett_m_out_2[i] = barrett_m_p[i-(num_out_2-sizeP)];
            barrett_k_out_2[i] = barrett_k_p[i-(num_out_2-sizeP)];
        }
    }
            vector<uint64_t> xQHat_buf(sizePartQl_2 * N_val);
fast_bases_conv(
                partsCt_2.data(), out_poly_2.data(),
                sizePartQl_2, num_out_2,
                in_primes_2.data(), out_primes_2.data(),
                qHatInvModq_2.data(), qHatModp_2.data(),
                barrett_m_in_2.data(), barrett_k_in_2.data(),
                barrett_m_out_2.data(), barrett_k_out_2.data(),
                xQHat_buf.data()
            );

            uint32_t errors_2 = 0;
            for (uint32_t l = 0; l < num_out_2; ++l) {
                for (uint32_t i = 0; i < N_val; ++i) {
                    if (out_poly_2[l * N_val + i] != partsCtCompl_2_expected[l * N_val + i]) {
                        if (errors_2 < 5) {
                            cout << "  MISMATCH limb " << l << " idx " << i
                                 << ": expected=" << partsCtCompl_2_expected[l * N_val + i]
                                 << " got=" << out_poly_2[l * N_val + i] << endl;
                        }
                        errors_2++;
                    }
                }
            }
            if (errors_2 == 0)
                cout << "  >>> Partition 2 TEST PASSED! <<<" << endl;
            else
                cout << "  >>> Partition 2 TEST FAILED with " << errors_2 << " errors <<<" << endl;
        }
    }
    cout << "\n=== TEST 4: approx_mod_down on res1 ===" << endl;vector<uint64_t> approx_mod_down_in_1, approx_mod_down_expected_1;
    read_bin(tv_dir + "tv_keyswitch_ext_res1.bin", approx_mod_down_in_1, (sizeQl + sizeP) * N_val);
    read_bin(tv_dir + "tv_keyswitch_final_res1.bin", approx_mod_down_expected_1, sizeQl * N_val);
    
    vector<uint64_t> PInvModq, PHatInvModp, PHatModq;
    read_bin(tv_dir + "tv_keyswitch_PInvModq.bin", PInvModq, sizeQl);
    read_bin(tv_dir + "tv_keyswitch_PHatInvModp.bin", PHatInvModp, sizeP);
    read_bin(tv_dir + "tv_keyswitch_PHatModq.bin", PHatModq, sizeP * sizeQl);
    
    vector<uint64_t> out_poly_approx_1(sizeQl * N_val, 0);
    cout << "  Running approx_mod_down on res1..." << endl;
    
    // Construct in_primes which is {Q_primes[0..9], P_primes[0..3]}
    vector<uint64_t> q_primes_10(Q_primes_all.begin(), Q_primes_all.begin() + sizeQl);
    
    vector<uint64_t> buf_P_1(sizeP * N_val);
    vector<uint64_t> partPSwitchedToQ_1(sizeQl * N_val);
    vector<uint64_t> xQHatInvModqi_buf_1(sizeP * N_val);

    approx_mod_down(
        approx_mod_down_in_1.data(),
        out_poly_approx_1.data(),
        sizeQl,
        sizeP,
        q_primes_10.data(),
        P_primes.data(),
        PInvModq.data(),
        PHatInvModp.data(),
        PHatModq.data(),
        inv_twiddles_p.data(),
        twiddles_q.data(),
        barrett_m_p.data(),
        barrett_k_p.data(),
        barrett_m_q.data(),
        barrett_k_q.data(),
        n_inv_p.data(),
        buf_P_1.data(),
        partPSwitchedToQ_1.data(),
        xQHatInvModqi_buf_1.data()
    );
    
    uint32_t errors_approx_1 = 0;
    for (uint32_t l = 0; l < sizeQl; ++l) {
        for (uint32_t i = 0; i < N_val; ++i) {
            uint64_t val = out_poly_approx_1[l * N_val + i];
            if (val != approx_mod_down_expected_1[l * N_val + i]) {
                if (errors_approx_1 < 5) {
                    cout << "  MISMATCH approx_mod_down 1 limb " << l << " idx " << i
                         << ": expected=" << approx_mod_down_expected_1[l * N_val + i]
                         << " got=" << val << endl;
                }
                errors_approx_1++;
            }
        }
    }
    if (errors_approx_1 == 0)
        cout << "  >>> approx_mod_down 1 TEST PASSED! <<<" << endl;
    else
        cout << "  >>> approx_mod_down 1 TEST FAILED with " << errors_approx_1 << " errors <<<" << endl;

    cout << "\n=== TEST 4b: approx_mod_down on res0 ===" << endl;
    
    vector<uint64_t> approx_mod_down_in_0, approx_mod_down_expected_0;
    read_bin(tv_dir + "tv_keyswitch_ext_res0.bin", approx_mod_down_in_0, (sizeQl + sizeP) * N_val);
    read_bin(tv_dir + "tv_keyswitch_final_res0.bin", approx_mod_down_expected_0, sizeQl * N_val);
    
    vector<uint64_t> out_poly_approx_0(sizeQl * N_val, 0);
    cout << "  Running approx_mod_down on res0..." << endl;
    
    vector<uint64_t> buf_P_0(sizeP * N_val);
    vector<uint64_t> partPSwitchedToQ_0(sizeQl * N_val);
    vector<uint64_t> xQHatInvModqi_buf_0(sizeP * N_val);

    approx_mod_down(
        approx_mod_down_in_0.data(),
        out_poly_approx_0.data(),
        sizeQl,
        sizeP,
        q_primes_10.data(),
        P_primes.data(),
        PInvModq.data(),
        PHatInvModp.data(),
        PHatModq.data(),
        inv_twiddles_p.data(),
        twiddles_q.data(),
        barrett_m_p.data(),
        barrett_k_p.data(),
        barrett_m_q.data(),
        barrett_k_q.data(),
        n_inv_p.data(),
        buf_P_0.data(),
        partPSwitchedToQ_0.data(),
        xQHatInvModqi_buf_0.data()
    );
    
    uint32_t errors_approx_0 = 0;
    for (uint32_t l = 0; l < sizeQl; ++l) {
        for (uint32_t i = 0; i < N_val; ++i) {
            uint64_t val = out_poly_approx_0[l * N_val + i];
            if (val != approx_mod_down_expected_0[l * N_val + i]) {
                if (errors_approx_0 < 5) {
                    cout << "  MISMATCH approx_mod_down 0 limb " << l << " idx " << i
                         << ": expected=" << approx_mod_down_expected_0[l * N_val + i]
                         << " got=" << val << endl;
                }
                errors_approx_0++;
            }
        }
    }
    if (errors_approx_0 == 0)
        cout << "  >>> approx_mod_down 0 TEST PASSED! <<<" << endl;
    else
        cout << "  >>> approx_mod_down 0 TEST FAILED with " << errors_approx_0 << " errors <<<" << endl;
    cout << "\n=== TEST 5: key_switch top function ===" << endl;
    
    // Load evk_a and evk_b
    // IMPORTANT: eval key files have 15 limbs (11 Q + 4 P, at key-generation level)
    // but we need 14 limbs (10 Q + 4 P, at rescaled level). Must skip Q[10].
    uint32_t sizeQlP = sizeQl + sizeP; // 10 + 4 = 14
    uint32_t sizeQlP_file = num_file_q + sizeP; // 11 + 4 = 15 (file layout)
    vector<uint64_t> evk_a(numPartQ * sizeQlP * N_val);
    vector<uint64_t> evk_b(numPartQ * sizeQlP * N_val);
    
    for (uint32_t j = 0; j < numPartQ; ++j) {
        vector<uint64_t> a_full, b_full;
        read_bin(tv_dir + "tv_evalkey_a_" + std::to_string(j) + ".bin", a_full, sizeQlP_file * N_val);
        read_bin(tv_dir + "tv_evalkey_b_" + std::to_string(j) + ".bin", b_full, sizeQlP_file * N_val);
        
        // Copy Q[0..9] (first sizeQl limbs)
        memcpy(&evk_a[j * sizeQlP * N_val], a_full.data(), sizeQl * N_val * sizeof(uint64_t));
        memcpy(&evk_b[j * sizeQlP * N_val], b_full.data(), sizeQl * N_val * sizeof(uint64_t));
        
        // Skip Q[10], copy P[0..3] from file position num_file_q * N_val
        memcpy(&evk_a[j * sizeQlP * N_val + sizeQl * N_val],
               a_full.data() + num_file_q * N_val,
               sizeP * N_val * sizeof(uint64_t));
        memcpy(&evk_b[j * sizeQlP * N_val + sizeQl * N_val],
               b_full.data() + num_file_q * N_val,
               sizeP * N_val * sizeof(uint64_t));
    }
    
    // Load c_in
    vector<uint64_t> c_in(sizeQl * N_val);
    read_bin(tv_dir + "tv_keyswitch_in.bin", c_in, sizeQl * N_val);
    
    // Output buffers
    vector<uint64_t> c_out_0(sizeQl * N_val);
    vector<uint64_t> c_out_1(sizeQl * N_val);
    
    // Read N inverse
    vector<uint64_t> n_inv_q(sizeQl);
    for (uint32_t i = 0; i < sizeQl; ++i) n_inv_q[i] = n_inv_all[i];
    
    // Read qHatInvModq and qHatModp for all partitions
    vector<uint64_t> qHatInvModq_0, qHatModp_0;
    vector<uint64_t> qHatInvModq_1, qHatModp_1;
    vector<uint64_t> qHatInvModq_2, qHatModp_2;
    read_bin(tv_dir + "tv_keyswitch_PartQlHatInvModq_0.bin", qHatInvModq_0, alpha);
    read_bin(tv_dir + "tv_keyswitch_PartQlHatModp_0.bin", qHatModp_0, alpha * 10);
    read_bin(tv_dir + "tv_keyswitch_PartQlHatInvModq_1.bin", qHatInvModq_1, alpha);
    read_bin(tv_dir + "tv_keyswitch_PartQlHatModp_1.bin", qHatModp_1, alpha * 10);
    read_bin(tv_dir + "tv_keyswitch_PartQlHatInvModq_2.bin", qHatInvModq_2, sizeQl - 2*alpha);
    read_bin(tv_dir + "tv_keyswitch_PartQlHatModp_2.bin", qHatModp_2, (sizeQl - 2*alpha) * 12);
    
    vector<uint64_t> ext_res0, ext_res1;
    read_bin(tv_dir + "tv_keyswitch_ext_res0.bin", ext_res0, sizeQlP * N_val);
    read_bin(tv_dir + "tv_keyswitch_ext_res1.bin", ext_res1, sizeQlP * N_val);
    

    // Call key_switch
    
    vector<ap_uint<512>> key_gmem((numPartQ * sizeQlP * N_val * 2) / 8);
    for (size_t i = 0; i < evk_a.size() / 8; i++) {
        ap_uint<512> beat;
        for (int j=0; j<8; j++) beat(64*j+63, 64*j) = evk_a[i*8+j];
        key_gmem[i] = beat;
    }
    for (size_t i = 0; i < evk_b.size() / 8; i++) {
        ap_uint<512> beat;
        for (int j=0; j<8; j++) beat(64*j+63, 64*j) = evk_b[i*8+j];
        key_gmem[evk_a.size() / 8 + i] = beat;
    }
    vector<uint64_t> partsCt_dbg(sizeQlP * N_val), partsCtCompl_dbg(sizeQlP * N_val), digit_dbg(sizeQlP * N_val);

    cout << "  Running key_switch..." << endl;
    key_switch(
        c_in.data(), key_gmem.data(), 0, evk_a.size(), c_out_0.data(), c_out_1.data(),
        sizeQl, sizeP, numPartQ, alpha,
        Q_primes_all.data(), P_primes.data(),
        barrett_m_q.data(), barrett_k_q.data(),
        barrett_m_p.data(), barrett_k_p.data(),
        twiddles_q.data(), inv_twiddles_q.data(),
        twiddles_p.data(), inv_twiddles_p.data(),
        n_inv_q.data(), n_inv_p.data(),
        qHatInvModq_0.data(), qHatModp_0.data(),
        qHatInvModq_1.data(), qHatModp_1.data(),
        qHatInvModq_2.data(), qHatModp_2.data(),
        PInvModq.data(), PHatInvModp.data(), PHatModq.data(),
        ext_res0.data(), ext_res1.data(), partsCt_dbg.data(), partsCtCompl_dbg.data(), digit_dbg.data()
    );
    

    
    // Check results
    vector<uint64_t> final_res0_expected;
    vector<uint64_t> final_res1_expected;
    read_bin(tv_dir + "tv_keyswitch_final_res0.bin", final_res0_expected, sizeQl * N_val);
    read_bin(tv_dir + "tv_keyswitch_final_res1.bin", final_res1_expected, sizeQl * N_val);
    
    uint32_t errors_ks0 = 0;
    uint32_t errors_ks1 = 0;
    
    for (uint32_t l = 0; l < sizeQl; ++l) {
        for (uint32_t i = 0; i < N_val; ++i) {
            if (c_out_0[l * N_val + i] != final_res0_expected[l * N_val + i]) {
                if (errors_ks0 < 5) cout << "  MISMATCH res0 limb " << l << " idx " << i << ": expected=" << final_res0_expected[l * N_val + i] << " got=" << c_out_0[l * N_val + i] << endl;
                errors_ks0++;
            }
            if (c_out_1[l * N_val + i] != final_res1_expected[l * N_val + i]) {
                if (errors_ks1 < 5) cout << "  MISMATCH res1 limb " << l << " idx " << i << ": expected=" << final_res1_expected[l * N_val + i] << " got=" << c_out_1[l * N_val + i] << endl;
                errors_ks1++;
            }
        }
    }
    
    if (errors_ks0 == 0 && errors_ks1 == 0)
        cout << "  >>> key_switch TEST PASSED! <<<" << endl;
    else
        cout << "  >>> key_switch TEST FAILED with res0 errors: " << errors_ks0 << " res1 errors: " << errors_ks1 << " <<<" << endl;
    
    return 0;
}
