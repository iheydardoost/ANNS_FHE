#ifndef FHE_CONFIG_H
#define FHE_CONFIG_H

#include <string>
#include <vector>
#include <cstdint>

namespace anns_fhe
{

    struct FHEConfig
    {
        std::string project_root;

        // Dataset paths
        std::string dataset_path;
        std::string query_path;
        std::string groundtruth_path;
        int dimension = 128;
        std::string models_output_dir;
        std::string encoding_output_dir;

        // IVF & PQ parameters
        int n_list = 32;
        int m_subvectors = 8;
        int k_subcentroids = 256;

        // FHE parameters
        bool encryption_enabled = false;
        bool use_encryption = true;
        int poly_modulus_degree = 16384;
        int scale_bits = 40;
        std::string security_level = "HEStd_128_classic";
        int multiplicative_depth = 40;
        int n_probe = 2;
        std::string sign_approx_method = "chebyshev";
        int chebyshev_degree = 13;
        int composition_iterations = 3;   // kept for backward compat with "composition" method
        std::string serialization_dir;
        double ram_limit_gb = 32.0;

        // Load config from JSON file
        bool load(const std::string& filepath);

        // Helper to resolve paths relative to project root
        std::string resolve_path(const std::string& path) const;
    };

} // namespace anns_fhe

#endif // FHE_CONFIG_H
