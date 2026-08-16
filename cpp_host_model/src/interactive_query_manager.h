#pragma once

#include "fhe_host_driver.h"
#include <vector>
#include <string>
#include <map>

struct SearchResult {
    int rank;
    int vector_id;
    float distance;
};

class InteractiveQueryManager {
public:
    // Slot assignments in poly_gmem
    static constexpr uint32_t SLOT_CT_QUERY_C0 = 0x00000000;
    static constexpr uint32_t SLOT_CT_QUERY_C1 = 0x00040000;
    static constexpr uint32_t SLOT_PT_CENTROIDS = 0x00080000;
    
    static constexpr uint32_t SLOT_CT_DIFF_C0 = 0x000C0000;
    static constexpr uint32_t SLOT_CT_DIFF_C1 = 0x00100000;
    static constexpr uint32_t SLOT_CT_SQ_C0 = 0x00140000;
    static constexpr uint32_t SLOT_CT_SQ_C1 = 0x00180000;

    static constexpr uint32_t SLOT_CT_COMPACT_C0 = 0x001C0000;
    static constexpr uint32_t SLOT_CT_COMPACT_C1 = 0x00200000;

    static constexpr uint32_t SLOT_CT_QUERY_DIMPACK_C0 = 0x00240000;
    static constexpr uint32_t SLOT_CT_QUERY_DIMPACK_C1 = 0x00280000;

    static constexpr uint32_t SLOT_PT_BATCH = 0x002C0000;
    static constexpr uint32_t SLOT_PT_MASK = 0x00300000;

    static constexpr uint32_t SLOT_CT_ALL_DISTS_C0 = 0x00340000;
    static constexpr uint32_t SLOT_CT_ALL_DISTS_C1 = 0x00380000;

    InteractiveQueryManager(FHEHostDriver* driver);

    bool load_dataset_and_models(const std::string& dataset_base);

    // Full Interactive Query Flow
    std::vector<SearchResult> execute_interactive_query(
        const std::vector<float>& query,
        int top_k = 8,
        int n_probe = 1
    );

    // Verification against golden vectors
    bool verify_checkpoint(const std::string& name, uint32_t word_offset, const std::string& golden_bin, size_t words);

    // Sub-Stages
    void stage1_coarse_centroid_distance();
    void stage1b_distance_compaction();
    int stage2_client_cluster_selection(int n_probe = 1);
    void stage3_fine_candidate_batch_distance(int selected_cluster);
    std::vector<SearchResult> stage4_client_top_k(int top_k = 8);

private:
    FHEHostDriver* m_driver;
    SimMemoryBus* m_bus;

    static constexpr int N = 16384;
    const int D = 128;
    const int n_list = 32;
    const int B = 64; // Slots / D = 8192 / 128
    const int M = 8;
    const int K = 256;
    const int sub_dim = 16;
    const int slots = 8192;

    // Plaintext Index Data
    std::vector<float> m_centroids;       // 32 x 128
    std::vector<float> m_codebooks;       // 8 x 256 x 16
    std::vector<int32_t> m_assignments;   // 10000
    std::vector<uint8_t> m_pq_codes;      // 10000 x 8
    std::vector<std::vector<int>> m_cluster_vids;

    // Runtime Query State
    std::vector<float> m_current_query;
    int m_selected_cluster = -1;
    int m_total_candidates = 0;
    std::map<int, int> m_slot_to_vid;
};
