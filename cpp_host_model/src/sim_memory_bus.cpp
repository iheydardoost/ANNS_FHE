#include "../include/sim_memory_bus.h"
#include <fstream>
#include <iostream>

SimMemoryBus::SimMemoryBus(size_t poly_words, size_t key_words) {
    size_t poly_beats = (poly_words + 7) / 8;
    size_t key_beats = (key_words + 7) / 8;
    m_poly_gmem.resize(poly_beats, 0);
    m_key_gmem.resize(key_beats, 0);
}

void SimMemoryBus::write_poly(uint32_t word_offset, const uint64_t* data, size_t count) {
    if (word_offset + count > poly_size_words()) {
        throw std::out_of_range("SimMemoryBus::write_poly out of bounds");
    }
    for (size_t i = 0; i < count; ++i) {
        size_t global_idx = word_offset + i;
        size_t beat_idx = global_idx / 8;
        size_t word_in_beat = global_idx % 8;
        ap_uint<64> w = data[i];
        m_poly_gmem[beat_idx].range(word_in_beat * 64 + 63, word_in_beat * 64) = w;
    }
}

void SimMemoryBus::read_poly(uint32_t word_offset, uint64_t* data, size_t count) const {
    if (word_offset + count > poly_size_words()) {
        throw std::out_of_range("SimMemoryBus::read_poly out of bounds");
    }
    for (size_t i = 0; i < count; ++i) {
        size_t global_idx = word_offset + i;
        size_t beat_idx = global_idx / 8;
        size_t word_in_beat = global_idx % 8;
        ap_uint<64> w = m_poly_gmem[beat_idx].range(word_in_beat * 64 + 63, word_in_beat * 64);
        data[i] = static_cast<uint64_t>(w);
    }
}

void SimMemoryBus::write_key(uint64_t word_offset, const uint64_t* data, size_t count) {
    if (word_offset + count > key_size_words()) {
        throw std::out_of_range("SimMemoryBus::write_key out of bounds");
    }
    for (size_t i = 0; i < count; ++i) {
        size_t global_idx = word_offset + i;
        size_t beat_idx = global_idx / 8;
        size_t word_in_beat = global_idx % 8;
        ap_uint<64> w = data[i];
        m_key_gmem[beat_idx].range(word_in_beat * 64 + 63, word_in_beat * 64) = w;
    }
}

void SimMemoryBus::read_key(uint64_t word_offset, uint64_t* data, size_t count) const {
    if (word_offset + count > key_size_words()) {
        throw std::out_of_range("SimMemoryBus::read_key out of bounds");
    }
    for (size_t i = 0; i < count; ++i) {
        size_t global_idx = word_offset + i;
        size_t beat_idx = global_idx / 8;
        size_t word_in_beat = global_idx % 8;
        ap_uint<64> w = m_key_gmem[beat_idx].range(word_in_beat * 64 + 63, word_in_beat * 64);
        data[i] = static_cast<uint64_t>(w);
    }
}

void SimMemoryBus::load_poly_file(uint32_t word_offset, const std::string& path, size_t expected_words) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("SimMemoryBus::load_poly_file cannot open: " + path);
    f.seekg(0, std::ios::end);
    size_t size_bytes = f.tellg();
    f.seekg(0, std::ios::beg);

    size_t words = size_bytes / sizeof(uint64_t);
    if (expected_words > 0 && words != expected_words) {
        throw std::runtime_error("SimMemoryBus::load_poly_file size mismatch in " + path);
    }
    std::vector<uint64_t> buf(words);
    f.read(reinterpret_cast<char*>(buf.data()), size_bytes);
    write_poly(word_offset, buf.data(), words);
}

void SimMemoryBus::load_key_file(uint64_t word_offset, const std::string& path, size_t expected_words) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("SimMemoryBus::load_key_file cannot open: " + path);
    f.seekg(0, std::ios::end);
    size_t size_bytes = f.tellg();
    f.seekg(0, std::ios::beg);

    size_t words = size_bytes / sizeof(uint64_t);
    if (expected_words > 0 && words != expected_words) {
        throw std::runtime_error("SimMemoryBus::load_key_file size mismatch in " + path);
    }
    std::vector<uint64_t> buf(words);
    f.read(reinterpret_cast<char*>(buf.data()), size_bytes);
    write_key(word_offset, buf.data(), words);
}

void SimMemoryBus::save_poly_file(uint32_t word_offset, const std::string& path, size_t count) const {
    std::vector<uint64_t> buf(count);
    read_poly(word_offset, buf.data(), count);
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("SimMemoryBus::save_poly_file cannot open: " + path);
    f.write(reinterpret_cast<const char*>(buf.data()), count * sizeof(uint64_t));
}
