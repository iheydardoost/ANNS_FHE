#ifndef FHE_CONTEXT_MANAGER_H
#define FHE_CONTEXT_MANAGER_H

#include "openfhe.h"
#include "fhe_config.h"
#include <vector>

namespace anns_fhe
{

    class FHEContextManager
    {
    public:
        FHEContextManager() = default;
        ~FHEContextManager() = default;

        bool init(const FHEConfig& config);

        // Encrypts a full D-dimensional query into M subvector ciphertexts
        std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>> 
                encrypt_query(const std::vector<float>& query, int m_subvectors, int sub_dim) const;

        // Decrypts a ciphertext and returns the first element (scalar)
        double decrypt_scalar(const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ciphertext) const;

        // Decrypts a ciphertext and returns the first 'limit' elements
        std::vector<double> decrypt_vector(const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ciphertext, int limit) const;

        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> get_context() const { return m_cc; }
        lbcrypto::KeyPair<lbcrypto::DCRTPoly> get_keypair() const { return m_keypair; }

    private:
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> m_cc;
        lbcrypto::KeyPair<lbcrypto::DCRTPoly> m_keypair;
        bool m_is_initialized = false;
    };

} // namespace anns_fhe

#endif // FHE_CONTEXT_MANAGER_H
