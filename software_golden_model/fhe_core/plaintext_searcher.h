#ifndef PLAINTEXT_SEARCHER_H
#define PLAINTEXT_SEARCHER_H

#include <vector>
#include <string>
#include <utility>
#include <cstdint>
#include "fhe_config.h"

namespace anns_fhe {

class PlaintextSearcher {
public:
    PlaintextSearcher() = default;
    ~PlaintextSearcher() = default;

    bool load_data(const FHEConfig& config);

    // Runs a single query and returns the top-k nearest neighbors as (index, distance) pairs
    std::vector<std::pair<int, float>> search(const std::vector<float>& query, int n_probe, int top_k) const;

    int get_num_vectors() const { return m_num_vectors; }

private:
    std::vector<float> m_centroids;       // Shape: (n_list, dimension)
    std::vector<float> m_codebooks;       // Shape: (m_subvectors, k_subcentroids, sub_dim)
    std::vector<int32_t> m_assignments;   // Shape: (num_vectors)
    std::vector<uint16_t> m_pq_codes;      // Shape: (num_vectors, m_subvectors)

    int m_n_list = 0;
    int m_dimension = 0;
    int m_m_subvectors = 0;
    int m_k_subcentroids = 0;
    int m_sub_dim = 0;
    int m_num_vectors = 0;
};

} // namespace anns_fhe

#endif // PLAINTEXT_SEARCHER_H
