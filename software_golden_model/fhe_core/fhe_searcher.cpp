#include "fhe_searcher.h"
#include "sign_approximator.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <set>

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

        for (int bit = 0; bit < 31; ++bit)
        {
            if (abs_rot & (1 << bit))
            {
                int step = sign * (1 << bit);
                result = cc->EvalRotate(result, step);
            }
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
                std::cerr << "[FHESearcher] Cannot open: " << path << std::endl;
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
                std::cerr << "[FHESearcher] Cannot open: " << codes_path << std::endl;
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

        std::cout << "[FHESearcher] Loaded plaintext data: " << m_num_vectors
                  << " vectors across " << config.n_list << " inverted lists." << std::endl;
        return true;
    }

    // ---------------------------------------------------------------------------
    // Stage 1a: Coarse centroid distance computation
    // Output: distances packed at stride=dim within the ciphertext
    // -----------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::compute_coarse_distances(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct_query,
        const Ciphertext<DCRTPoly>& ct_centroids,
        const FHEConfig& config) const
    {
        // 1. Componentwise difference: q - c
        auto ct_diff = cc->EvalSub(ct_query, ct_centroids);

        // 2. Componentwise square: (q - c)^2   (consumes 1 mult-level)
        auto ct_sq = cc->EvalMult(ct_diff, ct_diff);
        cc->Rescale(ct_sq);

        // 3. Tree reduction within each dim-wide centroid block
        const int dim     = config.dimension;        // 128
        const int sub_dim = dim / config.m_subvectors; // 16

        auto ct_acc = ct_sq;
        for (int stride = 1; stride < sub_dim; stride *= 2)
        {
            auto shifted = cc->EvalRotate(ct_acc, stride);
            ct_acc = cc->EvalAdd(ct_acc, shifted);
        }
        for (int stride = sub_dim; stride < dim; stride *= 2)
        {
            auto shifted = cc->EvalRotate(ct_acc, stride);
            ct_acc = cc->EvalAdd(ct_acc, shifted);
        }

        return ct_acc;
    }

    // ---------------------------------------------------------------------------
    // Stage 1b: Compact stride-dim distances into contiguous slots [0..n_list-1]
    // -----------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::compact_distances(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct_strided,
        const FHEConfig& config) const
    {
        const int n_list = config.n_list;
        const int dim    = config.dimension;
        const int slots  = config.poly_modulus_degree >> 1;

        Ciphertext<DCRTPoly> ct_compact;
        bool first = true;

        for (int i = 0; i < n_list; ++i)
        {
            std::vector<double> mask(slots, 0.0);
            mask[i * dim] = 1.0;
            auto pt_mask = cc->MakeCKKSPackedPlaintext(mask);

            // Select slot i*dim  (consumes 1 mult-level)
            auto ct_sel = cc->EvalMult(ct_strided, pt_mask);

            // Rotate left by i*(dim - 1) to land at slot i
            int rot = i * (dim - 1);
            if (rot != 0)
                ct_sel = rotate(cc, ct_sel, rot);

            if (first) {
                ct_compact = ct_sel;
                first = false;
            } else {
                ct_compact = cc->EvalAdd(ct_compact, ct_sel);
            }
        }
        cc->Rescale(ct_compact);
        return ct_compact;
    }

    // ---------------------------------------------------------------------------
    // Stage 2 & 4: HMR sub-operations
    // -----------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::replicate_row(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct,
        int matrix_size) const
    {
        const int logN = static_cast<int>(std::ceil(std::log2(static_cast<double>(matrix_size))));
        auto result = ct;
        for (int i = 0; i < logN; ++i)
        {
            auto shifted = cc->EvalRotate(result, -(1 << (logN + i)));
            result = cc->EvalAdd(result, shifted);
        }
        return result;
    }

    Ciphertext<DCRTPoly> FHESearcher::transpose_row(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct,
        int matrix_size) const
    {
        const int total_slots = cc->GetEncodingParams()->GetBatchSize();
        Ciphertext<DCRTPoly> ct_row_starts;
        bool first = true;

        for (int i = 0; i < matrix_size; ++i)
        {
            std::vector<double> mask(total_slots, 0.0);
            if (i < total_slots)
                mask[i] = 1.0;
            auto pt_mask = cc->MakeCKKSPackedPlaintext(mask);
            auto ct_masked = cc->EvalMult(ct, pt_mask);
            int rot = i * (1 - matrix_size);
            if (rot != 0)
                ct_masked = rotate(cc, ct_masked, rot);
            if (first) {
                ct_row_starts = ct_masked;
                first = false;
            } else {
                ct_row_starts = cc->EvalAdd(ct_row_starts, ct_masked);
            }
        }
        cc->Rescale(ct_row_starts);
        return replicate_column(cc, ct_row_starts, matrix_size);
    }

    Ciphertext<DCRTPoly> FHESearcher::replicate_column(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct,
        int matrix_size) const
    {
        const int logN = static_cast<int>(std::ceil(std::log2(static_cast<double>(matrix_size))));
        auto result = ct;
        for (int i = 0; i < logN; ++i)
        {
            auto shifted = cc->EvalRotate(result, -(1 << i));
            result = cc->EvalAdd(result, shifted);
        }
        return result;
    }

    Ciphertext<DCRTPoly> FHESearcher::sum_rows(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct,
        int matrix_size) const
    {
        const int logN = static_cast<int>(std::ceil(std::log2(static_cast<double>(matrix_size))));
        auto result = ct;
        for (int i = 0; i < logN; ++i)
        {
            auto shifted = cc->EvalRotate(result, -(1 << i));
            result = cc->EvalAdd(result, shifted);
        }
        return result;
    }

    // ---------------------------------------------------------------------------
    // Stage 2 & 4: HMR Ranking (Mazzone et al. USENIX '25)
    // -----------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::hmr_rank(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct_distances,
        int n,
        double dist_bound,
        const FHEConfig& config) const
    {
        auto ct_row  = replicate_row(cc, ct_distances, n);
        auto ct_col  = transpose_row(cc, ct_distances, n);
        auto ct_diff = cc->EvalSub(ct_col, ct_row);

        // Sign approximation: +1 when diff > 0, 0 when diff < 0
        auto ct_cmp = SignApproximator::eval_sign(
            cc, ct_diff, -dist_bound, dist_bound, config);

        auto ct_row_sums     = sum_rows(cc, ct_cmp, n);
        auto ct_ranked_at_iN = cc->EvalAdd(ct_row_sums, 1.0);

        // Compact from positions i*n to positions i
        const int total_slots = cc->GetEncodingParams()->GetBatchSize();
        Ciphertext<DCRTPoly> ct_compacted;
        bool first = true;
        for (int i = 0; i < n; ++i)
        {
            std::vector<double> mask(total_slots, 0.0);
            if (i * n < total_slots)
                mask[i * n] = 1.0;
            auto pt_mask = cc->MakeCKKSPackedPlaintext(mask);
            auto ct_masked = cc->EvalMult(ct_ranked_at_iN, pt_mask);
            int rot = i * (1 - n);
            if (rot != 0)
                ct_masked = rotate(cc, ct_masked, rot);
            if (first) {
                ct_compacted = ct_masked;
                first = false;
            } else {
                ct_compacted = cc->EvalAdd(ct_compacted, ct_masked);
            }
        }
        cc->Rescale(ct_compacted);
        return ct_compacted;
    }

    // ---------------------------------------------------------------------------
    // Stage 3a: Build ADC Lookup Table for a centroid
    // -----------------------------------------------------------------------
    std::vector<Ciphertext<DCRTPoly>> FHESearcher::build_adc_lut(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct_query,
        const std::vector<Ciphertext<DCRTPoly>>& ct_codebooks,
        int centroid_id,
        const FHEConfig& config) const
    {
        const int M        = config.m_subvectors;
        const int K        = config.k_subcentroids;
        const int dim      = config.dimension;
        const int sub_dim  = dim / M;
        const int slots    = config.poly_modulus_degree >> 1;

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
            cc->Rescale(ct_q_sub_raw);

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
            cc->Rescale(ct_dist);

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
    // Stage 3b: Compute candidate vector distance using fast Galois rotations
    // -----------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::compute_candidate_distance(
        const CryptoContext<DCRTPoly>& cc,
        const std::vector<Ciphertext<DCRTPoly>>& lut,
        int vector_idx,
        const FHEConfig& config,
        std::vector<double>& mask_buffer) const
    {
        const int M        = config.m_subvectors;
        const int dim      = config.dimension;
        const int sub_dim  = dim / M;
        const int slots    = config.poly_modulus_degree >> 1;

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

        // Mask slot 0 to isolate total distance
        std::fill(mask_buffer.begin(), mask_buffer.end(), 0.0);
        if (slots > 0)
            mask_buffer[0] = 1.0;
        auto pt_mask = cc->MakeCKKSPackedPlaintext(mask_buffer);
        auto ct_dist = cc->EvalMult(ct_sum, pt_mask);
        cc->Rescale(ct_dist);

        return ct_dist;
    }

    // ---------------------------------------------------------------------------
    // Stage 4: Batch HMR & hierarchical merge-filtering via Top-K Indicator
    // -----------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::hmr_filter_batches(
        const CryptoContext<DCRTPoly>& cc,
        const std::vector<Ciphertext<DCRTPoly>>& cand_dists,
        const std::vector<int>& cand_ids,
        const Ciphertext<DCRTPoly>& ct_mask_coarse,
        int top_k,
        double dist_bound,
        const FHEConfig& config,
        std::vector<int>& out_batch_ids) const
    {
        const int slots = cc->GetEncodingParams()->GetBatchSize();
        const int N_cand = static_cast<int>(cand_dists.size());
        const int batch_size = 64;

        std::vector<Ciphertext<DCRTPoly>> current_batch_dists;
        std::vector<std::vector<int>> current_batch_ids;

        // Group initial candidates into batches of 64
        int num_batches = (N_cand + batch_size - 1) / batch_size;
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

                // Rotate candidate distance from slot 0 to slot j
                auto shifted = (j != 0) ? rotate(cc, cand_dists[c_idx], -j) : cand_dists[c_idx];
                if (first) {
                    ct_batch = shifted;
                    first = false;
                } else {
                    ct_batch = cc->EvalAdd(ct_batch, shifted);
                }
            }
            // Pad empty slots with LARGE_DIST so they get ranked last
            if (count < batch_size)
            {
                std::vector<double> pad_mask(slots, 0.0);
                for (int j = count; j < batch_size; ++j)
                    pad_mask[j] = dist_bound;
                auto pt_pad = cc->MakeCKKSPackedPlaintext(pad_mask);
                ct_batch = cc->EvalAdd(ct_batch, pt_pad);
            }

            // Apply coarse indicator filter to batch distances:
            // Extract coarse indicator I_{c_j} for each candidate j in batch
            // ct_batch_coarse_mask has I_{c_j} at slot j
            std::set<int> unique_centroids;
            for (int j = 0; j < count; ++j)
            {
                int cid = m_assignments[b_ids[j]];
                if (cid >= 0 && cid < config.n_list)
                    unique_centroids.insert(cid);
            }

            Ciphertext<DCRTPoly> ct_coarse_mask_batch;
            bool first_c = true;
            for (int cid : unique_centroids)
            {
                std::vector<double> c_mask(slots, 0.0);
                for (int j = 0; j < count; ++j)
                {
                    if (m_assignments[b_ids[j]] == cid)
                        c_mask[j] = 1.0;
                }
                auto pt_c = cc->MakeCKKSPackedPlaintext(c_mask);

                // Rotate coarse mask slot cid to slot 0, then replicate to slot 0..63 and mask
                auto shifted_coarse = (cid != 0) ? rotate(cc, ct_mask_coarse, cid) : ct_mask_coarse;
                auto rep_coarse = replicate_column(cc, shifted_coarse, batch_size);
                auto masked_coarse = cc->EvalMult(rep_coarse, pt_c);
                cc->Rescale(masked_coarse);

                if (first_c) {
                    ct_coarse_mask_batch = masked_coarse;
                    first_c = false;
                } else {
                    ct_coarse_mask_batch = cc->EvalAdd(ct_coarse_mask_batch, masked_coarse);
                }
            }

            if (!first_c)
            {
                // effective_dist = dist + (1 - I_coarse) * dist_bound
                auto ct_inv_coarse = cc->EvalSub(1.0, ct_coarse_mask_batch);
                auto ct_coarse_pen = cc->EvalMult(ct_inv_coarse, dist_bound);
                cc->Rescale(ct_coarse_pen);
                ct_batch = cc->EvalAdd(ct_batch, ct_coarse_pen);
            }

            // Run HMR on this batch of 64
            auto ct_ranks = hmr_rank(cc, ct_batch, batch_size, dist_bound, config);

            // Apply Top-K Indicator Optimization: I(r) = ChebyshevStep(top_k - r)
            auto ct_indicator = SignApproximator::eval_sign(
                cc, cc->EvalSub(static_cast<double>(top_k) + 0.5, ct_ranks),
                -batch_size, batch_size, config);

            // Filter distances: non-top-k candidates get penalized by dist_bound
            auto ct_inv_ind = cc->EvalSub(1.0, ct_indicator);
            auto ct_pen = cc->EvalMult(ct_inv_ind, dist_bound);
            cc->Rescale(ct_pen);
            auto ct_filtered = cc->EvalAdd(ct_batch, ct_pen);

            current_batch_dists.push_back(ct_filtered);
            current_batch_ids.push_back(b_ids);
        }

        // Hierarchical merge-filtering across batches
        while (current_batch_dists.size() > 1)
        {
            std::vector<Ciphertext<DCRTPoly>> next_batch_dists;
            std::vector<std::vector<int>> next_batch_ids;

            for (size_t b = 0; b < current_batch_dists.size(); b += 8)
            {
                size_t group_end = std::min(b + 8, current_batch_dists.size());
                Ciphertext<DCRTPoly> ct_merged;
                std::vector<int> merged_ids(batch_size, -1);
                bool first = true;

                for (size_t g = b; g < group_end; ++g)
                {
                    // Take up to top_k slots from each batch (or just the first 8 slots if sorted/packed)
                    // Here we pack 8 slots from each of the grouped batches
                    for (int k = 0; k < top_k && (g - b) * top_k + k < size_t(batch_size); ++k)
                    {
                        int target_slot = static_cast<int>((g - b) * top_k + k);
                        merged_ids[target_slot] = current_batch_ids[g][k];

                        std::vector<double> s_mask(slots, 0.0);
                        if (k < slots)
                            s_mask[k] = 1.0;
                        auto pt_s = cc->MakeCKKSPackedPlaintext(s_mask);
                        auto isolated = cc->EvalMult(current_batch_dists[g], pt_s);
                        cc->Rescale(isolated);

                        int shift = k - target_slot;
                        if (shift != 0)
                            isolated = rotate(cc, isolated, shift);

                        if (first) {
                            ct_merged = isolated;
                            first = false;
                        } else {
                            ct_merged = cc->EvalAdd(ct_merged, isolated);
                        }
                    }
                }

                auto ct_ranks = hmr_rank(cc, ct_merged, batch_size, dist_bound, config);
                auto ct_indicator = SignApproximator::eval_sign(
                    cc, cc->EvalSub(static_cast<double>(top_k) + 0.5, ct_ranks),
                    -batch_size, batch_size, config);

                auto ct_inv_ind = cc->EvalSub(1.0, ct_indicator);
                auto ct_pen = cc->EvalMult(ct_inv_ind, dist_bound);
                cc->Rescale(ct_pen);
                auto ct_filtered = cc->EvalAdd(ct_merged, ct_pen);

                next_batch_dists.push_back(ct_filtered);
                next_batch_ids.push_back(merged_ids);
            }
            current_batch_dists = std::move(next_batch_dists);
            current_batch_ids   = std::move(next_batch_ids);
        }

        out_batch_ids = current_batch_ids[0];
        return current_batch_dists[0];
    }

    // ---------------------------------------------------------------------------
    // Full Search Orchestration
    // -----------------------------------------------------------------------
    std::vector<std::pair<int, float>> FHESearcher::search(
        const FHEContextManager& ctx_mgr,
        const std::vector<float>& query,
        int top_k,
        const FHEConfig& config,
        std::vector<double>* timings) const
    {
        auto t0 = std::chrono::high_resolution_clock::now();

        const auto& cc = ctx_mgr.get_context();
        const int slots = config.poly_modulus_degree >> 1;
        const double dist_bound = 100.0; // upper bound for Chebyshev comparison

        // Encrypt query packed
        auto ct_query = ctx_mgr.encrypt_query_packed(query, config);

        // -----------------------------------------------------------------------
        // Stage 1: Coarse distance computation
        // -----------------------------------------------------------------------
        auto ct_strided = compute_coarse_distances(
            cc, ct_query, ctx_mgr.get_encrypted_centroids(), config);

        auto ct_compact = compact_distances(cc, ct_strided, config);

        auto t1 = std::chrono::high_resolution_clock::now();

        // -----------------------------------------------------------------------
        // Stage 2: Coarse HMR Ranking & Top-K Indicator (n_probe = 2)
        // -----------------------------------------------------------------------
        auto ct_ranks_coarse = hmr_rank(cc, ct_compact, config.n_list, dist_bound, config);

        // Top-K Indicator: 1.0 for top n_probe centroids, 0.0 otherwise
        auto ct_mask_coarse = SignApproximator::eval_sign(
            cc, cc->EvalSub(static_cast<double>(config.n_probe) + 0.5, ct_ranks_coarse),
            -config.n_list, config.n_list, config);

        // -----------------------------------------------------------------------
        // Stage 3: ADC LUT construction & candidate distance computation
        // -----------------------------------------------------------------------
        // Build LUTs for all centroids (since coarse selection is homomorphically masked)
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

        auto t2 = std::chrono::high_resolution_clock::now();

        // -----------------------------------------------------------------------
        // Stage 4: Fine HMR Ranking & Hierarchical Merge-Filtering
        // -----------------------------------------------------------------------
        std::vector<int> final_batch_ids;
        auto ct_final = hmr_filter_batches(
            cc, cand_dists, cand_ids, ct_mask_coarse, top_k, dist_bound, config, final_batch_ids);

        auto t3 = std::chrono::high_resolution_clock::now();

        // -----------------------------------------------------------------------
        // Stage 5: Client receipt & decryption of final top-k results
        // -----------------------------------------------------------------------
        auto dec_dists = ctx_mgr.decrypt_vector(ct_final, static_cast<int>(final_batch_ids.size()));

        std::vector<std::pair<int, float>> results;
        for (size_t j = 0; j < final_batch_ids.size(); ++j)
        {
            if (final_batch_ids[j] >= 0 && dec_dists[j] < dist_bound * 0.5)
            {
                results.emplace_back(final_batch_ids[j], static_cast<float>(dec_dists[j]));
            }
        }

        // Sort surviving top candidates cleanly
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

        return results;
    }

} // namespace anns_fhe
