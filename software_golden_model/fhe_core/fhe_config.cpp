#include "fhe_config.h"
#include "third_party/nlohmann/json.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>

using json = nlohmann::json;

namespace anns_fhe
{

    bool FHEConfig::load(const std::string& filepath)
    {
        std::ifstream file(filepath);
        if (!file.is_open())
        {
            std::cerr << "Error: Could not open config file: " << filepath << std::endl;
            return false;
        }

        json j;
        try
        {
            file >> j;
        }
        catch (const json::parse_error& e)
        {
            std::cerr << "Error: Failed to parse config JSON: " << e.what() << std::endl;
            return false;
        }

        // --- Top-level ---
        if (j.contains("project_root"))
            project_root = j["project_root"].get<std::string>();

        // --- Dataset ---
        if (j.contains("dataset"))
        {
            const auto& d = j["dataset"];
            if (d.contains("path"))               dataset_path       = d["path"].get<std::string>();
            if (d.contains("query_path"))         query_path         = d["query_path"].get<std::string>();
            if (d.contains("groundtruth_path"))   groundtruth_path   = d["groundtruth_path"].get<std::string>();
            if (d.contains("dimension"))          dimension          = d["dimension"].get<int>();
            if (d.contains("models_output_dir"))  models_output_dir  = d["models_output_dir"].get<std::string>();
            if (d.contains("encoding_output_dir"))encoding_output_dir= d["encoding_output_dir"].get<std::string>();
        }

        // --- IVF ---
        if (j.contains("ivf"))
        {
            const auto& ivf = j["ivf"];
            if (ivf.contains("n_list")) n_list = ivf["n_list"].get<int>();
        }

        // --- PQ ---
        if (j.contains("pq"))
        {
            const auto& pq = j["pq"];
            if (pq.contains("m_subvectors"))  m_subvectors  = pq["m_subvectors"].get<int>();
            if (pq.contains("k_subcentroids")) k_subcentroids = pq["k_subcentroids"].get<int>();
        }

        // --- Encryption ---
        if (j.contains("encryption"))
        {
            const auto& enc = j["encryption"];
            if (enc.contains("enabled"))             encryption_enabled   = enc["enabled"].get<bool>();
            if (enc.contains("use_encryption"))      use_encryption       = enc["use_encryption"].get<bool>();
            if (enc.contains("poly_modulus_degree")) poly_modulus_degree  = enc["poly_modulus_degree"].get<int>();
            if (enc.contains("scale_bits"))          scale_bits           = enc["scale_bits"].get<int>();
            if (enc.contains("security_level"))      security_level       = enc["security_level"].get<std::string>();
            if (enc.contains("multiplicative_depth"))multiplicative_depth = enc["multiplicative_depth"].get<int>();
            if (enc.contains("n_probe"))             n_probe              = enc["n_probe"].get<int>();
            if (enc.contains("sign_approx_method"))  sign_approx_method   = enc["sign_approx_method"].get<std::string>();
            if (enc.contains("chebyshev_degree"))    chebyshev_degree     = enc["chebyshev_degree"].get<int>();
            if (enc.contains("composition_iterations")) composition_iterations = enc["composition_iterations"].get<int>();
            if (enc.contains("serialization_dir"))   serialization_dir    = enc["serialization_dir"].get<std::string>();
            if (enc.contains("ram_limit_gb"))        ram_limit_gb         = enc["ram_limit_gb"].get<double>();
        }

        return true;
    }

    std::string FHEConfig::resolve_path(const std::string& path) const
    {
        if (std::filesystem::path(path).is_absolute())
        {
            return path;
        }
        std::filesystem::path root(project_root);
        return (root / path).lexically_normal().string();
    }

} // namespace anns_fhe
