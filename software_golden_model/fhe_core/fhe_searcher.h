#ifndef FHE_SEARCHER_H
#define FHE_SEARCHER_H

#include <vector>
#include <string>
#include <utility>
#include <cstdint>
#include "openfhe.h"
#include "fhe_config.h"
#include "fhe_context_manager.h"

namespace anns_fhe {

class FHESearcher {
public:
    FHESearcher() = default;
    ~FHESearcher() = default;

    bool load_data(const FHEConfig& config);

    // Runs a private search using homomorphic encryption.
    // In interactive mode, it returns the top-k results after decrypting and sorting.
    // In fully homomorphic mode, it performs CAS homomorphically on a small candidate set.
    std::vector<std::pair<int, float>> search(
        const FHEContextManager& ctx_mgr,
        const std::vector<float>& query,
        int n_probe,
        int top_k,
        const FHEConfig& config) const;

    // Helper to evaluate the absolute value of a ciphertext: |x| = x * sign(x)
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> eval_abs(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> cc,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_x,
        const FHEConfig& config) const;

    // Compare and Swap: takes ct_a, ct_b and returns {ct_min, ct_max}
    std::pair<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>, lbcrypto::Ciphertext<lbcrypto::DCRTPoly>> compare_and_swap(
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> cc,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_a,
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct_b,
        const FHEConfig& config) const;

private:
    std::vector<float> m_centroids;       // Shape: (n_list, dimension)
    std::vector<float> m_codebooks;       // Shape: (m_subvectors, k_subcentroids, sub_dim)
    std::vector<int32_t> m_assignments;   // Shape: (num_vectors)
    std::vector<uint8_t> m_pq_codes;      // Shape: (num_vectors, m_subvectors)

    int m_n_list = 0;
    int m_dimension = 0;
    int m_m_subvectors = 0;
    int m_k_subcentroids = 0;
    int m_sub_dim = 0;
    int m_num_vectors = 0;
};

} // namespace anns_fhe

#endif // FHE_SEARCHER_H
