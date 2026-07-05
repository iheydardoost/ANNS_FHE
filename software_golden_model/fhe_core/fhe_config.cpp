#include "fhe_config.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace anns_fhe {

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n\"{}[]");
    if (std::string::npos == first) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\r\n\"{}[]:,");
    return str.substr(first, (last - first + 1));
}

bool FHEConfig::load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open config file: " << filepath << std::endl;
        return false;
    }

    std::string line;
    std::string current_section = "";

    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        // Check if section header starts (contains section name and opening brace '{')
        if (line.find("dataset") != std::string::npos && line.find('{') != std::string::npos) {
            current_section = "dataset";
            continue;
        } else if (line.find("ivf") != std::string::npos && line.find('{') != std::string::npos) {
            current_section = "ivf";
            continue;
        } else if (line.find("pq") != std::string::npos && line.find('{') != std::string::npos) {
            current_section = "pq";
            continue;
        } else if (line.find("encryption") != std::string::npos && line.find('{') != std::string::npos) {
            current_section = "encryption";
            continue;
        }

        size_t colon_pos = line.find(':');
        if (colon_pos == std::string::npos) continue;

        std::string key = trim(line.substr(0, colon_pos));
        std::string val = trim(line.substr(colon_pos + 1));

        if (key.empty() || val.empty()) continue;

        if (current_section == "") {
            if (key == "project_root") project_root = val;
        } else if (current_section == "dataset") {
            if (key == "path") dataset_path = val;
            else if (key == "query_path") query_path = val;
            else if (key == "groundtruth_path") groundtruth_path = val;
            else if (key == "dimension") dimension = std::stoi(val);
            else if (key == "models_output_dir") models_output_dir = val;
            else if (key == "encoding_output_dir") encoding_output_dir = val;
        } else if (current_section == "ivf") {
            if (key == "n_list") n_list = std::stoi(val);
        } else if (current_section == "pq") {
            if (key == "m_subvectors") m_subvectors = std::stoi(val);
            else if (key == "k_subcentroids") k_subcentroids = std::stoi(val);
        } else if (current_section == "encryption") {
            if (key == "enabled") encryption_enabled = (val == "true" || val == "1");
            else if (key == "use_encryption") use_encryption = (val == "true" || val == "1");
            else if (key == "scheme") /* CKKS */;
            else if (key == "poly_modulus_degree") poly_modulus_degree = std::stoi(val);
            else if (key == "coeff_modulus_bits") {
                // If it is array style, e.g. [60, 40, 40, 40, 40, 40, 40, 60]
                // Let's strip brackets and parse comma separated values
                std::string bits_str = val;
                bits_str.erase(std::remove(bits_str.begin(), bits_str.end(), '['), bits_str.end());
                bits_str.erase(std::remove(bits_str.begin(), bits_str.end(), ']'), bits_str.end());
                std::stringstream ss(bits_str);
                std::string num;
                coeff_modulus_bits.clear();
                while (std::getline(ss, num, ',')) {
                    coeff_modulus_bits.push_back(std::stoi(trim(num)));
                }
            }
            else if (key == "scale_bits") scale_bits = std::stoi(val);
            else if (key == "security_level") security_level = val;
            else if (key == "n_probe") n_probe = std::stoi(val);
            else if (key == "interactive_top_k") interactive_top_k = (val == "true" || val == "1");
            else if (key == "sign_approx_method") sign_approx_method = val;
            else if (key == "composition_iterations") composition_iterations = std::stoi(val);
        }
    }
    
    // Set default coeff_modulus_bits if not specified
    if (coeff_modulus_bits.empty()) {
        coeff_modulus_bits = {60, 40, 40, 40, 40, 40, 40, 60};
    }

    return true;
}

std::string FHEConfig::resolve_path(const std::string& path) const {
    if (std::filesystem::path(path).is_absolute()) {
        return path;
    }
    std::filesystem::path root(project_root);
    return (root / path).lexically_normal().string();
}

} // namespace anns_fhe
