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

        while (abs_rot > 0) 
        {
            int step_size = 1;
            while ((step_size << 1) <= abs_rot) 
            {
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
        const FHEConfig& config) const
    {
        const int n_list = config.n_list;
        const int dim = config.dimension;
        const int slots = config.poly_modulus_degree >> 1;
        
        std::vector<double> packed_centroids(slots, 0.0);
        for (int c = 0; c < n_list; ++c) 
        {
            for (int d = 0; d < dim; ++d) 
            {
                if (c * dim + d < slots) 
                {
                    packed_centroids[c * dim + d] = static_cast<double>((*m_p_centroids)[c * dim + d]);
                }
            }
        }
        auto pt_centroids = cc->MakeCKKSPackedPlaintext(packed_centroids, 1, ct_query->GetLevel());

        // 1. Componentwise difference: q - c
        auto ct_diff = cc->EvalSub(ct_query, pt_centroids);

        #ifdef ENABLE_LOGGING
        std::cout << "===== in compute_coarse_distances(), 1" << std::endl;
        #endif

        // 2. Componentwise square: (q - c)^2  (Level 1)
        ct_diff = cc->EvalMult(ct_diff, ct_diff);
        ct_diff = cc->Rescale(ct_diff);

        #ifdef ENABLE_LOGGING
        std::cout << "===== in compute_coarse_distances(), 2" << std::endl;
        #endif

        // 3. Tree reduction over 128 dims (0 depth)
        for (int stride = 1; stride < config.dimension; stride *= 2)
        {
            auto shifted = cc->EvalRotate(ct_diff, stride);
            ct_diff = cc->EvalAdd(ct_diff, shifted);

            #ifdef ENABLE_LOGGING
            std::cout << "===== in compute_coarse_distances(), 3: " << stride << "/" << config.dimension << std::endl;
            #endif
        }

        return ct_diff;
    }

    // ---------------------------------------------------------------------------
    // Step 1b: Compact distances at stride dim into slots [0..n_list]
    // Depth: 1 mult level for plaintext mask isolation
    // ---------------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::compact_distances(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct_strided,
        const FHEConfig& config) const
    {
        const size_t current_level = ct_strided->GetLevel();
        const int n_list = config.n_list;
        const int dim    = config.dimension;
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

    std::vector<int> FHESearcher::select_clusters_interactive(
        const FHEContextManager& ctx_mgr,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_compact_dists,
        const FHEConfig& config) const
    {
        auto dec_dists = ctx_mgr.decrypt_vector(ct_compact_dists, config.n_list);

        std::vector<std::pair<int, float>> dist_idx;
        for (int i = 0; i < config.n_list; ++i) 
        {
            dist_idx.push_back({i, static_cast<float>(dec_dists[i])});
        }

        std::sort(dist_idx.begin(), dist_idx.end(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        });

        std::vector<int> selected_clusters;
        for (int i = 0; i < config.n_probe && i < config.n_list; ++i) 
        {
            selected_clusters.push_back(dist_idx[i].first);
        }

        return selected_clusters;
    }

    // ---------------------------------------------------------------------------
    // Step 2: Top-n_probe Centroid Ranking (n_list Pairwise Matrix)
    // Depth consumed: 2 + (Chebyshev Sign) + 1 + (Indicator) -> check openfhe_stats::depth2degree()
    // ---------------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::rank_top_nprobe_centroids(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct_compact_dists,
        double dist_bound,
        const FHEConfig& config) const
    {
        const size_t N = static_cast<size_t>(config.n_list);

        // Mazzone N=n_list matrix re-encoding (0 mult depth)
        // c_row has row i as [D_0, D_1, ..., D_n_list]
        auto c_row       = anns_fhe::replicate_row(ct_compact_dists, N);

        // c_col_trans transposes row into column 0 (1 mult depth)
        auto c_col = anns_fhe::transpose_row(ct_compact_dists, N, true);
        c_col = cc->Rescale(c_col);

        // c_col replicates column 0 across all n_list columns (0 mult depth)
        c_col = anns_fhe::replicate_column(c_col, N);

        // Difference matrix: Δ_ij = D_i - D_j (0 mult depth)
        auto ct_diff = cc->EvalSub(c_row, c_col);

        // Scaling distances (1 mult depth)
        double B = 100.0; // the whole bandwidth of evaluation
        double scale_factor = B / dist_bound;
        std::vector<double> scale_vec((cc->GetRingDimension() >> 1), scale_factor);
        auto pt_scale = cc->MakeCKKSPackedPlaintext(scale_vec, 1, ct_compact_dists->GetLevel());
        ct_diff = cc->EvalMult(ct_diff, pt_scale);
        ct_diff = cc->Rescale(ct_diff);

        // 1. Evaluate ApproxSign(Δ_ij) (check openfhe_stats::depth2degree())
        int degree = config.eval_sign_deg;
        double eps = 1.5 * (B/2) / degree;
        auto S_ij = SignApproximator::eval_sign(cc, ct_diff, -B/2, B/2, degree, eps);

        #ifdef ENABLE_LOGGING
        std::cout << "===== in rank_top_nprobe_centroids(), 1" << std::endl;
        std::cout << "Current Level: " << S_ij->GetLevel() << std::endl;
        std::cout << "Noise Scale Deg: " << S_ij->GetNoiseScaleDeg() << std::endl;
        #endif

        // 2. Sum across columns to compute rank vector R_i = Σ_j S_ij (1 mult depth)
        auto ct_ranks = anns_fhe::sum_rows(S_ij, N, true);
        ct_ranks = cc->Rescale(ct_ranks);
        ct_ranks = cc->EvalAdd(ct_ranks, 0.5);

        #ifdef ENABLE_LOGGING
        std::cout << "===== in rank_top_nprobe_centroids(), 2" << std::endl;
        std::cout << "Current Level: " << ct_ranks->GetLevel() << std::endl;
        std::cout << "Noise Scale Deg: " << ct_ranks->GetNoiseScaleDeg() << std::endl;
        ctx_mgr_ptr->decrypt_and_print_vector(ct_ranks, "ct_ranks", config.n_list, true);
        #endif

        // 3. Evaluate Top-n_probe indicator polynomial P_nprobe(R_i) (check openfhe_stats::depth2degree())
        // Dynamic n_probe threshold from config
        degree = config.eval_indicator_deg;
        double threshold_min = 0.5;
        double threshold_max = static_cast<double>(config.n_probe) + 0.5;
        ct_ranks = SignApproximator::eval_indicator(
            cc, ct_ranks, threshold_min, threshold_max, 0.0, static_cast<double>(N)+0.5, degree);

        #ifdef ENABLE_LOGGING
        std::cout << "===== in rank_top_nprobe_centroids(), 3" << std::endl;
        #endif

        return ct_ranks;
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
    // Depth consumed: 1 level
    // ---------------------------------------------------------------------------
    Ciphertext<DCRTPoly> FHESearcher::compute_batch_distances_dimpack(
        const CryptoContext<DCRTPoly>& cc,
        const Ciphertext<DCRTPoly>& ct_query_dimpack,
        const std::vector<int>& batch_vec_ids,
        const FHEConfig& config) const
    {
        const int D     = config.dimension;
        const int slots = config.poly_modulus_degree >> 1;
        const int B     = slots / D;

        // Build plaintext batch in dimension-major layout
        std::vector<double> packed;
        build_dimpack_plaintext(batch_vec_ids, B, config, packed);
        auto pt_batch = cc->MakeCKKSPackedPlaintext(packed, 1, ct_query_dimpack->GetLevel());

        // Componentwise difference: q[d] - x_j[d] for all (d, j)  (0 mult depth)
        auto ct_diff = cc->EvalSub(ct_query_dimpack, pt_batch);

        // Componentwise square  (1 mult depth)
        ct_diff = cc->EvalMult(ct_diff, ct_diff);
        ct_diff = cc->Rescale(ct_diff);

        // Tree reduction across D dimension blocks (0 mult depth)
        // stride = B, 2B, 4B, ..., (D/2)*B
        // After reduction: slot[j] = sum_{d=0}^{D-1} (q[d] - x_j[d])^2
        for (int stride = B; stride < D * B; stride *= 2)
        {
            auto rotated = cc->EvalRotate(ct_diff, stride);
            ct_diff = cc->EvalAdd(ct_diff, rotated);
        }

        return ct_diff;
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
        ct_adjusted = cc->EvalSub(ct_adjusted, penalty);

        // ct_masked = ct_shifted * ct_m_c  (1 mult depth)
        ct_adjusted = cc->EvalMult(ct_adjusted, ct_m_c_replicated);
        ct_adjusted = cc->Rescale(ct_adjusted);

        // ct_final = ct_masked + P  (plaintext addition, 0 mult depth)
        ct_adjusted = cc->EvalAdd(ct_adjusted, penalty);

        return ct_adjusted;
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
        const double dist_bound = 8.0e5;
        ctx_mgr_ptr = const_cast<anns_fhe::FHEContextManager*>(&ctx_mgr);

        // Encrypt query packed
        m_p_centroids = &ctx_mgr.get_plaintext_centroids();
        m_p_codebooks = &ctx_mgr.get_plaintext_codebooks();
        
        auto ct_query = ctx_mgr.encrypt_query_packed(query, config);

        #ifdef ENABLE_LOGGING
        std::cout << "Encrypt query packed done!" << std::endl;
        std::cout << "Current Level: " << ct_query->GetLevel() << std::endl;
        std::cout << "Noise Scale Deg: " << ct_query->GetNoiseScaleDeg() << std::endl;
        std::cout << "--------------------------------------------------------------" << std::endl;
        #endif

        // -----------------------------------------------------------------------
        // Coarse distance computation & compaction (Depth +2)
        // -----------------------------------------------------------------------
        auto t1 = t0;
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> ct_compact;

        if(config.interactive || config.add_penalty)
        {
            auto ct_strided = compute_coarse_distances(cc, ct_query, config);

            #ifdef ENABLE_LOGGING
            std::cout << "*** after compute_coarse_distances()" << std::endl;
            std::cout << "Current Level: " << ct_strided->GetLevel() << std::endl;
            std::cout << "Noise Scale Deg: " << ct_strided->GetNoiseScaleDeg() << std::endl;
            #endif

            ct_compact = compact_distances(cc, ct_strided, config);

            #ifdef ENABLE_LOGGING
            std::cout << "*** after compact_distances()" << std::endl;
            std::cout << "Current Level: " << ct_compact->GetLevel() << std::endl;
            std::cout << "Noise Scale Deg: " << ct_compact->GetNoiseScaleDeg() << std::endl;
            #endif

            if (stats) stats->level_coarse_dist = ct_compact->GetLevel();
            auto t1 = std::chrono::high_resolution_clock::now();

            #ifdef ENABLE_LOGGING
            std::cout << "### Coarse distance computation & compaction done!" << std::endl;
            std::cout << "time = " << std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0) << " ms" << std::endl;
            ctx_mgr.decrypt_and_print_vector(ct_compact, "ct_compact", config.n_list, true);
            std::cout << "--------------------------------------------------------------" << std::endl;
            #endif
        }

        // -----------------------------------------------------------------------
        // Interactive vs Sequential distance computation
        // -----------------------------------------------------------------------
        std::vector<std::pair<int, float>> all_candidates;
        int final_mult_level = 0;
        int final_noise_degree = 0;

        Ciphertext<DCRTPoly> ct_all_dists;
        int global_offset = 0;
        std::map<int, int> slot_to_vec_id;

        const int B = slots / config.dimension;
        const double penalty = 1.0e6;

        auto t2 = t1;
        bool first_accum = true;

        if (config.interactive)
        {   
            // -----------------------------------------------------------------------
            // encryted, interactive mode
            // -----------------------------------------------------------------------
            auto selected_clusters = select_clusters_interactive(ctx_mgr, ct_compact, config);
            t2 = std::chrono::high_resolution_clock::now();

            #ifdef ENABLE_LOGGING
            std::cout << "### Top-n_probe Centroid Selection done interactively!" << std::endl;
            std::cout << "time = " << std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1) << " ms" << std::endl;
            std::cout << "--------------------------------------------------------------" << std::endl;
            #endif

            auto ct_query_dimpack = ctx_mgr.encrypt_query_dimpack(query, config);

            for (int c : selected_clusters)
            {
                const auto& c_vids = m_cluster_vector_ids[c];
                if (c_vids.empty()) continue;

                for (int batch_start = 0; batch_start < static_cast<int>(c_vids.size()); batch_start += B)
                {
                    int batch_end = std::min(batch_start + B, static_cast<int>(c_vids.size()));
                    std::vector<int> batch_ids(c_vids.begin() + batch_start, c_vids.begin() + batch_end);
                    int batch_count = static_cast<int>(batch_ids.size());

                    auto ct_dists = compute_batch_distances_dimpack(cc, ct_query_dimpack, batch_ids, config);

                    std::vector<double> mask_batch(slots, 0.0);
                    std::fill(mask_batch.begin(), mask_batch.begin() + c_vids.size(), 1.0);
                    auto pt_mask_batch = cc->MakeCKKSPackedPlaintext(mask_batch, 1, ct_dists->GetLevel());
                    auto ct_dists_batch = cc->EvalMult(ct_dists, pt_mask_batch);
                    ct_dists_batch = cc->Rescale(ct_dists_batch);
                    
                    auto ct_shifted = rotate(cc, ct_dists_batch, -global_offset);
                    
                    if (first_accum) 
                    {
                        ct_all_dists = ct_shifted;
                        first_accum = false;
                    } 
                    else 
                    {
                        ct_all_dists = cc->EvalAdd(ct_all_dists, ct_shifted);
                    }

                    for (int j = 0; j < batch_count; ++j) 
                    {
                        slot_to_vec_id[global_offset + j] = batch_ids[j];
                    }
                    global_offset += batch_count;
                }
            }
        }
        else if(config.add_penalty)
        {
            // -----------------------------------------------------------------------
            // encryted, (non-interactive) full mode, adding penalty using coarse centroids
            // -----------------------------------------------------------------------
            std::vector<Ciphertext<DCRTPoly>> ct_m_c_vec;

            auto ct_mask_coarse = rank_top_nprobe_centroids(cc, ct_compact, dist_bound, config);
                
            #ifdef ENABLE_LOGGING
            std::cout << "*** after rank_top_nprobe_centroids()" << std::endl;
            std::cout << "Current Level: " << ct_mask_coarse->GetLevel() << std::endl;
            std::cout << "Noise Scale Deg: " << ct_mask_coarse->GetNoiseScaleDeg() << std::endl;
            ctx_mgr.decrypt_and_print_vector(ct_mask_coarse, "ct_mask_coarse", config.n_list, true);
            #endif
            
            if (stats) stats->level_coarse_rank = ct_mask_coarse->GetLevel();

            t2 = std::chrono::high_resolution_clock::now();

            #ifdef ENABLE_LOGGING
            std::cout << "### Top-n_probe Centroid Ranking done!" << std::endl;
            std::cout << "time = " << std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1) << " ms" << std::endl;
            std::cout << "--------------------------------------------------------------" << std::endl;
            #endif

            ct_m_c_vec.resize(config.n_list);
            for (int c = 0; c < config.n_list; ++c)
            {
                std::vector<double> mask_c(slots, 0.0);
                mask_c[c] = 1.0;
                auto pt_mask_c = cc->MakeCKKSPackedPlaintext(mask_c, 1, ct_mask_coarse->GetLevel());
                auto ct_m_c_slot = cc->EvalMult(ct_mask_coarse, pt_mask_c);
                ct_m_c_slot = cc->Rescale(ct_m_c_slot);

                if (c != 0)
                {
                    ct_m_c_slot = rotate(cc, ct_m_c_slot, c);
                }

                for (int stride = 1; stride < B; stride *= 2)
                {
                    auto shifted = cc->EvalRotate(ct_m_c_slot, -stride);
                    ct_m_c_slot = cc->EvalAdd(ct_m_c_slot, shifted);
                }

                ct_m_c_vec[c] = ct_m_c_slot;
            }

            auto ct_query_dimpack = ctx_mgr.encrypt_query_dimpack(query, config);

            #ifdef ENABLE_LOGGING
            std::cout << "### Pre-extracted " << config.n_list << " probe mask scalars." << std::endl;
            #endif

            for (int c = 0; c < config.n_list; ++c)
            {
                const auto& c_vids = m_cluster_vector_ids[c];
                if (c_vids.empty()) continue;

                for (int batch_start = 0; batch_start < static_cast<int>(c_vids.size()); batch_start += B)
                {
                    int batch_end = std::min(batch_start + B, static_cast<int>(c_vids.size()));
                    std::vector<int> batch_ids(c_vids.begin() + batch_start, c_vids.begin() + batch_end);
                    int batch_count = static_cast<int>(batch_ids.size());

                    auto ct_dists = compute_batch_distances_dimpack(cc, ct_query_dimpack, batch_ids, config);
                    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> ct_shifted;

                    auto ct_penalized = apply_probe_penalty(cc, ct_dists, ct_m_c_vec[c], penalty, config);

                    std::vector<double> mask_batch(slots, 0.0);
                    std::fill(mask_batch.begin(), mask_batch.begin() + batch_count, 1.0);
                    auto pt_mask_batch = cc->MakeCKKSPackedPlaintext(mask_batch, 1, ct_penalized->GetLevel());
                    auto ct_penalized_batch = cc->EvalMult(ct_penalized, pt_mask_batch);
                    ct_penalized_batch = cc->Rescale(ct_penalized_batch);
                    
                    ct_shifted = rotate(cc, ct_penalized_batch, -global_offset);
                    
                    if (first_accum) 
                    {
                        ct_all_dists = ct_shifted;
                        first_accum = false;
                    } 
                    else 
                    {
                        ct_all_dists = cc->EvalAdd(ct_all_dists, ct_shifted);
                    }

                    for (int j = 0; j < batch_count; ++j) 
                    {
                        slot_to_vec_id[global_offset + j] = batch_ids[j];
                    }
                    global_offset += batch_count;

                    #ifdef ENABLE_LOGGING
                    std::cout << "  Cluster " << c << " batch [" << batch_start << ".." << batch_end
                            << ") processed (" << batch_count << " candidates)" << std::endl;
                    #endif
                }
            }

            if (stats && config.n_list > 0) stats->level_lut_dist = ct_all_dists->GetLevel();
        }
        else
        {
            // -----------------------------------------------------------------------
            // encryted, (non-interactive) full mode, no coarse computation
            // this is the nearest neighbor exact solution
            // -----------------------------------------------------------------------

            auto ct_query_dimpack = ctx_mgr.encrypt_query_dimpack(query, config);

            for (int batch_start = 0; batch_start < m_num_vectors; batch_start += B)
            {
                int batch_end = std::min(batch_start + B, m_num_vectors);
                int batch_count = batch_end - batch_start;
                std::vector<int> batch_ids(batch_count);
                std::iota(batch_ids.begin(), batch_ids.end(), batch_start);

                auto ct_dists = compute_batch_distances_dimpack(cc, ct_query_dimpack, batch_ids, config);
                lbcrypto::Ciphertext<lbcrypto::DCRTPoly> ct_shifted;

                std::vector<double> mask_batch(slots, 0.0);
                std::fill(mask_batch.begin(), mask_batch.begin() + batch_count, 1.0);

                auto pt_mask_batch = cc->MakeCKKSPackedPlaintext(mask_batch, 1, ct_dists->GetLevel());
                auto ct_dists_batch = cc->EvalMult(ct_dists, pt_mask_batch);
                ct_dists_batch = cc->Rescale(ct_dists_batch);
                
                ct_shifted = rotate(cc, ct_dists_batch, -global_offset);
                
                if (first_accum) 
                {
                    ct_all_dists = ct_shifted;
                    first_accum = false;
                } 
                else 
                {
                    ct_all_dists = cc->EvalAdd(ct_all_dists, ct_shifted);
                }

                for (int j = 0; j < batch_count; ++j) 
                {
                    slot_to_vec_id[global_offset + j] = batch_ids[j];
                }
                global_offset += batch_count;

                #ifdef ENABLE_LOGGING
                std::cout << " Batch [" << batch_start << ".." << batch_end
                        << ") processed (" << batch_count << " candidates)" << std::endl;
                #endif
            }

            if (stats && config.n_list > 0) stats->level_lut_dist = ct_all_dists->GetLevel();
        }

        if (!first_accum) 
        {
            auto dec_dists = ctx_mgr.decrypt_vector(ct_all_dists, global_offset);
            final_mult_level = ct_all_dists->GetLevel();
            final_noise_degree = ct_all_dists->GetNoiseScaleDeg();
            for (int i = 0; i < global_offset; ++i) 
            {
                all_candidates.emplace_back(slot_to_vec_id[i], static_cast<float>(dec_dists[i]));
            }
        }

        auto t3 = std::chrono::high_resolution_clock::now();

        #ifdef ENABLE_LOGGING
        std::cout << "### Distance computation done!" << std::endl;
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
            if (config.interactive || (!config.add_penalty) || (all_candidates[i].second < penalty * 0.5f))
            {
                results.push_back(all_candidates[i]);
            }
        }

        if (stats && !results.empty()) stats->level_fine_rank = 0;  // Client-side, no FHE depth
        auto t4 = std::chrono::high_resolution_clock::now();

        #ifdef ENABLE_LOGGING
        std::cout << "### Client-side top-k selection done!" << std::endl;
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
