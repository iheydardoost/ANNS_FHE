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
     * FHESearcher — Online query engine for encrypted IVF-PQ ANNS.
     *
     * Implements the full search pipeline:
     *   Stage 1: Coarse SIMD-packed centroid distances
     *   Stage 2: HMR coarse ranking (USENIX '25 Mazzone et al.) & Top-K Indicator
     *   Stage 3: ADC fine LUT construction & candidate distance computation
     *   Stage 4: HMR fine ranking on candidate batches & hierarchical merge-filtering
     *
     * All ciphertexts passed as const Ciphertext<DCRTPoly>& to avoid deep copies.
     * No decryption while query search or ranking; indices remain plaintext.
     */
    class FHESearcher
    {
    public:
        FHESearcher() = default;

        // Load plaintext index data (PQ codes, IVF assignments, centroid coords).
        // This data is NOT encrypted — IDs and codes stay plaintext.
        bool load_plaintext_data(const FHEConfig& config);

        /**
         * Full search: returns top_k (plaintext_index, approx_distance) pairs.
         *
         * @param ctx_mgr   Loaded FHEContextManager (has cc, keys, encrypted index)
         * @param query     Raw float query vector (dimension = config.dimension)
         * @param top_k     Number of nearest neighbors to return
         * @param config    Runtime config
         * @param timings   Optional output: [coarse_ms, fine_ms] per-stage timings
         */
        std::vector<std::pair<int, float>> search(
            const FHEContextManager& ctx_mgr,
            const std::vector<float>& query,
            int top_k,
            const FHEConfig& config,
            std::vector<double>* timings = nullptr) const;

        int num_vectors() const { return m_num_vectors; }

    private:
        // Helper: Decompose arbitrary rotation into binary power-of-2 shifts
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> rotate(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct,
            int rot) const;

        // -----------------------------------------------------------------------
        // Stage 1: Coarse centroid distance computation
        // ct_query layout:  [q_d0..q_d127, q_d0..q_d127, ...(×n_list)]
        // ct_centroids layout: [c0_d0..c0_d127, c1_d0..c1_d127, ...]
        // Output: distances packed at stride=dim within the ciphertext
        // -----------------------------------------------------------------------
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> compute_coarse_distances(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_query,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_centroids,
            const FHEConfig& config) const;

        // Extract n_list scalar distances from stride-dim representation
        // and re-pack them into slots [0..n_list-1].
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> compact_distances(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_strided,
            const FHEConfig& config) const;

        // -----------------------------------------------------------------------
        // Stage 2 & 4: HMR Ranking (Mazzone et al. USENIX '25)
        // Input:  ct_distances with n scalars packed in slots [0..n-1]
        // Output: ct_ranks with rank[i] = count of j where distances[i] > distances[j] + 1
        // -----------------------------------------------------------------------
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> hmr_rank(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_distances,
            int n,
            double dist_bound,
            const FHEConfig& config) const;

        // HMR sub-operations (all consume 0 mult-levels except transpose_row)
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> replicate_row(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct,
            int matrix_size) const;

        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> transpose_row(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct,
            int matrix_size) const;

        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> replicate_column(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct,
            int matrix_size) const;

        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> sum_rows(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct,
            int matrix_size) const;

        // -----------------------------------------------------------------------
        // Stage 3: ADC LUT construction and lookup
        // -----------------------------------------------------------------------
        // Build M encrypted LUTs for a given centroid.
        // lut[m] contains K squared sub-distances in slots {0, sub_dim, 2*sub_dim, ...}
        std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>> build_adc_lut(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_query,
            const std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>>& ct_codebooks,
            int centroid_id,
            const FHEConfig& config) const;

        // Look up the total ADC distance for one candidate vector using plaintext PQ codes.
        // Returns a ciphertext with the summed squared distance in slot 0.
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> compute_candidate_distance(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>>& lut,
            int vector_idx,
            const FHEConfig& config,
            std::vector<double>& mask_buffer) const;

        // -----------------------------------------------------------------------
        // Stage 4: Fine ranking and batch merge-filtering via HMR + Top-K Indicator
        // -----------------------------------------------------------------------
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> hmr_filter_batches(
            const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
            const std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>>& cand_dists,
            const std::vector<int>& cand_ids,
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_mask_coarse,
            int top_k,
            double dist_bound,
            const FHEConfig& config,
            std::vector<int>& out_batch_ids) const;

        // -----------------------------------------------------------------------
        // Plaintext index data (not encrypted)
        // -----------------------------------------------------------------------
        std::vector<int32_t>  m_assignments;      // ivf_assignments.bin: vector → centroid
        std::vector<uint16_t> m_pq_codes;         // pq_codes.bin: vector × M codes
        std::vector<float>    m_centroids_plain;  // ivf_centroids.bin: raw centroids (for ADC residual)

        // Precomputed: inverted lists (centroid → list of vector indices)
        std::vector<std::vector<int>> m_inverted_lists;

        int m_num_vectors  = 0;
    };

} // namespace anns_fhe

#endif // FHE_SEARCHER_H
