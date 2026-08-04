#ifndef FHE_CONTEXT_MANAGER_H
#define FHE_CONTEXT_MANAGER_H

#include "openfhe.h"
#include "fhe_config.h"
#include <vector>
#include <cstdint>

namespace anns_fhe
{

    class FHEContextManager
    {
    public:
        FHEContextManager() = default;
        ~FHEContextManager() = default;

        // -----------------------------------------------------------------------
        // Offline Preprocessing API  (called by "preprocess" subcommand)
        // -----------------------------------------------------------------------

        // Initialize CryptoContext + generate all keys. No bootstrapping.
        bool init_and_keygen(const FHEConfig& config);

        // Serialize CryptoContext, public key, secret key, and eval keys to disk.
        bool serialize_all(const FHEConfig& config) const;

        // Load plaintext centroids → SIMD-pack → encrypt → serialize.
        // Output: serialization_dir/encrypted_centroids.bin
        bool encrypt_and_serialize_centroids(const FHEConfig& config) const;

        // Load plaintext codebooks → SIMD-pack → encrypt → serialize.
        // Output: serialization_dir/encrypted_codebooks.bin  (M ciphertexts)
        bool encrypt_and_serialize_codebooks(const FHEConfig& config) const;

        // -----------------------------------------------------------------------
        // Online Query API  (called by "search" subcommand)
        // -----------------------------------------------------------------------

        // Load CryptoContext + all keys + encrypted index data from disk.
        bool load_from_disk(const FHEConfig& config);

        // Load plaintext centroids and codebooks for Step 3 SIMD-batched distances.
        bool load_plaintext_index(const FHEConfig& config);

        // -----------------------------------------------------------------------
        // Client-Side Simulation Helpers
        // -----------------------------------------------------------------------

        // Encrypt a full D-dimensional query vector into a single SIMD-packed
        // ciphertext whose layout matches the encrypted centroids.
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> encrypt_query_packed(
            const std::vector<float>& query,
            const FHEConfig& config) const;

        // Encrypt query in dimension-major layout for SIMD-batched Step 3.
        // Layout: slots [d*B..d*B+B-1] = q[d] for all d, where B = slots/D.
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> encrypt_query_dimpack(
            const std::vector<float>& query,
            const FHEConfig& config) const;

        // Decrypt a ciphertext and return the first 'limit' real-valued slots.
        std::vector<double> decrypt_vector(
            const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct,
            int limit) const;

        // -----------------------------------------------------------------------
        // Accessors
        // -----------------------------------------------------------------------
        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> get_context() const { return m_cc; }

        // Pre-loaded encrypted index (available after load_from_disk)
        const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& get_encrypted_centroids() const
            { return m_encrypted_centroids; }

        const std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>>& get_encrypted_codebooks() const
            { return m_encrypted_codebooks; }

        const std::vector<float>& get_plaintext_centroids() const
            { return m_plaintext_centroids; }

        const std::vector<float>& get_plaintext_codebooks() const
            { return m_plaintext_codebooks; }

        bool is_initialized() const { return m_is_initialized; }

        // -----------------------------------------------------------------------
        // Utility
        // -----------------------------------------------------------------------
        static void print_ciphertext_coefficients(const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct, usint num_coeffs_to_print);

        void decrypt_and_print_vector(const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ct,
                                        std::string name, usint num_coeffs_to_print, bool print_sorted) const;

    private:
        // Compute the full set of rotation key indices needed by the algorithm.
        static std::vector<int32_t> compute_rotation_indices(const FHEConfig& config);

        lbcrypto::CryptoContext<lbcrypto::DCRTPoly> m_cc;
        lbcrypto::KeyPair<lbcrypto::DCRTPoly>       m_keypair;

        // Pre-loaded encrypted index data (online mode)
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly>              m_encrypted_centroids;
        std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>> m_encrypted_codebooks;

        // Plaintext index data for Step 3 (SIMD-batched distance computation)
        std::vector<float> m_plaintext_centroids;   // n_list * D floats
        std::vector<float> m_plaintext_codebooks;   // M * K * sub_dim floats

        bool m_is_initialized = false;
    };

} // namespace anns_fhe

#endif // FHE_CONTEXT_MANAGER_H
