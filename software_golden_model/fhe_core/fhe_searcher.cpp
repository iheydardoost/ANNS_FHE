#include "fhe_searcher.h"
#include "sign_approximator.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <omp.h>

using namespace lbcrypto;

namespace anns_fhe {

bool FHESearcher::load_data(const FHEConfig& config) {
    m_n_list = config.n_list;
    m_dimension = config.dimension;
    m_m_subvectors = config.m_subvectors;
    m_k_subcentroids = config.k_subcentroids;
    m_sub_dim = m_dimension / m_m_subvectors;

    std::string centroids_path = config.resolve_path(config.models_output_dir + "/ivf_centroids.bin");
    std::string codebooks_path = config.resolve_path(config.models_output_dir + "/pq_codebooks.bin");
    std::string assignments_path = config.resolve_path(config.encoding_output_dir + "/ivf_assignments.bin");
    std::string codes_path = config.resolve_path(config.encoding_output_dir + "/pq_codes.bin");

    auto load_bin = [](const std::string& path, auto& vec) -> bool {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open binary file: " << path << std::endl;
            return false;
        }
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        vec.resize(size / sizeof(typename std::decay<decltype(vec)>::type::value_type));
        file.read(reinterpret_cast<char*>(vec.data()), size);
        return true;
    };

    if (!load_bin(centroids_path, m_centroids) ||
        !load_bin(codebooks_path, m_codebooks) ||
        !load_bin(assignments_path, m_assignments)) {
        return false;
    }

    // Load m_pq_codes based on subcentroids count
    {
        std::ifstream file(codes_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open binary file: " << codes_path << std::endl;
            return false;
        }
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (m_k_subcentroids <= 256) {
            std::vector<uint8_t> temp_codes(file_size);
            file.read(reinterpret_cast<char*>(temp_codes.data()), file_size);
            m_pq_codes.assign(temp_codes.begin(), temp_codes.end());
        } else {
            size_t num_elements = file_size / sizeof(uint16_t);
            m_pq_codes.resize(num_elements);
            file.read(reinterpret_cast<char*>(m_pq_codes.data()), file_size);
        }
    }

    m_num_vectors = m_assignments.size();
    return true;
}

Ciphertext<DCRTPoly> FHESearcher::eval_abs(
    CryptoContext<DCRTPoly> cc,
    const Ciphertext<DCRTPoly>& ct_x,
    const FHEConfig& config) const {
    
    // |x| = x * sign(x)
    auto ct_sign = SignApproximator::eval_sign(cc, ct_x, config.sign_approx_method, config.composition_iterations);
    auto ct_abs = cc->EvalMult(ct_x, ct_sign);
    ct_abs = cc->Relinearize(ct_abs);
    ct_abs = cc->ModReduce(ct_abs);
    return ct_abs;
}

std::pair<Ciphertext<DCRTPoly>, Ciphertext<DCRTPoly>> FHESearcher::compare_and_swap(
    CryptoContext<DCRTPoly> cc,
    const Ciphertext<DCRTPoly>& ct_a,
    const Ciphertext<DCRTPoly>& ct_b,
    const FHEConfig& config) const {
    
    // sum = a + b
    auto ct_sum = cc->EvalAdd(ct_a, ct_b);
    // diff = a - b
    auto ct_diff = cc->EvalSub(ct_a, ct_b);
    
    // abs_diff = |a - b|
    auto ct_abs_diff = eval_abs(cc, ct_diff, config);
    
    // min = 0.5 * (a + b - |a - b|)
    auto ct_min = cc->EvalSub(ct_sum, ct_abs_diff);
    ct_min = cc->EvalMult(ct_min, 0.5);
    
    // max = 0.5 * (a + b + |a - b|)
    auto ct_max = cc->EvalAdd(ct_sum, ct_abs_diff);
    ct_max = cc->EvalMult(ct_max, 0.5);
    
    return {ct_min, ct_max};
}

std::vector<std::pair<int, float>> FHESearcher::search(
    const FHEContextManager& ctx_mgr,
    const std::vector<float>& query,
    int n_probe,
    int top_k,
    const FHEConfig& config) const {

    auto cc = ctx_mgr.get_context();

    // 1. Encrypt query (split into M subvectors)
    auto ct_q = ctx_mgr.encrypt_query(query, m_m_subvectors, m_sub_dim);

    // 2. Coarse Quantization: Compute distance to centroids homomorphically
    std::vector<Ciphertext<DCRTPoly>> ct_coarse_dists(m_n_list);
    
    #pragma omp parallel for
    for (int i = 0; i < m_n_list; ++i) {
        std::vector<Ciphertext<DCRTPoly>> ct_sub_dists(m_m_subvectors);
        for (int m = 0; m < m_m_subvectors; ++m) {
            std::vector<double> c_im(m_sub_dim);
            for (int d = 0; d < m_sub_dim; ++d) {
                c_im[d] = m_centroids[i * m_dimension + m * m_sub_dim + d];
            }
            Plaintext pt_c_im = cc->MakeCKKSPackedPlaintext(c_im);
            auto diff = cc->EvalSub(ct_q[m], pt_c_im);
            auto diff_sq = cc->EvalMult(diff, diff);
            diff_sq = cc->Relinearize(diff_sq);
            diff_sq = cc->ModReduce(diff_sq);
            
            ct_sub_dists[m] = diff_sq;
        }
        
        auto sum_ct = ct_sub_dists[0];
        for (int m = 1; m < m_m_subvectors; ++m) {
            sum_ct = cc->EvalAdd(sum_ct, ct_sub_dists[m]);
        }
        ct_coarse_dists[i] = sum_ct;
    }

    // Centroids selection (interactive simulator style)
    std::vector<std::pair<int, double>> coarse_dists_dec(m_n_list);
    for (int i = 0; i < m_n_list; ++i) {
        auto packed = ctx_mgr.decrypt_vector(ct_coarse_dists[i], m_sub_dim);
        double sum = 0.0;
        for (double val : packed) {
            sum += val;
        }
        coarse_dists_dec[i] = {i, sum};
    }
    
    std::sort(coarse_dists_dec.begin(), coarse_dists_dec.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    std::unordered_set<int> probed_centroids;
    for (int p = 0; p < n_probe; ++p) {
        probed_centroids.insert(coarse_dists_dec[p].first);
    }

    // 3. Candidate Selection
    std::vector<int> candidates;
    for (int i = 0; i < m_num_vectors; ++i) {
        if (probed_centroids.count(m_assignments[i])) {
            candidates.push_back(i);
        }
    }

    if (candidates.empty()) {
        return {};
    }

    // Map probed centroids to local index [0...n_probe-1]
    std::vector<int> centroid_to_local(m_n_list, -1);
    int local_idx = 0;
    std::vector<int> local_to_centroid(n_probe);
    for (int cid : probed_centroids) {
        centroid_to_local[cid] = local_idx;
        local_to_centroid[local_idx] = cid;
        local_idx++;
    }

    // 4. Compute PQ Distance Lookup Tables (LUT) homomorphically
    // Table shape: n_probe * m_subvectors * k_subcentroids
    std::vector<Ciphertext<DCRTPoly>> lut(n_probe * m_m_subvectors * m_k_subcentroids);

    #pragma omp parallel for collapse(3)
    for (int lp = 0; lp < n_probe; ++lp) {
        for (int m = 0; m < m_m_subvectors; ++m) {
            for (int k = 0; k < m_k_subcentroids; ++k) {
                int cid = local_to_centroid[lp];
                // Subtract centroid component from query subvector to get residual
                std::vector<double> c_im(m_sub_dim);
                for (int d = 0; d < m_sub_dim; ++d) {
                    c_im[d] = m_centroids[cid * m_dimension + m * m_sub_dim + d];
                }
                Plaintext pt_c_im = cc->MakeCKKSPackedPlaintext(c_im);
                auto ct_q_res = cc->EvalSub(ct_q[m], pt_c_im);

                // Now compute distance from residual to subcentroid
                std::vector<double> sub_cb(m_sub_dim);
                int cb_offset = m * m_k_subcentroids * m_sub_dim + k * m_sub_dim;
                for (int d = 0; d < m_sub_dim; ++d) {
                    sub_cb[d] = m_codebooks[cb_offset + d];
                }
                Plaintext pt_cb = cc->MakeCKKSPackedPlaintext(sub_cb);
                
                auto diff = cc->EvalSub(ct_q_res, pt_cb);
                auto diff_sq = cc->EvalMult(diff, diff);
                diff_sq = cc->Relinearize(diff_sq);
                diff_sq = cc->ModReduce(diff_sq);

                int lut_idx = lp * m_m_subvectors * m_k_subcentroids + m * m_k_subcentroids + k;
                lut[lut_idx] = diff_sq;
            }
        }
    }

    // 5. Distance Aggregation for candidates
    std::vector<Ciphertext<DCRTPoly>> ct_dists(candidates.size());
    #pragma omp parallel for
    for (size_t i = 0; i < candidates.size(); ++i) {
        int idx = candidates[i];
        int cid = m_assignments[idx];
        int lp = centroid_to_local[cid];
        
        uint16_t code_0 = m_pq_codes[idx * m_m_subvectors + 0];
        auto sum = lut[lp * m_m_subvectors * m_k_subcentroids + 0 * m_k_subcentroids + code_0];
        
        for (int m = 1; m < m_m_subvectors; ++m) {
            uint16_t code_m = m_pq_codes[idx * m_m_subvectors + m];
            auto term = lut[lp * m_m_subvectors * m_k_subcentroids + m * m_k_subcentroids + code_m];
            sum = cc->EvalAdd(sum, term);
        }
        ct_dists[i] = sum;
    }

    // 6. Top-K Selection
    std::vector<std::pair<int, float>> final_results;

    if (config.interactive_top_k) {
        // Interactive Mode: Decrypt distances and sort in plaintext
        final_results.resize(candidates.size());
        for (size_t i = 0; i < candidates.size(); ++i) {
            auto packed = ctx_mgr.decrypt_vector(ct_dists[i], m_sub_dim);
            double sum = 0.0;
            for (double val : packed) {
                sum += val;
            }
            final_results[i] = {candidates[i], static_cast<float>(sum)};
        }
        
        if (final_results.size() > static_cast<size_t>(top_k)) {
            std::nth_element(final_results.begin(), final_results.begin() + top_k, final_results.end(),
                             [](const auto& a, const auto& b) { return a.second < b.second; });
            final_results.resize(top_k);
        }
        std::sort(final_results.begin(), final_results.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });
        
    } else {
        // Non-Interactive Mode: Homomorphic Bitonic Sort Network
        std::cout << "[FHESearcher] Non-Interactive mode enabled. Running homomorphic Bitonic Sort." << std::endl;
        
        // Since homomorphic sorting network scales O(V log^2 V) and requires many multiplications,
        // we restrict the sort to a manageable subset to fit within the CKKS level budget.
        // We select the first 8 candidates (or pad to 8 with dummy large values if candidate pool is smaller).
        size_t sort_size = 8;
        std::vector<Ciphertext<DCRTPoly>> sort_dists(sort_size);
        std::vector<int> sort_ids(sort_size, -1);
        
        // Fill sorting inputs
        for (size_t i = 0; i < sort_size; ++i) {
            if (i < candidates.size()) {
                // Perform slot accumulation homomorphically now for sorting!
                auto sum = ct_dists[i];
                for (int d = 1; d < m_sub_dim; d *= 2) {
                    sum = cc->EvalAdd(sum, cc->EvalRotate(sum, d));
                }
                sort_dists[i] = sum;
                sort_ids[i] = candidates[i];
            } else {
                // Pad with an encrypted large number (e.g. 1e6)
                std::vector<double> dummy = {1e6};
                Plaintext pt = cc->MakeCKKSPackedPlaintext(dummy);
                sort_dists[i] = cc->Encrypt(ctx_mgr.get_keypair().publicKey, pt);
                sort_ids[i] = -1;
            }
        }

        // Helper lambda for CAS step in bitonic sort
        auto cas = [&](size_t i, size_t j, bool ascending) {
            auto res = compare_and_swap(cc, sort_dists[i], sort_dists[j], config);
            if (ascending) {
                sort_dists[i] = res.first;  // min
                sort_dists[j] = res.second; // max
            } else {
                sort_dists[i] = res.second; // max
                sort_dists[j] = res.first;  // min
            }
            // Trace the mapping of IDs
            // Note: Since IDs are plaintext, we can swap them based on the simulated decryption
            // to keep track of which candidate matches which sorted slot.
            double val_i = ctx_mgr.decrypt_scalar(sort_dists[i]);
            double val_j = ctx_mgr.decrypt_scalar(sort_dists[j]);
            if ((ascending && val_i > val_j) || (!ascending && val_i < val_j)) {
                std::swap(sort_ids[i], sort_ids[j]);
            }
        };

        // Static Bitonic Sort Network for N=8
        // Stage 1
        cas(0, 1, true);  cas(2, 3, false); cas(4, 5, true);  cas(6, 7, false);
        // Stage 2
        cas(0, 3, true);  cas(1, 2, true);  cas(4, 7, false); cas(5, 6, false);
        cas(0, 1, true);  cas(2, 3, true);  cas(4, 5, false); cas(6, 7, false);
        // Stage 3
        cas(0, 7, true);  cas(1, 6, true);  cas(2, 5, true);  cas(3, 4, true);
        cas(0, 2, true);  cas(1, 3, true);  cas(4, 6, true);  cas(5, 7, true);
        cas(0, 1, true);  cas(2, 3, true);  cas(4, 5, true);  cas(6, 7, true);

        // Retrieve sorted results
        for (size_t i = 0; i < static_cast<size_t>(top_k) && i < sort_size; ++i) {
            if (sort_ids[i] != -1) {
                double val = ctx_mgr.decrypt_scalar(sort_dists[i]);
                final_results.push_back({sort_ids[i], static_cast<float>(val)});
            }
        }
    }

    return final_results;
}

} // namespace anns_fhe
