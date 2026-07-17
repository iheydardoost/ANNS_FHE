#include "plaintext_searcher.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <unordered_set>

namespace anns_fhe
{

    bool PlaintextSearcher::load_data(const FHEConfig& config)
    {
        m_n_list = config.n_list;
        m_dimension = config.dimension;
        m_m_subvectors = config.m_subvectors;
        m_k_subcentroids = config.k_subcentroids;
        m_sub_dim = m_dimension / m_m_subvectors;

        std::string centroids_path = config.resolve_path(config.models_output_dir + "/ivf_centroids.bin");
        std::string codebooks_path = config.resolve_path(config.models_output_dir + "/pq_codebooks.bin");
        std::string assignments_path = config.resolve_path(config.encoding_output_dir + "/ivf_assignments.bin");
        std::string codes_path = config.resolve_path(config.encoding_output_dir + "/pq_codes.bin");

        // Helper lambda to load binary file
        auto load_bin = [](const std::string& path, auto& vec) -> bool {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if(!file.is_open())
            {
                std::cerr << "Error: Could not open binary file: " << path << std::endl;
                return false;
            }
            size_t size = file.tellg();
            file.seekg(0, std::ios::beg);
            vec.resize(size / sizeof(typename std::decay<decltype(vec)>::type::value_type));
            file.read(reinterpret_cast<char*>(vec.data()), size);
            return true;
            };

        if(!load_bin(centroids_path, m_centroids) ||
            !load_bin(codebooks_path, m_codebooks) ||
            !load_bin(assignments_path, m_assignments))
        {
            return false;
        }

        // Load m_pq_codes based on subcentroids count
        {
            std::ifstream file(codes_path, std::ios::binary | std::ios::ate);
            if(!file.is_open())
            {
                std::cerr << "Error: Could not open binary file: " << codes_path << std::endl;
                return false;
            }
            size_t file_size = file.tellg();
            file.seekg(0, std::ios::beg);

            if(m_k_subcentroids <= 256)
            {
                std::vector<uint8_t> temp_codes(file_size);
                file.read(reinterpret_cast<char*>(temp_codes.data()), file_size);
                m_pq_codes.assign(temp_codes.begin(), temp_codes.end());
            }
            else
            {
                size_t num_elements = file_size / sizeof(uint16_t);
                m_pq_codes.resize(num_elements);
                file.read(reinterpret_cast<char*>(m_pq_codes.data()), file_size);
            }
        }

        m_num_vectors = m_assignments.size();

        // Verify sizes
        if(m_centroids.size() != static_cast<size_t>(m_n_list * m_dimension))
        {
            std::cerr << "Error: Centroids size mismatch. Expected " << m_n_list * m_dimension << ", got " << m_centroids.size() << std::endl;
            return false;
        }
        if(m_codebooks.size() != static_cast<size_t>(m_m_subvectors * m_k_subcentroids * m_sub_dim))
        {
            std::cerr << "Error: Codebooks size mismatch. Got " << m_codebooks.size() << std::endl;
            return false;
        }
        if(m_pq_codes.size() != static_cast<size_t>(m_num_vectors * m_m_subvectors))
        {
            std::cerr << "Error: PQ codes size mismatch. Got " << m_pq_codes.size() << std::endl;
            return false;
        }
        m_inverted_lists.clear();
        m_inverted_lists.resize(m_n_list);
        for (int i = 0; i < m_num_vectors; ++i)
        {
            m_inverted_lists[m_assignments[i]].push_back(i);
        }

        std::cout << "[PlaintextSearcher] Successfully loaded " << m_num_vectors << " vectors from index." << std::endl;
        return true;
    }

    std::vector<std::pair<int, float>> PlaintextSearcher::search(const std::vector<float>& query, int n_probe, int top_k, PlaintextScratch& scratch) const
    {

        // 1. Coarse Quantization: Find closest n_probe centroids
        scratch.coarse_distances.resize(m_n_list);
        for(int i = 0; i < m_n_list; ++i)
        {
            float dist = 0.0f;
            for(int d = 0; d < m_dimension; ++d)
            {
                float diff = query[d] - m_centroids[i * m_dimension + d];
                dist += diff * diff;
            }
            scratch.coarse_distances[i] = { i, dist };
        }

        std::sort(scratch.coarse_distances.begin(), scratch.coarse_distances.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

        int actual_probes = std::min(n_probe, m_n_list);
        scratch.probed_centroids.resize(actual_probes);
        for(int p = 0; p < actual_probes; ++p)
        {
            scratch.probed_centroids[p] = scratch.coarse_distances[p].first;
        }

        // 2. Identify Candidate database IDs in probed clusters using precomputed inverted lists
        scratch.candidates.clear();
        for(int cid : scratch.probed_centroids)
        {
            scratch.candidates.insert(scratch.candidates.end(), m_inverted_lists[cid].begin(), m_inverted_lists[cid].end());
        }

        if(scratch.candidates.empty())
        {
            return {};
        }

        // 3. Compute ADC Distance Lookup Tables (LUT)
        // Structure: LUT[centroid_idx][subvector_idx][subcentroid_idx]
        // Since we only query probed centroids, we can map centroid_idx to a local index [0...n_probe-1]
        scratch.centroid_to_local.assign(m_n_list, -1);
        int local_idx = 0;
        for(int cid : scratch.probed_centroids)
        {
            scratch.centroid_to_local[cid] = local_idx++;
        }

        scratch.lut.assign(actual_probes * m_m_subvectors * m_k_subcentroids, 0.0f);

        for(int cid : scratch.probed_centroids)
        {
            int l_cid = scratch.centroid_to_local[cid];

            // Compute query residual: q - centroid
            scratch.q_res.resize(m_dimension);
            for(int d = 0; d < m_dimension; ++d)
            {
                scratch.q_res[d] = query[d] - m_centroids[cid * m_dimension + d];
            }

            // Compute distances to subcentroids
            for(int m = 0; m < m_m_subvectors; ++m)
            {
                int start_col = m * m_sub_dim;

                for(int k = 0; k < m_k_subcentroids; ++k)
                {
                    float dist = 0.0f;
                    int cb_offset = m * m_k_subcentroids * m_sub_dim + k * m_sub_dim;

                    for(int d = 0; d < m_sub_dim; ++d)
                    {
                        float diff = scratch.q_res[start_col + d] - m_codebooks[cb_offset + d];
                        dist += diff * diff;
                    }

                    int lut_offset = l_cid * m_m_subvectors * m_k_subcentroids + m * m_k_subcentroids + k;
                    scratch.lut[lut_offset] = dist;
                }
            }
        }

        // 4. Distance aggregation for candidates
        std::vector<std::pair<int, float>> results(scratch.candidates.size());
        for(size_t i = 0; i < scratch.candidates.size(); ++i)
        {
            int idx = scratch.candidates[i];
            int cid = m_assignments[idx];
            int l_cid = scratch.centroid_to_local[cid];

            float dist = 0.0f;
            for(int m = 0; m < m_m_subvectors; ++m)
            {
                uint16_t code = m_pq_codes[idx * m_m_subvectors + m];
                int lut_offset = l_cid * m_m_subvectors * m_k_subcentroids + m * m_k_subcentroids + code;
                dist += scratch.lut[lut_offset];
            }
            results[i] = { idx, dist };
        }

        // 5. Select Top-K candidates
        if(results.size() > static_cast<size_t>(top_k))
        {
            std::nth_element(results.begin(), results.begin() + top_k, results.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            results.resize(top_k);
        }

        std::sort(results.begin(), results.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

        return results;
    }

} // namespace anns_fhe
