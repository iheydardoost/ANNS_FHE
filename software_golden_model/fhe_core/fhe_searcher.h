#ifndef FHE_SEARCHER_H
#define FHE_SEARCHER_H

#include "openfhe.h"
#include "fhe_config.h"
#include "fhe_context_manager.h"
#include <vector>
#include <cstdint>
#include <utility>   // std::pair

namespace anns_fhe
{

    /**
     * Struct for recording ciphertext levels across pipeline stages (silent tracking).
     */
    struct FHESearchStats
    {
        int level_coarse_dist = 0;   // Step 1: Coarse centroid distances
        int level_coarse_rank = 0;   // Step 2: Top-n_probe centroid ranking
        int level_lut_dist    = 0;   // Step 3: ADC LUT & candidate block distances
        int level_fine_rank   = 0;   // Step 4: Block-wise top-k candidate selection
    };

    /**
     * FHESearcher — Online query engine for encrypted IVF-PQ ANNS.
     *
     * Implements the full search pipeline:
     *   Step 1: Coarse SIMD-packed centroid distances (32 centroids, Depth 2)
     *   Step 2: Top-n_probe centroid ranking (N=32 matrix, Depth 9)
     *   Step 3: ADC LUT construction & block distance computation (Depth 2)
     *   Step 4: Block-wise Top-k candidate selection (N=64 matrix, Depth 5)
     */
    class FHESearcher
    {
    public:
        FHESearcher() = default;

        // Load plaintext index data (PQ codes, IVF assignments, centroid coords).
        bool load_plaintext_data(const FHEConfig& config);

        /**
         * Full search: returns top_k (plaintext_index, approx_distance) pairs.
         *
         * @param ctx_mgr   Loaded FHEContextManager
         * @param query     Raw float query vector
         * @param top_k     Number of nearest neighbors to return
         * @param config    Runtime config
         * @param timings   Optional output: [coarse_ms, fine_ms] per-stage timings
         * @param stats     Optional output: FHESearchStats tracking levels
         */
        std::vector<std::pair<int, float>> search(
            const FHEContextManager& ctx_mgr,
            const std::vector<float>& query,
            int top_k,
            const FHEConfig& config,
            std::vector<double>* timings = nullptr,
            FHESearchStats* stats = nullptr) const;

        int num_vectors() const { return m_num_vectors; }

    private:

        // Helper: Decompose arbitrary rotation into binary power-of-2 shifts
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> rotate(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct,
            int rot) const;

        // Helper function to bring NoiseScaleDeg back to 1
        void normalize_scale(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
             lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct) const
        {
            while (ct->GetNoiseScaleDeg() > 1)
            {
                ct = cc->Rescale(ct);
            }
        }

        // -----------------------------------------------------------------------
        // Step 1: Coarse centroid distance computation (Depth 2)
        // -----------------------------------------------------------------------
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> compute_coarse_distances(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_query,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_centroids,
            const FHEConfig& config) const;

        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> compact_distances(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_strided,
            const FHEConfig& config) const;

        // -----------------------------------------------------------------------
        // Step 2: Top-n_probe centroid ranking (N=32 Matrix, Depth 9)
        // -----------------------------------------------------------------------
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> rank_top_nprobe_centroids(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_compact_dists,
            double dist_bound,
            const FHEConfig& config) const;

        // -----------------------------------------------------------------------
        // Step 3: ADC LUT construction & block distance computation (Depth 2)
        // -----------------------------------------------------------------------
        std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>> build_adc_lut(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_query,
            const std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>>& ct_codebooks,
            int centroid_id,
            const FHEConfig& config) const;

        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> compute_candidate_distance(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>>& lut,
            int vector_idx,
            const FHEConfig& config,
            std::vector<double>& mask_buffer) const;

        // -----------------------------------------------------------------------
        // Step 4: Block-wise Top-k candidate selection (N=64 Blocks, Depth 5)
        // -----------------------------------------------------------------------
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> filter_block_top_k(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_block_dists,
            int top_k,
            double dist_bound,
            const FHEConfig& config) const;

        // -----------------------------------------------------------------------
        // Plaintext index data (not encrypted)
        // -----------------------------------------------------------------------
        std::vector<int32_t>  m_assignments;      // ivf_assignments.bin: vector → centroid
        std::vector<uint16_t> m_pq_codes;         // pq_codes.bin: vector × M codes
        std::vector<float>    m_centroids_plain;  // ivf_centroids.bin: raw centroids

        std::vector<std::vector<int>> m_inverted_lists;
        int m_num_vectors = 0;
    };

} // namespace anns_fhe

#endif // FHE_SEARCHER_H
