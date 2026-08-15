#include <iostream>
#include <fstream>
#include <vector>
#include "mod_arith.h"

int main() {
    std::ifstream in_tv("../../../integration_tools/test_vectors/tv_mod_arith.bin", std::ios::binary);
    if (!in_tv) {
        std::cerr << "Failed to open test vectors" << std::endl;
        return 1;
    }
    
    std::ifstream in_q("../../../integration_tools/test_vectors/rns_primes.bin", std::ios::binary);
    std::vector<uint64_t> primes(11);
    in_q.read((char*)primes.data(), 11 * sizeof(uint64_t));
    
    std::ifstream in_mu("../../../integration_tools/test_vectors/barrett_constants.bin", std::ios::binary);
    std::vector<ap_uint<128>> mus(11);
    std::vector<uint32_t> ks(11);
    for (int i = 0; i < 11; i++) {
        // Read 16 bytes for m
        uint64_t m_buf[2];
        in_mu.read((char*)m_buf, 16);
        // Assuming little-endian, which is true for x86
        mus[i] = ((ap_uint<128>)m_buf[1] << 64) | m_buf[0];
        in_mu.read((char*)&ks[i], sizeof(uint32_t));
    }
    
    int errors = 0;
    
    // We generated 1000 vectors per prime
    for (int i = 0; i < 11; i++) {
        uint64_t q = primes[i];
        ap_uint<128> m = mus[i];
        uint32_t k = ks[i];
        
        for (int j = 0; j < 1000; j++) {
            uint64_t v[5];
            in_tv.read((char*)v, 5 * sizeof(uint64_t));
            uint64_t a = v[0];
            uint64_t b = v[1];
            uint64_t expected_add = v[2];
            uint64_t expected_sub = v[3];
            uint64_t expected_mul = v[4];
            
            uint64_t actual_add = mod_add(a, b, q);
            uint64_t actual_sub = mod_sub(a, b, q);
            uint64_t actual_mul = mod_mul(a, b, q, m, k);
            
            if (actual_add != expected_add) {
                std::cerr << "Error in add: a=" << a << ", b=" << b << ", expected=" << expected_add << ", actual=" << actual_add << std::endl;
                errors++;
            }
            if (actual_sub != expected_sub) {
                std::cerr << "Error in sub: a=" << a << ", b=" << b << ", expected=" << expected_sub << ", actual=" << actual_sub << std::endl;
                errors++;
            }
            if (actual_mul != expected_mul) {
                std::cerr << "Error in mul: a=" << a << ", b=" << b << ", q=" << q << ", m=" << m << ", k=" << k << ", expected=" << expected_mul << ", actual=" << actual_mul << std::endl;
                errors++;
            }
        }
    }
    
    if (errors == 0) {
        std::cout << "SUCCESS: All modular arithmetic tests passed." << std::endl;
    } else {
        std::cout << "FAILED: " << errors << " errors found." << std::endl;
    }
    
    return errors;
}
