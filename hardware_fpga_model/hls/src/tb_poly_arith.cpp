#include <iostream>
#include <fstream>
#include <vector>
#include "poly_arith.h"

int main() {
    uint32_t N = 16384;
    uint32_t num_limbs = 11;
    
    // Read primes
    std::ifstream in_q("../../../integration_tools/test_vectors/rns_primes.bin", std::ios::binary);
    std::vector<uint64_t> primes(num_limbs);
    in_q.read((char*)primes.data(), num_limbs * sizeof(uint64_t));
    
    // Read barrett constants
    std::ifstream in_mu("../../../integration_tools/test_vectors/barrett_constants.bin", std::ios::binary);
    std::vector<ap_uint<128>> mus(num_limbs);
    std::vector<uint32_t> ks(num_limbs);
    for (size_t i = 0; i < num_limbs; i++) {
        uint64_t m_buf[2];
        in_mu.read((char*)m_buf, 16);
        mus[i] = ((ap_uint<128>)m_buf[1] << 64) | m_buf[0];
        in_mu.read((char*)&ks[i], sizeof(uint32_t));
    }
    
    // Read inputs
    std::ifstream in_a("../../../integration_tools/test_vectors/tv_poly_a.bin", std::ios::binary);
    std::vector<uint64_t> data_a(num_limbs * N);
    in_a.read((char*)data_a.data(), num_limbs * N * sizeof(uint64_t));
    
    std::ifstream in_b("../../../integration_tools/test_vectors/tv_poly_b.bin", std::ios::binary);
    std::vector<uint64_t> data_b(num_limbs * N);
    in_b.read((char*)data_b.data(), num_limbs * N * sizeof(uint64_t));
    
    // Read expected outputs
    std::ifstream out_add("../../../integration_tools/test_vectors/tv_poly_add.bin", std::ios::binary);
    std::vector<uint64_t> expected_add(num_limbs * N);
    out_add.read((char*)expected_add.data(), num_limbs * N * sizeof(uint64_t));

    std::ifstream out_sub("../../../integration_tools/test_vectors/tv_poly_sub.bin", std::ios::binary);
    std::vector<uint64_t> expected_sub(num_limbs * N);
    out_sub.read((char*)expected_sub.data(), num_limbs * N * sizeof(uint64_t));

    std::ifstream out_mul("../../../integration_tools/test_vectors/tv_poly_mul.bin", std::ios::binary);
    std::vector<uint64_t> expected_mul(num_limbs * N);
    out_mul.read((char*)expected_mul.data(), num_limbs * N * sizeof(uint64_t));
    
    int errors_add = 0;
    int errors_sub = 0;
    int errors_mul = 0;
    
    std::vector<uint64_t> actual_out(N);

    for (size_t i = 0; i < num_limbs; i++) {
        uint64_t q = primes[i];
        ap_uint<128> m = mus[i];
        uint32_t k = ks[i];
        
        const uint64_t* a = &data_a[i * N];
        const uint64_t* b = &data_b[i * N];
        
        // Test ADD
        poly_add(a, b, actual_out.data(), q);
        for(size_t j = 0; j < N; j++) {
            if (expected_add[i * N + j] != actual_out[j]) {
                if (errors_add < 10) std::cerr << "ADD Error at limb " << i << " idx " << j << std::endl;
                errors_add++;
            }
        }

        // Test SUB
        poly_sub(a, b, actual_out.data(), q);
        for(size_t j = 0; j < N; j++) {
            if (expected_sub[i * N + j] != actual_out[j]) {
                if (errors_sub < 10) std::cerr << "SUB Error at limb " << i << " idx " << j << std::endl;
                errors_sub++;
            }
        }

        // Test MUL
        poly_mul(a, b, actual_out.data(), q, m, k);
        for(size_t j = 0; j < N; j++) {
            if (expected_mul[i * N + j] != actual_out[j]) {
                if (errors_mul < 10) std::cerr << "MUL Error at limb " << i << " idx " << j << std::endl;
                errors_mul++;
            }
        }
    }
    
    int total_errors = errors_add + errors_sub + errors_mul;
    if (total_errors == 0) {
        std::cout << "SUCCESS: All Poly Arithmetic tests passed bit-accurately!" << std::endl;
    } else {
        std::cout << "FAILED: " << total_errors << " errors found (" 
                  << errors_add << " add, " << errors_sub << " sub, " << errors_mul << " mul)." << std::endl;
    }
    
    return total_errors;
}
