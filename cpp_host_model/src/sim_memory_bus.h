#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <stdexcept>
#include <ap_int.h>

class SimMemoryBus {
public:
    // poly_words default: 8M words (~64 MB), key_words default: 64M words (~512 MB)
    SimMemoryBus(size_t poly_words = 8388608, size_t key_words = 67108864);

    ap_uint<512>* poly_gmem_ptr() { return m_poly_gmem.data(); }
    ap_uint<512>* key_gmem_ptr() { return m_key_gmem.data(); }

    void write_poly(uint32_t word_offset, const uint64_t* data, size_t count);
    void read_poly(uint32_t word_offset, uint64_t* data, size_t count) const;

    void write_key(uint64_t word_offset, const uint64_t* data, size_t count);
    void read_key(uint64_t word_offset, uint64_t* data, size_t count) const;

    void load_poly_file(uint32_t word_offset, const std::string& path, size_t expected_words = 0);
    void load_key_file(uint64_t word_offset, const std::string& path, size_t expected_words = 0);
    void save_poly_file(uint32_t word_offset, const std::string& path, size_t count) const;

    size_t poly_size_words() const { return m_poly_gmem.size() * 8; }
    size_t key_size_words() const { return m_key_gmem.size() * 8; }

private:
    std::vector<ap_uint<512>> m_poly_gmem;
    std::vector<ap_uint<512>> m_key_gmem;
};
