#include <iostream>
#include <fstream>
#include <vector>
#include "rescale.h"

int main() {
    uint32_t N = 16384;
    uint32_t num_limbs = 11;
    uint32_t last_limb = num_limbs - 1;
    
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
    
    // Read N inverse
    std::ifstream in_ninv("../../../integration_tools/test_vectors/n_inv.bin", std::ios::binary);
    std::vector<uint64_t> ninv(num_limbs);
    in_ninv.read((char*)ninv.data(), num_limbs * sizeof(uint64_t));
    
    // Read inverse twiddles
    std::ifstream in_invtw("../../../integration_tools/test_vectors/inv_twiddles.bin", std::ios::binary);
    std::vector<uint64_t> inv_twiddles(num_limbs * N);
    in_invtw.read((char*)inv_twiddles.data(), num_limbs * N * sizeof(uint64_t));
    
    // Read input (eval)
    std::ifstream in_eval("../../../integration_tools/test_vectors/tv_last_eval.bin", std::ios::binary);
    std::vector<uint64_t> data_in(N);
    in_eval.read((char*)data_in.data(), N * sizeof(uint64_t));
    
    // Read expected output (coeff)
    std::ifstream out_coeff("../../../integration_tools/test_vectors/tv_last_coeff.bin", std::ios::binary);
    std::vector<uint64_t> data_expected(N);
    out_coeff.read((char*)data_expected.data(), N * sizeof(uint64_t));
    
    // Run INTT
    std::vector<uint64_t> actual_out = data_in;
    
    // Print first 5 elements before INTT
    std::cout << "Before INTT:" << std::endl;
    for(int i=0; i<5; i++) std::cout << actual_out[i] << " ";
    std::cout << std::endl;
    
    intt_inverse(actual_out.data(), &inv_twiddles[last_limb * N], ninv[last_limb], primes[last_limb], mus[last_limb], ks[last_limb]);
    
    // Print first 5 elements after INTT
    std::cout << "After INTT:" << std::endl;
    for(int i=0; i<5; i++) std::cout << actual_out[i] << " ";
    std::cout << std::endl;
    
    std::cout << "Expected:" << std::endl;
    for(int i=0; i<5; i++) std::cout << data_expected[i] << " ";
    std::cout << std::endl;
    
    std::cout << "N_inv = " << ninv[last_limb] << std::endl;
    
    int errors = 0;
    for(size_t j = 0; j < N; j++) {
        if (data_expected[j] != actual_out[j]) {
            if (errors < 10) std::cerr << "INTT Error idx " << j << ". Exp: " << data_expected[j] << " Act: " << actual_out[j] << std::endl;
            errors++;
        }
    }
    
    if (errors == 0) {
        std::cout << "SUCCESS: INTT matches OpenFHE!" << std::endl;
    } else {
        std::cout << "FAILED: " << errors << " INTT errors found." << std::endl;
    }
    
    return errors;
}
