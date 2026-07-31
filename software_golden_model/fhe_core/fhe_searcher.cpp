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
        const std::string cent_path    = config.resolve_path(config.models_output_dir   + "/ivf_centroids.bin");

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

        if (!load_bin(assign_path, m_assignments) ||
            !load_bin(cent_path,   m_centroids_plain))
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

        // Build inverted lists
        m_inverted_lists.clear();
        m_inverted_lists.resize(config.n_list);
        for (int i = 0; i < m_num_vectors; ++i)
        {
            int c = m_assignments[i];
            if (c >= 0 && c < config.n_list)
                m_inverted_lists[c].push_back(i);
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

            #ifdef ENABLE_LOGGING
            std::cout << "===== in compact_distances(), 1: " << i << "/" << n_list << std::endl;
            #endif

            // 2. Extract slot i*dim
            auto ct_sel = cc->EvalMult(ct_strided, pt_mask);
            ct_sel = cc->Rescale(ct_sel);

            #ifdef ENABLE_LOGGING
            std::cout << "===== in compact_distances(), 2: " << i << "/" << n_list << std::endl;
            #endif

            // 3. Move slot (i * dim) to slot i (Left shift by i * (dim - 1))
            int shift = i * (dim - 1);
            if (shift != 0) {
                ct_sel = rotate(cc, ct_sel, shift);
            }

            #ifdef ENABLE_LOGGING
            std::cout << "===== in compact_distances(), 3: " << i << "/" << n_list << std::endl;
            #endif

            // 4. Accumulate
            if (first) {
                ct_compact = ct_sel;
                first = false;
            } else {
                ct_compact = cc->EvalAdd(ct_compact, ct_sel);
            }

            #ifdef ENABLE_LOGGING
            std::cout << "===== in compact_distances(), 4: " << i << "/" << n_list << std::endl;
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
    // Step 3a: Construct m_subvectors x k_subcentroids Look-Up Table (LUT) in 1 Ciphertext
    // Depth: 1 mult level (subvector residual squaring) + 1 mult level (compacting mask) = 2 levels
    // ---------------------------------------------------------------------------
    std::vector<Ciphertext<DCRTPoly>> FHESearcher::build_adc_lut(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct_query,
        const std::vector<Ciphertext<DCRTPoly>>& ct_codebooks,
        int centroid_id,
        const FHEConfig& config) const
    {
        const int M       = config.m_subvectors;
        const int K       = config.k_subcentroids;
        const int dim     = config.dimension;
        const int sub_dim = dim / M;
        const int slots   = config.poly_modulus_degree >> 1;

        std::vector<Ciphertext<DCRTPoly>> lut(M);
        const float* c_ptr = m_centroids_plain.data() + centroid_id * dim;

        for (int m = 0; m < M; ++m)
        {
            // Replicate centroid subvector K times
            std::vector<double> centroid_rep(slots, 0.0);
            for (int k = 0; k < K; ++k)
                for (int d = 0; d < sub_dim; ++d)
                {
                    int slot = k * sub_dim + d;
                    if (slot < slots)
                        centroid_rep[slot] = static_cast<double>(c_ptr[m * sub_dim + d]);
                }
            auto pt_centroid = cc->MakeCKKSPackedPlaintext(centroid_rep);

            // Extract subvector m from query
            std::vector<double> q_sub_mask(slots, 0.0);
            for (int d = 0; d < sub_dim; ++d)
                if (m * sub_dim + d < slots)
                    q_sub_mask[m * sub_dim + d] = 1.0;
            auto pt_q_mask = cc->MakeCKKSPackedPlaintext(q_sub_mask);

            auto ct_q_sub_raw = cc->EvalMult(ct_query, pt_q_mask);
            ct_q_sub_raw = cc->Rescale(ct_q_sub_raw);

            if (m * sub_dim > 0)
                ct_q_sub_raw = rotate(cc, ct_q_sub_raw, m * sub_dim);

            auto ct_q_rep = ct_q_sub_raw;
            for (int stride = sub_dim; stride < K * sub_dim; stride *= 2)
            {
                auto shifted = cc->EvalRotate(ct_q_rep, -stride);
                ct_q_rep = cc->EvalAdd(ct_q_rep, shifted);
            }

            // Residual against codebook: (q - c - cb)^2
            auto ct_res  = cc->EvalSub(ct_q_rep, pt_centroid);
            auto ct_diff = cc->EvalSub(ct_res, ct_codebooks[m]);
            auto ct_dist = cc->EvalMult(ct_diff, ct_diff);
            ct_dist = cc->Rescale(ct_dist);

            // Sum within sub_dim blocks
            for (int stride = 1; stride < sub_dim; stride *= 2)
            {
                auto shifted = cc->EvalRotate(ct_dist, stride);
                ct_dist = cc->EvalAdd(ct_dist, shifted);
            }
            lut[m] = ct_dist;
        }
        return lut;
    }

    // ---------------------------------------------------------------------------
    // Step 3b: Candidate distance computation for 1 candidate vector
    // ---------------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::compute_candidate_distance(
        const CryptoContext<DCRTPoly>& cc,
        const std::vector<Ciphertext<DCRTPoly>>& lut,
        int vector_idx,
        const FHEConfig& config,
        std::vector<double>& mask_buffer) const
    {
        const int M       = config.m_subvectors;
        const int dim     = config.dimension;
        const int sub_dim = dim / M;
        const int slots   = config.poly_modulus_degree >> 1;

        Ciphertext<DCRTPoly> ct_sum;
        bool first = true;

        for (int m = 0; m < M; ++m)
        {
            uint16_t code = m_pq_codes[static_cast<size_t>(vector_idx) * M + m];
            int hot_slot  = static_cast<int>(code) * sub_dim;

            auto shifted = (hot_slot != 0) ? rotate(cc, lut[m], hot_slot) : lut[m];
            if (first) {
                ct_sum = shifted;
                first  = false;
            } else {
                ct_sum = cc->EvalAdd(ct_sum, shifted);
            }
        }

        std::fill(mask_buffer.begin(), mask_buffer.end(), 0.0);
        if (slots > 0)
            mask_buffer[0] = 1.0;
        auto pt_mask = cc->MakeCKKSPackedPlaintext(mask_buffer);
        auto ct_dist = cc->EvalMult(ct_sum, pt_mask);
        cc->Rescale(ct_dist);

        return ct_dist;
    }

    // ---------------------------------------------------------------------------
    // Step 4: Block-wise Top-k Candidate Selection (N=64 Blocks)
    // Depth: 3 levels (Chebyshev Sign deg 15) + 2 levels (Indicator deg 7) = 5 levels
    // Total Depth after Step 4 = 13 + 5 = 18 levels
    // ---------------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::filter_block_top_k(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct_block_dists,
        int top_k,
        double dist_bound,
        const FHEConfig& config) const
    {
        const size_t N = 64;

        // Mazzone N=64 matrix re-encoding (0 mult depth)
        auto c_row       = anns_fhe::replicate_row(ct_block_dists, N);
        normalize_scale(cc, c_row);
        auto c_col_trans = anns_fhe::transpose_row(ct_block_dists, N, true);
        normalize_scale(cc, c_col_trans);
        auto c_col       = anns_fhe::replicate_column(c_col_trans, N);

        // Difference matrix Δ_uv = Dist_u - Dist_v
        auto ct_diff = cc->EvalSub(c_col, c_row);

        // 1. Evaluate ApproxSign(Δ_uv) (Degree 15, Depth 3)
        auto S_uv = SignApproximator::eval_sign(cc, ct_diff, -dist_bound, dist_bound, openfhe_stats::depth2degree(3), 1e-20);

        // 2. Compute local ranks via column sum (0 mult depth)
        auto ct_sum_cols = anns_fhe::sum_columns(S_uv, N, true);
        normalize_scale(cc, ct_sum_cols);
        auto ct_ranks    = anns_fhe::transpose_column(ct_sum_cols, N, true);
        normalize_scale(cc, ct_ranks);

        // 3. Evaluate Top-k indicator polynomial P_k(Rank) (Degree 7, Depth 2)
        double threshold_min = 0.5;
        double threshold_max = static_cast<double>(top_k) + 0.5;
        auto ct_indicator = SignApproximator::eval_indicator(
            cc, ct_ranks, threshold_min, threshold_max, 0.0, static_cast<double>(N), openfhe_stats::depth2degree(2));

        // 4. Apply penalty mask to non-top-k candidates
        auto ct_inv_ind = cc->EvalSub(1.0, ct_indicator);
        auto ct_pen     = cc->EvalMult(ct_inv_ind, dist_bound);
        cc->Rescale(ct_pen);
        auto ct_filtered = cc->EvalAdd(ct_block_dists, ct_pen);

        return ct_filtered;
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
        // Step 3: ADC LUT construction & candidate block distances (Depth 2)
        // -----------------------------------------------------------------------
        std::vector<std::vector<Ciphertext<DCRTPoly>>> all_luts(config.n_list);
        for (int c = 0; c < config.n_list; ++c)
        {
            all_luts[c] = build_adc_lut(cc, ct_query, ctx_mgr.get_encrypted_codebooks(), c, config);
        }

        std::vector<Ciphertext<DCRTPoly>> cand_dists;
        std::vector<int> cand_ids;
        std::vector<double> mask_buffer(slots, 0.0);

        for (int i = 0; i < m_num_vectors; ++i)
        {
            int cid = m_assignments[i];
            if (cid >= 0 && cid < config.n_list)
            {
                auto ct_d = compute_candidate_distance(cc, all_luts[cid], i, config, mask_buffer);
                cand_dists.push_back(ct_d);
                cand_ids.push_back(i);
            }
        }

        if (stats && !cand_dists.empty()) stats->level_lut_dist = cand_dists[0]->GetLevel();
        auto t3 = std::chrono::high_resolution_clock::now();

        #ifdef ENABLE_LOGGING
        std::cout << "Step 3: ADC LUT construction & candidate block distances done!" << std::endl;
        std::cout << "stats->level_lut_dist = " << stats->level_lut_dist << std::endl;
        std::cout << "time = " << std::chrono::duration_cast<std::chrono::milliseconds>(t3-t2) << " ms" << std::endl;
        std::cout << "--------------------------------------------------------------" << std::endl;
        #endif

        // -----------------------------------------------------------------------
        // Step 4: Block-wise Top-k candidate selection (N=64 Blocks, Depth 5)
        // -----------------------------------------------------------------------
        const int batch_size = 64;
        const int N_cand = static_cast<int>(cand_dists.size());
        const int num_batches = (N_cand + batch_size - 1) / batch_size;

        std::vector<Ciphertext<DCRTPoly>> filtered_blocks;
        std::vector<std::vector<int>> block_candidate_ids;

        for (int b = 0; b < num_batches; ++b)
        {
            int start_idx = b * batch_size;
            int count = std::min(batch_size, N_cand - start_idx);

            Ciphertext<DCRTPoly> ct_batch;
            std::vector<int> b_ids(batch_size, -1);
            bool first = true;

            for (int j = 0; j < count; ++j)
            {
                int c_idx = start_idx + j;
                b_ids[j] = cand_ids[c_idx];

                auto shifted = (j != 0) ? rotate(cc, cand_dists[c_idx], -j) : cand_dists[c_idx];
                if (first) {
                    ct_batch = shifted;
                    first = false;
                } else {
                    ct_batch = cc->EvalAdd(ct_batch, shifted);
                }
            }
            if (count < batch_size)
            {
                std::vector<double> pad_mask(slots, 0.0);
                for (int j = count; j < batch_size; ++j)
                    pad_mask[j] = dist_bound;
                auto pt_pad = cc->MakeCKKSPackedPlaintext(pad_mask);
                ct_batch = cc->EvalAdd(ct_batch, pt_pad);
            }

            auto ct_filtered = filter_block_top_k(cc, ct_batch, top_k, dist_bound, config);
            filtered_blocks.push_back(ct_filtered);
            block_candidate_ids.push_back(b_ids);
        }

        if (stats && !filtered_blocks.empty()) stats->level_fine_rank = filtered_blocks[0]->GetLevel();

        auto t4 = std::chrono::high_resolution_clock::now();

        #ifdef ENABLE_LOGGING
        std::cout << "Step 4: Block-wise Top-k candidate selection done!" << std::endl;
        std::cout << "stats->level_fine_rank = " << stats->level_fine_rank << std::endl;
        std::cout << "time = " << std::chrono::duration_cast<std::chrono::milliseconds>(t4-t3) << std::endl;
        std::cout << "--------------------------------------------------------------" << std::endl;
        #endif

        // -----------------------------------------------------------------------
        // Decrypt final top-k candidates for client result retrieval
        // -----------------------------------------------------------------------
        std::vector<std::pair<int, float>> results;
        if (!filtered_blocks.empty())
        {
            auto dec_dists = ctx_mgr.decrypt_vector(filtered_blocks[0], static_cast<int>(block_candidate_ids[0].size()));
            for (size_t j = 0; j < block_candidate_ids[0].size(); ++j)
            {
                if (block_candidate_ids[0][j] >= 0 && dec_dists[j] < dist_bound * 0.5)
                {
                    results.emplace_back(block_candidate_ids[0][j], static_cast<float>(dec_dists[j]));
                }
            }
        }

        std::sort(results.begin(), results.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });
        if (static_cast<int>(results.size()) > top_k)
            results.resize(top_k);

        if (timings)
        {
            timings->resize(2);
            (*timings)[0] = std::chrono::duration<double, std::milli>(t1 - t0).count();
            (*timings)[1] = std::chrono::duration<double, std::milli>(t3 - t1).count();
        }

        #ifdef ENABLE_LOGGING
        std::cout << "Decrypt final top-k candidates for client result retrieval done!" << std::endl;
        std::cout << "--------------------------------------------------------------" << std::endl;
        #endif

        return results;
    }

} // namespace anns_fhe
