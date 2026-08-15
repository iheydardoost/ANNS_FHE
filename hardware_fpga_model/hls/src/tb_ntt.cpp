#include <iostream>
#include <fstream>
#include <vector>
#include "ntt.h"

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
    
    // Read twiddles
    std::ifstream in_tw("../../../integration_tools/test_vectors/twiddles.bin", std::ios::binary);
    std::vector<uint64_t> twiddles(num_limbs * N);
    in_tw.read((char*)twiddles.data(), num_limbs * N * sizeof(uint64_t));
    
    // Read inputs
    std::ifstream in_data("../../../integration_tools/test_vectors/tv_ntt_in.bin", std::ios::binary);
    std::vector<uint64_t> data_in(num_limbs * N);
    in_data.read((char*)data_in.data(), num_limbs * N * sizeof(uint64_t));
    
    // Read expected outputs
    std::ifstream out_data("../../../integration_tools/test_vectors/tv_ntt_out.bin", std::ios::binary);
    std::vector<uint64_t> data_expected(num_limbs * N);
    out_data.read((char*)data_expected.data(), num_limbs * N * sizeof(uint64_t));
    
    int errors = 0;
    
    for (size_t i = 0; i < num_limbs; i++) {
        uint64_t q = primes[i];
        ap_uint<128> m = mus[i];
        uint32_t k = ks[i];
        
        std::vector<uint64_t> a(N);
        for(size_t j = 0; j < N; j++) {
            a[j] = data_in[i * N + j];
        }
        
        const uint64_t* tw = &twiddles[i * N];
        
        ntt_forward(a.data(), tw, q, m, k);
        
        for(size_t j = 0; j < N; j++) {
            uint64_t expected = data_expected[i * N + j];
            uint64_t actual = a[j];
            if (expected != actual) {
                if (errors < 10) {
                    std::cerr << "NTT Error at limb " << i << ", idx " << j 
                              << ". Expected " << expected << ", got " << actual << std::endl;
                }
                errors++;
            }
        }
    }
    
    if (errors == 0) {
        std::cout << "SUCCESS: All NTT tests passed bit-accurately!" << std::endl;
    } else {
        std::cout << "FAILED: " << errors << " errors found." << std::endl;
    }
    
    return errors;
}
