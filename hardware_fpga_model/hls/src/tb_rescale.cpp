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
    
    // Read twiddles
    std::ifstream in_tw("../../../integration_tools/test_vectors/twiddles.bin", std::ios::binary);
    std::vector<uint64_t> twiddles(num_limbs * N);
    in_tw.read((char*)twiddles.data(), num_limbs * N * sizeof(uint64_t));
    
    // Read inverse twiddles
    std::ifstream in_invtw("../../../integration_tools/test_vectors/inv_twiddles.bin", std::ios::binary);
    std::vector<uint64_t> inv_twiddles(num_limbs * N);
    in_invtw.read((char*)inv_twiddles.data(), num_limbs * N * sizeof(uint64_t));
    
    // Read QlQlInv
    std::ifstream in_qlql("../../../integration_tools/test_vectors/QlQlInvModqlDivqlModq.bin", std::ios::binary);
    std::vector<uint64_t> qlql(last_limb);
    in_qlql.read((char*)qlql.data(), last_limb * sizeof(uint64_t));
    
    // Read qlInv
    std::ifstream in_qlinv("../../../integration_tools/test_vectors/qlInvModq.bin", std::ios::binary);
    std::vector<uint64_t> qlinv(last_limb);
    in_qlinv.read((char*)qlinv.data(), last_limb * sizeof(uint64_t));
    
    // Read input (Rescale IN)
    std::ifstream in_data("../../../integration_tools/test_vectors/tv_rescale_in.bin", std::ios::binary);
    std::vector<uint64_t> data_in(num_limbs * N);
    in_data.read((char*)data_in.data(), num_limbs * N * sizeof(uint64_t));
    
    // Read expected output (Rescale OUT, 11 limbs)
    std::ifstream out_data("../../../integration_tools/test_vectors/tv_rescale_out.bin", std::ios::binary);
    std::vector<uint64_t> data_expected(last_limb * N);
    out_data.read((char*)data_expected.data(), last_limb * N * sizeof(uint64_t));
    
    int errors = 0;
    
    std::vector<uint64_t> actual_out(last_limb * N);

    rescale(data_in.data(), actual_out.data(), num_limbs, primes.data(), mus.data(), ks.data(), 
            &inv_twiddles[last_limb * N], twiddles.data(), ninv[last_limb], qlql.data(), qlinv.data());
    
    for (size_t i = 0; i < last_limb; i++) {
        for(size_t j = 0; j < N; j++) {
            uint64_t expected = data_expected[i * N + j];
            uint64_t actual = actual_out[i * N + j];
            if (expected != actual) {
                if (errors < 10) std::cerr << "Rescale Error at limb " << i << " idx " << j << ". Exp: " << expected << " Act: " << actual << std::endl;
                errors++;
            }
        }
    }
    
    if (errors == 0) {
        std::cout << "SUCCESS: All Rescale tests passed bit-accurately!" << std::endl;
    } else {
        std::cout << "FAILED: " << errors << " errors found." << std::endl;
    }
    
    return errors;
}
