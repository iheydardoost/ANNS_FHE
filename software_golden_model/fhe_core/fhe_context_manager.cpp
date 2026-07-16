#include "fhe_context_manager.h"
#include <iostream>

using namespace lbcrypto;

namespace anns_fhe
{

    bool FHEContextManager::init(const FHEConfig& config)
    {
        // Multiplicative depth:
        // For interactive Top-K, we only need depth 2 (subtraction/multiplication + additions).
        // For fully homomorphic Top-K with sorting HMR, we need depth 8 to evaluate comparisons.
        int depth = config.interactive_top_k ? 2 : 8;

        CCParams<CryptoContextCKKSRNS> parameters;
        parameters.SetMultiplicativeDepth(depth);
        parameters.SetScalingModSize(config.scale_bits);

        parameters.SetBatchSize(config.poly_modulus_degree >> 1);

        // Map security level string to OpenFHE enum
        SecurityLevel secLevel = HEStd_128_classic;
        if(config.security_level == "HEStd_128_classic") secLevel = HEStd_128_classic;
        else if(config.security_level == "HEStd_192_classic") secLevel = HEStd_192_classic;
        else if(config.security_level == "HEStd_256_classic") secLevel = HEStd_256_classic;
        else if(config.security_level == "HEStd_NotSet") secLevel = HEStd_NotSet;
        parameters.SetSecurityLevel(secLevel);

        if(config.security_level == "HEStd_NotSet")
        {
            parameters.SetRingDim(config.poly_modulus_degree);
        }

        parameters.SetScalingTechnique(FLEXIBLEAUTOEXT);

        m_cc = GenCryptoContext(parameters);

        // -------- 1 -----------------------------------------------------------
        // What it does: Enables the core, baseline asymmetric cryptosystem.
        // What it unlocks: KeyGen(), Encrypt(), and Decrypt().
        m_cc->Enable(PKE);
        // ----------------------------------------------------------------------

        // -------- 2 -----------------------------------------------------------
        // What it does: Unlocks the algorithms that transition a ciphertext encrypted under a Secret Key A
        //               into a ciphertext encrypted under a Secret Key B without decrypting it.
        // What it unlocks: Relinearization (compressing a 3-element polynomial back to a 2-element standard ciphertext
        //                  after multiplication) and vector rotations.
        m_cc->Enable(KEYSWITCH);
        // ----------------------------------------------------------------------

        // -------- 3 -----------------------------------------------------------
        // What it does: Enables the Proxy Re-encryption module. This allows a third-party "proxy" (like a cloud server)
        //               to transform a ciphertext encrypted under Alice's public key into a ciphertext that can be decrypted
        //               by Bob's private key, without the proxy ever learning the plaintext or either private key.
        // What it unlocks: Re-encryption key generation (ReKeyGen) and evaluation (ReEncrypt).
        // m_cc->Enable(PRE);
        // ----------------------------------------------------------------------

        // -------- 4 -----------------------------------------------------------
        // What it does: Unlocks basic homomorphic evaluation of additions and multiplications up to a fixed depth limit,
        //               alongside noise management.
        // What it unlocks: EvalAdd, EvalSub, EvalMult, and critical CKKS operations like Rescale.
        m_cc->Enable(LEVELEDSHE);
        // ----------------------------------------------------------------------

        // -------- 5 -----------------------------------------------------------
        // What it does: Unlocks high-level algebraic operations on top of basic additions and multiplications.
        // What it unlocks: Matrix/vector operations, computing sums across all vector slots (EvalSum),
        //                  computing inner products (EvalInnerProduct), evaluating Chebyshev polynomials
        //                  for arbitrary functional approximation (like calculating sigmoid(x) or sin(x)),
        //                  and complex linear transformations.
        // m_cc->Enable(ADVANCEDSHE);
        // ----------------------------------------------------------------------

        // -------- 6 -----------------------------------------------------------
        // What it does: Unlocks Threshold Homomorphic Encryption. This allows a group of N participants
        //               to collectively generate a shared public key, perform computations on it,
        //               and collaboratively decrypt the result. No single party can decrypt the data alone.
        // What it unlocks: Multi-party key generation, joint decryption shares calculation, and collaborative bootstrapping (TCKKS).
        // m_cc->Enable(MULTIPARTY);
        // ----------------------------------------------------------------------

        // -------- 7 -----------------------------------------------------------
        // What it does: Unlocks the Bootstrapping module—the self-refreshing operation that resets noise levels inside a ciphertext.
        // What it unlocks: EvalBootstrapSetup, EvalBootstrapKeyGen, and EvalBootstrap.
        // m_cc->Enable(FHE);
        // ----------------------------------------------------------------------

        // -------- 8 -----------------------------------------------------------
        // What it does: Unlocks the capability to dynamically convert ciphertexts between different FHE schemes at runtime.
        // What it unlocks: Transforming a vectorized CKKS ciphertext (optimized for floats)
        //                  into a FHEW/TFHE ciphertext (optimized for fast boolean gates/lookup tables),
        //                  evaluating a non-linear gate (like a comparison x > y), and converting the result back to CKKS.
        // m_cc->Enable(SCHEMESWITCH);
        // ----------------------------------------------------------------------

        std::cout << "[FHEContextManager] CryptoContext initialized successfully." << std::endl;

        // Generate keys
        m_keypair = m_cc->KeyGen();
        m_cc->EvalMultKeyGen(m_keypair.secretKey);

        // Generate rotation keys for slot accumulation of size sub_dim
        int sub_dim = config.dimension / config.m_subvectors;
        std::vector<int32_t> rotationIndices;
        for(int i = 1; i < sub_dim; i *= 2)
        {
            rotationIndices.push_back(i);
        }
        m_cc->EvalRotateKeyGen(m_keypair.secretKey, rotationIndices);

        m_is_initialized = true;
        return true;
    }

    std::vector<Ciphertext<DCRTPoly>> 
            FHEContextManager::encrypt_query(const std::vector<float>& query, int m_subvectors, int sub_dim) const
    {

        std::vector<Ciphertext<DCRTPoly>> encrypted_query;
        if(!m_is_initialized) return encrypted_query;

        for(int m = 0; m < m_subvectors; ++m)
        {
            std::vector<double> subvector_data(sub_dim, 0.0);
            for(int d = 0; d < sub_dim; ++d)
            {
                subvector_data[d] = query[m * sub_dim + d];
            }
            Plaintext pt = m_cc->MakeCKKSPackedPlaintext(subvector_data);
            auto ct = m_cc->Encrypt(m_keypair.publicKey, pt);
            encrypted_query.push_back(ct);
        }

        return encrypted_query;
    }

    double FHEContextManager::decrypt_scalar(const Ciphertext<DCRTPoly>& ciphertext) const
    {
        Plaintext pt;
        m_cc->Decrypt(m_keypair.secretKey, ciphertext, &pt);
        pt->SetLength(1);
        return pt->GetRealPackedValue()[0];
    }

    std::vector<double> FHEContextManager::decrypt_vector(const Ciphertext<DCRTPoly>& ciphertext, int limit) const
    {
        Plaintext pt;
        m_cc->Decrypt(m_keypair.secretKey, ciphertext, &pt);
        pt->SetLength(limit);
        std::vector<double> res(limit);
        auto packed = pt->GetRealPackedValue();
        for(int i = 0; i < limit; ++i)
        {
            res[i] = packed[i];
        }
        return res;
    }

} // namespace anns_fhe
