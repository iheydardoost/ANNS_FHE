#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <fstream>
#include <stdexcept>
#include <filesystem>
#include <thread>
#include <atomic>
#include <omp.h>
#include "fhe_config.h"
#include "fhe_context_manager.h"
#include "plaintext_searcher.h"
#include "fhe_searcher.h"

using namespace anns_fhe;
using namespace lbcrypto;

// Helper to load all SIFT query vectors from an fvecs file
std::vector<std::vector<float>> load_all_query_vectors(const std::string& path, int expected_dim)
{
    std::ifstream file(path, std::ios::binary);
    if(!file.is_open())
    {
        throw std::runtime_error("Could not open query file: " + path);
    }

    std::vector<std::vector<float>> queries;
    while(true)
    {
        int dim = 0;
        file.read(reinterpret_cast<char*>(&dim), sizeof(int));
        if(file.eof() || file.fail())
        {
            break;
        }
        if(dim != expected_dim)
        {
            throw std::runtime_error("Dimension mismatch in query file.");
        }
        std::vector<float> query(expected_dim);
        file.read(reinterpret_cast<char*>(query.data()), expected_dim * sizeof(float));
        if(file.fail())
        {
            throw std::runtime_error("Failed to read query vector data.");
        }
        queries.push_back(query);
    }
    return queries;
}

std::vector<float> load_query_vector(const std::string& path, int query_idx, int expected_dim)
{
    std::ifstream file(path, std::ios::binary);
    if(!file.is_open())
    {
        throw std::runtime_error("Could not open query file: " + path);
    }

    size_t record_bytes = (expected_dim + 1) * sizeof(float);
    file.seekg(query_idx * record_bytes, std::ios::beg);
    if(file.fail())
    {
        throw std::runtime_error("Seek failed. Query index might be out of range: " + std::to_string(query_idx));
    }

    int dim = 0;
    file.read(reinterpret_cast<char*>(&dim), sizeof(int));
    if(file.fail() || dim != expected_dim)
    {
        throw std::runtime_error("Dimension mismatch in query file.");
    }

    std::vector<float> query(expected_dim);
    file.read(reinterpret_cast<char*>(query.data()), expected_dim * sizeof(float));
    if(file.fail())
    {
        throw std::runtime_error("Failed to read query vector data.");
    }
    return query;
}

void dump_ciphertext_rns(const Ciphertext<DCRTPoly>& ct, const std::string& filepath)
{
    std::ofstream out(filepath);
    if(!out.is_open())
    {
        std::cerr << "Warning: Could not write test vector file: " << filepath << std::endl;
        return;
    }
    const auto& elements = ct->GetElements();
    for(size_t poly_idx = 0; poly_idx < elements.size(); ++poly_idx)
    {
        const auto& poly = elements[poly_idx];
        out << "c" << poly_idx << " degree=" << poly.GetRingDimension() << std::endl;
        size_t num_towers = poly.GetNumOfElements();
        for(size_t tower_idx = 0; tower_idx < num_towers; ++tower_idx)
        {
            const auto& single_poly = poly.GetElementAtIndex(tower_idx);
            out << "tower=" << tower_idx << " modulus=" << single_poly.GetModulus() << std::endl;
            for(usint i = 0; i < single_poly.GetLength(); ++i)
            {
                out << single_poly[i].ConvertToInt() << " ";
            }
            out << std::endl;
        }
    }
}

int main(int argc, char* argv[])
{
    if(argc < 4)
    {
        std::cerr << "Usage: " << argv[0] << " <config_json_path> <query_index> <top_k> [-j num_threads] [-n num_queries]" << std::endl;
        return 1;
    }

    std::string config_path = argv[1];
    int query_idx = std::stoi(argv[2]);
    int top_k = std::stoi(argv[3]);
    int num_threads = 1;
    int num_queries = -1;

    for(int i = 4; i < argc; ++i)
    {
        if(std::string(argv[i]) == "-j" && i + 1 < argc)
        {
            num_threads = std::stoi(argv[i + 1]);
            ++i;
        }
        else if(std::string(argv[i]) == "-n" && i + 1 < argc)
        {
            num_queries = std::stoi(argv[i + 1]);
            ++i;
        }
    }

    FHEConfig config;
    if(!config.load(config_path))
    {
        std::cerr << "Error loading configuration." << std::endl;
        return 1;
    }

    // Resolve query file path
    std::string query_filepath = config.resolve_path(config.query_path);
    std::vector<std::vector<float>> queries;
    bool is_batch = (query_idx == -1);

    try
    {
        if(is_batch)
        {
            queries = load_all_query_vectors(query_filepath, config.dimension);
            if(num_queries > 0 && queries.size() > static_cast<size_t>(num_queries))
            {
                queries.resize(num_queries);
            }
        }
        else
        {
            queries.push_back(load_query_vector(query_filepath, query_idx, config.dimension));
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << "Error loading query: " << e.what() << std::endl;
        return 1;
    }

    if(!config.use_encryption)
    {
        // --- PLAINTEXT MODE ---
        PlaintextSearcher searcher;
        if(!searcher.load_data(config))
        {
            std::cerr << "Error loading index data for plaintext search." << std::endl;
            return 1;
        }

        std::vector<std::vector<std::pair<int, float>>> all_results(queries.size());
        std::vector<double> individual_latencies(queries.size(), 0.0);

        auto start = std::chrono::high_resolution_clock::now();
        if(is_batch && num_threads > 1)
        {
            std::atomic<size_t> next_q(0);
            std::vector<std::jthread> worker_threads;
            for(int t = 0; t < num_threads; ++t)
            {
                worker_threads.emplace_back([&]() {
                    // Restrict internal OpenMP loops to 1 thread for each query worker thread
                    omp_set_num_threads(1);
                    while(true)
                    {
                        size_t q = next_q.fetch_add(1);
                        if(q >= queries.size()) break;
                        auto q_start = std::chrono::high_resolution_clock::now();
                        all_results[q] = searcher.search(queries[q], config.n_probe, top_k);
                        auto q_end = std::chrono::high_resolution_clock::now();
                        individual_latencies[q] = std::chrono::duration<double, std::milli>(q_end - q_start).count();
                    }
                    });
            }
            // Clear workers vector to block and join all threads
            worker_threads.clear();
        }
        else
        {
            // Sequential execution
            for(size_t q = 0; q < queries.size(); ++q)
            {
                auto q_start = std::chrono::high_resolution_clock::now();
                all_results[q] = searcher.search(queries[q], config.n_probe, top_k);
                auto q_end = std::chrono::high_resolution_clock::now();
                individual_latencies[q] = std::chrono::duration<double, std::milli>(q_end - q_start).count();
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

        // Print output tokens for python parser
        if(is_batch)
        {
            for(size_t q = 0; q < queries.size(); ++q)
            {
                std::cout << "RESULTS_" << q << ": ";
                for(size_t i = 0; i < all_results[q].size(); ++i)
                {
                    std::cout << all_results[q][i].first << ":" << all_results[q][i].second;
                    if(i < all_results[q].size() - 1) std::cout << ",";
                }
                std::cout << std::endl;
            }
            double sum_latencies = 0.0;
            for(double l : individual_latencies) sum_latencies += l;
            double avg_active = sum_latencies / queries.size();

            std::cout << "BATCH_LATENCY: " << elapsed_ms << " ms" << std::endl;
            std::cout << "AVG_ACTIVE_LATENCY: " << avg_active << " ms" << std::endl;
            std::cout << "THROUGHPUT_LATENCY: " << (elapsed_ms / queries.size()) << " ms" << std::endl;
        }
        else
        {
            std::cout << "RESULTS: ";
            for(size_t i = 0; i < all_results[0].size(); ++i)
            {
                std::cout << all_results[0][i].first << ":" << all_results[0][i].second;
                if(i < all_results[0].size() - 1) std::cout << ",";
            }
            std::cout << std::endl;
            std::cout << "LATENCY: " << elapsed_ms << " ms (Critical search kernels)" << std::endl;
        }

    }
    else
    {
        // --- ENCRYPTED FHE MODE ---
        double est_ram = (static_cast<double>(config.n_probe) * config.m_subvectors * config.k_subcentroids * 2.0) / 1024.0;
        if(est_ram > 32.0)
        {
            std::cerr << "ERROR: The requested search configuration is unsafe with homomorphic encryption enabled." << std::endl;
            std::cerr << "Estimated LUT memory usage: " << est_ram << " GB (exceeds safety limit of 32 GB)." << std::endl;
            std::cerr << "LUT Ciphertexts count: " << (config.n_probe * config.m_subvectors * config.k_subcentroids) << std::endl;
            std::cerr << "To prevent system freezing/crashing, please reduce n_probe, k_subcentroids, or m_subvectors in config.json, or disable encryption." << std::endl;
            return 1;
        }

        FHEContextManager ctx_mgr;
        if(!ctx_mgr.init(config))
        {
            std::cerr << "Error initializing FHE context." << std::endl;
            return 1;
        }

        FHESearcher searcher;
        if(!searcher.load_data(config))
        {
            std::cerr << "Error loading index data for FHE search." << std::endl;
            return 1;
        }

        std::vector<std::vector<std::pair<int, float>>> all_results(queries.size());
        std::vector<double> individual_latencies(queries.size(), 0.0);

        auto start = std::chrono::high_resolution_clock::now();
        if(is_batch && num_threads > 1)
        {
            std::atomic<size_t> next_q(0);
            std::vector<std::jthread> worker_threads;
            for(int t = 0; t < num_threads; ++t)
            {
                worker_threads.emplace_back([&]() {
                    // Restrict internal OpenFHE and OpenMP loops to 1 thread to avoid oversubscription
                    omp_set_num_threads(1);
                    while(true)
                    {
                        size_t q = next_q.fetch_add(1);
                        if(q >= queries.size()) break;
                        auto q_start = std::chrono::high_resolution_clock::now();
                        all_results[q] = searcher.search(ctx_mgr, queries[q], config.n_probe, top_k, config);
                        auto q_end = std::chrono::high_resolution_clock::now();
                        individual_latencies[q] = std::chrono::duration<double, std::milli>(q_end - q_start).count();
                    }
                    });
            }
            // Clear workers vector to block and join all threads
            worker_threads.clear();
        }
        else
        {
            // Sequential execution
            for(size_t q = 0; q < queries.size(); ++q)
            {
                auto q_start = std::chrono::high_resolution_clock::now();
                all_results[q] = searcher.search(ctx_mgr, queries[q], config.n_probe, top_k, config);
                auto q_end = std::chrono::high_resolution_clock::now();
                individual_latencies[q] = std::chrono::duration<double, std::milli>(q_end - q_start).count();
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

        // Dump Golden Test Vectors for RTL/HLS validation (Task 1.2.4)
        std::string vector_dir = config.resolve_path(config.encoding_output_dir + "/golden_vectors");
        std::filesystem::create_directories(vector_dir);

        auto ct_query = ctx_mgr.encrypt_query(queries[0], config.m_subvectors, config.dimension / config.m_subvectors);
        if(!ct_query.empty())
        {
            dump_ciphertext_rns(ct_query[0], vector_dir + "/query_subvector_0.txt");
        }

        if(is_batch)
        {
            for(size_t q = 0; q < queries.size(); ++q)
            {
                std::cout << "RESULTS_" << q << ": ";
                for(size_t i = 0; i < all_results[q].size(); ++i)
                {
                    std::cout << all_results[q][i].first << ":" << all_results[q][i].second;
                    if(i < all_results[q].size() - 1) std::cout << ",";
                }
                std::cout << std::endl;
            }
            double sum_latencies = 0.0;
            for(double l : individual_latencies) sum_latencies += l;
            double avg_active = sum_latencies / queries.size();

            std::cout << "BATCH_LATENCY: " << elapsed_ms << " ms" << std::endl;
            std::cout << "AVG_ACTIVE_LATENCY: " << avg_active << " ms" << std::endl;
            std::cout << "THROUGHPUT_LATENCY: " << (elapsed_ms / queries.size()) << " ms" << std::endl;
        }
        else
        {
            std::cout << "RESULTS: ";
            for(size_t i = 0; i < all_results[0].size(); ++i)
            {
                std::cout << all_results[0][i].first << ":" << all_results[0][i].second;
                if(i < all_results[0].size() - 1) std::cout << ",";
            }
            std::cout << std::endl;
            std::cout << "LATENCY: " << elapsed_ms << " ms (Critical FHE search kernels)" << std::endl;
            std::cout << "GOLDEN_VECTORS_SAVED: " << vector_dir << std::endl;
        }
    }

    return 0;
}
