#include <iostream>
#include <fstream>
#include <vector>
#include "automorphism.h"

int main() {
    uint32_t N = 16384;
    uint32_t num_limbs = 11;
    
    // Read auto map
    std::ifstream in_map("../../../integration_tools/test_vectors/auto_map.bin", std::ios::binary);
    std::vector<uint64_t> auto_map(N);
    in_map.read((char*)auto_map.data(), N * sizeof(uint64_t));
    
    // Read input (which was poly_ntt)
    std::ifstream in_data("../../../integration_tools/test_vectors/tv_poly_a.bin", std::ios::binary);
    std::vector<uint64_t> data_in(num_limbs * N);
    in_data.read((char*)data_in.data(), num_limbs * N * sizeof(uint64_t));
    
    // Read expected output
    std::ifstream out_data("../../../integration_tools/test_vectors/tv_poly_auto.bin", std::ios::binary);
    std::vector<uint64_t> data_expected(num_limbs * N);
    out_data.read((char*)data_expected.data(), num_limbs * N * sizeof(uint64_t));
    
    int errors = 0;
    
    std::vector<uint64_t> actual_out(N);

    for (size_t i = 0; i < num_limbs; i++) {
        const uint64_t* a = &data_in[i * N];
        
        automorphism(a, actual_out.data(), auto_map.data());
        
        for(size_t j = 0; j < N; j++) {
            uint64_t expected = data_expected[i * N + j];
            uint64_t actual = actual_out[j];
            if (expected != actual) {
                if (errors < 10) std::cerr << "Auto Error at limb " << i << " idx " << j << ". Exp: " << expected << " Act: " << actual << std::endl;
                errors++;
            }
        }
    }
    
    if (errors == 0) {
        std::cout << "SUCCESS: All Automorphism tests passed bit-accurately!" << std::endl;
    } else {
        std::cout << "FAILED: " << errors << " errors found." << std::endl;
    }
    
    return errors;
}
