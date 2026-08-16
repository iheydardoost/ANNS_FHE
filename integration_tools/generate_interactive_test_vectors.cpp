#define _USE_MATH_DEFINES
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <set>
#include <map>
#include <filesystem>
#include "openfhe.h"

using namespace lbcrypto;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static uint32_t ReverseBits(uint32_t x, int numBits) {
    uint32_t result = 0;
    for (int i = 0; i < numBits; i++) {
        result = (result << 1) | (x & 1);
        x >>= 1;
    }
    return result;
}

static uint64_t mod_inverse(uint64_t a, uint64_t m) {
    int64_t m0 = m, t, q;
    int64_t x0 = 0, x1 = 1;
    if (m == 1) return 0;
    int64_t a_signed = a;
    while (a_signed > 1) {
        q = a_signed / m0;
        t = m0;
        m0 = a_signed % m0, a_signed = t;
        t = x0;
        x0 = x1 - q * x0;
        x1 = t;
    }
    if (x1 < 0) x1 += m;
    return x1;
}

static void DumpDCRTPoly(const DCRTPoly& poly, const std::string& filename) {
    auto params = poly.GetParams()->GetParams();
    size_t num_limbs = params.size();
    size_t N = params[0]->GetRingDimension();
    std::vector<uint64_t> data(num_limbs * N);
    for (size_t i = 0; i < num_limbs; i++) {
        auto& elem = poly.GetElementAtIndex(i);
        for (size_t j = 0; j < N; j++) {
            data[i * N + j] = elem[j].ConvertToInt();
        }
    }
    std::ofstream out(filename, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(uint64_t));
    out.close();
}

static void DumpDCRTPolyLevel(const DCRTPoly& poly, uint32_t sizeQl, uint32_t sizeP, const std::string& filename) {
    size_t N = poly.GetParams()->GetParams()[0]->GetRingDimension();
    size_t total_limbs = sizeQl + sizeP;
    std::vector<uint64_t> data(total_limbs * N);
    
    // Copy sizeQl Q limbs (indices 0 .. sizeQl-1)
    for (size_t i = 0; i < sizeQl; i++) {
        auto& elem = poly.GetElementAtIndex(i);
        for (size_t j = 0; j < N; j++) {
            data[i * N + j] = elem[j].ConvertToInt();
        }
    }
    // Copy sizeP P limbs (indices 11 .. 11 + sizeP - 1)
    for (size_t i = 0; i < sizeP; i++) {
        auto& elem = poly.GetElementAtIndex(11 + i);
        for (size_t j = 0; j < N; j++) {
            data[(sizeQl + i) * N + j] = elem[j].ConvertToInt();
        }
    }
    std::ofstream out(filename, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(uint64_t));
    out.close();
}

static void DumpCiphertext(const Ciphertext<DCRTPoly>& ct, const std::string& prefix) {
    const auto& elems = ct->GetElements();
    DumpDCRTPoly(elems[0], prefix + "_c0.bin");
    DumpDCRTPoly(elems[1], prefix + "_c1.bin");
}

static std::vector<std::vector<float>> read_fvecs(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return {};
    std::vector<std::vector<float>> vecs;
    int32_t dim = 0;
    while (f.read(reinterpret_cast<char*>(&dim), sizeof(int32_t))) {
        std::vector<float> v(dim);
        f.read(reinterpret_cast<char*>(v.data()), dim * sizeof(float));
        vecs.push_back(std::move(v));
    }
    return vecs;
}

static std::vector<std::vector<int32_t>> read_ivecs(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return {};
    std::vector<std::vector<int32_t>> vecs;
    int32_t dim = 0;
    while (f.read(reinterpret_cast<char*>(&dim), sizeof(int32_t))) {
        std::vector<int32_t> v(dim);
        f.read(reinterpret_cast<char*>(v.data()), dim * sizeof(int32_t));
        vecs.push_back(std::move(v));
    }
    return vecs;
}

// Decompose arbitrary rotation into binary power-of-2 shifts (matching software model)
static Ciphertext<DCRTPoly> rotate_decomp(
    const CryptoContext<DCRTPoly>& cc,
    const Ciphertext<DCRTPoly>& ct,
    int rot) 
{
    if (rot == 0) return ct;
    auto result = ct;
    int abs_rot = std::abs(rot);
    int sign = (rot > 0) ? 1 : -1;
    while (abs_rot > 0) {
        int step_size = 1;
        while ((step_size << 1) <= abs_rot) {
            step_size <<= 1;
        }
        int step = sign * step_size;
        result = cc->EvalRotate(result, step);
        abs_rot -= step_size;
    }
    return result;
}

int main() {
    try {
        std::cout << "=================================================================" << std::endl;
        std::cout << "   ANNS_FHE: Interactive Search Test Vector Generator (OpenFHE)  " << std::endl;
        std::cout << "=================================================================" << std::endl;

        const std::string out_dir = "test_vectors/interactive/";
        fs::create_directories(out_dir);

        // -----------------------------------------------------------------------
        // 1. Initialize CryptoContext (N=16384, mult_depth=10, scale_bits=45)
        // -----------------------------------------------------------------------
        const uint32_t N_val = 16384;
        const uint32_t slots = N_val / 2; // 8192
        const int D = 128;
        const int n_list = 32;
        const int B = slots / D; // 64 candidates per batch
        const int M = 8;
        const int K = 256;
        const int sub_dim = D / M; // 16

        CCParams<CryptoContextCKKSRNS> parameters;
        parameters.SetMultiplicativeDepth(10);
        parameters.SetScalingModSize(45);
        parameters.SetScalingTechnique(FIXEDMANUAL);
        parameters.SetFirstModSize(60);
        parameters.SetRingDim(N_val);
        parameters.SetSecurityLevel(HEStd_NotSet);

        CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
        cc->Enable(PKE);
        cc->Enable(KEYSWITCH);
        cc->Enable(LEVELEDSHE);

        auto cryptoParams = cc->GetCryptoParameters();
        auto cryptoParamsRNS = std::dynamic_pointer_cast<CryptoParametersRNS>(cryptoParams);
        auto cryptoParamsCKKS = std::dynamic_pointer_cast<CryptoParametersCKKSRNS>(cryptoParams);
        auto elemParams = cryptoParams->GetElementParams();
        auto params = elemParams->GetParams();
        auto paramsP = cryptoParamsRNS->GetParamsP()->GetParams();

        uint32_t num_q_limbs = params.size(); // 11
        uint32_t num_p_limbs = paramsP.size(); // 4
        std::cout << "Initialized CryptoContext: N=" << N_val << ", Q Limbs=" << num_q_limbs 
                  << ", P Limbs=" << num_p_limbs << ", Slots=" << slots << std::endl;

        // -----------------------------------------------------------------------
        // 2. Compute Rotation Indices Required by Interactive Pipeline
        // -----------------------------------------------------------------------
        std::set<int32_t> rot_indices_set;

        // 2.1 Coarse Tree Sum strides (powers of 2 up to 64)
        for (int stride = 1; stride < D; stride *= 2) {
            rot_indices_set.insert(stride);
        }

        // 2.2 Compaction shifts (decomposed into powers of 2)
        for (int i = 0; i < n_list; ++i) {
            int shift = i * (D - 1);
            int abs_rot = std::abs(shift);
            while (abs_rot > 0) {
                int step_size = 1;
                while ((step_size << 1) <= abs_rot) step_size <<= 1;
                rot_indices_set.insert(step_size);
                abs_rot -= step_size;
            }
        }

        // 2.3 Fine Tree Sum strides (B * 2^s up to 4096)
        for (int stride = B; stride < D * B; stride *= 2) {
            rot_indices_set.insert(stride);
        }

        // 2.4 Batch Position Shifts (powers of 2 negative shifts)
        for (int i = 1; i <= 10000; i <<= 1) {
            rot_indices_set.insert(-i);
        }

        std::vector<int32_t> rot_indices(rot_indices_set.begin(), rot_indices_set.end());
        std::cout << "Generating " << rot_indices.size() << " rotation keys..." << std::endl;

        auto keyPair = cc->KeyGen();
        cc->EvalMultKeyGen(keyPair.secretKey);
        cc->EvalRotateKeyGen(keyPair.secretKey, rot_indices);
        std::cout << "Key generation complete." << std::endl;

        // -----------------------------------------------------------------------
        // 3. Dump Parameters & Context Tables
        // -----------------------------------------------------------------------
        std::vector<uint64_t> Q_primes(num_q_limbs), P_primes(num_p_limbs);
        for (size_t i = 0; i < num_q_limbs; i++) Q_primes[i] = params[i]->GetModulus().ConvertToInt();
        for (size_t i = 0; i < num_p_limbs; i++) P_primes[i] = paramsP[i]->GetModulus().ConvertToInt();

        std::ofstream out_qp(out_dir + "rns_primes_Q.bin", std::ios::binary);
        out_qp.write(reinterpret_cast<const char*>(Q_primes.data()), num_q_limbs * sizeof(uint64_t));
        out_qp.close();

        std::ofstream out_pp(out_dir + "rns_primes_P.bin", std::ios::binary);
        out_pp.write(reinterpret_cast<const char*>(P_primes.data()), num_p_limbs * sizeof(uint64_t));
        out_pp.close();

        // Barrett Constants
        std::ofstream out_barrett(out_dir + "barrett_constants.bin", std::ios::binary);
        std::vector<std::shared_ptr<ILNativeParams>> all_p;
        for (size_t i = 0; i < num_q_limbs; i++) all_p.push_back(params[i]);
        for (size_t i = 0; i < num_p_limbs; i++) all_p.push_back(paramsP[i]);
        std::vector<uint64_t> n_inv_all(all_p.size());

        for (size_t i = 0; i < all_p.size(); i++) {
            uint64_t q = all_p[i]->GetModulus().ConvertToInt();
            uint32_t msb = 64 - __builtin_clzll(q);
            uint32_t k = 2 * msb + 3;
            __uint128_t m = ((__uint128_t)1 << k) / (__uint128_t)q;
            out_barrett.write(reinterpret_cast<const char*>(&m), 16);
            out_barrett.write(reinterpret_cast<const char*>(&k), 4);
            n_inv_all[i] = mod_inverse(N_val, q);
        }
        out_barrett.close();

        std::ofstream out_ninv(out_dir + "n_inv.bin", std::ios::binary);
        out_ninv.write(reinterpret_cast<const char*>(n_inv_all.data()), n_inv_all.size() * sizeof(uint64_t));
        out_ninv.close();

        // Twiddles & Inv Twiddles
        std::vector<uint64_t> twiddles(all_p.size() * N_val);
        std::vector<uint64_t> inv_twiddles(all_p.size() * N_val);
        for (size_t i = 0; i < all_p.size(); i++) {
            uint64_t q = all_p[i]->GetModulus().ConvertToInt();
            NativeInteger root = all_p[i]->GetRootOfUnity();
            NativeInteger root_inv = root.ModInverse(q);
            for (size_t j = 0; j < N_val; j++) {
                uint32_t br = ReverseBits(j, 14);
                twiddles[i * N_val + j] = root.ModExp(br, q).ConvertToInt();
                inv_twiddles[i * N_val + j] = root_inv.ModExp(br, q).ConvertToInt();
            }
        }
        std::ofstream out_tw(out_dir + "twiddles.bin", std::ios::binary);
        out_tw.write(reinterpret_cast<const char*>(twiddles.data()), twiddles.size() * sizeof(uint64_t));
        out_tw.close();

        std::ofstream out_itw(out_dir + "inv_twiddles.bin", std::ios::binary);
        out_itw.write(reinterpret_cast<const char*>(inv_twiddles.data()), inv_twiddles.size() * sizeof(uint64_t));
        out_itw.close();

        // Relinearization Key (EvalMult)
        auto evalMultKeyMap = cc->GetEvalMultKeyVector(keyPair.secretKey->GetKeyTag());
        auto relinKey = evalMultKeyMap[0];
        const auto& av = relinKey->GetAVector();
        const auto& bv = relinKey->GetBVector();
        for (uint32_t j = 0; j < av.size(); ++j) {
            DumpDCRTPoly(av[j], out_dir + "evalkey_mult_a_" + std::to_string(j) + ".bin");
            DumpDCRTPoly(bv[j], out_dir + "evalkey_mult_b_" + std::to_string(j) + ".bin");
        }

        // Rotation Keys & Galois Permutation Maps
        std::ofstream out_rot_meta(out_dir + "rotation_keys_meta.txt");
        for (int32_t step : rot_indices) {
            uint32_t auto_idx = cc->FindAutomorphismIndex(step);
            auto evalRotKeyMap = cc->GetEvalAutomorphismKeyMap(keyPair.secretKey->GetKeyTag());
            auto rotKey = evalRotKeyMap[auto_idx];
            const auto& r_av = rotKey->GetAVector();
            const auto& r_bv = rotKey->GetBVector();
            for (uint32_t j = 0; j < r_av.size(); ++j) {
                DumpDCRTPolyLevel(r_av[j], 10, num_p_limbs, out_dir + "rotkey_step" + std::to_string(step) + "_a_" + std::to_string(j) + ".bin");
                DumpDCRTPolyLevel(r_bv[j], 10, num_p_limbs, out_dir + "rotkey_step" + std::to_string(step) + "_b_" + std::to_string(j) + ".bin");
            }

            // Generate auto_map for this auto_idx
            DCRTPoly poly_map(elemParams, Format::EVALUATION, true);
            for (size_t l = 0; l < num_q_limbs; l++) {
                auto vec = poly_map.GetElementAtIndex(l).GetValues();
                for (size_t i = 0; i < N_val; i++) vec[i] = NativeInteger(i);
                NativePoly p(params[l], Format::EVALUATION, true);
                p.SetValues(std::move(vec), Format::EVALUATION);
                poly_map.SetElementAtIndex(l, std::move(p));
            }
            DCRTPoly poly_map_out = poly_map.AutomorphismTransform(auto_idx);
            std::vector<uint32_t> map_u32(N_val);
            auto map_vec = poly_map_out.GetElementAtIndex(0).GetValues();
            for (size_t i = 0; i < N_val; i++) map_u32[i] = map_vec[i].ConvertToInt();
            std::ofstream out_map(out_dir + "auto_map_step" + std::to_string(step) + ".bin", std::ios::binary);
            out_map.write(reinterpret_cast<const char*>(map_u32.data()), N_val * sizeof(uint32_t));
            out_map.close();

            out_rot_meta << "step=" << step << " auto_idx=" << auto_idx << "\n";
        }
        out_rot_meta.close();

        // -----------------------------------------------------------------------
        // 4. Load Models & Encoded Dataset
        // -----------------------------------------------------------------------
        std::cout << "\nLoading SIFT models and dataset..." << std::endl;
        
        std::string dataset_base = "../../dataset/";
        if (!fs::exists(dataset_base + "siftsmall_IVFPQ_models/ivf_centroids.bin")) {
            dataset_base = "../dataset/";
            if (!fs::exists(dataset_base + "siftsmall_IVFPQ_models/ivf_centroids.bin")) {
                dataset_base = "dataset/";
            }
        }
        std::cout << "Using dataset base path: " << dataset_base << std::endl;

        std::vector<float> centroids(n_list * D);
        {
            std::ifstream fc(dataset_base + "siftsmall_IVFPQ_models/ivf_centroids.bin", std::ios::binary);
            if (!fc) throw std::runtime_error("Cannot open ivf_centroids.bin at " + dataset_base);
            fc.read(reinterpret_cast<char*>(centroids.data()), centroids.size() * sizeof(float));
        }
        std::vector<float> codebooks(M * K * sub_dim);
        {
            std::ifstream fcb(dataset_base + "siftsmall_IVFPQ_models/pq_codebooks.bin", std::ios::binary);
            if (!fcb) throw std::runtime_error("Cannot open pq_codebooks.bin at " + dataset_base);
            fcb.read(reinterpret_cast<char*>(codebooks.data()), codebooks.size() * sizeof(float));
        }
        std::vector<int32_t> assignments(10000);
        {
            std::ifstream fa(dataset_base + "siftsmall_IVFPQ_encoded/ivf_assignments.bin", std::ios::binary);
            if (!fa) throw std::runtime_error("Cannot open ivf_assignments.bin at " + dataset_base);
            fa.read(reinterpret_cast<char*>(assignments.data()), assignments.size() * sizeof(int32_t));
        }
        std::vector<uint8_t> pq_codes(10000 * M);
        {
            std::ifstream fpc(dataset_base + "siftsmall_IVFPQ_encoded/pq_codes.bin", std::ios::binary);
            if (!fpc) throw std::runtime_error("Cannot open pq_codes.bin at " + dataset_base);
            fpc.read(reinterpret_cast<char*>(pq_codes.data()), pq_codes.size() * sizeof(uint8_t));
        }

        std::vector<std::vector<int>> cluster_vids(n_list);
        for (size_t i = 0; i < assignments.size(); ++i) {
            int c = assignments[i];
            if (c >= 0 && c < n_list) cluster_vids[c].push_back(i);
        }

        auto queries = read_fvecs(dataset_base + "siftsmall/siftsmall_query.fvecs");
        auto groundtruth = read_ivecs(dataset_base + "siftsmall/siftsmall_groundtruth.ivecs");
        if (queries.empty()) throw std::runtime_error("Cannot open siftsmall_query.fvecs at " + dataset_base);
        const auto& query0 = queries[0];

        // -----------------------------------------------------------------------
        // 5. Execute Phase 1: Coarse Distance Computation & Compaction
        // -----------------------------------------------------------------------
        std::cout << "\nExecuting Phase 1: Coarse Distance & Compaction on Query 0..." << std::endl;

        // Plaintext Centroids
        std::vector<double> packed_centroids(slots, 0.0);
        for (int c = 0; c < n_list; ++c)
            for (int d = 0; d < D; ++d)
                packed_centroids[c * D + d] = static_cast<double>(centroids[c * D + d]);
        Plaintext pt_centroids = cc->MakeCKKSPackedPlaintext(packed_centroids);
        pt_centroids->Encode();
        DumpDCRTPoly(pt_centroids->GetElement<DCRTPoly>(), out_dir + "pt_centroids.bin");

        // Replicated Query
        std::vector<double> packed_query(slots, 0.0);
        for (int c = 0; c < n_list; ++c)
            for (int d = 0; d < D; ++d)
                packed_query[c * D + d] = static_cast<double>(query0[d]);
        Plaintext pt_query = cc->MakeCKKSPackedPlaintext(packed_query);
        auto ct_query = cc->Encrypt(keyPair.publicKey, pt_query);
        DumpCiphertext(ct_query, out_dir + "ct_query");

        // CP2.1: EvalSub
        auto ct_diff = cc->EvalSub(ct_query, pt_centroids);
        DumpCiphertext(ct_diff, out_dir + "cp2_ct_diff");

        // CP2.2: EvalMult + Rescale
        auto ct_sq = cc->EvalMult(ct_diff, ct_diff);
        ct_sq = cc->Rescale(ct_sq);
        DumpCiphertext(ct_sq, out_dir + "cp2_ct_sq");

        // CP2.3: 7-step Tree Sum
        for (int stride = 1; stride < D; stride *= 2) {
            auto shifted = cc->EvalRotate(ct_sq, stride);
            ct_sq = cc->EvalAdd(ct_sq, shifted);
        }
        DumpCiphertext(ct_sq, out_dir + "cp2_ct_treesum");

        // CP3: 32-step Distance Compaction
        Ciphertext<DCRTPoly> ct_compact;
        bool first_compact = true;
        for (int i = 0; i < n_list; ++i) {
            std::vector<double> mask(slots, 0.0);
            mask[i * D] = 1.0;
            auto pt_mask = cc->MakeCKKSPackedPlaintext(mask, 1, ct_sq->GetLevel());
            auto ct_sel = cc->EvalMult(ct_sq, pt_mask);
            ct_sel = cc->Rescale(ct_sel);

            int shift = i * (D - 1);
            if (shift != 0) {
                ct_sel = rotate_decomp(cc, ct_sel, shift);
            }
            if (first_compact) {
                ct_compact = ct_sel;
                first_compact = false;
            } else {
                ct_compact = cc->EvalAdd(ct_compact, ct_sel);
            }
        }
        DumpCiphertext(ct_compact, out_dir + "cp3_ct_compact");

        // -----------------------------------------------------------------------
        // 6. Interactive Selection (Client Decrypt)
        // -----------------------------------------------------------------------
        Plaintext pt_dec_coarse;
        cc->Decrypt(keyPair.secretKey, ct_compact, &pt_dec_coarse);
        pt_dec_coarse->SetLength(n_list);
        auto coarse_dists = pt_dec_coarse->GetRealPackedValue();

        std::vector<std::pair<int, float>> dist_idx;
        for (int i = 0; i < n_list; ++i) dist_idx.push_back({i, static_cast<float>(coarse_dists[i])});
        std::sort(dist_idx.begin(), dist_idx.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        int selected_cluster = dist_idx[0].first;

        std::cout << "Selected Cluster for Query 0: Cluster " << selected_cluster 
                  << " (Coarse Dist: " << dist_idx[0].second << ")" << std::endl;

        std::ofstream out_cp4(out_dir + "cp4_selection.txt");
        out_cp4 << "selected_cluster=" << selected_cluster << "\n";
        for (int i = 0; i < n_list; i++) out_cp4 << "c" << dist_idx[i].first << "=" << dist_idx[i].second << "\n";
        out_cp4.close();

        // -----------------------------------------------------------------------
        // 7. Execute Phase 2: Fine Batch Distance (PQ ADC)
        // -----------------------------------------------------------------------
        std::cout << "\nExecuting Phase 2: Fine Batch Distance on Cluster " << selected_cluster << "..." << std::endl;

        // Dimension-Major Query
        std::vector<double> packed_dim_q(slots, 0.0);
        for (int d = 0; d < D; ++d)
            for (int j = 0; j < B; ++j)
                packed_dim_q[d * B + j] = static_cast<double>(query0[d]);
        Plaintext pt_dim_q = cc->MakeCKKSPackedPlaintext(packed_dim_q);
        auto ct_query_dimpack = cc->Encrypt(keyPair.publicKey, pt_dim_q);
        DumpCiphertext(ct_query_dimpack, out_dir + "ct_query_dimpack");

        const auto& c_vids = cluster_vids[selected_cluster];
        std::cout << "Cluster " << selected_cluster << " contains " << c_vids.size() << " candidate vectors." << std::endl;

        Ciphertext<DCRTPoly> ct_all_dists;
        bool first_fine_accum = true;
        int global_offset = 0;
        std::map<int, int> slot_to_vid;

        for (size_t b_start = 0; b_start < c_vids.size(); b_start += B) {
            size_t b_end = std::min(b_start + B, c_vids.size());
            size_t batch_count = b_end - b_start;
            std::vector<int> b_vids(c_vids.begin() + b_start, c_vids.begin() + b_end);

            // Build candidate batch plaintext
            std::vector<double> packed_batch(slots, 0.0);
            for (size_t j = 0; j < b_vids.size(); ++j) {
                int vid = b_vids[j];
                int c = assignments[vid];
                for (int d = 0; d < D; ++d) {
                    double val = static_cast<double>(centroids[c * D + d]);
                    int m = d / sub_dim;
                    int sub_d = d % sub_dim;
                    uint8_t code = pq_codes[vid * M + m];
                    int cb_offset = m * (K * sub_dim) + code * sub_dim + sub_d;
                    val += static_cast<double>(codebooks[cb_offset]);
                    packed_batch[d * B + j] = val;
                }
            }
            Plaintext pt_batch = cc->MakeCKKSPackedPlaintext(packed_batch, 1, ct_query_dimpack->GetLevel());
            pt_batch->Encode();
            std::string b_name = "batch_" + std::to_string(b_start / B);
            DumpDCRTPoly(pt_batch->GetElement<DCRTPoly>(), out_dir + "pt_" + b_name + ".bin");

            // EvalSub
            auto ct_fine_diff = cc->EvalSub(ct_query_dimpack, pt_batch);
            // EvalMult + Rescale
            auto ct_fine_sq = cc->EvalMult(ct_fine_diff, ct_fine_diff);
            ct_fine_sq = cc->Rescale(ct_fine_sq);

            // 7-step Tree Sum across 128 dimensions (stride = B * 2^s)
            for (int stride = B; stride < D * B; stride *= 2) {
                auto rotated = cc->EvalRotate(ct_fine_sq, stride);
                ct_fine_sq = cc->EvalAdd(ct_fine_sq, rotated);
            }

            // Validity mask & Rescale
            std::vector<double> mask_batch(slots, 0.0);
            std::fill(mask_batch.begin(), mask_batch.begin() + batch_count, 1.0);
            auto pt_mask_batch = cc->MakeCKKSPackedPlaintext(mask_batch, 1, ct_fine_sq->GetLevel());
            auto ct_masked = cc->EvalMult(ct_fine_sq, pt_mask_batch);
            ct_masked = cc->Rescale(ct_masked);

            // Position Shift & Accumulate
            auto ct_shifted = rotate_decomp(cc, ct_masked, -global_offset);
            if (first_fine_accum) {
                ct_all_dists = ct_shifted;
                first_fine_accum = false;
            } else {
                ct_all_dists = cc->EvalAdd(ct_all_dists, ct_shifted);
            }

            for (size_t j = 0; j < batch_count; ++j) {
                slot_to_vid[global_offset + j] = b_vids[j];
            }
            global_offset += batch_count;
        }
        DumpCiphertext(ct_all_dists, out_dir + "cp5_ct_all_dists");

        // -----------------------------------------------------------------------
        // 8. Client Top-K Extraction & Ground Truth Validation
        // -----------------------------------------------------------------------
        Plaintext pt_dec_fine;
        cc->Decrypt(keyPair.secretKey, ct_all_dists, &pt_dec_fine);
        pt_dec_fine->SetLength(global_offset);
        auto cand_dists = pt_dec_fine->GetRealPackedValue();

        std::vector<std::pair<int, float>> all_candidates;
        for (int i = 0; i < global_offset; ++i) {
            all_candidates.emplace_back(slot_to_vid[i], static_cast<float>(cand_dists[i]));
        }
        std::sort(all_candidates.begin(), all_candidates.end(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        });

        int top_k = 8;
        std::cout << "\n=== Top-" << top_k << " Results for Query 0 ===" << std::endl;
        std::ofstream out_cp6(out_dir + "cp6_top_k.txt");
        for (int i = 0; i < top_k && i < static_cast<int>(all_candidates.size()); ++i) {
            std::cout << "  Rank " << i + 1 << ": Vector ID " << all_candidates[i].first 
                      << " (Dist: " << all_candidates[i].second << ")" << std::endl;
            out_cp6 << all_candidates[i].first << " " << all_candidates[i].second << "\n";
        }
        out_cp6.close();

        // Check Recall vs Ground Truth
        int hits = 0;
        const auto& gt0 = groundtruth[0];
        for (int i = 0; i < top_k && i < static_cast<int>(all_candidates.size()); ++i) {
            for (int g : gt0) {
                if (g == all_candidates[i].first) { hits++; break; }
            }
        }
        double recall = static_cast<double>(hits) / top_k;
        std::cout << ">>> Validated Recall@" << top_k << ": " << recall * 100.0 << "% (" << hits << "/" << top_k << " hits) <<<" << std::endl;

        std::cout << "\n>>> All interactive test vectors generated successfully in: " << out_dir << " <<<" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
}

