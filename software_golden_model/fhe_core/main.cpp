/**
 * FHE-Core — Encrypted IVF-PQ Approximate Nearest Neighbor Search
 *
 * Usage:
 *   fhe_core_bin preprocess <config.json>
 *       Offline: generate keys, encrypt centroids/codebooks, serialize to disk.
 *
 *   fhe_core_bin search <config.json> [query_idx] [top_k] [-j <threads>] [-n <num_queries>]
 *       Online: load serialized keys + encrypted index, run encrypted search.
 *       query_idx = -1 → batch mode (all queries)
 *       top_k defaults to config.encryption.top_k
 */

#include "fhe_config.h"
#include "fhe_context_manager.h"
#include "fhe_searcher.h"
#include "plaintext_searcher.h"
#include "thread_pool.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <atomic>

#ifdef _OPENMP
#include <omp.h>
#endif

using namespace anns_fhe;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Utility: Read .fvecs file (SIFT format: dim(int32), then dim floats per row)
// ---------------------------------------------------------------------------
static std::vector<std::vector<float>> read_fvecs(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
    {
        std::cerr << "Error: Cannot open: " << path << std::endl;
        return {};
    }
    std::vector<std::vector<float>> vecs;
    int32_t dim = 0;
    while (f.read(reinterpret_cast<char*>(&dim), sizeof(int32_t)))
    {
        std::vector<float> v(dim);
        f.read(reinterpret_cast<char*>(v.data()), dim * sizeof(float));
        vecs.push_back(std::move(v));
    }
    return vecs;
}

// ---------------------------------------------------------------------------
// Utility: Read .ivecs file (same layout as .fvecs but int32 values)
// ---------------------------------------------------------------------------
static std::vector<std::vector<int32_t>> read_ivecs(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
    {
        std::cerr << "Error: Cannot open: " << path << std::endl;
        return {};
    }
    std::vector<std::vector<int32_t>> vecs;
    int32_t dim = 0;
    while (f.read(reinterpret_cast<char*>(&dim), sizeof(int32_t)))
    {
        std::vector<int32_t> v(dim);
        f.read(reinterpret_cast<char*>(v.data()), dim * sizeof(int32_t));
        vecs.push_back(std::move(v));
    }
    return vecs;
}

// ---------------------------------------------------------------------------
// Compute Recall@K from results vs. ground truth
// ---------------------------------------------------------------------------
static double compute_recall(
    const std::vector<std::pair<int, float>>& results,
    const std::vector<int32_t>& gt,
    int top_k)
{
    int hits = 0;
    for (int i = 0; i < top_k && i < static_cast<int>(results.size()); ++i)
    {
        int found = results[i].first;
        for (int g : gt)
            if (g == found) { ++hits; break; }
    }
    return static_cast<double>(hits) / std::max(1, top_k);
}

// ---------------------------------------------------------------------------
// Print results in the same format as the original fhe_core
// ---------------------------------------------------------------------------
static void print_results(
    const std::vector<std::pair<int, float>>& results,
    int query_idx)
{
    std::cout << "RESULTS " << query_idx << ":";
    for (const auto& [idx, dist] : results)
        std::cout << " " << idx;
    std::cout << std::endl;
}

static void print_latency(double coarse_ms, double fine_ms, int query_idx)
{
    std::cout << "LATENCY " << query_idx << ":"
              << " coarse=" << coarse_ms << "ms"
              << " fine=" << fine_ms << "ms"
              << " total=" << (coarse_ms + fine_ms) << "ms" << std::endl;
}

// ---------------------------------------------------------------------------
// MODE: preprocess
// ---------------------------------------------------------------------------
static int mode_preprocess(const FHEConfig& config)
{
    std::cout << "\n=== FHE-Core Preprocessing ===" << std::endl;
    std::cout << "Depth=" << config.multiplicative_depth
              << "  Slots=" << (config.poly_modulus_degree >> 1)
              << "  n_list=" << config.n_list
              << "  M=" << config.m_subvectors
              << "  K=" << config.k_subcentroids << std::endl;

    FHEContextManager ctx_mgr;

    // 1. Initialize CryptoContext + generate all keys
    std::cout << "\n[1/4] Initializing CryptoContext and generating keys..." << std::endl;
    if (!ctx_mgr.init_and_keygen(config))
    {
        std::cerr << "Error: Key generation failed." << std::endl;
        return 1;
    }

    // 2. Serialize CryptoContext + keys
    std::cout << "\n[2/4] Serializing CryptoContext and keys..." << std::endl;
    if (!ctx_mgr.serialize_all(config))
    {
        std::cerr << "Error: Serialization failed." << std::endl;
        return 1;
    }

    // 3. Encrypt and serialize IVF centroids
    std::cout << "\n[3/4] Encrypting IVF centroids..." << std::endl;
    if (!ctx_mgr.encrypt_and_serialize_centroids(config))
    {
        std::cerr << "Error: Centroid encryption failed." << std::endl;
        return 1;
    }

    // 4. Encrypt and serialize PQ codebooks
    std::cout << "\n[4/4] Encrypting PQ codebooks..." << std::endl;
    if (!ctx_mgr.encrypt_and_serialize_codebooks(config))
    {
        std::cerr << "Error: Codebook encryption failed." << std::endl;
        return 1;
    }

    std::cout << "\n=== Preprocessing complete. Files written to: "
              << config.resolve_path(config.serialization_dir) << " ===" << std::endl;
    return 0;
}

// ---------------------------------------------------------------------------
// MODE: search (FHE)
// ---------------------------------------------------------------------------
static int mode_search_fhe(
    const FHEConfig& config,
    int query_idx,   // -1 = all queries
    int top_k,
    int jobs,
    int limit)
{
    std::cout << "\n=== FHE-Core Encrypted Search ===" << std::endl;

    // --- Load CryptoContext + keys + encrypted index ---
    FHEContextManager ctx_mgr;
    std::cout << "[1/3] Loading serialized keys and encrypted index..." << std::endl;
    if (!ctx_mgr.load_from_disk(config))
    {
        std::cerr << "Error: Failed to load serialized data. Run 'preprocess' first." << std::endl;
        return 1;
    }

    // --- Load plaintext data ---
    FHESearcher searcher;
    std::cout << "[2/3] Loading plaintext index data (PQ codes, assignments)..." << std::endl;
    if (!searcher.load_plaintext_data(config))
    {
        std::cerr << "Error: Failed to load plaintext data." << std::endl;
        return 1;
    }

    // --- Load queries ---
    std::cout << "[3/3] Loading queries..." << std::endl;
    const std::string q_path = config.resolve_path(config.query_path);
    auto queries = read_fvecs(q_path);
    if (queries.empty())
    {
        std::cerr << "Error: No queries loaded from: " << q_path << std::endl;
        return 1;
    }

    // --- Load ground truth (for recall validation) ---
    const std::string gt_path = config.resolve_path(config.groundtruth_path);
    auto groundtruth = read_ivecs(gt_path);

    // --- Determine query range ---
    int q_start = 0, q_end = static_cast<int>(queries.size());
    if (query_idx >= 0)
    {
        if (query_idx >= q_end)
        {
            std::cerr << "Error: query_idx " << query_idx
                      << " out of range [0, " << q_end << ")" << std::endl;
            return 1;
        }
        q_start = query_idx;
        q_end   = query_idx + 1;
    }
    if (limit > 0 && (q_end - q_start) > limit)
    {
        q_end = q_start + limit;
    }

#ifdef _OPENMP
    omp_set_num_threads(1);
#endif

    std::cout << "\nRunning " << (q_end - q_start) << " queries, top_k=" << top_k
              << ", n_probe=" << config.n_probe << " with " << jobs << " threads" << std::endl;
    std::cout << "==========================================" << std::endl;

    // --- Per-query search loop ---
    using Clock = std::chrono::high_resolution_clock;
    std::vector<double> total_latencies(q_end - q_start, 0.0);
    double recall_sum = 0.0;
    int recall_count  = 0;

    struct FHEQueryResult {
        std::vector<std::pair<int, float>> results;
        std::vector<double> timings;
        double elapsed_ms = 0.0;
        double recall = 0.0;
        bool has_recall = false;
    };

    std::vector<FHEQueryResult> batch_results(q_end - q_start);

    int num_workers = (jobs > 0) ? jobs : 1;
    anns_fhe::ThreadPool pool(num_workers);

    std::atomic<int> next_query(q_start);
    std::vector<std::future<void>> futures;
    futures.reserve(num_workers);

    auto t_start = Clock::now();

    for (int w = 0; w < num_workers; ++w)
    {
        futures.push_back(pool.enqueue([&]() {
            while (true)
            {
                int qi = next_query.fetch_add(1);
                if (qi >= q_end) break;

                auto t0 = Clock::now();

                std::vector<double> timings;
                auto results = searcher.search(ctx_mgr, queries[qi], top_k, config, &timings);

                auto t1 = Clock::now();
                double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                
                int idx = qi - q_start;
                batch_results[idx].results = std::move(results);
                batch_results[idx].timings = std::move(timings);
                batch_results[idx].elapsed_ms = elapsed_ms;
                total_latencies[idx] = elapsed_ms;

                if (!groundtruth.empty() && qi < static_cast<int>(groundtruth.size()))
                {
                    batch_results[idx].recall = compute_recall(batch_results[idx].results, groundtruth[qi], top_k);
                    batch_results[idx].has_recall = true;
                }
            }
        }));
    }

    // Wait for all worker threads to finish
    for (auto& f : futures)
    {
        f.get();
    }

    auto t_end = Clock::now();
    double wall_clock_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    // Print all results sequentially from the main thread
    for (int qi = q_start; qi < q_end; ++qi)
    {
        int idx = qi - q_start;
        const auto& qr = batch_results[idx];
        print_results(qr.results, qi);

        if (qr.timings.size() >= 2)
            print_latency(qr.timings[0], qr.timings[1], qi);
        else
            std::cout << "LATENCY " << qi << ": total=" << qr.elapsed_ms << "ms" << std::endl;

        if (qr.has_recall)
        {
            recall_sum += qr.recall;
            ++recall_count;
            std::cout << "RECALL@" << top_k << " query=" << qi
                      << " : " << qr.recall << std::endl;
        }
    }

    // --- Summary statistics ---
    if (!total_latencies.empty())
    {
        double sum_lat = std::accumulate(total_latencies.begin(), total_latencies.end(), 0.0);
        double avg_lat = sum_lat / total_latencies.size();
        double min_lat = *std::min_element(total_latencies.begin(), total_latencies.end());
        double max_lat = *std::max_element(total_latencies.begin(), total_latencies.end());

        std::cout << "\n=== Search Statistics ===" << std::endl;
        std::cout << "Queries:       " << total_latencies.size() << std::endl;
        std::cout << "Wall-clock:    " << wall_clock_ms << " ms" << std::endl;
        std::cout << "Avg latency:   " << avg_lat << " ms" << std::endl;
        std::cout << "Min latency:   " << min_lat << " ms" << std::endl;
        std::cout << "Max latency:   " << max_lat << " ms" << std::endl;
        std::cout << "Throughput:    " << (total_latencies.size() / (wall_clock_ms / 1000.0)) << " queries/sec" << std::endl;

        if (recall_count > 0)
            std::cout << "Avg Recall@" << top_k << ": "
                      << (recall_sum / recall_count) << std::endl;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// MODE: search (plaintext — preserved for validation)
// ---------------------------------------------------------------------------
static int mode_search_plain(
    const FHEConfig& config,
    int query_idx,
    int top_k,
    int jobs,
    int limit)
{
    PlaintextSearcher searcher;
    if (!searcher.load_data(config))
    {
        std::cerr << "Error: Failed to load plaintext index." << std::endl;
        return 1;
    }

    const std::string q_path = config.resolve_path(config.query_path);
    auto queries = read_fvecs(q_path);
    if (queries.empty())
    {
        std::cerr << "Error: No queries loaded." << std::endl;
        return 1;
    }

    const std::string gt_path = config.resolve_path(config.groundtruth_path);
    auto groundtruth = read_ivecs(gt_path);

    int q_start = 0, q_end = static_cast<int>(queries.size());
    if (query_idx >= 0) { q_start = query_idx; q_end = query_idx + 1; }
    if (limit > 0 && (q_end - q_start) > limit)
    {
        q_end = q_start + limit;
    }

#ifdef _OPENMP
    omp_set_num_threads(1);
#endif

    using Clock = std::chrono::high_resolution_clock;
    std::vector<double> total_latencies(q_end - q_start, 0.0);
    double recall_sum = 0.0;
    int recall_count = 0;

    struct PlainQueryResult {
        std::vector<std::pair<int, float>> results;
        double elapsed_ms = 0.0;
        double recall = 0.0;
        bool has_recall = false;
    };

    std::vector<PlainQueryResult> batch_results(q_end - q_start);

    int num_workers = (jobs > 0) ? jobs : 1;
    anns_fhe::ThreadPool pool(num_workers);

    std::atomic<int> next_query(q_start);
    std::vector<std::future<void>> futures;
    futures.reserve(num_workers);

    auto t_start = Clock::now();

    for (int w = 0; w < num_workers; ++w)
    {
        futures.push_back(pool.enqueue([&]() {
            anns_fhe::PlaintextScratch scratch;
            while (true)
            {
                int qi = next_query.fetch_add(1);
                if (qi >= q_end) break;

                auto t0 = Clock::now();
                auto results = searcher.search(queries[qi], config.n_probe, top_k, scratch);
                auto t1 = Clock::now();
                double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                
                int idx = qi - q_start;
                batch_results[idx].results = std::move(results);
                batch_results[idx].elapsed_ms = elapsed_ms;
                total_latencies[idx] = elapsed_ms;

                if (!groundtruth.empty() && qi < static_cast<int>(groundtruth.size()))
                {
                    batch_results[idx].recall = compute_recall(batch_results[idx].results, groundtruth[qi], top_k);
                    batch_results[idx].has_recall = true;
                }
            }
        }));
    }

    // Wait for all worker threads to finish
    for (auto& f : futures)
    {
        f.get();
    }

    auto t_end = Clock::now();
    double wall_clock_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    // Print all results sequentially from the main thread
    for (int qi = q_start; qi < q_end; ++qi)
    {
        int idx = qi - q_start;
        const auto& qr = batch_results[idx];
        print_results(qr.results, qi);
        std::cout << "LATENCY " << qi << ": total=" << qr.elapsed_ms << "ms" << std::endl;

        if (qr.has_recall)
        {
            recall_sum += qr.recall;
            ++recall_count;
            std::cout << "RECALL@" << top_k << " query=" << qi
                      << " : " << qr.recall << std::endl;
        }
    }

    if (!total_latencies.empty())
    {
        double sum_lat = std::accumulate(total_latencies.begin(), total_latencies.end(), 0.0);
        double avg_lat = sum_lat / total_latencies.size();
        double min_lat = *std::min_element(total_latencies.begin(), total_latencies.end());
        double max_lat = *std::max_element(total_latencies.begin(), total_latencies.end());

        std::cout << "\n=== Search Statistics ===" << std::endl;
        std::cout << "Queries:       " << total_latencies.size() << std::endl;
        std::cout << "Wall-clock:    " << wall_clock_ms << " ms" << std::endl;
        std::cout << "Avg latency:   " << avg_lat << " ms" << std::endl;
        std::cout << "Min latency:   " << min_lat << " ms" << std::endl;
        std::cout << "Max latency:   " << max_lat << " ms" << std::endl;
        std::cout << "Throughput:    " << (total_latencies.size() / (wall_clock_ms / 1000.0)) << " queries/sec" << std::endl;
    }

    if (recall_count > 0)
        std::cout << "\nAvg Recall@" << top_k << ": "
                  << (recall_sum / recall_count) << std::endl;

    return 0;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " preprocess <config.json>\n"
                  << "  " << argv[0] << " search <config.json> [query_idx] [top_k]\n";
        return 1;
    }

    const std::string mode       = argv[1];
    const std::string config_path = argv[2];

    // Load config
    FHEConfig config;
    if (!config.load(config_path))
    {
        std::cerr << "Error: Failed to load config: " << config_path << std::endl;
        return 1;
    }

    // ---- preprocess ----
    if (mode == "preprocess")
    {
        return mode_preprocess(config);
    }

    // ---- search ----
    if (mode == "search")
    {
        int query_idx = -1;   // -1 = all queries
        int top_k     = 8;    // default; overridden by CLI arg

        if (argc >= 4) query_idx = std::atoi(argv[3]);
        if (argc >= 5) top_k     = std::atoi(argv[4]);

        if (top_k <= 0) top_k = 8;

        int jobs  = 1;
        int limit = -1;
        for (int i = 5; i < argc; ++i)
        {
            if (std::string(argv[i]) == "-j" && i + 1 < argc)
            {
                jobs = std::atoi(argv[i + 1]);
                i++;
            }
            else if (std::string(argv[i]) == "-n" && i + 1 < argc)
            {
                limit = std::atoi(argv[i + 1]);
                i++;
            }
        }

        // Dispatch to FHE or plaintext mode
        if (config.use_encryption && config.encryption_enabled)
            return mode_search_fhe(config, query_idx, top_k, jobs, limit);
        else
            return mode_search_plain(config, query_idx, top_k, jobs, limit);
    }

    std::cerr << "Error: Unknown mode '" << mode
              << "'. Expected: preprocess | search" << std::endl;
    return 1;
}
