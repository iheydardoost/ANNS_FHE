#include "fhe_context_manager.h"
#include <iostream>

using namespace lbcrypto;

namespace anns_fhe {

bool FHEContextManager::init(const FHEConfig& config) {
    // Multiplicative depth:
    // For interactive Top-K, we only need depth 2 (subtraction/multiplication + additions).
    // For fully homomorphic Top-K with sorting network, we need depth 12 to evaluate comparisons.
    int depth = config.interactive_top_k ? 2 : 12;

    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetRingDim(config.poly_modulus_degree);
    parameters.SetMultiplicativeDepth(depth);
    parameters.SetScalingModSize(config.scale_bits);
    
    // Map security level string to OpenFHE enum
    SecurityLevel secLevel = HEStd_128_classic;
    if (config.security_level == "HEStd_128_classic") secLevel = HEStd_128_classic;
    else if (config.security_level == "HEStd_192_classic") secLevel = HEStd_192_classic;
    else if (config.security_level == "HEStd_256_classic") secLevel = HEStd_256_classic;
    else if (config.security_level == "HEStd_NotSet") secLevel = HEStd_NotSet;
    parameters.SetSecurityLevel(secLevel);

    parameters.SetScalingTechnique(FLEXIBLEAUTO);

    m_cc = GenCryptoContext(parameters);
    
    m_cc->Enable(PKE);
    m_cc->Enable(KEYSWITCH);
    m_cc->Enable(LEVELEDSHE);

    std::cout << "[FHEContextManager] CryptoContext initialized successfully." << std::endl;

    // Generate keys
    m_keypair = m_cc->KeyGen();
    m_cc->EvalMultKeyGen(m_keypair.secretKey);

    // Generate rotation keys for slot accumulation of size sub_dim
    int sub_dim = config.dimension / config.m_subvectors;
    std::vector<int32_t> rotationIndices;
    for (int i = 1; i < sub_dim; i *= 2) {
        rotationIndices.push_back(i);
    }
    m_cc->EvalRotateKeyGen(m_keypair.secretKey, rotationIndices);
    
    m_is_initialized = true;
    return true;
}

std::vector<Ciphertext<DCRTPoly>> FHEContextManager::encrypt_query(
    const std::vector<float>& query, int m_subvectors, int sub_dim) const {
    
    std::vector<Ciphertext<DCRTPoly>> encrypted_query;
    if (!m_is_initialized) return encrypted_query;

    for (int m = 0; m < m_subvectors; ++m) {
        std::vector<double> subvector_data(sub_dim, 0.0);
        for (int d = 0; d < sub_dim; ++d) {
            subvector_data[d] = query[m * sub_dim + d];
        }
        Plaintext pt = m_cc->MakeCKKSPackedPlaintext(subvector_data);
        auto ct = m_cc->Encrypt(m_keypair.publicKey, pt);
        encrypted_query.push_back(ct);
    }

    return encrypted_query;
}

double FHEContextManager::decrypt_scalar(const Ciphertext<DCRTPoly>& ciphertext) const {
    Plaintext pt;
    m_cc->Decrypt(m_keypair.secretKey, ciphertext, &pt);
    pt->SetLength(1);
    return pt->GetRealPackedValue()[0];
}

std::vector<double> FHEContextManager::decrypt_vector(const Ciphertext<DCRTPoly>& ciphertext, int limit) const {
    Plaintext pt;
    m_cc->Decrypt(m_keypair.secretKey, ciphertext, &pt);
    pt->SetLength(limit);
    std::vector<double> res(limit);
    auto packed = pt->GetRealPackedValue();
    for (int i = 0; i < limit; ++i) {
        res[i] = packed[i];
    }
    return res;
}

} // namespace anns_fhe
