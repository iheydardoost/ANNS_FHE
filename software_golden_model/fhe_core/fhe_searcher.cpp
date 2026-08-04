#include "fhe_searcher.h"
#include "sign_approximator.h"
#include "openfhe_statistics/utils-matrices.h"
#include "utils-eval.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <set>
#include "fhe_utility.h"

anns_fhe::FHEContextManager* ctx_mgr_ptr = nullptr;

using namespace lbcrypto;

namespace anns_fhe
{

    // ---------------------------------------------------------------------------
    // Helper: Decompose arbitrary rotation into binary power-of-2 shifts
    // ---------------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::rotate(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct,
        int rot) const
    {
        if (rot == 0) return ct;
    
        auto result = ct;
        int abs_rot = std::abs(rot);
        int sign = (rot > 0) ? 1 : -1;

        // To avoid needing keys > 64, perform rotations in max-64 steps:
        while (abs_rot > 0) {
            int step_size = 1;
            // Find largest power of 2 <= 64 that fits in abs_rot
            while ((step_size << 1) <= abs_rot && (step_size << 1) <= 64) {
                step_size <<= 1;
            }
            
            int step = sign * step_size;
            result = cc->EvalRotate(result, step);
            abs_rot -= step_size;
        }

        return result;
    }

    // ---------------------------------------------------------------------------
    // Load plaintext index data (NOT encrypted)
    // ---------------------------------------------------------------------------
    bool FHESearcher::load_plaintext_data(const FHEConfig& config)
    {
        const std::string assign_path  = config.resolve_path(config.encoding_output_dir + "/ivf_assignments.bin");
        const std::string codes_path   = config.resolve_path(config.encoding_output_dir + "/pq_codes.bin");

        auto load_bin = [](const std::string& path, auto& vec) -> bool {
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (!f.is_open())
            {
                return false;
            }
            size_t bytes = f.tellg();
            f.seekg(0);
            vec.resize(bytes / sizeof(typename std::decay<decltype(vec)>::type::value_type));
            f.read(reinterpret_cast<char*>(vec.data()), bytes);
            return true;
        };

        if (!load_bin(assign_path, m_assignments))
        {
            return false;
        }

        // PQ codes
        {
            std::ifstream f(codes_path, std::ios::binary | std::ios::ate);
            if (!f.is_open())
            {
                return false;
            }
            size_t bytes = f.tellg();
            f.seekg(0);
            if (config.k_subcentroids <= 256)
            {
                std::vector<uint8_t> tmp(bytes);
                f.read(reinterpret_cast<char*>(tmp.data()), bytes);
                m_pq_codes.assign(tmp.begin(), tmp.end());
            }
            else
            {
                m_pq_codes.resize(bytes / sizeof(uint16_t));
                f.read(reinterpret_cast<char*>(m_pq_codes.data()), bytes);
            }
        }

        m_num_vectors = static_cast<int>(m_assignments.size());
        const int M   = config.m_subvectors;

        // Build inverted lists and cluster-grouped mappings
        m_cluster_vector_ids.clear();
        m_cluster_vector_ids.resize(config.n_list);

        m_cluster_pq_codes.clear();
        m_cluster_pq_codes.resize(config.n_list);

        for (int i = 0; i < m_num_vectors; ++i)
        {
            int c = m_assignments[i];
            if (c >= 0 && c < config.n_list)
            {
                m_cluster_vector_ids[c].push_back(i);

                for (int m = 0; m < M; ++m)
                {
                    m_cluster_pq_codes[c].push_back(m_pq_codes[static_cast<size_t>(i) * M + m]);
                }
            }
        }

        return true;
    }

    // ---------------------------------------------------------------------------
    // Step 1a: Coarse centroid distance computation
    // Depth: 1 mult level (for squaring) + 0 for tree sum
    // ---------------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::compute_coarse_distances(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct_query,
        const Ciphertext<DCRTPoly>& ct_centroids,
        const FHEConfig& config) const
    {
        // 1. Componentwise difference: q - c
        auto ct_diff = cc->EvalSub(ct_query, ct_centroids);

        #ifdef ENABLE_LOGGING
        std::cout << "===== in compute_coarse_distances(), 1" << std::endl;
        #endif

        // 2. Componentwise square: (q - c)^2  (Level 1)
        auto ct_sq = cc->EvalMult(ct_diff, ct_diff);
        ct_sq = cc->Rescale(ct_sq);

        #ifdef ENABLE_LOGGING
        std::cout << "===== in compute_coarse_distances(), 2" << std::endl;
        #endif

        // 3. Tree reduction over 128 dims (0 depth)
        auto ct_acc = ct_sq;
        for (int stride = 1; stride < config.dimension; stride *= 2)
        {
            auto shifted = cc->EvalRotate(ct_acc, stride);
            ct_acc = cc->EvalAdd(ct_acc, shifted);

            #ifdef ENABLE_LOGGING
            std::cout << "===== in compute_coarse_distances(), 3: " << stride << "/" << config.dimension << std::endl;
            #endif
        }

        return ct_acc;
    }

    // ---------------------------------------------------------------------------
    // Step 1b: Compact distances at stride 128 into slots [0..31]
    // Depth: 1 mult level for plaintext mask isolation => Total Step 1 Depth = 2
    // ---------------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::compact_distances(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct_strided,
        const FHEConfig& config) const
    {
        const size_t current_level = ct_strided->GetLevel();
        const int n_list = config.n_list; // 32
        const int dim    = config.dimension; // 128
        const int slots  = config.poly_modulus_degree >> 1;

        Ciphertext<DCRTPoly> ct_compact;
        bool first = true;

        for (int i = 0; i < n_list; ++i)
        {
            // 1. Build mask for slot (i * dim)
            std::vector<double> mask(slots, 0.0);
            if (i * dim < slots) {
                mask[i * dim] = 1.0;
            }
            auto pt_mask = cc->MakeCKKSPackedPlaintext(mask, 1, current_level);

            // 2. Extract slot i*dim
            auto ct_sel = cc->EvalMult(ct_strided, pt_mask);
            ct_sel = cc->Rescale(ct_sel);

            // 3. Move slot (i * dim) to slot i (Left shift by i * (dim - 1))
            int shift = i * (dim - 1);
            if (shift != 0) {
                ct_sel = rotate(cc, ct_sel, shift);
            }

            // 4. Accumulate
            if (first) {
                ct_compact = ct_sel;
                first = false;
            } else {
                ct_compact = cc->EvalAdd(ct_compact, ct_sel);
            }

            #ifdef ENABLE_LOGGING
            std::cout << "===== in compact_distances(): " << i << "/" << n_list << std::endl;
            #endif
        }

        return ct_compact;
    }

    // ---------------------------------------------------------------------------
    // Step 2: Top-n_probe Centroid Ranking (N=32 Pairwise Matrix)
    // Depth: 3 + 7 levels (Chebyshev Sign deg 59) + 7 levels (Indicator deg 59) = 17 levels
    // ---------------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::rank_top_nprobe_centroids(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct_compact_dists,
        double dist_bound,
        const FHEConfig& config) const
    {
        const size_t N = static_cast<size_t>(config.n_list); // 32

        // Mazzone N=32 matrix re-encoding (0 mult depth)
        // c_row has row i as [D_0, D_1, ..., D_31]
        auto c_row       = anns_fhe::replicate_row(ct_compact_dists, N);

        // c_col_trans transposes row into column 0 (1 mult depth)
        auto c_col_trans = anns_fhe::transpose_row(ct_compact_dists, N, true);
        c_col_trans = cc->Rescale(c_col_trans);

        // c_col replicates column 0 across all 32 columns (0 mult depth)
        auto c_col       = anns_fhe::replicate_column(c_col_trans, N);

        // Difference matrix: Δ_ij = D_i - D_j (0 mult depth)
        auto ct_diff     = cc->EvalSub(c_row, c_col);

        // Scaling distances (1 mult depth)
        double B = 100.0; // the whole bandwidth of evaluation
        double scale_factor = B / (dist_bound*0.6);
        std::vector<double> scale_vec((cc->GetRingDimension() >> 1), scale_factor);
        auto pt_scale = cc->MakeCKKSPackedPlaintext(scale_vec, 1, ct_compact_dists->GetLevel());
        auto ct_diff_scaled = cc->EvalMult(ct_diff, pt_scale);
        ct_diff_scaled = cc->Rescale(ct_diff_scaled);

        // 1. Evaluate ApproxSign(Δ_ij) (Degree 59, Depth 7)
        uint32_t degree = openfhe_stats::depth2degree(7);
        double eps = 1.5 * (B/2) / degree;
        auto S_ij = SignApproximator::eval_sign(cc, ct_diff_scaled, -B/2, B/2, degree, eps);

        #ifdef ENABLE_LOGGING
        std::cout << "===== in rank_top_nprobe_centroids(), 1" << std::endl;
        std::cout << "Current Level: " << S_ij->GetLevel() << std::endl;
        std::cout << "Noise Scale Deg: " << S_ij->GetNoiseScaleDeg() << std::endl;
        #endif

        // 2. Sum across columns to compute rank vector R_i = Σ_j S_ij (1 mult depth)
        auto ct_ranks    = anns_fhe::sum_rows(S_ij, N, true);
        ct_ranks = cc->Rescale(ct_ranks);
        ct_ranks = cc->EvalAdd(ct_ranks, 0.5);

        #ifdef ENABLE_LOGGING
        std::cout << "===== in rank_top_nprobe_centroids(), 2" << std::endl;
        std::cout << "Current Level: " << ct_ranks->GetLevel() << std::endl;
        std::cout << "Noise Scale Deg: " << ct_ranks->GetNoiseScaleDeg() << std::endl;
        ctx_mgr_ptr->decrypt_and_print_vector(ct_ranks, "ct_ranks", config.n_list, true);
        #endif

        // 3. Evaluate Top-n_probe indicator polynomial P_nprobe(R_i) (Degree 59, Depth 7)
        // Dynamic n_probe threshold from config (1 to 4)
        degree = openfhe_stats::depth2degree(7);
        double threshold_min = 0.5;
        double threshold_max = static_cast<double>(config.n_probe) + 0.5;
        auto M_cent = SignApproximator::eval_indicator(
            cc, ct_ranks, threshold_min, threshold_max, 0.0, static_cast<double>(N), degree);

        #ifdef ENABLE_LOGGING
        std::cout << "===== in rank_top_nprobe_centroids(), 3" << std::endl;
        #endif

        return M_cent;
    }

    // ---------------------------------------------------------------------------
    // Step 3a: Build dimension-major plaintext batch from PQ reconstructions
    // Layout: slot[d*B + j] = x_j_approx[d] where x_j_approx = centroid + codebook lookup
    // ---------------------------------------------------------------------------
    void FHESearcher::build_dimpack_plaintext(
        const std::vector<int>& batch_vec_ids,
        int B,
        const FHEConfig& config,
        std::vector<double>& out_packed) const
    {
        const int D       = config.dimension;
        const int M       = config.m_subvectors;
        const int K       = config.k_subcentroids;
        const int sub_dim = D / M;
        const int slots   = config.poly_modulus_degree >> 1;

        out_packed.assign(slots, 0.0);

        for (int j = 0; j < static_cast<int>(batch_vec_ids.size()); ++j)
        {
            int vid = batch_vec_ids[j];
            int c   = m_assignments[vid];  // cluster assignment

            for (int d = 0; d < D; ++d)
            {
                // Start with centroid component
                double val = static_cast<double>((*m_p_centroids)[c * D + d]);

                // Add PQ codebook component
                int m = d / sub_dim;
                int sub_d = d % sub_dim;
                uint16_t code = m_pq_codes[static_cast<size_t>(vid) * M + m];
                int cb_offset = m * K * sub_dim + code * sub_dim + sub_d;
                val += static_cast<double>((*m_p_codebooks)[cb_offset]);

                out_packed[d * B + j] = val;
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Step 3b: Compute batch distances using dimension-major SIMD packing
    // ct_query_dimpack has layout: slot[d*B+j] = q[d] for all j
    // pt_batch has layout: slot[d*B+j] = x_j_approx[d]
    // Result: slot[j] = ||q - x_j_approx||^2 for j = 0..B-1
    // Depth consumed: 2 level
    // ---------------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::compute_batch_distances_dimpack(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct_query_dimpack,
        const std::vector<int>& batch_vec_ids,
        const FHEConfig& config) const
    {
        const int D     = config.dimension;
        const int slots = config.poly_modulus_degree >> 1;
        const int B     = slots / D;  // 256

        // Build plaintext batch in dimension-major layout
        std::vector<double> packed;
        build_dimpack_plaintext(batch_vec_ids, B, config, packed);
        auto pt_batch = cc->MakeCKKSPackedPlaintext(packed, 1, ct_query_dimpack->GetLevel());

        // Componentwise difference: q[d] - x_j[d] for all (d, j)  (0 mult depth)
        auto ct_diff = cc->EvalSub(ct_query_dimpack, pt_batch);

        // Componentwise square  (1 mult depth)
        auto ct_sq = cc->EvalMult(ct_diff, ct_diff);
        ct_sq = cc->Rescale(ct_sq);

        // Tree reduction across D dimension blocks (1 mult depth)
        ct_sq = anns_fhe::sum_rows(ct_sq, B, true);
        ct_sq = cc->Rescale(ct_sq);

        return ct_sq;
    }

    // ---------------------------------------------------------------------------
    // Step 3c: Apply probe penalty mask
    // Formula: ct_final = (ct_dist - P) * ct_m_c + P
    // When m_c ≈ 1 (probed):     ct_final ≈ ct_dist
    // When m_c ≈ 0 (not probed): ct_final ≈ P  (large → excluded from top-k)
    // ---------------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::apply_probe_penalty(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct_dist,
        const Ciphertext<DCRTPoly>& ct_m_c_replicated,
        double penalty,
        const FHEConfig& config) const
    {
        // Bring ct_dist to the same level as ct_m_c_replicated using LevelReduce
        auto ct_adjusted = ct_dist;
        int drop_level = ct_m_c_replicated->GetLevel() - ct_adjusted->GetLevel();
        if (drop_level > 0)
        {
            ct_adjusted = cc->LevelReduce(ct_adjusted, nullptr, drop_level);
        }

        // ct_shifted = ct_dist - P  (plaintext subtraction, 0 mult depth)
        auto ct_shifted = cc->EvalSub(ct_adjusted, penalty);

        // ct_masked = ct_shifted * ct_m_c  (1 mult depth)
        auto ct_masked = cc->EvalMult(ct_shifted, ct_m_c_replicated);
        ct_masked = cc->Rescale(ct_masked);

        // ct_final = ct_masked + P  (plaintext addition, 0 mult depth)
        auto ct_final = cc->EvalAdd(ct_masked, penalty);

        return ct_final;
    }


    // ---------------------------------------------------------------------------
    // Full Search Orchestration
    // ---------------------------------------------------------------------------
    std::vector<std::pair<int, float>> FHESearcher::search(
        const FHEContextManager& ctx_mgr,
        const std::vector<float>& query,
        int top_k,
        const FHEConfig& config,
        std::vector<double>* timings,
        FHESearchStats* stats) const
    {
        auto t0 = std::chrono::high_resolution_clock::now();

        const auto& cc = ctx_mgr.get_context();
        const int slots = config.poly_modulus_degree >> 1;
        const double dist_bound = 1.0e6;
        ctx_mgr_ptr = const_cast<anns_fhe::FHEContextManager*>(&ctx_mgr);

        // Encrypt query packed
        auto ct_query = ctx_mgr.encrypt_query_packed(query, config);

        #ifdef ENABLE_LOGGING
        std::cout << "Encrypt query packed done!" << std::endl;
        std::cout << "Current Level: " << ct_query->GetLevel() << std::endl;
        std::cout << "Noise Scale Deg: " << ct_query->GetNoiseScaleDeg() << std::endl;
        std::cout << "--------------------------------------------------------------" << std::endl;
        #endif

        // -----------------------------------------------------------------------
        // Step 1: Coarse distance computation & compaction (Depth +2)
        // -----------------------------------------------------------------------
        auto ct_strided = compute_coarse_distances(
            cc, ct_query, ctx_mgr.get_encrypted_centroids(), config);

        #ifdef ENABLE_LOGGING
        std::cout << "*** after compute_coarse_distances()" << std::endl;
        std::cout << "Current Level: " << ct_strided->GetLevel() << std::endl;
        std::cout << "Noise Scale Deg: " << ct_strided->GetNoiseScaleDeg() << std::endl;
        #endif

        auto ct_compact = compact_distances(cc, ct_strided, config);

        #ifdef ENABLE_LOGGING
        std::cout << "*** after compact_distances()" << std::endl;
        std::cout << "Current Level: " << ct_compact->GetLevel() << std::endl;
        std::cout << "Noise Scale Deg: " << ct_compact->GetNoiseScaleDeg() << std::endl;
        #endif

        if (stats) stats->level_coarse_dist = ct_compact->GetLevel();
        auto t1 = std::chrono::high_resolution_clock::now();

        #ifdef ENABLE_LOGGING
        std::cout << "Step 1: Coarse distance computation & compaction done!" << std::endl;
        std::cout << "time = " << std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0) << " ms" << std::endl;
        ctx_mgr.decrypt_and_print_vector(ct_compact, "ct_compact", config.n_list, true);
        std::cout << "--------------------------------------------------------------" << std::endl;
        #endif

        // -----------------------------------------------------------------------
        // Step 2: Top-n_probe Centroid Ranking (N=32 Matrix, Depth +17)
        // -----------------------------------------------------------------------
        auto ct_mask_coarse = rank_top_nprobe_centroids(cc, ct_compact, dist_bound, config);

        #ifdef ENABLE_LOGGING
        std::cout << "*** after rank_top_nprobe_centroids()" << std::endl;
        std::cout << "Current Level: " << ct_mask_coarse->GetLevel() << std::endl;
        std::cout << "Noise Scale Deg: " << ct_mask_coarse->GetNoiseScaleDeg() << std::endl;
        ctx_mgr.decrypt_and_print_vector(ct_mask_coarse, "ct_mask_coarse", config.n_list, true);
        #endif
        
        if (stats) stats->level_coarse_rank = ct_mask_coarse->GetLevel();
        auto t2 = std::chrono::high_resolution_clock::now();

        #ifdef ENABLE_LOGGING
        std::cout << "Step 2: Top-n_probe Centroid Ranking done!" << std::endl;
        std::cout << "time = " << std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1) << " ms" << std::endl;
        std::cout << "--------------------------------------------------------------" << std::endl;
        #endif

        // -----------------------------------------------------------------------
        // Step 3: SIMD-batched ADC distance computation (Dimension-major packing)
        // -----------------------------------------------------------------------
        m_p_centroids = &ctx_mgr.get_plaintext_centroids();
        m_p_codebooks = &ctx_mgr.get_plaintext_codebooks();

        // Encrypt query in dimension-major layout for Step 3
        auto ct_query_dimpack = ctx_mgr.encrypt_query_dimpack(query, config);

        const int B = slots / config.dimension;  // 256
        const double penalty = 1.0e6;

        // Pre-extract per-cluster mask scalars from ct_mask_coarse
        // ct_mask_coarse has m_c at slot c for c = 0..n_list-1
        std::vector<Ciphertext<DCRTPoly>> ct_m_c_vec(config.n_list);
        for (int c = 0; c < config.n_list; ++c)
        {
            // Extract scalar m_c from slot c
            std::vector<double> mask_c(slots, 0.0);
            mask_c[c] = 1.0;
            auto pt_mask_c = cc->MakeCKKSPackedPlaintext(mask_c, 1, ct_mask_coarse->GetLevel());
            auto ct_m_c_slot = cc->EvalMult(ct_mask_coarse, pt_mask_c);
            ct_m_c_slot = cc->Rescale(ct_m_c_slot);

            // Shift slot c to slot 0
            if (c != 0)
                ct_m_c_slot = rotate(cc, ct_m_c_slot, c);

            // Replicate scalar to B slots [0..B-1]
            for (int stride = 1; stride < B; stride *= 2)
            {
                auto shifted = cc->EvalRotate(ct_m_c_slot, -stride);
                ct_m_c_slot = cc->EvalAdd(ct_m_c_slot, shifted);
            }

            ct_m_c_vec[c] = ct_m_c_slot;
        }

        #ifdef ENABLE_LOGGING
        std::cout << "Step 3: Pre-extracted " << config.n_list << " probe mask scalars." << std::endl;
        #endif

        // Process each cluster: compute distances, apply probe penalty
        std::vector<std::pair<int, float>> all_candidates;
        int final_mult_level = 0;
        int final_noise_degree = 0;

        for (int c = 0; c < config.n_list; ++c)
        {
            const auto& c_vids = m_cluster_vector_ids[c];
            if (c_vids.empty()) continue;

            // Process in batches of B
            for (int batch_start = 0; batch_start < static_cast<int>(c_vids.size()); batch_start += B)
            {
                int batch_end = std::min(batch_start + B, static_cast<int>(c_vids.size()));
                std::vector<int> batch_ids(c_vids.begin() + batch_start, c_vids.begin() + batch_end);
                int batch_count = static_cast<int>(batch_ids.size());

                // Compute SIMD-batched distances (depth +2 from fresh query)
                auto ct_dists = compute_batch_distances_dimpack(cc, ct_query_dimpack, batch_ids, config);

                // Apply probe penalty mask (level-matches and multiplies by m_c) (depth +1)
                auto ct_penalized = apply_probe_penalty(cc, ct_dists, ct_m_c_vec[c], penalty, config);

                // Decrypt distances (client-side simulation)
                auto dec_dists = ctx_mgr.decrypt_vector(ct_penalized, batch_count);
                final_mult_level = ct_penalized->GetLevel();
                final_noise_degree = ct_penalized->GetNoiseScaleDeg();

                for (int j = 0; j < batch_count; ++j)
                {
                    all_candidates.emplace_back(batch_ids[j], static_cast<float>(dec_dists[j]));
                }

                #ifdef ENABLE_LOGGING
                std::cout << "  Cluster " << c << " batch [" << batch_start << ".." << batch_end
                          << ") processed (" << batch_count << " candidates)" << std::endl;
                #endif
            }
        }

        if (stats && !all_candidates.empty()) stats->level_lut_dist = ct_m_c_vec[0]->GetLevel();
        auto t3 = std::chrono::high_resolution_clock::now();

        #ifdef ENABLE_LOGGING
        std::cout << "Step 3: SIMD-batched ADC distances + probe masking done!" << std::endl;
        std::cout << "Final Level: " << final_mult_level << std::endl;
        std::cout << "Noise Scale Deg: " << final_noise_degree << std::endl;
        std::cout << "Total candidates: " << all_candidates.size() << std::endl;
        std::cout << "time = " << std::chrono::duration_cast<std::chrono::milliseconds>(t3-t2) << " ms" << std::endl;
        std::cout << "--------------------------------------------------------------" << std::endl;
        #endif

        // -----------------------------------------------------------------------
        // Step 4: Client-side sorting and top-k selection (no homomorphic ranking)
        // -----------------------------------------------------------------------
        std::sort(all_candidates.begin(), all_candidates.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });

        std::vector<std::pair<int, float>> results;
        for (int i = 0; i < static_cast<int>(all_candidates.size()) && i < top_k; ++i)
        {
            if (all_candidates[i].second < penalty * 0.5f)
            {
                results.push_back(all_candidates[i]);
            }
        }

        if (stats && !results.empty()) stats->level_fine_rank = 0;  // Client-side, no FHE depth
        auto t4 = std::chrono::high_resolution_clock::now();

        #ifdef ENABLE_LOGGING
        std::cout << "Step 4: Client-side top-k selection done!" << std::endl;
        std::cout << "Selected " << results.size() << " results out of " << all_candidates.size() << " candidates" << std::endl;
        std::cout << "time = " << std::chrono::duration_cast<std::chrono::milliseconds>(t4-t3) << " ms" << std::endl;
        std::cout << "--------------------------------------------------------------" << std::endl;
        #endif

        if (timings)
        {
            timings->resize(2);
            (*timings)[0] = std::chrono::duration<double, std::milli>(t1 - t0).count();
            (*timings)[1] = std::chrono::duration<double, std::milli>(t4 - t1).count();
        }

        return results;

    }

} // namespace anns_fhe
